#!/usr/bin/env python3
from __future__ import annotations
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

def main() -> int:
    port = os.environ.get("PORT", "").strip()
    if not port:
        print("Usage: make phase5-hw-test PORT=/dev/ttyUSB0")
        return 2
    return subprocess.call([
        sys.executable,
        str(ROOT / "tools/uart_ota_sender.py"),
        "--port", port,
        "self-test",
        "--size", "1024",
        "--update-id", "0x50050001",
    ], cwd=ROOT, env=os.environ.copy())

if __name__ == "__main__":
    raise SystemExit(main())
