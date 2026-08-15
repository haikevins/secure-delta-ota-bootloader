from __future__ import annotations

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import mimetypes
import ssl
from urllib.parse import unquote, urlsplit


class FirmwareRequestHandler(BaseHTTPRequestHandler):
    server_version = "SDOTA-Phase15/1"

    def _send_file(self, *, head_only: bool) -> None:
        path = urlsplit(self.path).path

        if path == "/healthz":
            body = b"ok\n"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            if not head_only:
                self.wfile.write(body)
            return

        prefix = "/releases/"
        if not path.startswith(prefix):
            self.send_error(404)
            return

        relative_text = unquote(path[len(prefix):])
        parts = relative_text.split("/")
        if len(parts) != 2:
            self.send_error(404)
            return

        release_id, filename = parts
        if (
            not release_id
            or not filename
            or release_id in {".", ".."}
            or filename in {".", ".."}
            or "/" in filename
            or "\\" in filename
        ):
            self.send_error(404)
            return

        root: Path = self.server.release_root  # type: ignore[attr-defined]
        candidate = (root / release_id / filename).resolve()

        try:
            candidate.relative_to(root.resolve())
        except ValueError:
            self.send_error(404)
            return

        if not candidate.is_file():
            self.send_error(404)
            return

        # Deliberately no directory listing and no arbitrary repo file serving.
        allowed = {
            "manifest.json",
            "manifest.json.sig",
            "checksums.txt",
            "release-notes.md",
            "signing-public.pem",
        }
        if filename not in allowed and not (
            filename.endswith(".sdot") or filename.endswith(".bin")
        ):
            self.send_error(404)
            return

        data_size = candidate.stat().st_size
        content_type = (
            mimetypes.guess_type(filename)[0]
            or "application/octet-stream"
        )
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(data_size))
        self.send_header(
            "Cache-Control",
            "public, max-age=31536000, immutable",
        )
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()

        if not head_only:
            with candidate.open("rb") as stream:
                while True:
                    chunk = stream.read(64 * 1024)
                    if not chunk:
                        break
                    self.wfile.write(chunk)

    def do_GET(self) -> None:
        self._send_file(head_only=False)

    def do_HEAD(self) -> None:
        self._send_file(head_only=True)

    def log_message(self, format: str, *args: object) -> None:
        print(
            f"PHASE15_HTTPS peer={self.client_address[0]} "
            + (format % args),
            flush=True,
        )


def serve_https(
    release_root: Path,
    bind: str,
    port: int,
    cert_file: Path,
    key_file: Path,
) -> None:
    if not release_root.is_dir():
        raise RuntimeError(f"release root not found: {release_root}")
    if not cert_file.is_file() or not key_file.is_file():
        raise RuntimeError("TLS certificate/key file missing")

    server = ThreadingHTTPServer((bind, port), FirmwareRequestHandler)
    server.release_root = release_root.resolve()  # type: ignore[attr-defined]

    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(str(cert_file), str(key_file))
    server.socket = context.wrap_socket(server.socket, server_side=True)

    print(
        f"PHASE15_HTTPS_READY bind={bind} port={port} "
        f"root={release_root.resolve()}",
        flush=True,
    )

    try:
        server.serve_forever()
    finally:
        server.server_close()
