#!/usr/bin/env python3
"""TLS artifact server with deterministic Phase-16 HIL transport faults."""
from __future__ import annotations

import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import socket
import ssl
import sys


class FaultServer(ThreadingHTTPServer):
    artifact: Path
    route: str
    mode: str
    truncate_after: int
    served_count: int


class Handler(BaseHTTPRequestHandler):
    server: FaultServer

    def log_message(self, fmt: str, *args: object) -> None:
        print(
            f'P16_HTTPS peer={self.client_address[0]} '
            f'"{self.requestline}" {fmt % args}',
            flush=True,
        )

    def do_GET(self) -> None:  # noqa: N802
        if self.path == "/healthz":
            body = b"ok\n"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        if self.path != self.server.route:
            self.send_error(404)
            return

        data = self.server.artifact.read_bytes()
        self.server.served_count += 1

        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()

        if self.server.mode == "truncate":
            cut = min(self.server.truncate_after, max(0, len(data) - 1))
            self.wfile.write(data[:cut])
            self.wfile.flush()
            print(
                f"P16_HTTPS_FAULT=TRUNCATE sent={cut} total={len(data)}",
                flush=True,
            )
            try:
                self.connection.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            self.connection.close()
            return

        self.wfile.write(data)
        self.wfile.flush()
        print(
            f"P16_HTTPS_ARTIFACT=PASS size={len(data)} mode={self.server.mode}",
            flush=True,
        )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Phase-16 TLS artifact fault server"
    )
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8443)
    parser.add_argument("--cert", type=Path, required=True)
    parser.add_argument("--key", type=Path, required=True)
    parser.add_argument("--artifact", type=Path, required=True)
    parser.add_argument(
        "--route",
        default="/phase16/artifact.sdot",
    )
    parser.add_argument(
        "--mode",
        choices=("normal", "truncate"),
        default="normal",
    )
    parser.add_argument("--truncate-after", type=int, default=512)
    args = parser.parse_args()

    if not args.artifact.is_file():
        parser.error(f"artifact does not exist: {args.artifact}")
    if not args.route.startswith("/") or ".." in args.route:
        parser.error("--route must be an absolute fixed path without '..'")
    if args.truncate_after < 1:
        parser.error("--truncate-after must be >= 1")

    server = FaultServer((args.bind, args.port), Handler)
    server.artifact = args.artifact
    server.route = args.route
    server.mode = args.mode
    server.truncate_after = args.truncate_after
    server.served_count = 0

    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(str(args.cert), str(args.key))
    server.socket = context.wrap_socket(server.socket, server_side=True)

    print(
        f"P16_HTTPS_READY bind={args.bind} port={args.port} "
        f"mode={args.mode} route={args.route} "
        f"artifact={args.artifact}",
        flush=True,
    )

    try:
        # One request is enough for each deterministic Phase-16 scenario.
        while server.served_count == 0:
            server.handle_request()
    finally:
        server.server_close()

    if server.served_count != 1:
        print("P16_HTTPS_RESULT=FAIL artifact_not_served", flush=True)
        return 1

    print(
        f"P16_HTTPS_RESULT=PASS mode={args.mode}",
        flush=True,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
