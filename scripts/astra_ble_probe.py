"""Low-level BLE probe for the Astra OTA bridge (nRF52840).

Connects to the bridge, subscribes to the TX characteristic and prints every
notification with a timestamp, its length and its bytes.  Optionally writes
raw frames into the RX characteristic, which is the fastest way to tell a
UART/link problem apart from a protocol problem.

Examples
--------
    # just listen for 8 seconds
    python scripts/astra_ble_probe.py --seconds 8

    # send GET_INFO and watch the reply
    python scripts/astra_ble_probe.py --seconds 6 --write "A5 02 00 00 FC A2"

    # send GET_INFO, wait, then ABORT
    python scripts/astra_ble_probe.py --seconds 8 \
        --write "A5 02 00 00 FC A2" --write-at 3 "A5 13 00 00 AF D6"
"""

from __future__ import annotations

import argparse
import asyncio
import sys
import time
from typing import Iterable

from bleak import BleakClient, BleakScanner

SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  # write  -> nRF -> STM32
TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  # notify <- nRF <- STM32
CTRL_UUID = "6e400004-b5a3-f393-e0a9-e50e24dcca9e"  # write 0x01 -> STM32 reset into OTA


class Line:
    """Accumulates notifications so whole frames can be printed."""

    def __init__(self) -> None:
        self.buf = bytearray()
        self.start = 0.0

    def feed(self, chunk: bytes, stamp: float) -> list[str]:
        self.buf.extend(chunk)

        out: list[str] = []
        while True:
            if len(self.buf) < 4:
                return out
            if self.buf[0] != 0xA5:
                # Resynchronise: report the garbage, drop up to the next SOF.
                idx = self.buf.find(b"\xA5")
                junk = self.buf[: idx if idx > 0 else len(self.buf)]
                if junk:
                    out.append(f"{stamp:8.3f}s  junk {len(junk):3d}  {junk.hex(' ').upper()}")
                del self.buf[: idx if idx > 0 else len(self.buf)]
                continue

            length = self.buf[2] | (self.buf[3] << 8)
            total = 6 + length
            if len(self.buf) < total:
                return out

            frame = bytes(self.buf[:total])
            del self.buf[:total]
            out.append(
                f"{stamp:8.3f}s  frame {len(frame):3d}  {frame.hex(' ').upper()}"
            )
        return out


def parse_frames(values: Iterable[str]) -> list[bytes]:
    frames = []
    for value in values:
        hexes = value.replace(",", " ").replace("0x", " ").split()
        frames.append(bytes(int(h, 16) for h in hexes))
    return frames


async def find_device(name: str, timeout: float):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        devices = await BleakScanner.discover(timeout=4.0)
        for dev in devices:
            if (dev.name or "") == name:
                return dev
        print(f"  ... {name!r} not seen yet", flush=True)
    return None


async def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", default="AstraCar")
    parser.add_argument("--address", default=None)
    parser.add_argument("--seconds", type=float, default=8.0)
    parser.add_argument("--scan-timeout", type=float, default=25.0)
    parser.add_argument(
        "--write",
        action="append",
        default=[],
        metavar="HEX",
        help="frame to write at t=0 (repeatable)",
    )
    parser.add_argument(
        "--write-at",
        action="append",
        nargs=2,
        default=[],
        metavar=("SECONDS", "HEX"),
        help="frame to write after SECONDS (repeatable)",
    )
    parser.add_argument(
        "--ctrl",
        action="store_true",
        help="write 0x01 to the control characteristic at t=0 (STM32 OTA trigger)",
    )
    parser.add_argument(
        "--chunk",
        type=int,
        default=0,
        help="split writes into chunks of this many bytes (default: MTU-3)",
    )
    parser.add_argument(
        "--burst",
        type=int,
        default=0,
        metavar="N",
        help="write N bytes of the 0x00..0xFF pattern (echo / link stress test)",
    )
    parser.add_argument(
        "--burst-at",
        type=float,
        default=1.0,
        metavar="SECONDS",
        help="when to send --burst (default: 1 s after connect)",
    )
    args = parser.parse_args()

    device = args.address
    if device is None:
        print(f"scanning for {args.name!r} ...")
        found = await find_device(args.name, args.scan_timeout)
        if found is None:
            print("device not found", file=sys.stderr)
            return 2
        device = found
        print(f"found {found.name} [{found.address}]")
    else:
        print(f"connecting to {device}")

    line = Line()
    t0 = time.monotonic()
    rx_chunks = 0
    rx_bytes = 0

    def on_notify(_sender, data: bytearray) -> None:
        nonlocal rx_chunks, rx_bytes

        stamp = time.monotonic() - t0
        rx_chunks += 1
        rx_bytes += len(data)
        print(
            f"{stamp:8.3f}s  notify {len(data):3d}  {bytes(data).hex(' ').upper()}",
            flush=True,
        )
        for text in line.feed(bytes(data), stamp):
            print(text, flush=True)

    client = BleakClient(device, timeout=30.0)
    for attempt in range(5):
        try:
            await client.connect()
            break
        except Exception as exc:  # noqa: BLE001 - any BLE failure is worth a retry
            print(f"  connect failed: {exc}", flush=True)
            await asyncio.sleep(2.0)

    if not client.is_connected:
        print("could not connect", flush=True)
        return 3

    print(f"connected, mtu={client.mtu_size}", flush=True)

    try:
        await client.start_notify(TX_UUID, on_notify)

        if args.ctrl:
            await client.write_gatt_char(CTRL_UUID, b"\x01", response=True)
            print("trigger pulse sent", flush=True)

        chunk = args.chunk or max(20, client.mtu_size - 3)

        async def send(payload: bytes, when: float) -> None:
            delay = when - (time.monotonic() - t0)
            if delay > 0:
                await asyncio.sleep(delay)
            for off in range(0, len(payload), chunk):
                part = payload[off : off + chunk]
                try:
                    await client.write_gatt_char(RX_UUID, part, response=True)
                except Exception as exc:  # noqa: BLE001 - a lost link must not hide the log
                    print(f"  write failed: {exc}", flush=True)
                    return
                print(
                    f"{time.monotonic() - t0:8.3f}s  write  {len(part):3d}  "
                    f"{part.hex(' ').upper()}",
                    flush=True,
                )

        jobs = [send(frame, 0.0) for frame in parse_frames(args.write)]
        jobs += [
            send(parse_frames([frame])[0], float(when))
            for when, frame in args.write_at
        ]
        if args.burst > 0:
            pattern = bytes(i & 0xFF for i in range(args.burst))
            jobs.append(send(pattern, args.burst_at))
        if jobs:
            await asyncio.gather(*jobs)

        await asyncio.sleep(args.seconds)
    finally:
        try:
            await client.disconnect()
        except Exception:  # noqa: BLE001 - the link may already be gone
            pass

    if line.buf:
        print(f"leftover {len(line.buf):3d}  {line.buf.hex(' ').upper()}")
    print(
        f"summary: {rx_chunks} notifications, {rx_bytes} bytes received",
        flush=True,
    )

    return 0


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
