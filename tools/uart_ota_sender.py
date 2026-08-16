#!/usr/bin/env python3
"""PC UART client for Secure Delta OTA Phase 5/6/7."""
from __future__ import annotations

import argparse
from pathlib import Path
import sys
import struct
import time

from ota_uart_protocol import (
    CMD_ABORT, CMD_ACK, CMD_DATA, CMD_FINISH, CMD_HELLO, CMD_INSTALL, CMD_NACK,
    CMD_QUERY, CMD_RESUME, CMD_START, CMD_STATUS,
    Packet, ProtocolError,
    STATUS_OK, STATUS_PACKET_CRC_ERROR, STATUS_WRONG_SEQUENCE,
    UPDATE_ARTIFACT_READY, UPDATE_IDLE, UPDATE_RECEIVING,
    build_start_payload, cobs_encode, crc32, decode_frame,
    encode_frame, parse_ack, parse_hello, serialize_packet,
    CAP_DELTA_IMAGE,
    FW_IMAGE_DELTA,
    FW_IMAGE_FULL,
    CAP_SIGNATURE_VERIFY,
    STATUS_SIGNATURE_ERROR,
    STATUS_VERSION_REJECTED,
)

DEFAULT_BAUD = 115200
DEFAULT_TIMEOUT = 1.5
DEFAULT_RETRIES = 5
MAX_ARTIFACT = 128 * 1024
MAX_APPLICATION = 38 * 1024
APPLICATION_ADDRESS = 0x08006000
SRAM_BASE = 0x20000000
SRAM_END = 0x20005000

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

def transfer(link: SerialLink, data: bytes, update_id: int,
             target_version: int = 0,
             artifact_type: int = FW_IMAGE_FULL,
             base_version: int = 0,
             container_header_size: int = 0) -> None:
    if not data:
        raise ProtocolError("artifact must not be empty")
    if len(data) > MAX_ARTIFACT:
        raise ProtocolError("artifact exceeds 128 KiB incoming partition")
    if update_id == 0:
        raise ProtocolError("update_id must be non-zero")

    state = parse_hello(link.request(Packet(command=CMD_QUERY)))
    offset = 0
    sequence = 0

    if (state.update_state == UPDATE_RECEIVING and
            state.active_update_id == update_id and
            state.expected_artifact_size == len(data)):
        resume = require_ack(
            link.request(Packet(command=CMD_RESUME, update_id=update_id)),
            "RESUME"
        )
        offset = resume.next_expected_offset
        if offset > len(data) or (offset % 256) != 0:
            raise ProtocolError(
                f"device returned invalid persistent resume offset {offset}"
            )
        sequence = (offset // 256) & 0xFFFF
        print(
            f"Persistent RESUME: offset={offset}/{len(data)} "
            f"sequence={sequence}"
        )

    elif (state.update_state == UPDATE_ARTIFACT_READY and
          state.active_update_id == update_id and
          state.expected_artifact_size == len(data)):
        print(
            f"Artifact already ready: update_id=0x{update_id:08X} "
            f"size={len(data)}"
        )
        return

    else:
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
                payload=build_start_payload(
                    len(data),
                    artifact_crc,
                    artifact_type=artifact_type,
                    base_version=base_version,
                    target_version=target_version,
                    container_header_size=container_header_size,
                )
            )),
            "START"
        )

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

    # FINISH remains idempotent across retry.
    finish_retry = require_ack(link.request(finish_packet), "FINISH retry")
    if finish_retry.update_state != UPDATE_ARTIFACT_READY:
        raise ProtocolError("FINISH retry lost ARTIFACT_READY")

    print(
        f"Transfer PASS update_id=0x{update_id:08X} "
        f"size={len(data)} crc32=0x{crc32(data):08X}"
    )


def wait_for_application_version(link: SerialLink,
                                 target_version: int,
                                 wait_seconds: float = 20.0,
                                 required_state: int | None = None):
    deadline = time.monotonic() + wait_seconds
    old_timeout = link.timeout
    old_retries = link.retries
    link.timeout = 0.6
    link.retries = 1
    try:
        time.sleep(0.25)
        link.serial.reset_input_buffer()
        sequence = 0
        while time.monotonic() < deadline:
            try:
                response = link.request(Packet(
                    command=CMD_HELLO, update_id=0, sequence=sequence
                ))
                info = parse_hello(response)
                if (info.application_version == target_version and
                        (required_state is None or
                         info.update_state == required_state)):
                    return info
            except (TimeoutError, ProtocolError, OSError):
                pass
            sequence = (sequence + 1) & 0xFFFF
            time.sleep(0.15)
    finally:
        link.timeout = old_timeout
        link.retries = old_retries
    raise TimeoutError(
        f"updated application version 0x{target_version:08X} "
        f"did not appear within {wait_seconds:.1f}s"
    )


def validate_application_binary(data: bytes) -> None:
    if not 8 <= len(data) <= MAX_APPLICATION:
        raise ProtocolError(
            f"application size must be 8..{MAX_APPLICATION} bytes"
        )
    initial_msp = int.from_bytes(data[0:4], "little")
    reset_handler = int.from_bytes(data[4:8], "little")
    if not SRAM_BASE <= initial_msp <= SRAM_END or (initial_msp & 7):
        raise ProtocolError(f"invalid application MSP 0x{initial_msp:08X}")
    if (reset_handler & 1) == 0:
        raise ProtocolError(
            f"application Reset_Handler is not Thumb: 0x{reset_handler:08X}"
        )
    reset_code = reset_handler & ~1
    if not APPLICATION_ADDRESS <= reset_code < APPLICATION_ADDRESS + len(data):
        raise ProtocolError(
            f"application Reset_Handler outside image: 0x{reset_handler:08X}"
        )

def full_ota(link: SerialLink, data: bytes,
             update_id: int, target_version: int) -> None:
    if target_version == 0:
        raise ProtocolError("target_version must be non-zero")
    validate_application_binary(data)

    transfer(link, data, update_id, target_version=target_version)

    install_packet = Packet(
        command=CMD_INSTALL,
        update_id=update_id,
        offset=len(data),
        sequence=0,
    )

    try:
        install_response = link.request(install_packet)
        require_ack(install_response, "INSTALL")
        print("INSTALL ACK: PASS; waiting for bootloader install/reboot")
    except TimeoutError:
        # The ACK can be lost exactly at reset. The authoritative success check
        # is that the new application comes back and identifies its version.
        print("INSTALL ACK not observed; waiting for updated application")

    info = wait_for_application_version(
        link,
        target_version,
        wait_seconds=35.0,
        required_state=UPDATE_IDLE,
    )

    print(
        f"Full OTA + trial confirmation PASS update_id=0x{update_id:08X} "
        f"target_version=0x{target_version:08X} size={len(data)}"
    )


PHASE13_DELTA_MAGIC = 0x50333144
PHASE13_DELTA_HEADER_SIZE = 48
PHASE13_DELTA_PREFIX = struct.Struct("<IHHIIIIIIIII")
PHASE13_DELTA_CRC = struct.Struct("<I")


def parse_phase13_delta_artifact(data: bytes) -> dict[str, int]:
    if len(data) < PHASE13_DELTA_HEADER_SIZE:
        raise ProtocolError("Phase-13 delta artifact is too short")

    values = PHASE13_DELTA_PREFIX.unpack_from(data)
    stored_header_crc = PHASE13_DELTA_CRC.unpack_from(
        data, PHASE13_DELTA_HEADER_SIZE - 4
    )[0]

    if values[0] != PHASE13_DELTA_MAGIC:
        raise ProtocolError("not a D13P delta artifact")
    if values[1] != 1 or values[2] != PHASE13_DELTA_HEADER_SIZE:
        raise ProtocolError("unsupported D13P format/header size")
    if crc32(data[:PHASE13_DELTA_HEADER_SIZE - 4]) != stored_header_crc:
        raise ProtocolError("D13P header CRC mismatch")

    info = {
        "base_version": values[3],
        "target_version": values[4],
        "base_size": values[5],
        "patch_size": values[6],
        "target_size": values[7],
        "target_address": values[8],
        "base_crc32": values[9],
        "target_crc32": values[10],
        "patch_crc32": values[11],
    }

    if PHASE13_DELTA_HEADER_SIZE + info["patch_size"] != len(data):
        raise ProtocolError("D13P patch size/total size mismatch")
    if crc32(data[PHASE13_DELTA_HEADER_SIZE:]) != info["patch_crc32"]:
        raise ProtocolError("D13P patch CRC mismatch")
    if info["target_address"] != APPLICATION_ADDRESS:
        raise ProtocolError("D13P target address mismatch")

    return info


def delta_ota(link: SerialLink,
              data: bytes,
              update_id: int) -> None:
    info = parse_phase13_delta_artifact(data)

    hello = parse_hello(
        link.request(Packet(command=CMD_QUERY))
    )

    if (hello.capability_flags & CAP_DELTA_IMAGE) == 0:
        raise ProtocolError("STM32 does not advertise delta capability")

    if hello.application_version != info["base_version"]:
        raise ProtocolError(
            f"delta base mismatch: node=v{hello.application_version} "
            f"artifact=v{info['base_version']}"
        )

    transfer(
        link,
        data,
        update_id,
        target_version=info["target_version"],
        artifact_type=FW_IMAGE_DELTA,
        base_version=info["base_version"],
        container_header_size=PHASE13_DELTA_HEADER_SIZE,
    )

    install_packet = Packet(
        command=CMD_INSTALL,
        update_id=update_id,
        offset=len(data),
        sequence=0,
    )

    try:
        require_ack(
            link.request(install_packet),
            "DELTA INSTALL",
        )
        print(
            "DELTA INSTALL ACK: PASS; "
            "waiting for patch/install/trial"
        )
    except TimeoutError:
        print(
            "DELTA INSTALL ACK not observed; "
            "waiting for final application"
        )

    final = wait_for_application_version(
        link,
        info["target_version"],
        wait_seconds=45.0,
        required_state=UPDATE_IDLE,
    )

    print(
        f"Phase 13 Delta OTA PASS update_id=0x{update_id:08X} "
        f"base=v{info['base_version']} "
        f"target=v{final.application_version} "
        f"artifact={len(data)} patch={info['patch_size']}"
    )



PHASE14_CONTAINER_MAGIC = 0x544F4453
PHASE14_CONTAINER_HEADER_SIZE = 140
PHASE14_FIXED = struct.Struct("<IHHIIIIIIIII32s32sIHHHH")
PHASE14_EXT = struct.Struct("<IHHIII")
PHASE14_SIGNATURE_SIZE = 64


def parse_phase14_secure_container(data: bytes) -> dict[str, int]:
    if len(data) < PHASE14_CONTAINER_HEADER_SIZE + PHASE14_SIGNATURE_SIZE:
        raise ProtocolError("Phase-14 secure container is too short")

    fixed = PHASE14_FIXED.unpack_from(data, 0)
    extension = PHASE14_EXT.unpack_from(data, PHASE14_FIXED.size)

    if fixed[0] != PHASE14_CONTAINER_MAGIC:
        raise ProtocolError("not an SDOT secure container")
    if fixed[1] != 1 or fixed[2] != PHASE14_CONTAINER_HEADER_SIZE:
        raise ProtocolError("unsupported SDOT format/header size")
    if extension[0] != 0x31584353 or extension[1] != 1 or extension[2] != 20:
        raise ProtocolError("invalid SDOT SCX1 extension")
    if fixed[15] != 1 or fixed[16] != 1 or fixed[17] != PHASE14_SIGNATURE_SIZE:
        raise ProtocolError("unsupported SDOT hash/signature algorithm")

    image_type = fixed[5]
    base_version = fixed[7]
    target_version = fixed[8]
    payload_size = fixed[9]
    target_size = fixed[10]
    target_address = fixed[11]
    payload_crc32 = fixed[14]
    key_id = extension[3]
    base_size = extension[4]
    target_crc32 = extension[5]

    total = PHASE14_CONTAINER_HEADER_SIZE + payload_size + PHASE14_SIGNATURE_SIZE
    if total != len(data):
        raise ProtocolError("SDOT total length mismatch")
    if target_address != APPLICATION_ADDRESS:
        raise ProtocolError("SDOT target address mismatch")
    if crc32(
        data[PHASE14_CONTAINER_HEADER_SIZE:
             PHASE14_CONTAINER_HEADER_SIZE + payload_size]
    ) != payload_crc32:
        raise ProtocolError("SDOT payload CRC mismatch")
    if image_type not in (FW_IMAGE_FULL, FW_IMAGE_DELTA):
        raise ProtocolError("SDOT image type invalid")
    if image_type == FW_IMAGE_FULL and base_version != 0:
        raise ProtocolError("SDOT full image has non-zero base version")
    if image_type == FW_IMAGE_DELTA and base_version == 0:
        raise ProtocolError("SDOT delta has zero base version")

    return {
        "image_type": image_type,
        "base_version": base_version,
        "target_version": target_version,
        "payload_size": payload_size,
        "target_size": target_size,
        "base_size": base_size,
        "target_crc32": target_crc32,
        "key_id": key_id,
    }


def secure_ota(link: SerialLink,
               data: bytes,
               update_id: int) -> None:
    info = parse_phase14_secure_container(data)

    hello = parse_hello(
        link.request(Packet(command=CMD_QUERY))
    )

    if (hello.capability_flags & CAP_SIGNATURE_VERIFY) == 0:
        raise ProtocolError(
            "STM32 does not advertise signature verification capability"
        )

    if info["target_version"] <= hello.application_version:
        raise ProtocolError(
            f"secure target version v{info['target_version']} is not newer "
            f"than node v{hello.application_version}"
        )

    if info["image_type"] == FW_IMAGE_DELTA:
        if (hello.capability_flags & CAP_DELTA_IMAGE) == 0:
            raise ProtocolError("STM32 does not advertise delta capability")
        if hello.application_version != info["base_version"]:
            raise ProtocolError(
                f"secure delta base mismatch: node=v{hello.application_version} "
                f"container=v{info['base_version']}"
            )

    transfer(
        link,
        data,
        update_id,
        target_version=info["target_version"],
        artifact_type=info["image_type"],
        base_version=info["base_version"],
        container_header_size=PHASE14_CONTAINER_HEADER_SIZE,
    )

    install_packet = Packet(
        command=CMD_INSTALL,
        update_id=update_id,
        offset=len(data),
        sequence=0,
    )

    try:
        require_ack(
            link.request(install_packet),
            "SECURE INSTALL",
        )
        print(
            "SECURE INSTALL ACK: PASS; "
            "waiting for signature/patch/install/trial"
        )
    except TimeoutError:
        print(
            "SECURE INSTALL ACK not observed; "
            "waiting for final application"
        )

    final = wait_for_application_version(
        link,
        info["target_version"],
        wait_seconds=60.0,
        required_state=UPDATE_IDLE,
    )

    print(
        f"Phase 14 Secure OTA PASS update_id=0x{update_id:08X} "
        f"type={'delta' if info['image_type'] == FW_IMAGE_DELTA else 'full'} "
        f"target=v{final.application_version} "
        f"container={len(data)} payload={info['payload_size']} "
        f"key_id=0x{info['key_id']:08X}"
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
    ota = sub.add_parser("ota")
    ota.add_argument("file", type=Path)
    ota.add_argument("--update-id", type=parse_u32)
    ota.add_argument("--target-version", type=parse_u32, required=True)
    delta = sub.add_parser("delta-ota")
    delta.add_argument("file", type=Path)
    delta.add_argument("--update-id", type=parse_u32)
    secure = sub.add_parser("secure-ota")
    secure.add_argument("file", type=Path)
    secure.add_argument("--update-id", type=parse_u32)
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
        elif args.action == "ota":
            data = args.file.read_bytes()
            update_id = args.update_id
            if update_id is None:
                update_id = int(time.time_ns()) & 0xFFFFFFFF
                if update_id == 0:
                    update_id = 1
            full_ota(link, data, update_id, args.target_version)
        elif args.action == "delta-ota":
            data = args.file.read_bytes()
            update_id = args.update_id
            if update_id is None:
                update_id = int(time.time_ns()) & 0xFFFFFFFF
                if update_id == 0:
                    update_id = 1
            delta_ota(link, data, update_id)
        elif args.action == "secure-ota":
            data = args.file.read_bytes()
            update_id = args.update_id
            if update_id is None:
                update_id = int(time.time_ns()) & 0xFFFFFFFF
                if update_id == 0:
                    update_id = 1
            secure_ota(link, data, update_id)
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
