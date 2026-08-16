#!/usr/bin/env python3
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from uart_ota_sender import ProtocolError, validate_application_binary  # noqa: E402

def expect_error(data: bytes) -> None:
    try:
        validate_application_binary(data)
    except ProtocolError:
        return
    raise AssertionError("invalid application accepted")

def main() -> int:
    size = 4096
    image = bytearray(b"\xFF" * size)
    image[0:4] = (0x20005000).to_bytes(4, "little")
    image[4:8] = (0x08006101).to_bytes(4, "little")
    validate_application_binary(bytes(image))

    bad = bytearray(image)
    bad[0:4] = (0x20000004).to_bytes(4, "little")
    expect_error(bytes(bad))

    bad = bytearray(image)
    bad[4:8] = (0x08006100).to_bytes(4, "little")
    expect_error(bytes(bad))

    bad = bytearray(image)
    bad[4:8] = (0x08008001).to_bytes(4, "little")
    expect_error(bytes(bad))

    expect_error(b"\x00" * 7)
    print("full-image OTA PC application preflight tests: PASS")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
