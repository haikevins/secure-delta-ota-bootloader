#!/usr/bin/env python3
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from ota_uart_protocol import (  # noqa: E402
    CMD_DATA, Packet, ProtocolError, cobs_decode, cobs_encode,
    decode_frame, encode_frame, serialize_packet,
)

def main() -> int:
    packet = Packet(CMD_DATA, 0x12345678, 0x100, 7,
                    bytes([0, 1, 2, 0, 0xFE, 0xFF]))
    frame = encode_frame(packet)
    assert frame.endswith(b"\x00")
    assert b"\x00" not in frame[:-1]
    assert decode_frame(frame) == packet

    vector = bytes(range(1, 255)) + b"\x00" + bytes(range(64))
    assert cobs_decode(cobs_encode(vector)) == vector

    raw = bytearray(serialize_packet(packet))
    raw[-1] ^= 1
    try:
        decode_frame(cobs_encode(bytes(raw)) + b"\x00")
    except ProtocolError as exc:
        assert "CRC mismatch" in str(exc)
    else:
        raise AssertionError("bad CRC accepted")

    max_packet = Packet(CMD_DATA, payload=bytes(256))
    assert decode_frame(encode_frame(max_packet)) == max_packet

    print("Phase 5 Python protocol tests: PASS")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
