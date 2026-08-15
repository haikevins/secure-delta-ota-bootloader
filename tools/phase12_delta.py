#!/usr/bin/env python3
"""Generate and verify a Phase-12 JojoDiff-compatible firmware delta."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

from jojodiff_patch import apply_patch, generate_patch, inspect_patch

APP_START = 0x08006000
APP_MAX = 38 * 1024
SRAM_BASE = 0x20000000
SRAM_END = 0x20005000
SCHEMA = 1
PATCH_FORMAT = "jojodiff-compatible-v1"


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def validate_application(image: bytes, label: str) -> tuple[int, int]:
    if not 8 <= len(image) <= APP_MAX:
        raise ValueError(
            f"{label} size {len(image)} outside STM32 application partition"
        )

    msp, reset = struct.unpack_from("<II", image)
    if not SRAM_BASE <= msp <= SRAM_END or (msp & 7):
        raise ValueError(f"{label} invalid MSP 0x{msp:08X}")
    if (reset & 1) == 0:
        raise ValueError(f"{label} reset handler is not Thumb")
    if not APP_START <= (reset & ~1) < APP_START + len(image):
        raise ValueError(
            f"{label} reset handler 0x{reset:08X} outside application image"
        )
    return msp, reset


def run_external_applier(executable: Path,
                         source: Path,
                         patch: Path,
                         target: bytes) -> None:
    with tempfile.TemporaryDirectory(prefix="phase12-external-") as td:
        output = Path(td) / "reconstructed.bin"
        result = subprocess.run(
            [str(executable), str(source), str(patch), str(output)],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        print(result.stdout, end="")
        if result.returncode != 0:
            raise RuntimeError(
                f"external JanPatch-compatible applier returned "
                f"{result.returncode}"
            )
        if output.read_bytes() != target:
            raise RuntimeError(
                "external JanPatch-compatible reconstruction mismatch"
            )


def main() -> int:
    root = Path(__file__).resolve().parents[1]

    parser = argparse.ArgumentParser(
        description="Phase 12 deterministic delta release generator"
    )
    parser.add_argument(
        "--base",
        type=Path,
        default=(
            root
            / "node-stm32f103/application/out-phase12-base/application.bin"
        ),
    )
    parser.add_argument(
        "--target",
        type=Path,
        default=(
            root
            / "node-stm32f103/application/out-phase12-target/application.bin"
        ),
    )
    parser.add_argument("--base-version", type=int, default=1)
    parser.add_argument("--target-version", type=int, default=2)
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=root / "dist/phase12",
    )
    parser.add_argument(
        "--min-savings-percent",
        type=float,
        default=20.0,
        help="reject delta if patch saves less than this vs full target",
    )
    parser.add_argument(
        "--allow-inefficient",
        action="store_true",
        help="write a verified patch even if the selection threshold fails",
    )
    parser.add_argument(
        "--janpatch-cli",
        type=Path,
        default=(
            Path(os.environ["JANPATCH_CLI"])
            if os.environ.get("JANPATCH_CLI")
            else None
        ),
        help="optional external JANPatch/compatible CLI for cross-check",
    )
    args = parser.parse_args()

    if args.base_version <= 0 or args.target_version <= 0:
        raise SystemExit("base/target versions must be positive")
    if args.base_version == args.target_version:
        raise SystemExit("base and target versions must differ")
    if not 0.0 <= args.min_savings_percent < 100.0:
        raise SystemExit("min savings must be in [0, 100)")

    base = args.base.read_bytes()
    target = args.target.read_bytes()

    base_msp, base_reset = validate_application(base, "base")
    target_msp, target_reset = validate_application(target, "target")

    patch, stats = generate_patch(base, target)
    reconstructed = apply_patch(base, patch)

    if reconstructed != target:
        raise SystemExit("Phase-12 internal reconstruction mismatch")

    patch_info = inspect_patch(len(base), patch)
    if patch_info["target_size"] != len(target):
        raise SystemExit("patch stream target-size accounting mismatch")

    patch_ratio = len(patch) / len(target)
    savings_percent = (1.0 - patch_ratio) * 100.0
    eligible = savings_percent >= args.min_savings_percent

    if not eligible and not args.allow_inefficient:
        raise SystemExit(
            f"delta rejected: savings={savings_percent:.2f}% "
            f"< threshold={args.min_savings_percent:.2f}%"
        )

    output_dir = args.output_dir
    if not output_dir.is_absolute():
        output_dir = root / output_dir
    output_dir.mkdir(parents=True, exist_ok=True)

    patch_name = (
        f"application-v{args.base_version}-to-v{args.target_version}.jdiff"
    )
    patch_path = output_dir / patch_name
    manifest_path = output_dir / (
        f"application-v{args.base_version}-to-v{args.target_version}.json"
    )
    reconstructed_path = output_dir / (
        f"application-v{args.base_version}-to-v{args.target_version}"
        "-reconstructed.bin"
    )

    patch_path.write_bytes(patch)
    reconstructed_path.write_bytes(reconstructed)

    external_checked = False
    if args.janpatch_cli is not None:
        executable = args.janpatch_cli.resolve()
        if not executable.is_file() or not os.access(executable, os.X_OK):
            raise SystemExit(
                f"--janpatch-cli is not executable: {executable}"
            )
        run_external_applier(
            executable,
            args.base.resolve(),
            patch_path.resolve(),
            target,
        )
        external_checked = True

    manifest = {
        "schema": SCHEMA,
        "artifact_type": "delta",
        "patch_format": PATCH_FORMAT,
        "base_version": args.base_version,
        "target_version": args.target_version,
        "target_load_address": f"0x{APP_START:08X}",
        "base_size": len(base),
        "target_size": len(target),
        "patch_size": len(patch),
        "patch_ratio": round(patch_ratio, 6),
        "savings_percent": round(savings_percent, 2),
        "selection_threshold_percent": args.min_savings_percent,
        "delta_eligible": eligible,
        "base_sha256": sha256_bytes(base),
        "target_sha256": sha256_bytes(target),
        "patch_sha256": sha256_bytes(patch),
        "patch_crc32": f"0x{zlib.crc32(patch) & 0xFFFFFFFF:08X}",
        "target_crc32": f"0x{zlib.crc32(target) & 0xFFFFFFFF:08X}",
        "base_vector": {
            "msp": f"0x{base_msp:08X}",
            "reset": f"0x{base_reset:08X}",
        },
        "target_vector": {
            "msp": f"0x{target_msp:08X}",
            "reset": f"0x{target_reset:08X}",
        },
        "operations": {
            "count": stats.operations,
            "equal_bytes": stats.equal_bytes,
            "modified_bytes": stats.modified_bytes,
            "inserted_bytes": stats.inserted_bytes,
            "deleted_bytes": stats.deleted_bytes,
            "eql_ops": patch_info["EQL"],
            "mod_ops": patch_info["MOD"],
            "ins_ops": patch_info["INS"],
            "del_ops": patch_info["DEL"],
            "bkt_ops": patch_info["BKT"],
        },
        "verification": {
            "internal_roundtrip": True,
            "byte_for_byte_target_match": True,
            "external_janpatch_checked": external_checked,
        },
        "phase13_note": (
            "STM32 delta application is intentionally not enabled until "
            "Phase 13."
        ),
    }

    manifest_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )

    print(
        f"PHASE12_DELTA=PASS base=v{args.base_version} "
        f"target=v{args.target_version}"
    )
    print(
        f"PHASE12_SIZES base={len(base)} target={len(target)} "
        f"patch={len(patch)}"
    )
    print(
        f"PHASE12_SAVINGS={savings_percent:.2f}% "
        f"threshold={args.min_savings_percent:.2f}% "
        f"eligible={'yes' if eligible else 'no'}"
    )
    print(f"PHASE12_BASE_SHA256={sha256_bytes(base)}")
    print(f"PHASE12_TARGET_SHA256={sha256_bytes(target)}")
    print(f"PHASE12_PATCH_SHA256={sha256_bytes(patch)}")
    print(
        f"PHASE12_PATCH_CRC32=0x"
        f"{zlib.crc32(patch) & 0xFFFFFFFF:08X}"
    )
    print(f"PHASE12_PATCH={patch_path}")
    print(f"PHASE12_MANIFEST={manifest_path}")
    print("PHASE12_ROUNDTRIP=PASS byte-for-byte")
    if external_checked:
        print("PHASE12_EXTERNAL_JANPATCH=PASS")
    else:
        print(
            "PHASE12_EXTERNAL_JANPATCH=SKIPPED "
            "(set JANPATCH_CLI for optional cross-check)"
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
