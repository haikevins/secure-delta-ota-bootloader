#!/usr/bin/env python3
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
CANDIDATE = ROOT / "node-stm32f103/application/out-phase6-candidate/application.bin"

def main() -> int:
    port = os.environ.get("PORT", "").strip()
    if not port:
        print("Usage: make phase6-hw-test PORT=/dev/ttyUSB0")
        return 2
    if not CANDIDATE.is_file():
        print(f"Missing candidate: {CANDIDATE}")
        print("Run: make phase6-candidate")
        return 2

    return subprocess.call([
        sys.executable,
        str(ROOT / "tools/uart_ota_sender.py"),
        "--port", port,
        "ota",
        str(CANDIDATE),
        "--update-id", "0x60060001",
        "--target-version", "0x00000002",
    ], cwd=ROOT, env=os.environ.copy())

if __name__ == "__main__":
    raise SystemExit(main())
