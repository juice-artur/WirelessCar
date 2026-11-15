"""Full OTA run over BLE, without the Qt GUI.

Speaks the exact same protocol as WCControll: NUS RX/TX characteristics, frames
of 0xA5 | type | len u16 | payload | crc16, 2 KB blocks with stop-and-wait ACKs.

Examples
--------
    # the STM32 is already sitting in the bootloader
    python scripts/astra_ble_ota.py --image AstraStm32/AstraApplication/build/Debug/AstraApplication.ota

    # pulse the OTA request line first, exactly like the GUI does
    python scripts/astra_ble_ota.py --image ... --trigger
"""

from __future__ import annotations

import argparse
import asyncio
import struct
import sys
import time
import zlib
from typing import Optional

from bleak import BleakClient, BleakScanner

SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
CTRL_UUID = "6e400004-b5a3-f393-e0a9-e50e24dcca9e"

SOF = 0xA5
MSG_GET_INFO = 0x02
MSG_INFO = 0x82
MSG_BEGIN = 0x10
MSG_DATA = 0x11
MSG_END = 0x12
MSG_ABORT = 0x13
MSG_ACK = 0x90
MSG_STATUS = 0x91
MSG_DONE = 0x92

BLOCK_SIZE = 2048
MAX_PAYLOAD = BLOCK_SIZE + 8
MANIFEST_MAGIC = 0x41544F41

RESULT_NAMES = {
    0: "OK",
    1: "wrong state",
    2: "bad size",
    3: "CRC mismatch",
    4: "flash error",
    5: "bad offset",
    6: "bad frame",
}


def crc16_ccitt_false(data: bytes) -> int:
    """CRC-16/CCITT-FALSE, as AstraCrc16()."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else (crc << 1)
            crc &= 0xFFFF
    return crc


def build_frame(msg_type: int, payload: bytes = b"") -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload too large")
    head = bytes([msg_type]) + struct.pack("<H", len(payload)) + payload
    return bytes([SOF]) + head + struct.pack("<H", crc16_ccitt_false(head))


class Frames:
    """Incremental frame parser fed by BLE notifications."""

    def __init__(self) -> None:
        self.buf = bytearray()
        self.queue: asyncio.Queue[tuple[int, bytes]] = asyncio.Queue()

    def feed(self, chunk: bytes) -> None:
        self.buf.extend(chunk)

        while True:
            if not self.buf:
                return

            if self.buf[0] != SOF:
                idx = self.buf.find(SOF)
                del self.buf[: idx if idx > 0 else len(self.buf)]
                continue

            if len(self.buf) < 4:
                return

            length = self.buf[2] | (self.buf[3] << 8)
            if length > MAX_PAYLOAD:
                del self.buf[:1]
                continue

            total = 6 + length
            if len(self.buf) < total:
                return

            frame = bytes(self.buf[:total])
            crc = frame[4 + length] | (frame[5 + length] << 8)
            if crc16_ccitt_false(frame[1 : 4 + length]) != crc:
                print("  dropping a corrupt frame", flush=True)
                del self.buf[:1]
                continue

            del self.buf[:total]
            self.queue.put_nowait((frame[1], frame[4 : 4 + length]))

    async def expect(self, timeout: float, what: str) -> Optional[tuple[int, bytes]]:
        try:
            return await asyncio.wait_for(self.queue.get(), timeout)
        except asyncio.TimeoutError:
            print(f"  timeout after {timeout:.1f}s waiting for {what}", flush=True)
            return None

    async def expect_any(self, types: set[int], timeout: float,
                         what: str) -> Optional[tuple[int, bytes]]:
        """Wait for one of `types`, discarding unsolicited frames (INFO)."""
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                print(f"  timeout after {timeout:.1f}s waiting for {what}",
                      flush=True)
                return None

            frame = await self.expect(remaining, what)
            if frame is None:
                return None
            if frame[0] in types:
                return frame
            print(f"  ignoring unsolicited 0x{frame[0]:02X}", flush=True)

    def drain(self) -> list[tuple[int, bytes]]:
        out = []
        while not self.queue.empty():
            out.append(self.queue.get_nowait())
        return out


def load_image(path: str) -> tuple[int, int, bytes]:
    with open(path, "rb") as handle:
        blob = handle.read()

    if len(blob) < 16:
        raise SystemExit(f"{path}: too small for an .ota file")

    magic, version, code_size, crc32 = struct.unpack_from("<IIII", blob, 0)
    if magic != MANIFEST_MAGIC:
        raise SystemExit(f"{path}: bad manifest magic 0x{magic:08X}")

    code = blob[16:]
    if len(code) != code_size:
        raise SystemExit(
            f"{path}: manifest says {code_size} bytes of code, file has {len(code)}"
        )

    actual = zlib.crc32(code) & 0xFFFFFFFF
    if actual != crc32:
        raise SystemExit(
            f"{path}: CRC mismatch, manifest 0x{crc32:08X}, computed 0x{actual:08X}"
        )

    return version, crc32, code


async def find_device(name: str, timeout: float):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        for dev in await BleakScanner.discover(timeout=4.0):
            if (dev.name or "") == name:
                return dev
        print(f"  ... {name!r} not seen yet", flush=True)
    return None


async def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--image", required=True, help="path to the .ota image")
    ap.add_argument("--name", default="AstraCar")
    ap.add_argument("--address", default=None)
    ap.add_argument("--scan-timeout", type=float, default=60.0)
    ap.add_argument("--trigger", action="store_true",
                    help="pulse the STM32 OTA request line first (GUI behaviour)")
    ap.add_argument("--wait-bootloader", type=float, default=6.0,
                    help="seconds to wait after the trigger pulse")
    args = ap.parse_args()

    version, crc32, code = load_image(args.image)
    total = len(code)
    print(f"image: version {version // 10000}.{version // 100 % 100}.{version % 100}, "
          f"{total} bytes, crc32 0x{crc32:08X}")

    device = args.address
    if device is None:
        print(f"scanning for {args.name!r} ...", flush=True)
        found = await find_device(args.name, args.scan_timeout)
        if found is None:
            print("device not found", flush=True)
            return 2
        device = found
        print(f"found {found.name} [{found.address}]", flush=True)

    frames = Frames()
    client = BleakClient(device, timeout=30.0)

    for attempt in range(5):
        try:
            await client.connect()
            break
        except Exception as exc:  # noqa: BLE001 - retry any BLE failure
            print(f"  connect failed: {exc}", flush=True)
            await asyncio.sleep(2.0)

    if not client.is_connected:
        print("could not connect", flush=True)
        return 3

    print(f"connected, mtu={client.mtu_size}", flush=True)
    await client.start_notify(TX_UUID, lambda _s, data: frames.feed(bytes(data)))

    chunk = max(20, client.mtu_size - 3)

    async def write_frame(msg_type: int, payload: bytes = b"") -> bool:
        frame = build_frame(msg_type, payload)
        for off in range(0, len(frame), chunk):
            await client.write_gatt_char(RX_UUID, frame[off : off + chunk],
                                         response=True)
        return True

    try:
        if args.trigger:
            print("pulsing the OTA request line ...", flush=True)
            await client.write_gatt_char(CTRL_UUID, b"\x01", response=True)
            await asyncio.sleep(args.wait_bootloader)

        # ---------------------------------------------------------- handshake
        info = None
        for attempt in range(25):
            await write_frame(MSG_GET_INFO)
            reply = await frames.expect(0.4, "INFO")
            if reply is None:
                continue
            if reply[0] == MSG_INFO and len(reply[1]) >= 9:
                info = reply[1]
                break
            if reply[0] == MSG_STATUS:
                print(f"bootloader refused: {reply[1][0]}", flush=True)
                return 4

        if info is None:
            print("the bootloader did not answer GET_INFO", flush=True)
            return 5

        active_version, max_size, state = struct.unpack_from("<IIB", info, 0)
        print(f"bootloader: version {active_version // 10000}."
              f"{active_version // 100 % 100}.{active_version % 100}, "
              f"slot {max_size} bytes, state {state}", flush=True)

        # --------------------------------------------------------------- BEGIN
        t_start = time.monotonic()
        await write_frame(MSG_BEGIN, struct.pack("<III", total, crc32, version))
        reply = await frames.expect_any({MSG_ACK, MSG_STATUS}, 20.0,
                                        "ACK after BEGIN")
        if reply is None:
            return 6
        if reply[0] == MSG_STATUS:
            print(f"BEGIN rejected: {RESULT_NAMES.get(reply[1][0], reply[1][0])}",
                  flush=True)
            return 7
        next_offset = struct.unpack_from("<I", reply[1], 0)[0]
        if next_offset != 0:
            print(f"BEGIN ACK has offset {next_offset}, expected 0", flush=True)
            return 9
        print("BEGIN accepted", flush=True)

        # ---------------------------------------------------------- data blocks
        t_blocks = time.monotonic()
        for offset in range(0, total, BLOCK_SIZE):
            length = min(BLOCK_SIZE, total - offset)
            payload = struct.pack("<I", offset) + code[offset : offset + length]

            await write_frame(MSG_DATA, payload)

            expected_next = offset + length
            reply = await frames.expect_any({MSG_ACK, MSG_STATUS}, 10.0,
                                            f"ACK for offset {offset}")
            if reply is None:
                return 10

            if reply[0] == MSG_STATUS:
                print(f"block {offset} rejected: "
                      f"{RESULT_NAMES.get(reply[1][0], reply[1][0])}", flush=True)
                return 11

            got = struct.unpack_from("<I", reply[1], 0)[0]
            if got != expected_next:
                print(f"ACK offset {got}, expected {expected_next}", flush=True)
                return 13

            elapsed = max(time.monotonic() - t_blocks, 1e-3)
            print(f"  block {offset:5d}..{expected_next:5d} "
                  f"({expected_next * 100 // total:3d} %, "
                  f"{expected_next / elapsed / 1024:5.1f} KiB/s)", flush=True)

        # ------------------------------------------------------------------ END
        print("END, waiting for the CRC verdict ...", flush=True)
        await write_frame(MSG_END)

        reply = await frames.expect_any({MSG_DONE, MSG_STATUS}, 30.0, "DONE")
        if reply is None:
            return 15

        if reply[0] == MSG_DONE:
            print(f"OK: image verified and written in "
                  f"{time.monotonic() - t_start:.1f}s", flush=True)
            return 0

        code_byte = reply[1][0] if reply[1] else 255
        print(f"END rejected: {RESULT_NAMES.get(code_byte, code_byte)}", flush=True)
        return 16

    finally:
        try:
            await client.stop_notify(TX_UUID)
        except Exception:  # noqa: BLE001 - link may already be gone
            pass
        try:
            await client.disconnect()
        except Exception:  # noqa: BLE001 - link may already be gone
            pass


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
