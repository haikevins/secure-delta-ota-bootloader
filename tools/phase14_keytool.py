#!/usr/bin/env python3
"""Extract an ECDSA P-256 public key and provision the STM32 trust anchor."""
from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import tempfile


def run(command: list[str]) -> None:
    result = subprocess.run(
        command,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if result.returncode != 0:
        raise SystemExit(result.stdout)


def public_key_raw(private_or_public_key: Path) -> bytes:
    with tempfile.TemporaryDirectory(prefix="phase14-key-") as td:
        der = Path(td) / "public.der"

        result = subprocess.run(
            [
                "openssl",
                "pkey",
                "-in",
                str(private_or_public_key),
                "-pubout",
                "-outform",
                "DER",
                "-out",
                str(der),
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )

        if result.returncode != 0:
            # Retry as a public-only key.
            result = subprocess.run(
                [
                    "openssl",
                    "pkey",
                    "-pubin",
                    "-in",
                    str(private_or_public_key),
                    "-pubout",
                    "-outform",
                    "DER",
                    "-out",
                    str(der),
                ],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                check=False,
            )

        if result.returncode != 0:
            raise SystemExit(result.stdout)

        data = der.read_bytes()

    # SubjectPublicKeyInfo for P-256 ends with the uncompressed EC point:
    # 0x04 || X(32) || Y(32).
    if len(data) < 65 or data[-65] != 0x04:
        raise SystemExit("OpenSSL public key is not uncompressed P-256")

    point = data[-64:]

    # Ensure the supplied key is really prime256v1/P-256 by asking OpenSSL.
    check = subprocess.run(
        [
            "openssl",
            "pkey",
            "-in",
            str(private_or_public_key),
            "-text",
            "-noout",
        ],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if check.returncode != 0:
        check = subprocess.run(
            [
                "openssl",
                "pkey",
                "-pubin",
                "-in",
                str(private_or_public_key),
                "-text",
                "-noout",
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )

    normalized = check.stdout.lower()
    if (
        "prime256v1" not in normalized
        and "p-256" not in normalized
        and "nist p-256" not in normalized
    ):
        raise SystemExit(
            "Phase 14 requires an ECDSA P-256/prime256v1 key"
        )

    return point


def emit_header(point: bytes, key_id: int, output: Path) -> None:
    if key_id <= 0 or key_id > 0xFFFFFFFF:
        raise SystemExit("key-id must be in 1..0xFFFFFFFF")
    if len(point) != 64:
        raise SystemExit("internal public-key size mismatch")

    rows: list[str] = []
    for offset in range(0, len(point), 8):
        rows.append(
            "    "
            + ", ".join(f"0x{x:02X}U" for x in point[offset:offset + 8])
            + ("," if offset + 8 < len(point) else "")
        )

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        "\n".join(
            [
                "#ifndef PHASE14_TRUSTED_KEY_H",
                "#define PHASE14_TRUSTED_KEY_H",
                "",
                "#include <stdint.h>",
                "",
                "#define PHASE14_TRUSTED_KEY_PROVISIONED 1U",
                f"#define PHASE14_TRUSTED_KEY_ID          0x{key_id:08X}UL",
                "",
                "static const uint8_t g_phase14_trusted_public_key[64] =",
                "{",
                *rows,
                "};",
                "",
                "#endif",
                "",
            ]
        ),
        encoding="utf-8",
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("key", type=Path)
    parser.add_argument(
        "--key-id",
        type=lambda value: int(value, 0),
        required=True,
    )
    parser.add_argument(
        "--output",
        type=Path,
        required=True,
    )
    args = parser.parse_args()

    point = public_key_raw(args.key)
    emit_header(point, args.key_id, args.output)
    print(
        f"PHASE14_KEY_PROVISION=PASS key_id=0x{args.key_id:08X} "
        f"public_key_bytes={len(point)} output={args.output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
