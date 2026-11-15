#!/usr/bin/env python3
"""Pack an AstraApplication ELF into a flash image and an OTA payload.

The linker script keeps the application header in a dedicated Flash page at
APP_HEADER_ADDR and the code right after it.  We dump the ELF with objcopy,
patch the header (size + CRC32 of the code) and emit:

  <name>.bin  - complete Flash image starting at the header page
  <name>.ota  - 16 byte manifest + code, streamed by the Qt application
"""

import argparse
import pathlib
import struct
import subprocess
import sys
import tempfile
import zlib

HEADER_MAGIC = 0x52545341  # ASTRA_MAGIC_VALUE
MANIFEST_MAGIC = 0x41544F41  # "AOTA" as file bytes

HEADER_OFFSET_SIZE = 4
HEADER_OFFSET_CRC = 8
HEADER_OFFSET_VERSION = 12
HEADER_OFFSET_OTA_REQUEST = 16


def decode_version(value: int) -> str:
    return f"{value // 10000}.{(value // 100) % 100}.{value % 100}"


def fail(message: str) -> int:
    print(f"astra_image: {message}", file=sys.stderr)
    return 1


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--objcopy", required=True)
    parser.add_argument("--elf", required=True)
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--header-addr", type=lambda v: int(v, 0), required=True)
    parser.add_argument("--app-start", type=lambda v: int(v, 0), required=True)
    parser.add_argument("--max-code-size", type=lambda v: int(v, 0), required=True)
    parser.add_argument("--name", default="AstraApplication")
    args = parser.parse_args()

    out_dir = pathlib.Path(args.output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory() as tmp:
        raw = pathlib.Path(tmp) / "dump.bin"
        subprocess.run(
            [args.objcopy, "-O", "binary", args.elf, str(raw)],
            check=True,
        )
        blob = raw.read_bytes()

    header_size = args.app_start - args.header_addr
    if header_size <= 0 or len(blob) < header_size:
        return fail("unexpected image layout (header page is missing)")

    header = bytearray(blob[:header_size])
    magic, _, _, version, _ = struct.unpack_from("<IIIII", header, 0)

    if magic != HEADER_MAGIC:
        return fail("application header magic is missing in the ELF")

    code = blob[header_size:]
    if not code:
        return fail("application code is empty")

    # The bootloader writes whole double words, so the code size (and CRC
    # range) is rounded up to 8 bytes and padded with erased-Flash 0xFF.
    code_size = (len(code) + 7) & ~7
    if code_size > args.max_code_size:
        return fail(
            f"code is {code_size} bytes, but the slot holds "
            f"{args.max_code_size}"
        )

    code = code.ljust(code_size, b"\xff")
    crc = zlib.crc32(code) & 0xFFFFFFFF

    # Flash image used by the debug adapter / a .bin programmer.
    struct.pack_into("<I", header, HEADER_OFFSET_SIZE, code_size)
    struct.pack_into("<I", header, HEADER_OFFSET_CRC, crc)
    struct.pack_into("<I", header, HEADER_OFFSET_VERSION, version)
    struct.pack_into("<I", header, HEADER_OFFSET_OTA_REQUEST, 0)

    bin_path = out_dir / f"{args.name}.bin"
    bin_path.write_bytes(bytes(header) + code)

    # OTA payload: manifest + code.
    manifest = struct.pack("<IIII", MANIFEST_MAGIC, version, code_size, crc)
    ota_path = out_dir / f"{args.name}.ota"
    ota_path.write_bytes(manifest + code)

    print(
        f"astra_image: {args.name}: version {decode_version(version)}, "
        f"{code_size} bytes, crc32 0x{crc:08X}"
    )
    print(f"astra_image: {bin_path}")
    print(f"astra_image: {ota_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
