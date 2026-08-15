#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import shutil
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (
    ROOT
    / "node-stm32f103/application/out-phase9-candidate/application.bin"
)
DESTINATION = ROOT / "gateway-esp32/main/phase9_candidate.bin"

APP_START = 0x08006000
APP_MAX = 38 * 1024
SRAM_BASE = 0x20000000
SRAM_END = 0x20005000

def fail(message: str) -> None:
    print(f"Phase 9 candidate prepare: FAIL: {message}")
    raise SystemExit(1)

def main() -> int:
    if not SOURCE.is_file():
        fail(f"missing {SOURCE}")

    data = SOURCE.read_bytes()
    if not 8 <= len(data) <= APP_MAX:
        fail(f"candidate size {len(data)} outside 1..{APP_MAX}")

    msp, reset = struct.unpack_from("<II", data)
    if not SRAM_BASE <= msp <= SRAM_END or (msp & 7):
        fail(f"invalid MSP 0x{msp:08X}")
    if (reset & 1) == 0:
        fail(f"reset handler is not Thumb: 0x{reset:08X}")
    if not APP_START <= (reset & ~1) < APP_START + len(data):
        fail(f"reset handler outside image: 0x{reset:08X}")

    DESTINATION.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(SOURCE, DESTINATION)

    print(
        f"Phase 9 ESP32 embedded candidate prepared: "
        f"{len(data)} bytes, crc32=0x{zlib.crc32(data) & 0xFFFFFFFF:08X}"
    )
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
