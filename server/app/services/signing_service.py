from __future__ import annotations

import hashlib
import os
from pathlib import Path
import stat
import subprocess
import tempfile


class SigningError(RuntimeError):
    pass


def _run(command: list[str], *, input_bytes: bytes | None = None) -> bytes:
    result = subprocess.run(
        command,
        input=input_bytes,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if result.returncode != 0:
        raise SigningError(
            f"command failed ({result.returncode}): {' '.join(command)}\n"
            + result.stdout.decode("utf-8", errors="replace")
        )
    return result.stdout


def validate_private_key_path(key_path: Path, repo_root: Path) -> Path:
    key = key_path.expanduser().resolve()
    repo = repo_root.resolve()

    if not key.is_file():
        raise SigningError(f"signing key does not exist: {key}")

    try:
        key.relative_to(repo)
    except ValueError:
        pass
    else:
        raise SigningError(
            "private signing key must live outside the repository tree"
        )

    if os.name == "posix":
        mode = stat.S_IMODE(key.stat().st_mode)
        if mode & 0o077:
            raise SigningError(
                f"private signing key permissions must be 0600 or stricter; "
                f"got {mode:04o}"
            )

    _run(["openssl", "pkey", "-in", str(key), "-check", "-noout"])
    description = _run(
        ["openssl", "pkey", "-in", str(key), "-text_pub", "-noout"]
    ).decode("utf-8", errors="replace")

    if ("prime256v1" not in description and
            "P-256" not in description and
            "secp256r1" not in description):
        raise SigningError("signing key is not an ECDSA P-256 key")

    return key


def public_key_pem(private_key: Path) -> bytes:
    return _run([
        "openssl", "pkey",
        "-in", str(private_key),
        "-pubout",
    ])


def public_key_der(private_key: Path) -> bytes:
    return _run([
        "openssl", "pkey",
        "-in", str(private_key),
        "-pubout",
        "-outform", "DER",
    ])


def public_key_sha256(private_key: Path) -> str:
    return hashlib.sha256(public_key_der(private_key)).hexdigest()


def sign_bytes_der(private_key: Path, message: bytes) -> bytes:
    with tempfile.TemporaryDirectory(prefix="phase15-sign-") as td:
        tmp = Path(td)
        message_path = tmp / "message.bin"
        signature_path = tmp / "signature.der"
        message_path.write_bytes(message)

        _run([
            "openssl", "dgst", "-sha256",
            "-sign", str(private_key),
            "-out", str(signature_path),
            str(message_path),
        ])
        signature = signature_path.read_bytes()

        public_path = tmp / "public.pem"
        public_path.write_bytes(public_key_pem(private_key))
        verify_signature_der(public_path, message, signature)
        return signature


def verify_signature_der(
    public_key: Path,
    message: bytes,
    signature: bytes,
) -> None:
    with tempfile.TemporaryDirectory(prefix="phase15-verify-") as td:
        tmp = Path(td)
        message_path = tmp / "message.bin"
        signature_path = tmp / "signature.der"
        message_path.write_bytes(message)
        signature_path.write_bytes(signature)

        _run([
            "openssl", "dgst", "-sha256",
            "-verify", str(public_key),
            "-signature", str(signature_path),
            str(message_path),
        ])
