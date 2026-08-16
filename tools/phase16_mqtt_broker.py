#!/usr/bin/env python3
"""Phase 16 multi-connection TLS MQTT fault broker.

This is HIL-only infrastructure. Firmware bytes still travel over HTTPS.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import socket
import ssl
import struct
import sys
import time

import phase11_mqtt_broker as mqtt11


def log_event(path: Path | None, kind: str, **fields: object) -> None:
    if path is None:
        return
    record = {"ts": time.time(), "kind": kind, **fields}
    with path.open("a", encoding="utf-8") as stream:
        stream.write(json.dumps(record, sort_keys=True) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Phase-16 reconnect-capable TLS MQTT HIL fault broker"
    )
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8883)
    parser.add_argument("--cert", type=Path, required=True)
    parser.add_argument("--key", type=Path, required=True)
    parser.add_argument("--topic-base", default="sdota")
    parser.add_argument("--device-id", default="bluepill-001")
    parser.add_argument("--command-file", type=Path, required=True)
    parser.add_argument("--idle-timeout", type=float, default=180.0)
    parser.add_argument("--accept-timeout", type=float, default=240.0)
    parser.add_argument("--max-connections", type=int, default=4)
    parser.add_argument(
        "--expected-final",
        choices=("confirmed", "failed"),
        default="confirmed",
    )
    parser.add_argument(
        "--disconnect-on-state",
        choices=("none", "accepted", "downloaded", "installing"),
        default="none",
        help="close the first TLS connection once after this status",
    )
    parser.add_argument("--event-file", type=Path)
    args = parser.parse_args()

    if args.max_connections < 1:
        parser.error("--max-connections must be >= 1")
    if args.idle_timeout <= 0 or args.accept_timeout <= 0:
        parser.error("timeouts must be > 0")

    command_topic = f"{args.topic_base}/{args.device_id}/command"
    status_topic = f"{args.topic_base}/{args.device_id}/status"
    progress_topic = f"{args.topic_base}/{args.device_id}/progress"
    command_payload = args.command_file.read_bytes()
    json.loads(command_payload.decode("utf-8"))

    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(certfile=str(args.cert), keyfile=str(args.key))

    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind((args.bind, args.port))
    listener.listen(2)
    listener.settimeout(args.accept_timeout)

    print(
        f"PHASE16_MQTT_BROKER_READY bind={args.bind} port={args.port} "
        f"expected_final={args.expected_final} "
        f"disconnect_on_state={args.disconnect_on_state}",
        flush=True,
    )

    ever_command_sent = False
    ever_command_acked = False
    final_seen = False
    injected_disconnect = False
    connection_count = 0
    packet_id_seed = 0x1600

    try:
        while (
            connection_count < args.max_connections
            and not final_seen
        ):
            try:
                raw_conn, peer = listener.accept()
            except socket.timeout:
                print("P16_BROKER_ACCEPT_TIMEOUT", flush=True)
                break

            connection_count += 1
            subscribed = False
            command_sent_this_connection = False
            command_packet_id = (
                packet_id_seed + connection_count
            ) & 0xFFFF

            with raw_conn:
                try:
                    with context.wrap_socket(
                        raw_conn,
                        server_side=True,
                    ) as conn:
                        conn.settimeout(args.idle_timeout)
                        print(
                            f"P16_BROKER_TLS=PASS connection={connection_count} "
                            f"peer={peer[0]}:{peer[1]}",
                            flush=True,
                        )
                        log_event(
                            args.event_file,
                            "tls",
                            connection=connection_count,
                            peer=peer[0],
                        )

                        while True:
                            first, body = mqtt11.recv_packet(conn)
                            packet_type = first >> 4

                            if packet_type == 1:  # CONNECT
                                mqtt11.send_packet(conn, 0x20, b"\x00\x00")
                                print(
                                    f"P16_BROKER_CONNECT=PASS "
                                    f"connection={connection_count}",
                                    flush=True,
                                )

                            elif packet_type == 8:  # SUBSCRIBE
                                sub_id, topics = mqtt11.parse_subscribe(body)
                                granted = bytearray()

                                for topic, qos in topics:
                                    granted.append(min(qos, 1))
                                    if topic == command_topic:
                                        subscribed = True

                                mqtt11.send_packet(
                                    conn,
                                    0x90,
                                    struct.pack(">H", sub_id) + bytes(granted),
                                )
                                print(
                                    "P16_BROKER_SUBSCRIBE=PASS "
                                    f"connection={connection_count} "
                                    f"topics={topics}",
                                    flush=True,
                                )

                            elif packet_type == 3:  # PUBLISH
                                topic, payload, pub_id = mqtt11.parse_publish(
                                    first, body
                                )
                                qos = (first >> 1) & 0x03
                                text = payload.decode(
                                    "utf-8",
                                    errors="replace",
                                )

                                if qos == 1 and pub_id is not None:
                                    mqtt11.send_packet(
                                        conn,
                                        0x40,
                                        struct.pack(">H", pub_id),
                                    )

                                if topic == status_topic:
                                    print(
                                        f"P16_BROKER_STATUS {text}",
                                        flush=True,
                                    )
                                    try:
                                        decoded = json.loads(text)
                                    except json.JSONDecodeError:
                                        decoded = {}

                                    state = decoded.get("state")
                                    log_event(
                                        args.event_file,
                                        "status",
                                        state=state,
                                        payload=decoded,
                                    )

                                    if (
                                        state == "online"
                                        and subscribed
                                        and not ever_command_acked
                                        and not command_sent_this_connection
                                    ):
                                        mqtt11.send_publish_qos1(
                                            conn,
                                            command_topic,
                                            command_payload,
                                            command_packet_id,
                                        )
                                        command_sent_this_connection = True
                                        ever_command_sent = True
                                        print(
                                            "P16_BROKER_COMMAND_SENT=PASS "
                                            f"connection={connection_count}",
                                            flush=True,
                                        )

                                    if (
                                        args.disconnect_on_state != "none"
                                        and state == args.disconnect_on_state
                                        and not injected_disconnect
                                    ):
                                        injected_disconnect = True
                                        print(
                                            "P16_MQTT_FAULT=DISCONNECT "
                                            f"state={state} "
                                            f"connection={connection_count}",
                                            flush=True,
                                        )
                                        log_event(
                                            args.event_file,
                                            "mqtt_disconnect",
                                            state=state,
                                        )
                                        try:
                                            conn.shutdown(socket.SHUT_RDWR)
                                        except OSError:
                                            pass
                                        break

                                    if state == args.expected_final:
                                        final_seen = True
                                        print(
                                            "P16_BROKER_FINAL=PASS "
                                            f"state={state}",
                                            flush=True,
                                        )
                                        # PUBACK was already sent above.
                                        break

                                elif topic == progress_topic:
                                    print(
                                        f"P16_BROKER_PROGRESS {text}",
                                        flush=True,
                                    )
                                    try:
                                        decoded = json.loads(text)
                                    except json.JSONDecodeError:
                                        decoded = {}
                                    log_event(
                                        args.event_file,
                                        "progress",
                                        payload=decoded,
                                    )
                                else:
                                    print(
                                        f"P16_BROKER_PUBLISH topic={topic} "
                                        f"payload={text}",
                                        flush=True,
                                    )

                            elif packet_type == 4:  # PUBACK
                                if len(body) >= 2:
                                    ack_id = struct.unpack_from(
                                        ">H", body, 0
                                    )[0]
                                    if (
                                        command_sent_this_connection
                                        and ack_id == command_packet_id
                                    ):
                                        ever_command_acked = True
                                        print(
                                            "P16_BROKER_COMMAND_ACK=PASS "
                                            f"connection={connection_count}",
                                            flush=True,
                                        )

                            elif packet_type == 12:  # PINGREQ
                                mqtt11.send_packet(conn, 0xD0)

                            elif packet_type == 14:  # DISCONNECT
                                print(
                                    f"P16_BROKER_DISCONNECT=PASS "
                                    f"connection={connection_count}",
                                    flush=True,
                                )
                                break

                except (
                    EOFError,
                    ConnectionResetError,
                    BrokenPipeError,
                    ssl.SSLError,
                    socket.timeout,
                ) as exc:
                    print(
                        "P16_BROKER_CONNECTION_END "
                        f"connection={connection_count} {exc}",
                        flush=True,
                    )
                    log_event(
                        args.event_file,
                        "connection_end",
                        connection=connection_count,
                        detail=str(exc),
                    )

    finally:
        listener.close()

    if not ever_command_sent:
        print("P16_BROKER_RESULT=FAIL command_not_sent", flush=True)
        return 1
    if not ever_command_acked:
        print("P16_BROKER_RESULT=FAIL command_not_acked", flush=True)
        return 1
    if args.disconnect_on_state != "none" and not injected_disconnect:
        print("P16_BROKER_RESULT=FAIL fault_not_injected", flush=True)
        return 1
    if not final_seen:
        print(
            f"P16_BROKER_RESULT=FAIL "
            f"{args.expected_final}_status_missing",
            flush=True,
        )
        return 1

    print(
        f"P16_BROKER_RESULT=PASS final={args.expected_final} "
        f"connections={connection_count}",
        flush=True,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
