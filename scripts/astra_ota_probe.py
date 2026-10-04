"""Probe the Astra bootloader OTA handshake over UART.

The bootloader only speaks the beginning of the OTA protocol today.  Once the
application has requested an update, it announces itself and expects the image
header; erasing the slot, receiving the body, verifying the CRC and swapping the
metadata are not implemented yet, so this script cannot flash an image.

    device -> host   0xA5            CMD_STM32_READY, once after reset
    host   -> device 20 bytes        <IIIII>: magic, size, crc, version,
                                     otaRequest, taken verbatim from the first
                                     20 bytes of AstraApplication.bin
    device -> host   0x06            ASTRA_ACK, only if the header was accepted

The firmware is sent by pressing the user button in the application, which sets
otaRequest in the header page and resets the MCU.

Timing matters: the UART FIFO is disabled, so only one byte fits in the data
register, and the reception is not re-armed until the buffer has been consumed.
The host must therefore pause after every chunk instead of streaming freely.
--send-body exists to measure that behaviour and warns before it does it.

Usage:
    python scripts/astra_ota_probe.py --list-ports
    python scripts/astra_ota_probe.py --port COM7
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

# Mirrors Common/FlashLayout.h.in: the header owns one Flash page and the code
# is capped by APP_MAX_CODE_SIZE.
HEADER_PAGE_SIZE = 0x800
MAX_CODE_SIZE = 0x19800

DEFAULT_IMAGE = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "ProjectAstra", "AstraSTM32", "AstraApplication", "build", "Debug",
    "AstraApplication.bin")

EXIT_OK = 0
EXIT_NO_DEVICE = 1
EXIT_USAGE = 2


class ProbeError(Exception):
    """Raised when the probe cannot run at all."""


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
        raise ProbeError("cannot read {}: {}".format(path, error))

    if len(image) < HEADER_PAGE_SIZE + HEADER_STRUCT_SIZE:
        raise ProbeError(
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
        raise ProbeError("the image does not start with the Astra magic")

    if size == 0 or size > MAX_CODE_SIZE:
        raise ProbeError(
            "size {} is outside the slot limit 1..{}".format(size, MAX_CODE_SIZE))

    expected_length = HEADER_PAGE_SIZE + size
    if len(image) != expected_length:
        raise ProbeError(
            "{} is {} bytes, expected {} (0x800 header page + {} bytes of code)"
            .format(path, len(image), expected_length, size))

    # The same CRC-32/ISO-HDLC that astra_image.py wrote into the header.
    actual_crc = zlib.crc32(image[HEADER_PAGE_SIZE:HEADER_PAGE_SIZE + size]) & 0xFFFFFFFF
    if actual_crc != crc:
        raise ProbeError(
            "recomputed CRC 0x{:08X} does not match the header 0x{:08X}"
            .format(actual_crc, crc))
    detail("header CRC verified against the code range", verbose)

    return header, image[HEADER_PAGE_SIZE:]


def import_serial():
    try:
        # list_ports lives in a submodule that is not imported by "import serial".
        import serial.tools.list_ports
    except ImportError:
        raise ProbeError(
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


def write_all(port, payload, verbose):
    """Write every byte, because pyserial may accept a short write."""
    sent = 0
    while sent < len(payload):
        written = port.write(payload[sent:])
        if not written:
            raise ProbeError("the serial port stopped accepting data")
        sent += written
        detail("wrote {} of {} bytes".format(sent, len(payload)), verbose)

    return sent


def probe(arguments):
    serial = import_serial()

    if arguments.list_ports:
        list_ports(serial)
        return EXIT_OK

    if not arguments.port:
        raise ProbeError("--port is required (use --list-ports to see the options)")

    header, code = parse_image(arguments.image, arguments.verbose)

    try:
        port = serial.Serial(
            arguments.port,
            baudrate=arguments.baud,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.1,
            write_timeout=1.0)
    except serial.SerialException as error:
        raise ProbeError("cannot open {}: {}".format(arguments.port, error))

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
        info("   header sent: magic 0x{:08X}, size {}, crc 0x{:08X}".format(
            *struct.unpack(HEADER_STRUCT, header)[:3]))

        info("3. waiting for ASTRA_ACK (0x{:02X})".format(ASTRA_ACK))
        if wait_for_byte(port, ASTRA_ACK, arguments.ack_timeout, arguments.verbose):
            info("   the device accepted the header")
        else:
            info("   no ACK within {:.1f} s: the bootloader either rejected the"
                 .format(arguments.ack_timeout))
            info("   header or the firmware does not send one")

        if arguments.send_body:
            send_body(port, code, arguments)

    info("")
    info("the bootloader stops reading after the header, so nothing was written to flash")
    return EXIT_OK


def send_body(port, code, arguments):
    """Push the rest of the image to observe how the reception copes."""
    info("")
    info("WARNING: the bootloader does not read past the header, so these bytes")
    info("         will pile up in the data register and end in an overrun error")
    info("4. sending {} bytes in {} byte chunks every {:.3f} s".format(
        len(code), arguments.chunk_size, arguments.chunk_delay))

    started = time.monotonic()
    sent = 0

    for offset in range(0, len(code), arguments.chunk_size):
        chunk = code[offset:offset + arguments.chunk_size]
        write_all(port, chunk, arguments.verbose)
        sent += len(chunk)
        elapsed = time.monotonic() - started
        info("   {:>6} / {} bytes  ({:.0f} B/s)".format(sent, len(code), sent / elapsed))
        time.sleep(arguments.chunk_delay)


def parse_arguments(argv):
    parser = argparse.ArgumentParser(
        description="Probe the Astra bootloader OTA handshake over UART.")

    parser.add_argument("--port", help="serial port of the device, for example COM7")
    parser.add_argument("--image", default=DEFAULT_IMAGE,
                        help="packed image whose header is sent (default: %(default)s)")
    parser.add_argument("--baud", type=int, default=115200,
                        help="baud rate of USART1 (default: %(default)s)")
    parser.add_argument("--ready-timeout", type=float, default=10.0,
                        help="seconds to wait for CMD_STM32_READY (default: %(default)s)")
    parser.add_argument("--ack-timeout", type=float, default=2.0,
                        help="seconds to wait for ASTRA_ACK (default: %(default)s)")
    parser.add_argument("--chunk-size", type=int, default=256,
                        help="chunk size for --send-body (default: %(default)s)")
    parser.add_argument("--chunk-delay", type=float, default=0.05,
                        help="pause between chunks for --send-body (default: %(default)s)")
    parser.add_argument("--send-body", action="store_true",
                        help="also send the code range, which the device ignores today")
    parser.add_argument("--list-ports", action="store_true",
                        help="list the serial ports and exit")
    parser.add_argument("--verbose", action="store_true",
                        help="report every write and every ignored byte")

    arguments = parser.parse_args(argv)

    if arguments.chunk_size < 1 or arguments.chunk_size > 512:
        parser.error("--chunk-size must be between 1 and 512")

    return arguments


def main(argv=None):
    arguments = parse_arguments(argv)

    try:
        return probe(arguments)
    except ProbeError as error:
        print("error: {}".format(error), file=sys.stderr)
        return EXIT_USAGE


if __name__ == "__main__":
    sys.exit(main())