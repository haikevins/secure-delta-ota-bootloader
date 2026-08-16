#!/usr/bin/env python3
"""Pure-Python codec for Secure Delta OTA UART protocol v1."""
from __future__ import annotations

from dataclasses import dataclass
import struct
import zlib

MAGIC = 0xA55A
PROTOCOL_VERSION = 1
MAX_PAYLOAD = 256
MAX_ENCODED_FRAME = 320

CMD_HELLO = 0x01
CMD_QUERY = 0x02
CMD_START = 0x10
CMD_DATA = 0x11
CMD_FINISH = 0x12
CMD_ABORT = 0x13
CMD_RESUME = 0x14
CMD_INSTALL = 0x20
CMD_STATUS = 0x21
CMD_CONFIRM = 0x22
CMD_ACK = 0x70
CMD_NACK = 0x71

STATUS_OK = 0x00
STATUS_INVALID_PACKET = 0x01
STATUS_INVALID_STATE = 0x02
STATUS_WRONG_SEQUENCE = 0x03
STATUS_WRONG_OFFSET = 0x04
STATUS_PACKET_CRC_ERROR = 0x05
STATUS_STORAGE_ERROR = 0x06
STATUS_IMAGE_TOO_LARGE = 0x07
STATUS_UPDATE_ID_MISMATCH = 0x08
STATUS_BASE_MISMATCH = 0x09
STATUS_CONTAINER_ERROR = 0x0A
STATUS_SIGNATURE_ERROR = 0x0B
STATUS_VERSION_REJECTED = 0x0C

UPDATE_IDLE = 0
UPDATE_RECEIVING = 1
UPDATE_ARTIFACT_READY = 2
UPDATE_BACKING_UP = 7
UPDATE_INSTALLING = 8
UPDATE_VERIFYING_INSTALL = 9
UPDATE_TRIAL_BOOT = 10
UPDATE_CONFIRMED = 11
UPDATE_ROLLBACK = 12

CAP_FULL_IMAGE = 1 << 0
CAP_DELTA_IMAGE = 1 << 1
CAP_RESUME = 1 << 2
CAP_SIGNATURE_VERIFY = 1 << 3
CAP_ROLLBACK = 1 << 4

FW_IMAGE_FULL = 1
FW_IMAGE_DELTA = 2

HEADER = struct.Struct("<HBBIIHH")
CRC = struct.Struct("<I")
ACK_PAYLOAD = struct.Struct("<BBHIIII")
HELLO_PAYLOAD = struct.Struct("<BIIIIIBBHIII")
START_PAYLOAD = struct.Struct("<BBHIIIII")

class ProtocolError(ValueError):
    pass

@dataclass(frozen=True)
class Packet:
    command: int
    update_id: int = 0
    offset: int = 0
    sequence: int = 0
    payload: bytes = b""

@dataclass(frozen=True)
class AckInfo:
    status: int
    update_state: int
    acknowledged_sequence: int
    next_expected_offset: int
    received_size: int
    expected_size: int
    last_error_detail: int

@dataclass(frozen=True)
class HelloInfo:
    protocol_version: int
    bootloader_version: int
    application_version: int
    product_id: int
    hardware_revision: int
    capability_flags: int
    update_state: int
    last_status: int
    active_update_id: int
    next_expected_offset: int
    expected_artifact_size: int

def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF

def cobs_encode(data: bytes) -> bytes:
    out = bytearray(b"\x00")
    code_index = 0
    code = 1
    for byte in data:
        if byte == 0:
            out[code_index] = code
            code_index = len(out)
            out.append(0)
            code = 1
        else:
            out.append(byte)
            code += 1
            if code == 0xFF:
                out[code_index] = code
                code_index = len(out)
                out.append(0)
                code = 1
    out[code_index] = code
    return bytes(out)

def cobs_decode(data: bytes) -> bytes:
    if not data:
        raise ProtocolError("empty COBS frame")
    out = bytearray()
    index = 0
    while index < len(data):
        code = data[index]
        if code == 0:
            raise ProtocolError("zero COBS code")
        index += 1
        end = index + code - 1
        if end > len(data):
            raise ProtocolError("truncated COBS frame")
        out.extend(data[index:end])
        index = end
        if code != 0xFF and index < len(data):
            out.append(0)
    return bytes(out)

def serialize_packet(packet: Packet) -> bytes:
    payload = bytes(packet.payload)
    if len(payload) > MAX_PAYLOAD:
        raise ProtocolError("payload too large")
    raw = HEADER.pack(MAGIC, PROTOCOL_VERSION, packet.command,
                      packet.update_id, packet.offset,
                      packet.sequence, len(payload)) + payload
    return raw + CRC.pack(crc32(raw))

def deserialize_packet(raw: bytes) -> Packet:
    if len(raw) < HEADER.size + CRC.size:
        raise ProtocolError("packet too short")
    magic, version, command, update_id, offset, sequence, payload_len = HEADER.unpack_from(raw)
    if magic != MAGIC:
        raise ProtocolError(f"bad magic 0x{magic:04X}")
    if version != PROTOCOL_VERSION:
        raise ProtocolError(f"bad version {version}")
    if payload_len > MAX_PAYLOAD:
        raise ProtocolError("payload length too large")
    expected = HEADER.size + payload_len + CRC.size
    if len(raw) != expected:
        raise ProtocolError("packet length mismatch")
    payload = raw[HEADER.size:HEADER.size + payload_len]
    stored = CRC.unpack_from(raw, HEADER.size + payload_len)[0]
    computed = crc32(raw[:HEADER.size + payload_len])
    if stored != computed:
        raise ProtocolError(
            f"CRC mismatch stored=0x{stored:08X} computed=0x{computed:08X}"
        )
    return Packet(command, update_id, offset, sequence, payload)

def encode_frame(packet: Packet) -> bytes:
    encoded = cobs_encode(serialize_packet(packet))
    if len(encoded) > MAX_ENCODED_FRAME:
        raise ProtocolError("encoded frame too large")
    return encoded + b"\x00"

def decode_frame(frame: bytes) -> Packet:
    if frame.endswith(b"\x00"):
        frame = frame[:-1]
    return deserialize_packet(cobs_decode(frame))

def parse_ack(packet: Packet) -> AckInfo:
    if packet.command not in (CMD_ACK, CMD_NACK):
        raise ProtocolError("expected ACK/NACK")
    if len(packet.payload) != ACK_PAYLOAD.size:
        raise ProtocolError("bad ACK payload length")
    return AckInfo(*ACK_PAYLOAD.unpack(packet.payload))

def parse_hello(packet: Packet) -> HelloInfo:
    if packet.command != CMD_ACK:
        raise ProtocolError("HELLO/QUERY returned NACK")
    if len(packet.payload) != HELLO_PAYLOAD.size:
        raise ProtocolError("bad HELLO payload length")
    values = HELLO_PAYLOAD.unpack(packet.payload)
    return HelloInfo(
        values[0], values[1], values[2], values[3], values[4], values[5],
        values[6], values[7], values[9], values[10], values[11]
    )

def build_start_payload(artifact_size: int, artifact_crc32: int,
                        artifact_type: int = 1,
                        container_format_version: int = 1,
                        base_version: int = 0,
                        target_version: int = 0,
                        container_header_size: int = 0) -> bytes:
    return START_PAYLOAD.pack(
        artifact_type, 0, container_format_version,
        base_version, target_version, artifact_size,
        artifact_crc32, container_header_size
    )
