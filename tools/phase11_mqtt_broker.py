#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path
import socket
import ssl
import struct
import sys
from typing import BinaryIO


def encode_remaining_length(value: int) -> bytes:
    if value < 0 or value > 268_435_455:
        raise ValueError("invalid MQTT remaining length")

    out = bytearray()
    while True:
        digit = value % 128
        value //= 128
        if value:
            digit |= 0x80
        out.append(digit)
        if not value:
            break
    return bytes(out)


def recv_exact(sock: ssl.SSLSocket, size: int) -> bytes:
    chunks = bytearray()
    while len(chunks) < size:
        data = sock.recv(size - len(chunks))
        if not data:
            raise EOFError("MQTT peer closed connection")
        chunks.extend(data)
    return bytes(chunks)


def recv_packet(sock: ssl.SSLSocket) -> tuple[int, bytes]:
    first = recv_exact(sock, 1)[0]

    multiplier = 1
    remaining = 0
    for _ in range(4):
        digit = recv_exact(sock, 1)[0]
        remaining += (digit & 0x7F) * multiplier
        if (digit & 0x80) == 0:
            break
        multiplier *= 128
    else:
        raise ValueError("malformed MQTT remaining length")

    return first, recv_exact(sock, remaining)


def send_packet(sock: ssl.SSLSocket,
                first: int,
                body: bytes = b"") -> None:
    sock.sendall(
        bytes([first]) +
        encode_remaining_length(len(body)) +
        body
    )


def read_utf8(body: bytes, offset: int) -> tuple[str, int]:
    if offset + 2 > len(body):
        raise ValueError("truncated MQTT string")

    length = struct.unpack_from(">H", body, offset)[0]
    offset += 2

    if offset + length > len(body):
        raise ValueError("truncated MQTT string payload")

    return body[offset:offset + length].decode("utf-8"), offset + length


def encode_utf8(value: str) -> bytes:
    raw = value.encode("utf-8")
    if len(raw) > 65535:
        raise ValueError("MQTT string too long")
    return struct.pack(">H", len(raw)) + raw


def send_publish_qos1(sock: ssl.SSLSocket,
                      topic: str,
                      payload: bytes,
                      packet_id: int) -> None:
    body = (
        encode_utf8(topic) +
        struct.pack(">H", packet_id) +
        payload
    )
    send_packet(sock, 0x32, body)


def parse_publish(first: int,
                  body: bytes) -> tuple[str, bytes, int | None]:
    topic, offset = read_utf8(body, 0)
    qos = (first >> 1) & 0x03
    packet_id: int | None = None

    if qos:
        if offset + 2 > len(body):
            raise ValueError("truncated PUBLISH packet id")
        packet_id = struct.unpack_from(">H", body, offset)[0]
        offset += 2

    return topic, body[offset:], packet_id


def parse_subscribe(body: bytes) -> tuple[int, list[tuple[str, int]]]:
    if len(body) < 2:
        raise ValueError("truncated SUBSCRIBE")

    packet_id = struct.unpack_from(">H", body, 0)[0]
    offset = 2
    topics: list[tuple[str, int]] = []

    while offset < len(body):
        topic, offset = read_utf8(body, offset)
        if offset >= len(body):
            raise ValueError("missing SUBSCRIBE qos")
        qos = body[offset] & 0x03
        offset += 1
        topics.append((topic, qos))

    return packet_id, topics


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Minimal TLS MQTT 3.1.1 broker for Phase-11 HIL test"
    )
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8883)
    parser.add_argument("--cert", type=Path, required=True)
    parser.add_argument("--key", type=Path, required=True)
    parser.add_argument("--topic-base", default="sdota")
    parser.add_argument("--device-id", default="bluepill-001")
    parser.add_argument("--command-file", type=Path, required=True)
    args = parser.parse_args()

    command_topic = f"{args.topic_base}/{args.device_id}/command"
    status_topic = f"{args.topic_base}/{args.device_id}/status"
    progress_topic = f"{args.topic_base}/{args.device_id}/progress"

    command_payload = args.command_file.read_bytes()
    # Validate test input before a device connects.
    json.loads(command_payload.decode("utf-8"))

    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(
        certfile=str(args.cert),
        keyfile=str(args.key),
    )

    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind((args.bind, args.port))
    listener.listen(1)

    print(
        f"PHASE11_MQTT_BROKER_READY bind={args.bind} "
        f"port={args.port} command={command_topic}",
        flush=True,
    )

    command_sent = False
    command_acked = False
    subscribed = False
    confirmed_seen = False
    command_packet_id = 0x1101

    try:
        raw_conn, peer = listener.accept()
        with raw_conn:
            with context.wrap_socket(
                raw_conn,
                server_side=True,
            ) as conn:
                conn.settimeout(45.0)
                print(
                    f"P11_BROKER_TLS=PASS peer={peer[0]}:{peer[1]}",
                    flush=True,
                )

                while True:
                    first, body = recv_packet(conn)
                    packet_type = first >> 4

                    if packet_type == 1:  # CONNECT
                        send_packet(conn, 0x20, b"\x00\x00")
                        print("P11_BROKER_CONNECT=PASS", flush=True)

                    elif packet_type == 8:  # SUBSCRIBE
                        packet_id, topics = parse_subscribe(body)
                        granted = bytearray()

                        for topic, qos in topics:
                            granted.append(min(qos, 1))
                            if topic == command_topic:
                                subscribed = True

                        send_packet(
                            conn,
                            0x90,
                            struct.pack(">H", packet_id) + bytes(granted),
                        )
                        print(
                            "P11_BROKER_SUBSCRIBE=PASS "
                            f"topics={topics}",
                            flush=True,
                        )

                    elif packet_type == 3:  # PUBLISH
                        topic, payload, packet_id = parse_publish(first, body)
                        text = payload.decode("utf-8", errors="replace")
                        qos = (first >> 1) & 0x03

                        if qos == 1 and packet_id is not None:
                            send_packet(
                                conn,
                                0x40,
                                struct.pack(">H", packet_id),
                            )

                        if topic == status_topic:
                            print(
                                f"P11_BROKER_STATUS {text}",
                                flush=True,
                            )
                            try:
                                decoded = json.loads(text)
                            except json.JSONDecodeError:
                                decoded = {}

                            state = decoded.get("state")
                            if (
                                state == "online"
                                and subscribed
                                and not command_sent
                            ):
                                send_publish_qos1(
                                    conn,
                                    command_topic,
                                    command_payload,
                                    command_packet_id,
                                )
                                command_sent = True
                                print(
                                    "P11_BROKER_COMMAND_SENT=PASS",
                                    flush=True,
                                )

                            if state == "confirmed":
                                confirmed_seen = True
                                print(
                                    "P11_BROKER_CONFIRMED=PASS",
                                    flush=True,
                                )

                        elif topic == progress_topic:
                            print(
                                f"P11_BROKER_PROGRESS {text}",
                                flush=True,
                            )
                        else:
                            print(
                                f"P11_BROKER_PUBLISH topic={topic} "
                                f"payload={text}",
                                flush=True,
                            )

                    elif packet_type == 4:  # PUBACK
                        if len(body) >= 2:
                            packet_id = struct.unpack_from(">H", body, 0)[0]
                            if packet_id == command_packet_id:
                                command_acked = True
                                print(
                                    "P11_BROKER_COMMAND_ACK=PASS",
                                    flush=True,
                                )

                    elif packet_type == 12:  # PINGREQ
                        send_packet(conn, 0xD0)

                    elif packet_type == 14:  # DISCONNECT
                        print("P11_BROKER_DISCONNECT=PASS", flush=True)
                        break

                    else:
                        print(
                            f"P11_BROKER_PACKET type={packet_type}",
                            flush=True,
                        )

    except (EOFError, ConnectionResetError, socket.timeout) as exc:
        print(f"P11_BROKER_CONNECTION_END {exc}", flush=True)
    finally:
        listener.close()

    if not command_sent:
        print("P11_BROKER_RESULT=FAIL command_not_sent", flush=True)
        return 1
    if not command_acked:
        print("P11_BROKER_RESULT=FAIL command_not_acked", flush=True)
        return 1
    if not confirmed_seen:
        print("P11_BROKER_RESULT=FAIL confirmed_status_missing", flush=True)
        return 1

    print("P11_BROKER_RESULT=PASS", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
