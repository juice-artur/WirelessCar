#!/usr/bin/env python3
"""Fill the Astra application header image size and CRC after linking.

The application header occupies one complete Flash page ahead of the vector
table (see ProjectAstra/AstraSTM32/Common/FlashLayout.cmake).  Its `version`
field is known at compile time, but `size` and `crc` describe the linked image
and cannot be resolved until the final binary exists.  This script is the
post-link step that writes them.

It patches both artifacts so that every way of programming the target produces
the same image:

  1. Read __flash_image_end from the linker script symbol table.  It is the
     address just past the last code byte.
  2. Flatten the ELF with objcopy to obtain a gap-filled binary.
  3. size = __flash_image_end - image_start
     crc  = CRC-32/ISO-HDLC over the code range only.  The header page is not
            part of the range, so there is no circular dependency.
  4. Write the header struct and update the .header section of the ELF.
  5. Re-flatten the patched ELF and confirm it reproduces the same binary.

The addresses come from CMake, which is the single source of truth for the
layout.  When the ELF also exports them, the two are compared so that a stale
generated linker script is caught rather than silently mis-packed.

The CRC must match zlib.crc32 and the bitwise implementation in
ProjectAstra/AstraSTM32/AstraBootloader/BootloaderCore/Src/AstraCrc32.c.
"""

import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

HEADER_MAGIC = 0x52545341
HEADER_STRUCT = "<IIIII"
HEADER_STRUCT_SIZE = 20
OTA_NOT_REQUESTED = 0

IMAGE_END_SYMBOL = "__flash_image_end"


class ImageError(Exception):
    """Raised when the linked image cannot be packed consistently."""


def run(command):
    """Run a toolchain command and fail loudly with its diagnostics."""
    try:
        completed = subprocess.run(
            command,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except FileNotFoundError as error:
        raise ImageError("tool not found: {}".format(command[0])) from error
    except subprocess.CalledProcessError as error:
        detail = error.stderr.decode("utf-8", "replace").strip()
        raise ImageError(
            "{} failed (exit {}): {}".format(command[0], error.returncode, detail)
        ) from error
    return completed.stdout.decode("utf-8", "replace")


def read_symbols(nm, elf):
    """Return a name to address map for every symbol in the ELF."""
    symbols = {}
    for line in run([nm, elf]).splitlines():
        fields = line.split()
        if len(fields) == 3:
            symbols[fields[2]] = (int(fields[0], 16), fields[1])
    return symbols


def read_image_end(symbols, elf):
    """Return __flash_image_end, the address just past the last code byte.

    Only an absolute symbol is accepted.  GNU ld reclassifies plain
    assignments made inside SECTIONS according to whichever output section
    happens to be current, so a section-relative symbol would not carry a
    usable address here.
    """
    entry = symbols.get(IMAGE_END_SYMBOL)
    if entry is None:
        raise ImageError(
            "ELF {} does not export {}; the generated linker script is out of date".format(
                elf, IMAGE_END_SYMBOL
            )
        )

    address, kind = entry
    if kind not in ("A", "a"):
        raise ImageError(
            "ELF {} exports {} with type '{}' rather than absolute".format(
                elf, IMAGE_END_SYMBOL, kind
            )
        )
    return address


def cross_check(elf, symbols, expected):
    """Fail when the ELF disagrees with the addresses CMake supplied."""
    for name, value in expected.items():
        entry = symbols.get(name)
        if entry is not None and entry[0] != value:
            raise ImageError(
                "{} is 0x{:08X} in the ELF but CMake supplied 0x{:08X}; "
                "rebuild from a clean cache".format(name, entry[0], value)
            )


def flatten(objcopy, elf, binary):
    """Write the contiguous loadable image of `elf` to `binary`.

    objcopy follows load addresses, so the RAM-resident .data section is
    emitted at its Flash LMA.  objcopy's own default gap fill is zero, which is
    what the FlashLayout.h asserts rely on; passing --gap-fill explicitly makes
    this objcopy build pad across the LMA discontinuity instead.
    """
    run([objcopy, "-O", "binary", elf, binary])
    with open(binary, "rb") as stream:
        return stream.read()


def build_header(size, crc, version):
    """Return the packed AppHeader_t."""
    return struct.pack(
        HEADER_STRUCT, HEADER_MAGIC, size, crc, version, OTA_NOT_REQUESTED
    )


def verify(image, label, offset, size, crc, version):
    """Assert that `image` carries the expected header and a matching CRC."""
    if len(image) < HEADER_STRUCT_SIZE:
        raise ImageError("{}: image is shorter than the header".format(label))

    magic, image_size, image_crc, image_version, ota_request = struct.unpack(
        HEADER_STRUCT, image[0:HEADER_STRUCT_SIZE]
    )

    if magic != HEADER_MAGIC:
        raise ImageError(
            "{}: magic is 0x{:08X}, expected 0x{:08X}".format(label, magic, HEADER_MAGIC)
        )
    if image_size != size:
        raise ImageError(
            "{}: size is {}, expected {}".format(label, image_size, size)
        )
    if image_crc != crc:
        raise ImageError(
            "{}: crc is 0x{:08X}, expected 0x{:08X}".format(label, image_crc, crc)
        )
    if image_version != version:
        raise ImageError(
            "{}: version is {}, expected {}".format(label, image_version, version)
        )
    if ota_request != OTA_NOT_REQUESTED:
        raise ImageError(
            "{}: otaRequest is {}, expected {} for a freshly built image".format(
                label, ota_request, OTA_NOT_REQUESTED
            )
        )

    code = image[offset:offset + size]
    if len(code) != size:
        raise ImageError("{}: the code range is truncated".format(label))

    actual = zlib.crc32(code) & 0xFFFFFFFF
    if actual != crc:
        raise ImageError(
            "{}: recomputed CRC 0x{:08X} does not match the header 0x{:08X}".format(
                label, actual, crc
            )
        )


def pack(elf, out_bin, out_elf, nm, objcopy, version, header_addr, image_start, max_code_size):
    """Fill the header of `elf` and write `out_bin` and `out_elf`."""
    if image_start <= header_addr:
        raise ImageError(
            "image start 0x{:08X} must follow the header at 0x{:08X}".format(
                image_start, header_addr
            )
        )

    symbols = read_symbols(nm, elf)
    cross_check(elf, symbols, {"__app_header_addr": header_addr,
                               "__app_image_start": image_start})
    image_end = read_image_end(symbols, elf)

    if image_end < image_start:
        raise ImageError(
            "image end 0x{:08X} precedes the image start 0x{:08X}".format(
                image_end, image_start
            )
        )

    header_size = image_start - header_addr
    if header_size < HEADER_STRUCT_SIZE:
        raise ImageError("the header page is only {} bytes".format(header_size))

    size = image_end - image_start
    if size == 0:
        raise ImageError("the linked image is empty")
    if size > max_code_size:
        raise ImageError(
            "the image is {} bytes, which exceeds the {} byte code region".format(
                size, max_code_size
            )
        )

    with tempfile.TemporaryDirectory(prefix="astra-image-") as work:
        raw = flatten(objcopy, elf, os.path.join(work, "raw.bin"))

        expected_length = image_end - header_addr
        if len(raw) != expected_length:
            raise ImageError(
                "the binary spans 0x{:08X}..0x{:08X} but the linker reports an "
                "image end of 0x{:08X}".format(
                    header_addr, header_addr + len(raw), image_end
                )
            )

        crc = zlib.crc32(raw[header_size:header_size + size]) & 0xFFFFFFFF

        page = bytearray(raw[:header_size])
        page[0:HEADER_STRUCT_SIZE] = build_header(size, crc, version)
        page_bin = os.path.join(work, "header.bin")
        with open(page_bin, "wb") as stream:
            stream.write(page)

        run([objcopy, "--update-section", ".header={}".format(page_bin), elf, out_elf])

        # Derive the shipped binary from the patched ELF so that both artifacts
        # describe byte-for-byte the same image.
        final = flatten(objcopy, out_elf, out_bin)

    verify(final, "the patched ELF", header_size, size, crc, version)

    return size, crc


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--elf", required=True, help="linked ELF to read")
    parser.add_argument("--bin", required=True, help="packed binary to write")
    parser.add_argument("--patched-elf", required=True, help="ELF with the header filled in")
    parser.add_argument("--nm", required=True, help="arm-none-eabi-nm executable")
    parser.add_argument("--objcopy", required=True, help="arm-none-eabi-objcopy executable")
    parser.add_argument("--header-addr", required=True, type=lambda v: int(v, 0),
                        help="address of the application header")
    parser.add_argument("--image-start", required=True, type=lambda v: int(v, 0),
                        help="address of the first code byte, just after the header")
    parser.add_argument("--version", required=True, type=int, help="application version")
    parser.add_argument("--max-code-size", type=lambda v: int(v, 0), default=0x19800,
                        help="maximum application code size in bytes")
    arguments = parser.parse_args(argv)

    for tool, label in ((arguments.nm, "nm"), (arguments.objcopy, "objcopy")):
        if not shutil.which(tool) and not os.path.isfile(tool):
            raise ImageError("{} is not available: {}".format(label, tool))

    for output in (arguments.bin, arguments.patched_elf):
        os.makedirs(os.path.dirname(os.path.abspath(output)), exist_ok=True)

    size, crc = pack(
        arguments.elf,
        arguments.bin,
        arguments.patched_elf,
        arguments.nm,
        arguments.objcopy,
        arguments.version,
        arguments.header_addr,
        arguments.image_start,
        arguments.max_code_size,
    )

    print(
        "Astra image: size={} crc=0x{:08X} version={}".format(
            size, crc, arguments.version
        )
    )
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except ImageError as error:
        print("astra_image: {}".format(error), file=sys.stderr)
        sys.exit(1)
