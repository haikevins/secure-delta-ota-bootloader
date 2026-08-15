#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "gateway-esp32/build"

def main() -> int:
    if not BUILD.exists():
        print("ESP32 build guard: build directory absent; fresh configure will be used.")
        return 0

    cache = BUILD / "CMakeCache.txt"
    ninja = BUILD / "build.ninja"

    if cache.is_file() and ninja.is_file():
        print("ESP32 build guard: existing CMake/Ninja build directory is valid.")
        return 0

    print(
        "ESP32 build guard: removing partial/invalid build directory: "
        f"{BUILD}"
    )
    shutil.rmtree(BUILD)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
