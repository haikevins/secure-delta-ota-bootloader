#!/usr/bin/env python3
"""Wrap a JojoDiff patch in the compact STM32 delta envelope."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib

MAGIC = 0x50333144  # "D13P"
FORMAT_VERSION = 1
HEADER_SIZE = 48
APP_START = 0x08006000
APP_MAX = 38 * 1024

HEADER_WITHOUT_CRC = struct.Struct("<IHHIIIIIIIII")
HEADER_CRC = struct.Struct("<I")


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def validate_image(image: bytes, label: str) -> None:
    if not 8 <= len(image) <= APP_MAX:
        raise ValueError(f"{label} size outside 38 KiB application region")

    msp, reset = struct.unpack_from("<II", image)
    if not 0x20000000 <= msp <= 0x20005000 or (msp & 7):
        raise ValueError(f"{label} invalid MSP 0x{msp:08X}")
    if (reset & 1) == 0:
        raise ValueError(f"{label} Reset_Handler not Thumb")
    code = reset & ~1
    if not APP_START <= code < APP_START + len(image):
        raise ValueError(f"{label} Reset_Handler outside image")


def build_header(base: bytes,
                 target: bytes,
                 patch: bytes,
                 base_version: int,
                 target_version: int) -> bytes:
    if base_version <= 0 or target_version <= base_version:
        raise ValueError("target_version must be greater than base_version")

    prefix = HEADER_WITHOUT_CRC.pack(
        MAGIC,
        FORMAT_VERSION,
        HEADER_SIZE,
        base_version,
        target_version,
        len(base),
        len(patch),
        len(target),
        APP_START,
        crc32(base),
        crc32(target),
        crc32(patch),
    )

    assert len(prefix) == HEADER_SIZE - 4
    return prefix + HEADER_CRC.pack(crc32(prefix))


def parse_header(raw: bytes) -> dict[str, int]:
    if len(raw) < HEADER_SIZE:
        raise ValueError("artifact shorter than streaming delta reconstruction delta header")

    values = HEADER_WITHOUT_CRC.unpack_from(raw)
    stored_crc = HEADER_CRC.unpack_from(raw, HEADER_SIZE - 4)[0]

    if values[0] != MAGIC:
        raise ValueError("bad streaming delta reconstruction delta magic")
    if values[1] != FORMAT_VERSION or values[2] != HEADER_SIZE:
        raise ValueError("bad streaming delta reconstruction delta header version/size")
    if stored_crc != crc32(raw[:HEADER_SIZE - 4]):
        raise ValueError("streaming delta reconstruction delta header CRC mismatch")

    return {
        "base_version": values[3],
        "target_version": values[4],
        "base_image_size": values[5],
        "patch_size": values[6],
        "target_image_size": values[7],
        "target_load_address": values[8],
        "base_image_crc32": values[9],
        "target_image_crc32": values[10],
        "patch_crc32": values[11],
        "header_crc32": stored_crc,
    }


def main() -> int:
    root = Path(__file__).resolve().parents[1]

    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--base",
        type=Path,
        default=(
            root
            / "node-stm32f103/application/out-delta-base/application.bin"
        ),
    )
    parser.add_argument(
        "--target",
        type=Path,
        default=(
            root
            / "node-stm32f103/application/out-delta-target/application.bin"
        ),
    )
    parser.add_argument(
        "--patch",
        type=Path,
        default=root / "dist/delta/application-v1-to-v2.jdiff",
    )
    parser.add_argument("--base-version", type=int, default=1)
    parser.add_argument("--target-version", type=int, default=2)
    parser.add_argument(
        "--output",
        type=Path,
        default=root / "dist/delta/application-v1-to-v2.d13",
    )
    args = parser.parse_args()

    base = args.base.read_bytes()
    target = args.target.read_bytes()
    patch = args.patch.read_bytes()

    validate_image(base, "base")
    validate_image(target, "target")

    header = build_header(
        base,
        target,
        patch,
        args.base_version,
        args.target_version,
    )
    artifact = header + patch
    parsed = parse_header(artifact)

    if parsed["patch_size"] != len(patch):
        raise SystemExit("internal streaming delta reconstruction patch-size mismatch")
    if parsed["base_image_crc32"] != crc32(base):
        raise SystemExit("internal streaming delta reconstruction base CRC mismatch")
    if parsed["target_image_crc32"] != crc32(target):
        raise SystemExit("internal streaming delta reconstruction target CRC mismatch")
    if parsed["patch_crc32"] != crc32(patch):
        raise SystemExit("internal streaming delta reconstruction patch CRC mismatch")

    output = args.output
    if not output.is_absolute():
        output = root / output
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(artifact)

    meta = {
        "schema": 1,
        "artifact_type": "stm32-delta-functional-envelope",
        "format": "D13P",
        "base_version": args.base_version,
        "target_version": args.target_version,
        "base_size": len(base),
        "target_size": len(target),
        "patch_size": len(patch),
        "artifact_size": len(artifact),
        "header_size": HEADER_SIZE,
        "base_crc32": f"0x{crc32(base):08X}",
        "target_crc32": f"0x{crc32(target):08X}",
        "patch_crc32": f"0x{crc32(patch):08X}",
        "artifact_crc32": f"0x{crc32(artifact):08X}",
        "base_sha256": sha256(base),
        "target_sha256": sha256(target),
        "patch_sha256": sha256(patch),
        "artifact_sha256": sha256(artifact),
        "target_load_address": f"0x{APP_START:08X}",
        "security_note": (
            "CRC32 provides streaming delta reconstruction integrity/base binding only; "
            "signed authenticity remains signed secure container."
        ),
    }
    output.with_suffix(".json").write_text(
        json.dumps(meta, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )

    print(
        f"DELTA_ARTIFACT=PASS size={len(artifact)} "
        f"header={HEADER_SIZE} patch={len(patch)}"
    )
    print(f"DELTA_ARTIFACT_CRC32=0x{crc32(artifact):08X}")
    print(f"DELTA_BASE_CRC32=0x{crc32(base):08X}")
    print(f"DELTA_TARGET_CRC32=0x{crc32(target):08X}")
    print(f"DELTA_PATCH_CRC32=0x{crc32(patch):08X}")
    print(f"DELTA_ARTIFACT_PATH={output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
