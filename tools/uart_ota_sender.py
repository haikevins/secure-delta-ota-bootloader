#!/usr/bin/env python3
"""PC UART client for Secure Delta OTA Phase 5."""
from __future__ import annotations

import argparse
from pathlib import Path
import sys
import time

from ota_uart_protocol import (
    CMD_ABORT, CMD_ACK, CMD_DATA, CMD_FINISH, CMD_HELLO, CMD_NACK,
    CMD_QUERY, CMD_RESUME, CMD_START, CMD_STATUS,
    Packet, ProtocolError,
    STATUS_OK, STATUS_PACKET_CRC_ERROR, STATUS_WRONG_SEQUENCE,
    UPDATE_ARTIFACT_READY, UPDATE_IDLE,
    build_start_payload, cobs_encode, crc32, decode_frame,
    encode_frame, parse_ack, parse_hello, serialize_packet,
)

DEFAULT_BAUD = 115200
DEFAULT_TIMEOUT = 1.5
DEFAULT_RETRIES = 5
MAX_ARTIFACT = 128 * 1024

def require_pyserial():
    try:
        import serial  # type: ignore
    except ImportError as exc:
        raise SystemExit(
            "pyserial is required.\n"
            "Install: python3 -m pip install -r tools/requirements-phase5.txt"
        ) from exc
    return serial

class SerialLink:
    def __init__(self, port: str, baud: int, timeout: float, retries: int):
        serial = require_pyserial()
        self.serial = serial.Serial(
            port=port, baudrate=baud, bytesize=8, parity="N", stopbits=1,
            timeout=0.05, write_timeout=timeout
        )
        self.timeout = timeout
        self.retries = retries
        self.serial.reset_input_buffer()

    def close(self) -> None:
        self.serial.close()

    def _read_frame(self) -> Packet:
        deadline = time.monotonic() + self.timeout
        encoded = bytearray()
        while time.monotonic() < deadline:
            chunk = self.serial.read(1)
            if not chunk:
                continue
            byte = chunk[0]
            if byte == 0:
                if not encoded:
                    continue
                try:
                    return decode_frame(bytes(encoded))
                except ProtocolError:
                    encoded.clear()
                    continue
            encoded.append(byte)
            if len(encoded) > 320:
                encoded.clear()
        raise TimeoutError("UART response timeout")

    def request(self, packet: Packet) -> Packet:
        frame = encode_frame(packet)
        for attempt in range(1, self.retries + 1):
            self.serial.write(frame)
            self.serial.flush()
            try:
                response = self._read_frame()
            except TimeoutError:
                if attempt == self.retries:
                    raise
                continue

            if response.command not in (CMD_ACK, CMD_NACK):
                continue
            if response.sequence != packet.sequence:
                continue
            if response.update_id != packet.update_id:
                continue
            return response
        raise TimeoutError("UART retry exhaustion")

    def request_corrupt_crc(self, packet: Packet) -> Packet:
        raw = bytearray(serialize_packet(packet))
        raw[-1] ^= 1
        self.serial.write(cobs_encode(bytes(raw)) + b"\x00")
        self.serial.flush()
        return self._read_frame()

def require_ack(response: Packet, context: str):
    info = parse_ack(response)
    if response.command != CMD_ACK or info.status != STATUS_OK:
        raise ProtocolError(
            f"{context}: NACK status=0x{info.status:02X} "
            f"state={info.update_state} next={info.next_expected_offset} "
            f"detail=0x{info.last_error_detail:08X}"
        )
    return info

def print_hello(info) -> None:
    print(
        f"protocol={info.protocol_version} "
        f"bootloader=0x{info.bootloader_version:08X} "
        f"application=0x{info.application_version:08X}"
    )
    print(
        f"product=0x{info.product_id:08X} hw=0x{info.hardware_revision:08X} "
        f"capabilities=0x{info.capability_flags:08X}"
    )
    print(
        f"state={info.update_state} last_status=0x{info.last_status:02X} "
        f"update_id=0x{info.active_update_id:08X} "
        f"next={info.next_expected_offset} expected={info.expected_artifact_size}"
    )

def control(link: SerialLink, command: int, update_id: int = 0):
    response = link.request(Packet(command=command, update_id=update_id))
    if command in (CMD_HELLO, CMD_QUERY, CMD_STATUS):
        info = parse_hello(response)
        print_hello(info)
        return info
    info = require_ack(response, f"command 0x{command:02X}")
    print(
        f"ACK state={info.update_state} next={info.next_expected_offset} "
        f"received={info.received_size}/{info.expected_size}"
    )
    return info

def transfer(link: SerialLink, data: bytes, update_id: int) -> None:
    if not data:
        raise ProtocolError("artifact must not be empty")
    if len(data) > MAX_ARTIFACT:
        raise ProtocolError("artifact exceeds 128 KiB incoming partition")
    if update_id == 0:
        raise ProtocolError("update_id must be non-zero")

    state = parse_hello(link.request(Packet(command=CMD_QUERY)))
    if state.update_state != UPDATE_IDLE:
        require_ack(
            link.request(Packet(command=CMD_ABORT,
                                update_id=state.active_update_id)),
            "ABORT before START"
        )

    artifact_crc = crc32(data)
    require_ack(
        link.request(Packet(
            command=CMD_START,
            update_id=update_id,
            payload=build_start_payload(len(data), artifact_crc)
        )),
        "START"
    )

    offset = 0
    sequence = 0
    while offset < len(data):
        chunk = data[offset:offset + 256]
        packet = Packet(CMD_DATA, update_id, offset, sequence, chunk)
        ack = require_ack(link.request(packet), f"DATA offset={offset}")
        expected = offset + len(chunk)
        if ack.next_expected_offset != expected:
            raise ProtocolError("DATA ACK offset mismatch")
        offset = expected
        sequence = (sequence + 1) & 0xFFFF
        print(f"\rDATA {offset}/{len(data)} bytes", end="", flush=True)
    print()

    finish_packet = Packet(CMD_FINISH, update_id, len(data), sequence)
    finish = require_ack(link.request(finish_packet), "FINISH")
    if finish.update_state != UPDATE_ARTIFACT_READY:
        raise ProtocolError("FINISH did not enter ARTIFACT_READY")

    # Retry FINISH once deliberately to verify idempotence.
    finish_retry = require_ack(link.request(finish_packet), "FINISH retry")
    if finish_retry.update_state != UPDATE_ARTIFACT_READY:
        raise ProtocolError("FINISH retry lost ARTIFACT_READY")

    print(
        f"Transfer PASS update_id=0x{update_id:08X} "
        f"size={len(data)} crc32=0x{artifact_crc:08X}"
    )

def self_test(link: SerialLink, size: int, update_id: int) -> None:
    if not 1 <= size <= 4096:
        raise ProtocolError("self-test size must be 1..4096")
    pattern = bytes(((i * 37 + 11) & 0xFF) for i in range(size))

    hello = parse_hello(link.request(Packet(command=CMD_HELLO)))
    print("HELLO: PASS")

    corrupt = link.request_corrupt_crc(Packet(command=CMD_QUERY, sequence=0x1234))
    corrupt_info = parse_ack(corrupt)
    if corrupt.command != CMD_NACK or \
       corrupt_info.status != STATUS_PACKET_CRC_ERROR:
        raise ProtocolError("CRC error did not return expected NACK")
    print("CRC NACK: PASS")

    if hello.update_state != UPDATE_IDLE:
        require_ack(
            link.request(Packet(CMD_ABORT, hello.active_update_id)),
            "self-test ABORT"
        )

    require_ack(
        link.request(Packet(
            command=CMD_START, update_id=update_id,
            payload=build_start_payload(size, crc32(pattern))
        )),
        "self-test START"
    )

    wrong = link.request(Packet(
        command=CMD_DATA, update_id=update_id,
        offset=0, sequence=7,
        payload=pattern[:min(256, size)]
    ))
    wrong_info = parse_ack(wrong)
    if wrong.command != CMD_NACK or \
       wrong_info.status != STATUS_WRONG_SEQUENCE:
        raise ProtocolError("wrong sequence did not return expected NACK")
    print("Sequence NACK: PASS")

    offset = 0
    sequence = 0
    last_packet = None
    while offset < size:
        chunk = pattern[offset:offset + 256]
        last_packet = Packet(CMD_DATA, update_id, offset, sequence, chunk)
        ack = require_ack(link.request(last_packet), "self-test DATA")
        offset += len(chunk)
        if ack.next_expected_offset != offset:
            raise ProtocolError("DATA progress mismatch")
        sequence = (sequence + 1) & 0xFFFF

    assert last_packet is not None
    duplicate = require_ack(link.request(last_packet), "duplicate DATA")
    if duplicate.next_expected_offset != size:
        raise ProtocolError("duplicate DATA changed progress")
    print("Duplicate DATA ACK: PASS")

    finish_packet = Packet(CMD_FINISH, update_id, size, sequence)
    finish = require_ack(link.request(finish_packet), "self-test FINISH")
    if finish.update_state != UPDATE_ARTIFACT_READY:
        raise ProtocolError("FINISH state mismatch")

    # Idempotent FINISH retry is required by the frozen retry policy.
    finish2 = require_ack(link.request(finish_packet), "self-test FINISH retry")
    if finish2.update_state != UPDATE_ARTIFACT_READY:
        raise ProtocolError("FINISH retry state mismatch")
    print("FINISH retry ACK: PASS")

    query = parse_hello(link.request(Packet(command=CMD_QUERY)))
    if query.update_state != UPDATE_ARTIFACT_READY or \
       query.next_expected_offset != size:
        raise ProtocolError("QUERY final state/progress mismatch")

    print(
        f"Phase 5 UART protocol hardware self-test: PASS "
        f"(size={size}, update_id=0x{update_id:08X})"
    )

def parse_u32(value: str) -> int:
    n = int(value, 0)
    if not 0 <= n <= 0xFFFFFFFF:
        raise argparse.ArgumentTypeError("must fit uint32")
    return n

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT)
    parser.add_argument("--retries", type=int, default=DEFAULT_RETRIES)
    sub = parser.add_subparsers(dest="action", required=True)
    sub.add_parser("hello")
    sub.add_parser("query")
    sub.add_parser("status")
    abort = sub.add_parser("abort")
    abort.add_argument("--update-id", type=parse_u32, default=0)
    resume = sub.add_parser("resume")
    resume.add_argument("--update-id", type=parse_u32, required=True)
    send = sub.add_parser("send")
    send.add_argument("file", type=Path)
    send.add_argument("--update-id", type=parse_u32)
    test = sub.add_parser("self-test")
    test.add_argument("--size", type=int, default=1024)
    test.add_argument("--update-id", type=parse_u32, default=0x50050001)
    args = parser.parse_args()

    link = SerialLink(args.port, args.baud, args.timeout, args.retries)
    try:
        if args.action == "hello":
            control(link, CMD_HELLO)
        elif args.action == "query":
            control(link, CMD_QUERY)
        elif args.action == "status":
            control(link, CMD_STATUS)
        elif args.action == "abort":
            control(link, CMD_ABORT, args.update_id)
        elif args.action == "resume":
            control(link, CMD_RESUME, args.update_id)
        elif args.action == "send":
            data = args.file.read_bytes()
            update_id = args.update_id
            if update_id is None:
                update_id = int(time.time_ns()) & 0xFFFFFFFF
                if update_id == 0:
                    update_id = 1
            transfer(link, data, update_id)
        elif args.action == "self-test":
            self_test(link, args.size, args.update_id)
    except (ProtocolError, TimeoutError, OSError) as exc:
        print(f"UART OTA error: {exc}", file=sys.stderr)
        return 1
    finally:
        link.close()
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
