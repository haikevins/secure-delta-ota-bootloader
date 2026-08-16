#!/usr/bin/env python3
from __future__ import annotations

import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import ssl
import sys


class FirmwareHandler(BaseHTTPRequestHandler):
    server_version = "SecureDeltaOTA-HTTPS transport/1.0"

    def do_GET(self) -> None:
        expected_path = self.server.firmware_path  # type: ignore[attr-defined]
        firmware = self.server.firmware_bytes  # type: ignore[attr-defined]

        if self.path != expected_path:
            self.send_error(404, "Not Found")
            return

        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(firmware)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(firmware)

    def log_message(self, fmt: str, *args) -> None:
        print(
            "HTTPS server: " + (fmt % args),
            file=sys.stdout,
            flush=True,
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8443)
    parser.add_argument("--cert", type=Path, required=True)
    parser.add_argument("--key", type=Path, required=True)
    parser.add_argument("--file", type=Path, required=True)
    parser.add_argument(
        "--path",
        default="/artifact.bin",
    )
    args = parser.parse_args()

    firmware = args.file.read_bytes()
    if not firmware:
        raise SystemExit("firmware file is empty")

    httpd = ThreadingHTTPServer((args.bind, args.port), FirmwareHandler)
    httpd.firmware_path = args.path  # type: ignore[attr-defined]
    httpd.firmware_bytes = firmware  # type: ignore[attr-defined]

    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(
        certfile=str(args.cert),
        keyfile=str(args.key),
    )
    httpd.socket = context.wrap_socket(
        httpd.socket,
        server_side=True,
    )

    print(
        f"HTTPS_TEST_SERVER_READY "
        f"bind={args.bind} port={args.port} "
        f"path={args.path} size={len(firmware)}",
        flush=True,
    )

    try:
        httpd.serve_forever(poll_interval=0.2)
    except KeyboardInterrupt:
        pass
    finally:
        httpd.server_close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
