from __future__ import annotations

from dataclasses import dataclass
import socket
import ssl
import struct
from urllib.parse import urlparse


class MqttPublishError(RuntimeError):
    pass


def _encode_remaining_length(value: int) -> bytes:
    if value < 0 or value > 268_435_455:
        raise MqttPublishError("MQTT remaining length invalid")
    out = bytearray()
    while True:
        digit = value % 128
        value //= 128
        if value:
            digit |= 0x80
        out.append(digit)
        if not value:
            return bytes(out)


def _utf8(value: str) -> bytes:
    raw = value.encode("utf-8")
    if len(raw) > 65535:
        raise MqttPublishError("MQTT UTF-8 field too long")
    return struct.pack(">H", len(raw)) + raw


def _packet(first: int, body: bytes) -> bytes:
    return bytes([first]) + _encode_remaining_length(len(body)) + body


def _recv_exact(sock: ssl.SSLSocket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise MqttPublishError("MQTT connection closed")
        data.extend(chunk)
    return bytes(data)


def _recv_packet(sock: ssl.SSLSocket) -> tuple[int, bytes]:
    first = _recv_exact(sock, 1)[0]
    remaining = 0
    multiplier = 1
    for _ in range(4):
        digit = _recv_exact(sock, 1)[0]
        remaining += (digit & 0x7F) * multiplier
        if (digit & 0x80) == 0:
            return first, _recv_exact(sock, remaining)
        multiplier *= 128
    raise MqttPublishError("malformed MQTT remaining length")


@dataclass(frozen=True)
class MqttPublishConfig:
    broker_uri: str
    ca_file: str
    client_id: str
    username: str | None = None
    password: str | None = None
    timeout_seconds: float = 8.0


def publish_qos1(
    config: MqttPublishConfig,
    topic: str,
    payload: bytes,
) -> None:
    parsed = urlparse(config.broker_uri)
    if parsed.scheme != "mqtts" or not parsed.hostname:
        raise MqttPublishError("broker URI must use mqtts://")
    if parsed.username or parsed.password:
        raise MqttPublishError("put MQTT credentials in dedicated fields")
    if not topic or "\x00" in topic:
        raise MqttPublishError("MQTT topic invalid")
    if not config.client_id:
        raise MqttPublishError("MQTT client_id required")

    port = parsed.port or 8883

    flags = 0x02  # clean session
    payload_connect = _utf8(config.client_id)
    if config.username is not None:
        flags |= 0x80
        payload_connect += _utf8(config.username)
    if config.password is not None:
        if config.username is None:
            raise MqttPublishError("MQTT password requires username")
        flags |= 0x40
        payload_connect += _utf8(config.password)

    variable = (
        _utf8("MQTT")
        + bytes([4, flags])
        + struct.pack(">H", 30)
    )
    connect = _packet(0x10, variable + payload_connect)

    packet_id = 0x1501
    publish = _packet(
        0x32,  # PUBLISH QoS1, retain=0
        _utf8(topic) + struct.pack(">H", packet_id) + payload,
    )

    context = ssl.create_default_context(cafile=config.ca_file)
    context.minimum_version = ssl.TLSVersion.TLSv1_2

    with socket.create_connection(
        (parsed.hostname, port),
        timeout=config.timeout_seconds,
    ) as raw:
        with context.wrap_socket(
            raw,
            server_hostname=parsed.hostname,
        ) as sock:
            sock.settimeout(config.timeout_seconds)
            sock.sendall(connect)

            first, body = _recv_packet(sock)
            if (first >> 4) != 2 or len(body) != 2 or body[1] != 0:
                raise MqttPublishError("MQTT CONNACK rejected")

            sock.sendall(publish)
            while True:
                first, body = _recv_packet(sock)
                packet_type = first >> 4
                if packet_type == 4:
                    if len(body) != 2:
                        raise MqttPublishError("malformed MQTT PUBACK")
                    ack_id = struct.unpack(">H", body)[0]
                    if ack_id != packet_id:
                        raise MqttPublishError("unexpected MQTT PUBACK id")
                    break
                if packet_type == 13:  # PINGRESP
                    continue
                raise MqttPublishError(
                    f"unexpected MQTT packet type {packet_type}"
                )

            sock.sendall(_packet(0xE0, b""))
