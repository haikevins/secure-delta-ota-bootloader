#!/usr/bin/env python3
"""Build and inspect signed Secure Delta OTA container format v1."""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib

MAGIC = 0x544F4453
FORMAT_VERSION = 1
FIXED_HEADER_SIZE = 120
EXT_MAGIC = 0x31584353
EXT_VERSION = 1
EXT_SIZE = 20
HEADER_SIZE = FIXED_HEADER_SIZE + EXT_SIZE

PRODUCT_ID = 0x00001001
HARDWARE_REVISION = 1
FLAGS = 0

IMAGE_FULL = 1
IMAGE_DELTA = 2

HASH_SHA256 = 1
SIG_ECDSA_P256 = 1
SIG_SIZE = 64

APP_ADDRESS = 0x08006000
APP_MAX = 38 * 1024

FIXED = struct.Struct("<IHHIIIIIIIII32s32sIHHHH")
EXT = struct.Struct("<IHHIII")

assert FIXED.size == FIXED_HEADER_SIZE
assert EXT.size == EXT_SIZE
assert HEADER_SIZE == 140


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def sha256(data: bytes) -> bytes:
    return hashlib.sha256(data).digest()


def validate_application(image: bytes, label: str) -> None:
    if not 8 <= len(image) <= APP_MAX:
        raise SystemExit(f"{label} size outside STM32 application region")

    msp, reset = struct.unpack_from("<II", image)
    if not 0x20000000 <= msp <= 0x20005000 or (msp & 7):
        raise SystemExit(f"{label} invalid MSP 0x{msp:08X}")
    if (reset & 1) == 0:
        raise SystemExit(f"{label} reset handler is not Thumb")
    reset_code = reset & ~1
    if not APP_ADDRESS <= reset_code < APP_ADDRESS + len(image):
        raise SystemExit(
            f"{label} reset handler 0x{reset:08X} outside image"
        )


def der_read_length(data: bytes, offset: int) -> tuple[int, int]:
    if offset >= len(data):
        raise ValueError("truncated DER length")
    first = data[offset]
    offset += 1

    if (first & 0x80) == 0:
        return first, offset

    count = first & 0x7F
    if count == 0 or count > 4 or offset + count > len(data):
        raise ValueError("invalid DER length")

    value = int.from_bytes(data[offset:offset + count], "big")
    return value, offset + count


def der_to_raw_ecdsa(signature: bytes) -> bytes:
    offset = 0
    if not signature or signature[offset] != 0x30:
        raise ValueError("ECDSA signature is not a DER SEQUENCE")
    offset += 1
    sequence_length, offset = der_read_length(signature, offset)
    sequence_end = offset + sequence_length
    if sequence_end != len(signature):
        raise ValueError("ECDSA DER sequence length mismatch")

    values: list[bytes] = []
    for _ in range(2):
        if offset >= sequence_end or signature[offset] != 0x02:
            raise ValueError("ECDSA DER missing INTEGER")
        offset += 1
        integer_length, offset = der_read_length(signature, offset)
        if (
            integer_length == 0
            or offset + integer_length > sequence_end
        ):
            raise ValueError("invalid ECDSA INTEGER length")

        integer = signature[offset:offset + integer_length]
        offset += integer_length

        if integer[0] & 0x80:
            raise ValueError("negative ECDSA INTEGER")
        while len(integer) > 1 and integer[0] == 0:
            integer = integer[1:]
        if len(integer) > 32:
            raise ValueError("ECDSA INTEGER exceeds P-256 scalar size")

        values.append(integer.rjust(32, b"\x00"))

    if offset != sequence_end:
        raise ValueError("trailing ECDSA DER data")

    return values[0] + values[1]


def raw_to_der_ecdsa(signature: bytes) -> bytes:
    if len(signature) != 64:
        raise ValueError("raw ECDSA signature must be 64 bytes")

    encoded: list[bytes] = []
    for value in (signature[:32], signature[32:]):
        value = value.lstrip(b"\x00") or b"\x00"
        if value[0] & 0x80:
            value = b"\x00" + value
        encoded.append(b"\x02" + bytes([len(value)]) + value)

    body = b"".join(encoded)
    if len(body) >= 128:
        raise ValueError("unexpected P-256 DER length")
    return b"\x30" + bytes([len(body)]) + body


def sign_ecdsa_p256(private_key: Path, signed_bytes: bytes) -> bytes:
    with tempfile.TemporaryDirectory(prefix="signed secure container-sign-") as td:
        td_path = Path(td)
        message = td_path / "signed.bin"
        der_signature = td_path / "signature.der"
        message.write_bytes(signed_bytes)

        result = subprocess.run(
            [
                "openssl",
                "dgst",
                "-sha256",
                "-sign",
                str(private_key),
                "-out",
                str(der_signature),
                str(message),
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        if result.returncode != 0:
            raise SystemExit(result.stdout)

        return der_to_raw_ecdsa(der_signature.read_bytes())


def verify_openssl(
    key: Path,
    signed_bytes: bytes,
    raw_signature: bytes,
) -> None:
    with tempfile.TemporaryDirectory(prefix="signed secure container-verify-") as td:
        td_path = Path(td)
        message = td_path / "signed.bin"
        public_key = td_path / "public.pem"
        der_signature = td_path / "signature.der"
        message.write_bytes(signed_bytes)
        der_signature.write_bytes(raw_to_der_ecdsa(raw_signature))

        result = subprocess.run(
            [
                "openssl",
                "pkey",
                "-in",
                str(key),
                "-pubout",
                "-out",
                str(public_key),
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        if result.returncode != 0:
            raise SystemExit(result.stdout)

        result = subprocess.run(
            [
                "openssl",
                "dgst",
                "-sha256",
                "-verify",
                str(public_key),
                "-signature",
                str(der_signature),
                str(message),
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        if result.returncode != 0:
            raise SystemExit(
                "OpenSSL self-verification failed:\n" + result.stdout
            )


def pack_header(
    *,
    image_type: int,
    base_version: int,
    target_version: int,
    payload: bytes,
    target: bytes,
    base: bytes | None,
    key_id: int,
) -> bytes:
    if key_id <= 0 or key_id > 0xFFFFFFFF:
        raise SystemExit("key-id must be in 1..0xFFFFFFFF")
    if target_version <= 0:
        raise SystemExit("target-version must be positive")

    validate_application(target, "target")

    if image_type == IMAGE_FULL:
        if base is not None or base_version != 0:
            raise SystemExit("full container requires base-version=0")
        if payload != target:
            raise SystemExit("full payload must equal target image")
        base_hash = bytes(32)
        base_size = 0
    elif image_type == IMAGE_DELTA:
        if base is None:
            raise SystemExit("delta container requires --base")
        validate_application(base, "base")
        if base_version <= 0 or target_version <= base_version:
            raise SystemExit(
                "delta requires 0 < base-version < target-version"
            )
        base_hash = sha256(base)
        base_size = len(base)
    else:
        raise SystemExit("invalid image type")

    fixed = FIXED.pack(
        MAGIC,
        FORMAT_VERSION,
        HEADER_SIZE,
        PRODUCT_ID,
        HARDWARE_REVISION,
        image_type,
        FLAGS,
        base_version,
        target_version,
        len(payload),
        len(target),
        APP_ADDRESS,
        base_hash,
        sha256(target),
        crc32(payload),
        HASH_SHA256,
        SIG_ECDSA_P256,
        SIG_SIZE,
        0,
    )

    extension = EXT.pack(
        EXT_MAGIC,
        EXT_VERSION,
        EXT_SIZE,
        key_id,
        base_size,
        crc32(target),
    )

    header = fixed + extension
    if len(header) != HEADER_SIZE:
        raise AssertionError("canonical header size mismatch")
    return header


def build_container(args: argparse.Namespace) -> int:
    payload = args.payload.read_bytes()
    target = args.target.read_bytes()
    base = args.base.read_bytes() if args.base else None
    image_type = IMAGE_FULL if args.type == "full" else IMAGE_DELTA

    header = pack_header(
        image_type=image_type,
        base_version=args.base_version,
        target_version=args.target_version,
        payload=payload,
        target=target,
        base=base,
        key_id=args.key_id,
    )
    signed_bytes = header + payload
    signature = sign_ecdsa_p256(args.key, signed_bytes)
    if len(signature) != SIG_SIZE:
        raise SystemExit("raw P-256 signature size mismatch")

    verify_openssl(args.key, signed_bytes, signature)

    container = signed_bytes + signature
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(container)

    manifest = {
        "schema": 1,
        "format": "SDOT-v1",
        "header_size": HEADER_SIZE,
        "signature_algorithm": "ECDSA-P256-SHA256",
        "signature_encoding": "raw-r-s-big-endian",
        "signature_size": SIG_SIZE,
        "key_id": args.key_id,
        "image_type": args.type,
        "base_version": args.base_version,
        "target_version": args.target_version,
        "base_size": len(base) if base is not None else 0,
        "target_size": len(target),
        "payload_size": len(payload),
        "container_size": len(container),
        "payload_crc32": f"0x{crc32(payload):08X}",
        "target_crc32": f"0x{crc32(target):08X}",
        "base_sha256": sha256(base).hex() if base is not None else "0" * 64,
        "target_sha256": sha256(target).hex(),
        "signed_region_sha256": sha256(signed_bytes).hex(),
        "container_sha256": sha256(container).hex(),
        "openssl_signature_self_verify": True,
    }
    args.output.with_suffix(args.output.suffix + ".json").write_text(
        __import__("json").dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )

    print(
        f"SECURE_CONTAINER=PASS type={args.type} "
        f"base=v{args.base_version} target=v{args.target_version} "
        f"payload={len(payload)} container={len(container)}"
    )
    print(f"SECURE_CONTAINER_KEY_ID=0x{args.key_id:08X}")
    print(f"SECURE_CONTAINER_SHA256={sha256(container).hex()}")
    print(f"SECURE_CONTAINER_SIGNED_SHA256={sha256(signed_bytes).hex()}")
    print("SECURE_CONTAINER_OPENSSL_VERIFY=PASS")
    return 0


def inspect_container(path: Path) -> int:
    data = path.read_bytes()
    if len(data) < HEADER_SIZE + SIG_SIZE:
        raise SystemExit("container too short")

    fields = FIXED.unpack_from(data, 0)
    extension = EXT.unpack_from(data, FIXED_HEADER_SIZE)

    if fields[0] != MAGIC:
        raise SystemExit("bad SDOT magic")
    if fields[1] != FORMAT_VERSION or fields[2] != HEADER_SIZE:
        raise SystemExit("bad SDOT format/header")
    if extension[0] != EXT_MAGIC:
        raise SystemExit("bad SCX1 extension")

    payload_size = fields[9]
    total = HEADER_SIZE + payload_size + fields[17]
    if total != len(data):
        raise SystemExit("container total length mismatch")

    print(
        f"type={fields[5]} base_version={fields[7]} "
        f"target_version={fields[8]} payload_size={payload_size}"
    )
    print(
        f"target_size={fields[10]} key_id=0x{extension[3]:08X} "
        f"base_size={extension[4]}"
    )
    print(
        f"payload_crc32=0x{fields[14]:08X} "
        f"target_crc32=0x{extension[5]:08X}"
    )
    print(f"container_size={len(data)}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)

    build = sub.add_parser("build")
    build.add_argument("--type", choices=["full", "delta"], required=True)
    build.add_argument("--payload", type=Path, required=True)
    build.add_argument("--target", type=Path, required=True)
    build.add_argument("--base", type=Path)
    build.add_argument("--base-version", type=int, default=0)
    build.add_argument("--target-version", type=int, required=True)
    build.add_argument("--key", type=Path, required=True)
    build.add_argument(
        "--key-id",
        type=lambda value: int(value, 0),
        required=True,
    )
    build.add_argument("--output", type=Path, required=True)
    build.set_defaults(func=build_container)

    inspect = sub.add_parser("inspect")
    inspect.add_argument("container", type=Path)
    inspect.set_defaults(func=lambda args: inspect_container(args.container))

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
