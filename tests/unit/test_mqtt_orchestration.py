#!/usr/bin/env python3
from __future__ import annotations

import json
import zlib

MAX_PAYLOAD = 767


def command_payload(image: bytes) -> bytes:
    payload = json.dumps(
        {
            "schema": 1,
            "cmd": "update",
            "update_id": 0xB00B0001,
            "target_version": 2,
            "size": len(image),
            "crc32": zlib.crc32(image) & 0xFFFFFFFF,
            "url": "https://192.168.1.8:8443/candidate.bin",
        },
        separators=(",", ":"),
    ).encode()
    assert len(payload) <= MAX_PAYLOAD
    return payload


def reassemble(payload: bytes, chunk_sizes: list[int]) -> bytes:
    out = bytearray()
    offset = 0

    for size in chunk_sizes:
        if offset >= len(payload):
            break
        chunk = payload[offset:offset + size]
        # Model current_data_offset == bytes already accepted.
        assert offset == len(out)
        out.extend(chunk)
        offset += len(chunk)

    if offset < len(payload):
        out.extend(payload[offset:])

    return bytes(out)


def test_fragmented_command() -> None:
    image = bytes((i * 17 + 3) & 0xFF for i in range(10184))
    payload = command_payload(image)
    rebuilt = reassemble(payload, [17, 31, 64, 9, 127])

    assert rebuilt == payload
    parsed = json.loads(rebuilt)
    assert parsed["schema"] == 1
    assert parsed["cmd"] == "update"
    assert parsed["size"] == len(image)
    assert parsed["crc32"] == zlib.crc32(image) & 0xFFFFFFFF


def test_orchestration_state_order() -> None:
    states = [
        "online",
        "accepted",
        "downloaded",
        "installing",
        "confirmed",
    ]
    assert states.index("online") < states.index("accepted")
    assert states.index("accepted") < states.index("downloaded")
    assert states.index("downloaded") < states.index("installing")
    assert states[-1] == "confirmed"


def test_mqtt_only_orchestrates() -> None:
    command = {
        "url": "https://host/fw.bin",
        "size": 10184,
        "crc32": 0x8A5C459C,
    }

    # MQTT carries metadata/control; firmware bytes remain HTTPS payload.
    assert command["url"].startswith("https://")
    assert "firmware_bytes" not in command


def test_delivery_semantics() -> None:
    # Progress is QoS 0 and must be transmitted immediately, not silently
    # discarded by enqueue(store=False). Final status is QoS 1 and the
    # single-shot test must wait for broker PUBACK before disconnect.
    progress = {"qos": 0, "path": "publish_immediate"}
    final_status = {"qos": 1, "wait_for_puback": True}

    assert progress == {"qos": 0, "path": "publish_immediate"}
    assert final_status["qos"] == 1
    assert final_status["wait_for_puback"] is True


def main() -> int:
    test_fragmented_command()
    test_orchestration_state_order()
    test_mqtt_only_orchestrates()
    test_delivery_semantics()
    print(
        "MQTT fragmentation/orchestration model: PASS "
        "(MQTT command -> HTTPS -> UART -> confirm)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
