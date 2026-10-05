#!/usr/bin/env python3
"""Flash an Astra application image over UART.

The bootloader is already implemented, so this script only drives the host side
of the protocol documented in
ProjectAstra/Shared/AstraConstants/Include/UARTCommands.h:

    device -> host   0xA5            CMD_STM32_READY, once after reset
    host   -> device 20 bytes        <IIIII>: magic, size, crc, version,
                                     otaRequest, from the first 20 bytes of
                                     AstraApplication.bin
    device -> host   0x06            ASTRA_ACK, the header was accepted
                    0x15            ASTRA_NACK, rejected, nothing was erased
    device -> host   0xA6            ASTRA_ERASE_DONE, the slot is erased
    device -> host   0x06            ASTRA_ACK, one Flash page was stored
    device -> host   0x4F            ASTRA_IMAGE_OK, CRC matched
                    0xFE            ASTRA_IMAGE_BAD, CRC mismatch

The device is put into OTA mode by pressing the user button in the application,
which sets otaRequest in the header page and resets the MCU.  otaRequest is
cleared only when the transfer commits, so an interrupted run can simply be
repeated: the device re-announces itself on the next reset and the slot is
erased again from scratch.

The body is sent one Flash page at a time and every page is acknowledged before
the next one starts.  That is deliberate: the receive buffer on the device is
exactly one page, and it is not re-armed until it has been consumed, so the host
must not stream freely.  There is no retry, because a lost acknowledgement is
indistinguishable from a lost page and resending would corrupt the bytes that
already landed in Flash.

Usage:
    python scripts/astra_ota.py --list-ports
    python scripts/astra_ota.py --port COM7            # verify only
    python scripts/astra_ota.py --port COM7 --flash
"""

import argparse
import os
import struct
import sys
import time
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from astra_image import HEADER_MAGIC, HEADER_STRUCT, HEADER_STRUCT_SIZE

# Mirrors ProjectAstra/Shared/AstraConstants/Include/UARTCommands.h.
CMD_STM32_READY = 0xA5
ASTRA_ACK = 0x06
ASTRA_NACK = 0x15
ASTRA_ERASE_DONE = 0xA6
ASTRA_IMAGE_OK = 0x4F
ASTRA_IMAGE_BAD = 0xFE

NACK_REASONS = {
    ASTRA_NACK: "the magic or the size was rejected",
    ASTRA_ERASE_DONE: "the device started erasing unexpectedly",
    ASTRA_IMAGE_BAD: "the CRC of the stored image did not match the header",
}

# Mirrors Common/FlashLayout.h.in: the header owns one Flash page and the code
# is capped by APP_MAX_CODE_SIZE.
HEADER_PAGE_SIZE = 0x800
MAX_CODE_SIZE = 0x18800

DEFAULT_IMAGE = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "ProjectAstra", "AstraSTM32", "AstraApplication", "build", "Debug",
    "AstraApplication.bin")

EXIT_OK = 0
EXIT_NO_DEVICE = 1
EXIT_USAGE = 2
EXIT_FAILED = 3


class OtaError(Exception):
    """Raised when the transfer cannot continue."""


def info(message):
    print(message)


def detail(message, verbose):
    if verbose:
        print("  " + message)


def parse_image(path, verbose):
    """Unpack and cross-check the packed application image.

    The header is what the device validates, so a stale or truncated file has to
    be reported before a single byte reaches the wire.
    """
    try:
        with open(path, "rb") as stream:
            image = stream.read()
    except OSError as error:
        raise OtaError("cannot read {}: {}".format(path, error))

    if len(image) < HEADER_PAGE_SIZE + HEADER_STRUCT_SIZE:
        raise OtaError(
            "{} is {} bytes, too short to hold a header page and a code body"
            .format(path, len(image)))

    header = image[:HEADER_STRUCT_SIZE]
    magic, size, crc, version, ota_request = struct.unpack(HEADER_STRUCT, header)

    info("image            {}".format(path))
    info("  magic          0x{:08X}{}".format(
        magic, "" if magic == HEADER_MAGIC else "   <- expected 0x{:08X}".format(HEADER_MAGIC)))
    info("  size           {} bytes".format(size))
    info("  crc            0x{:08X}".format(crc))
    info("  version        {}".format(version))
    info("  otaRequest     {}".format(ota_request))

    if magic != HEADER_MAGIC:
        raise OtaError("the image does not start with the Astra magic")

    if size == 0 or size > MAX_CODE_SIZE:
        raise OtaError(
            "size {} is outside the slot limit 1..{}".format(size, MAX_CODE_SIZE))

    expected_length = HEADER_PAGE_SIZE + size
    if len(image) != expected_length:
        raise OtaError(
            "{} is {} bytes, expected {} (0x800 header page + {} bytes of code)"
            .format(path, len(image), expected_length, size))

    # The same CRC-32/ISO-HDLC that astra_image.py wrote into the header.
    actual_crc = zlib.crc32(image[HEADER_PAGE_SIZE:HEADER_PAGE_SIZE + size]) & 0xFFFFFFFF
    if actual_crc != crc:
        raise OtaError(
            "recomputed CRC 0x{:08X} does not match the header 0x{:08X}"
            .format(actual_crc, crc))
    detail("header CRC verified against the code range", verbose)

    return header, image[HEADER_PAGE_SIZE:]


def import_serial():
    try:
        # list_ports lives in a submodule that is not imported by "import serial".
        import serial.tools.list_ports
    except ImportError:
        raise OtaError(
            "pyserial is not installed. Run:\n"
            "    python -m pip install -r scripts/requirements.txt")

    return serial


def list_ports(serial):
    ports = serial.tools.list_ports.comports()
    if not ports:
        info("no serial ports found")
        return

    info("available serial ports:")
    for port in ports:
        info("  {:<12} {}".format(port.device, port.description))


def wait_for_byte(port, expected, timeout, verbose):
    """Wait for one specific byte and report anything else that shows up."""
    deadline = time.monotonic() + timeout
    noise = []

    while time.monotonic() < deadline:
        chunk = port.read(64)
        if not chunk:
            time.sleep(0.005)
            continue

        if expected in chunk:
            index = chunk.index(expected)
            noise.extend(chunk[:index])
            detail("ignored {} unexpected byte(s): {}".format(
                len(noise), " ".join("0x{:02X}".format(b) for b in noise)), verbose)
            return True

        noise.extend(chunk)

    detail("ignored {} unexpected byte(s): {}".format(
        len(noise), " ".join("0x{:02X}".format(b) for b in noise)), verbose)
    return False


def read_status(port, expected, timeout, verbose):
    """Read one status byte and classify it against what we expected."""
    chunk = port.read(1)
    deadline = time.monotonic() + timeout
    while not chunk and time.monotonic() < deadline:
        time.sleep(0.005)
        chunk = port.read(1)

    if not chunk:
        raise OtaError(
            "no status within {:.1f} s, expected 0x{:02X}".format(timeout, expected))

    status = chunk[0]

    if status != expected:
        reason = NACK_REASONS.get(status)
        suffix = " ({})".format(reason) if reason else ""
        raise OtaError(
            "the device answered 0x{:02X}{} instead of 0x{:02X}".format(
                status, suffix, expected))

    detail("received 0x{:02X}".format(status), verbose)
    return status


def write_all(port, payload, verbose):
    """Write every byte, because pyserial may accept a short write."""
    sent = 0
    while sent < len(payload):
        written = port.write(payload[sent:])
        if not written:
            raise OtaError("the serial port stopped accepting data")
        sent += written
        detail("wrote {} of {} bytes".format(sent, len(payload)), verbose)

    return sent


def send_body(port, code, size, arguments):
    """Stream the code range page by page, waiting for every acknowledgement."""
    pages = (size + HEADER_PAGE_SIZE - 1) // HEADER_PAGE_SIZE
    started = time.monotonic()

    info("5. sending {} bytes in {} page(s) of {} bytes".format(
        size, pages, HEADER_PAGE_SIZE))

    for index in range(pages):
        offset = index * HEADER_PAGE_SIZE
        chunk = code[offset:offset + HEADER_PAGE_SIZE]

        write_all(port, chunk, arguments.verbose)

        try:
            read_status(port, ASTRA_ACK, arguments.page_timeout, arguments.verbose)
        except OtaError as error:
            raise OtaError(
                "page {}/{} was not acknowledged: {}. The transfer cannot be "
                "resumed safely, so nothing will be retried; run it again from "
                "the start.".format(index + 1, pages, error))

        sent = offset + len(chunk)
        elapsed = max(time.monotonic() - started, 1e-6)
        info("   page {:>3}/{:<3} {:>7} / {} bytes  ({:.0f} B/s)".format(
            index + 1, pages, sent, size, sent / elapsed))


def probe(arguments):
    serial = import_serial()

    if arguments.list_ports:
        list_ports(serial)
        return EXIT_OK

    if not arguments.port:
        raise OtaError("--port is required (use --list-ports to see the options)")

    if arguments.flash and not arguments.image:
        raise OtaError("--flash needs an image, pass --image")

    header, code = parse_image(arguments.image, arguments.verbose)
    magic, size, crc, version, _ = struct.unpack(HEADER_STRUCT, header)

    try:
        port = serial.Serial(
            arguments.port,
            baudrate=arguments.baud,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.1,
            write_timeout=arguments.page_timeout)
    except serial.SerialException as error:
        raise OtaError("cannot open {}: {}".format(arguments.port, error))

    with port:
        port.reset_input_buffer()

        info("")
        info("port             {} at {} 8N1".format(arguments.port, arguments.baud))
        info("")
        info("1. waiting for CMD_STM32_READY (0x{:02X})".format(CMD_STM32_READY))
        info("   press the user button in the application if the device is not in OTA mode")

        if not wait_for_byte(port, CMD_STM32_READY, arguments.ready_timeout,
                             arguments.verbose):
            info("   TIMEOUT after {:.1f} s: the device never announced itself"
                 .format(arguments.ready_timeout))
            return EXIT_NO_DEVICE

        info("   the device announced itself")

        info("2. sending the {}-byte image header".format(HEADER_STRUCT_SIZE))
        write_all(port, header, arguments.verbose)

        info("3. waiting for ASTRA_ACK (0x{:02X}) or ASTRA_NACK (0x{:02X})".format(
            ASTRA_ACK, ASTRA_NACK))
        read_status(port, ASTRA_ACK, arguments.ack_timeout, arguments.verbose)
        info("   the device accepted the header, version {}".format(version))

        if not arguments.flash:
            info("")
            info("verified only: the header was accepted, nothing was erased")
            info("run again with --flash to write {} bytes".format(size))
            return EXIT_OK

        info("4. waiting for ASTRA_ERASE_DONE (0x{:02X})".format(ASTRA_ERASE_DONE))
        erase_started = time.monotonic()
        read_status(port, ASTRA_ERASE_DONE, arguments.erase_timeout,
                    arguments.verbose)
        info("   the slot is erased ({:.1f} s)".format(
            time.monotonic() - erase_started))

        send_body(port, code, size, arguments)

        info("6. waiting for ASTRA_IMAGE_OK (0x{:02X})".format(ASTRA_IMAGE_OK))
        read_status(port, ASTRA_IMAGE_OK, arguments.verify_timeout,
                    arguments.verbose)

    info("")
    info("the device committed the header and jumps to the new image")
    info("CRC 0x{:08X} over {} bytes, version {}".format(crc, size, version))
    return EXIT_OK


def parse_arguments(argv):
    parser = argparse.ArgumentParser(
        description="Flash an Astra application image over UART.")

    parser.add_argument("--port", help="serial port of the device, for example COM7")
    parser.add_argument("--image", default=DEFAULT_IMAGE,
                        help="packed image to send (default: %(default)s)")
    parser.add_argument("--flash", action="store_true",
                        help="write the image; without it only the header is verified")
    parser.add_argument("--baud", type=int, default=115200,
                        help="baud rate of USART1 (default: %(default)s)")
    parser.add_argument("--ready-timeout", type=float, default=10.0,
                        help="seconds to wait for CMD_STM32_READY (default: %(default)s)")
    parser.add_argument("--ack-timeout", type=float, default=2.0,
                        help="seconds to wait for the header acknowledgement (default: %(default)s)")
    parser.add_argument("--erase-timeout", type=float, default=30.0,
                        help="seconds to wait for ASTRA_ERASE_DONE (default: %(default)s)")
    parser.add_argument("--page-timeout", type=float, default=10.0,
                        help="seconds to wait for one page acknowledgement (default: %(default)s)")
    parser.add_argument("--verify-timeout", type=float, default=10.0,
                        help="seconds to wait for the CRC verdict (default: %(default)s)")
    parser.add_argument("--list-ports", action="store_true",
                        help="list the serial ports and exit")
    parser.add_argument("--verbose", action="store_true",
                        help="report every write and every status byte")

    arguments = parser.parse_args(argv)

    if arguments.list_ports and arguments.port:
        parser.error("--list-ports does not take a port")

    return arguments


def main(argv=None):
    arguments = parse_arguments(argv)

    try:
        return probe(arguments)
    except OtaError as error:
        print("error: {}".format(error), file=sys.stderr)
        return EXIT_FAILED


if __name__ == "__main__":
    sys.exit(main())