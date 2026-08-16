#!/usr/bin/env python3
from __future__ import annotations

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

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "node-stm32f103/application"
BOOT = ROOT / "node-stm32f103/bootloader"

APP_START = 0x08006000
APP_MAX = 38 * 1024
SRAM_BASE = 0x20000000
SRAM_END = 0x20005000

BASE_OUT = APP / "out-phase12-base/application.bin"
TARGET_OUT = APP / "out-phase12-target/application.bin"
DELTA_DIR = ROOT / "dist/phase12"
PATCH = DELTA_DIR / "application-v1-to-v2.jdiff"
MANIFEST = DELTA_DIR / "application-v1-to-v2.json"
RECONSTRUCTED = (
    DELTA_DIR / "application-v1-to-v2-reconstructed.bin"
)


def fail(message: str) -> None:
    print(f"Phase 12 check: FAIL: {message}")
    raise SystemExit(1)


def run(cmd: list[str],
        cwd: Path = ROOT,
        timeout: int = 180) -> str:
    result = subprocess.run(
        cmd,
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env=os.environ.copy(),
        timeout=timeout,
    )
    print(result.stdout, end="")
    if result.returncode != 0:
        fail(
            f"command returned {result.returncode}: "
            + " ".join(cmd)
        )
    return result.stdout


def choose_toolchain() -> str:
    requested = os.environ.get("TOOLCHAIN", "").strip()
    if requested:
        return requested
    if shutil.which("arm-none-eabi-gcc"):
        return "gcc"
    if (
        shutil.which("clang")
        and shutil.which("ld.lld")
        and shutil.which("llvm-objcopy")
    ):
        return "clang"
    fail("no supported STM32 ARM toolchain")
    raise AssertionError


def validate_application(path: Path,
                         label: str) -> tuple[int, int, int]:
    data = path.read_bytes()

    if not 8 <= len(data) <= APP_MAX:
        fail(f"{label} size={len(data)} outside 38 KiB application")

    msp, reset = struct.unpack_from("<II", data)

    if not SRAM_BASE <= msp <= SRAM_END or (msp & 7):
        fail(f"{label} invalid MSP 0x{msp:08X}")
    if (reset & 1) == 0:
        fail(f"{label} reset handler is not Thumb")
    if not APP_START <= (reset & ~1) < APP_START + len(data):
        fail(f"{label} reset handler outside image")

    return len(data), msp, reset


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    required = [
        "tools/jojodiff_patch.py",
        "tools/phase12_delta.py",
        "scripts/phase12_check.py",
        "tests/unit/test_phase12_jojodiff.py",
        "tests/host/phase12_jojo_subset_apply.c",
        "docs/phase-12-delta-generation.md",
        "docs/phase-12-checklist.md",
        "PHASE12_REPORT.md",
    ]

    missing = [p for p in required if not (ROOT / p).is_file()]
    if missing:
        fail("missing: " + ", ".join(missing))

    generator = (
        ROOT / "tools/jojodiff_patch.py"
    ).read_text(encoding="utf-8")
    delta_tool = (
        ROOT / "tools/phase12_delta.py"
    ).read_text(encoding="utf-8")
    makefile = (
        ROOT / "Makefile"
    ).read_text(encoding="utf-8")

    for token in [
        "ESC = 0xA7",
        "MOD = 0xA6",
        "INS = 0xA5",
        "DEL = 0xA4",
        "EQL = 0xA3",
        "def encode_length",
        "def escape_data",
        "def generate_patch",
        "def apply_patch",
        "SequenceMatcher",
        "autojunk: bool = False",
        "autojunk=autojunk",
    ]:
        if token not in generator:
            fail(f"JojoDiff generator missing {token}")

    if "BKT" not in generator:
        fail("JojoDiff compatibility parser must understand BKT")

    for token in [
        "base_sha256",
        "target_sha256",
        "patch_sha256",
        "patch_crc32",
        "selection_threshold_percent",
        "delta_eligible",
        "byte_for_byte_target_match",
        "JANPATCH_CLI",
    ]:
        if token not in delta_tool:
            fail(f"Phase-12 release tool missing {token}")

    for token in [
        "phase12-check:",
        "phase12-base:",
        "phase12-target:",
        "phase12-delta:",
    ]:
        if token not in makefile:
            fail(f"Makefile missing {token}")

    run([
        "python3",
        "-m",
        "py_compile",
        "tools/jojodiff_patch.py",
        "tools/phase12_delta.py",
        "scripts/phase12_check.py",
        "tests/unit/test_phase12_jojodiff.py",
    ])

    run(["python3", "tests/unit/test_phase12_jojodiff.py"])

    cc = (
        shutil.which("cc")
        or shutil.which("gcc")
        or shutil.which("clang")
    )
    if cc is None:
        fail("host C compiler missing")

    host_applier = ROOT / "build-host/phase12_jojo_subset_apply"
    host_applier.parent.mkdir(parents=True, exist_ok=True)

    run([
        cc,
        "-std=c11",
        "-O2",
        "-Wall",
        "-Wextra",
        "-Wpedantic",
        "-Werror",
        "tests/host/phase12_jojo_subset_apply.c",
        "-o",
        str(host_applier),
    ])

    toolchain = choose_toolchain()

    run([
        "make",
        f"TOOLCHAIN={toolchain}",
        "clean",
    ], cwd=BOOT)
    run([
        "make",
        f"TOOLCHAIN={toolchain}",
        "all",
    ], cwd=BOOT)

    for out_dir, build_dir, version in [
        ("out-phase12-base", "build-phase12-base", 1),
        ("out-phase12-target", "build-phase12-target", 2),
    ]:
        flags = f"-DAPPLICATION_VERSION=0x{version:08X}UL"

        run([
            "make",
            f"TOOLCHAIN={toolchain}",
            f"BUILD_DIR={build_dir}",
            f"OUT_DIR={out_dir}",
            f"PROJECT_CFLAGS={flags}",
            "clean",
        ], cwd=APP)

        run([
            "make",
            f"TOOLCHAIN={toolchain}",
            f"BUILD_DIR={build_dir}",
            f"OUT_DIR={out_dir}",
            f"PROJECT_CFLAGS={flags}",
            "all",
        ], cwd=APP)

    base_size, base_msp, base_reset = validate_application(
        BASE_OUT,
        "Phase-12 base v1",
    )
    target_size, target_msp, target_reset = validate_application(
        TARGET_OUT,
        "Phase-12 target v2",
    )

    if base_size == 0 or target_size == 0:
        fail("empty firmware image")

    # Generate once through the release tool.
    run([
        "python3",
        "tools/phase12_delta.py",
        "--base", str(BASE_OUT),
        "--target", str(TARGET_OUT),
        "--base-version", "1",
        "--target-version", "2",
        "--output-dir", str(DELTA_DIR),
        "--min-savings-percent", "20",
    ])

    if not PATCH.is_file() or not MANIFEST.is_file():
        fail("Phase-12 delta artifacts were not created")

    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))

    if manifest.get("schema") != 1:
        fail("Phase-12 manifest schema mismatch")
    if manifest.get("artifact_type") != "delta":
        fail("Phase-12 manifest artifact_type mismatch")
    if manifest.get("patch_format") != "jojodiff-compatible-v1":
        fail("Phase-12 patch format mismatch")
    if manifest.get("base_version") != 1:
        fail("Phase-12 base version mismatch")
    if manifest.get("target_version") != 2:
        fail("Phase-12 target version mismatch")
    if manifest.get("base_sha256") != sha256(BASE_OUT):
        fail("Phase-12 base SHA256 mismatch")
    if manifest.get("target_sha256") != sha256(TARGET_OUT):
        fail("Phase-12 target SHA256 mismatch")
    if manifest.get("patch_sha256") != sha256(PATCH):
        fail("Phase-12 patch SHA256 mismatch")

    patch_crc = zlib.crc32(PATCH.read_bytes()) & 0xFFFFFFFF
    if manifest.get("patch_crc32") != f"0x{patch_crc:08X}":
        fail("Phase-12 patch CRC32 mismatch")

    if not manifest.get("delta_eligible"):
        fail("real firmware delta failed configured selection policy")

    savings = float(manifest["savings_percent"])
    if savings < 20.0:
        fail(f"real firmware delta savings too low: {savings:.2f}%")

    if RECONSTRUCTED.read_bytes() != TARGET_OUT.read_bytes():
        fail("Python reconstructed image differs from target")

    # Independent C parser/applicator cross-check.
    with tempfile.TemporaryDirectory(prefix="phase12-c-apply-") as td:
        c_output = Path(td) / "target.bin"
        run([
            str(host_applier),
            str(BASE_OUT),
            str(PATCH),
            str(c_output),
        ])
        if c_output.read_bytes() != TARGET_OUT.read_bytes():
            fail("independent C JojoDiff subset reconstruction mismatch")

    print(
        "Phase 12 independent C JojoDiff-compatible reconstruction: PASS"
    )

    # Determinism: generate a second time into a temporary directory.
    with tempfile.TemporaryDirectory(prefix="phase12-repeat-") as td:
        repeat_dir = Path(td)
        run([
            "python3",
            "tools/phase12_delta.py",
            "--base", str(BASE_OUT),
            "--target", str(TARGET_OUT),
            "--base-version", "1",
            "--target-version", "2",
            "--output-dir", str(repeat_dir),
            "--min-savings-percent", "20",
        ])

        repeat_patch = repeat_dir / PATCH.name
        repeat_manifest = repeat_dir / MANIFEST.name

        if repeat_patch.read_bytes() != PATCH.read_bytes():
            fail("Phase-12 patch generation is not deterministic")

        first_manifest = json.loads(
            MANIFEST.read_text(encoding="utf-8")
        )
        second_manifest = json.loads(
            repeat_manifest.read_text(encoding="utf-8")
        )
        if first_manifest != second_manifest:
            fail("Phase-12 manifest generation is not deterministic")

    print("Phase 12 deterministic generation: PASS")

    external = os.environ.get("JANPATCH_CLI", "").strip()
    if external:
        run([
            "python3",
            "tools/phase12_delta.py",
            "--base", str(BASE_OUT),
            "--target", str(TARGET_OUT),
            "--base-version", "1",
            "--target-version", "2",
            "--output-dir", str(DELTA_DIR),
            "--min-savings-percent", "20",
            "--janpatch-cli", external,
        ])
        print("External JANPatch compatibility check: PASS")
    else:
        print(
            "External JANPatch compatibility check: SKIPPED "
            "(optional JANPATCH_CLI not set)"
        )

    print("Secure Delta OTA Phase 12 delta generation check: PASS")
    print(
        f"Base v1: {base_size} bytes "
        f"sha256={sha256(BASE_OUT)}"
    )
    print(
        f"Target v2: {target_size} bytes "
        f"sha256={sha256(TARGET_OUT)}"
    )
    print(
        f"Patch: {PATCH.stat().st_size} bytes "
        f"crc32=0x{patch_crc:08X} "
        f"savings={savings:.2f}%"
    )
    print(
        f"Base vectors MSP=0x{base_msp:08X} "
        f"reset=0x{base_reset:08X}"
    )
    print(
        f"Target vectors MSP=0x{target_msp:08X} "
        f"reset=0x{target_reset:08X}"
    )
    print(
        "Phase 13 boundary: STM32 does not apply the patch yet."
    )


if __name__ == "__main__":
    main()
