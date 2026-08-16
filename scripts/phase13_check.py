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
BOOT = ROOT / "node-stm32f103/bootloader"
APP = ROOT / "node-stm32f103/application"

BASE = APP / "out-phase13-base/application.bin"
TARGET = APP / "out-phase13-target/application.bin"
PATCH = ROOT / "dist/phase13/application-v1-to-v2.jdiff"
ARTIFACT = ROOT / "dist/phase13/application-v1-to-v2.d13"
ARTIFACT_META = ARTIFACT.with_suffix(".json")
BASELINE = ROOT / "dist/secure-delta-ota-phase13.bin"

APP_START = 0x08006000
APP_MAX = 38 * 1024
SRAM_BASE = 0x20000000
SRAM_END = 0x20005000
D13P_HEADER_SIZE = 48


def fail(message: str) -> None:
    print(f"Phase 13 check: FAIL: {message}")
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


def toolchain() -> str:
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
    phase13_hw = (
        ROOT / "scripts/phase13_hw_test.py"
    ).read_text(encoding="utf-8")

    required = [
        "shared/include/delta_patch.h",
        "shared/src/delta_patch.c",
        "node-stm32f103/bootloader/include/delta_patcher.h",
        "node-stm32f103/bootloader/src/delta_patcher.c",
        "node-stm32f103/bootloader/patch/janpatch_port.h",
        "node-stm32f103/bootloader/patch/janpatch_port.c",
        "tests/host/test_phase13_janpatch_port.c",
        "tools/phase13_delta_artifact.py",
        "tools/jojodiff_patch.py",
        "tools/uart_ota_sender.py",
        "scripts/phase13_hw_test.py",
        "docs/phase-13-stm32-delta.md",
        "docs/phase-13-checklist.md",
        "PHASE13_REPORT.md",
    ]

    missing = [p for p in required if not (ROOT / p).is_file()]
    if missing:
        fail("missing: " + ", ".join(missing))

    delta_header = (
        ROOT / "shared/src/delta_patch.c"
    ).read_text(encoding="utf-8")
    patch_port = (
        BOOT / "patch/janpatch_port.c"
    ).read_text(encoding="utf-8")
    delta_patcher = (
        BOOT / "src/delta_patcher.c"
    ).read_text(encoding="utf-8")
    boot_manager = (
        BOOT / "src/boot_manager.c"
    ).read_text(encoding="utf-8")
    installer = (
        BOOT / "src/image_installer.c"
    ).read_text(encoding="utf-8")
    receiver = (
        APP / "src/ota_receiver.c"
    ).read_text(encoding="utf-8")
    sender = (
        ROOT / "tools/uart_ota_sender.py"
    ).read_text(encoding="utf-8")
    makefile = (
        ROOT / "Makefile"
    ).read_text(encoding="utf-8")

    for token in [
        "DELTA_PATCH_MAGIC",
        "DELTA_PATCH_FORMAT_VERSION",
        "DELTA_PATCH_HEADER_SIZE",
        "DeltaPatch_ParseHeader",
        "DeltaPatch_HeaderCrc32",
    ]:
        if token not in delta_header:
            fail(f"D13P parser missing {token}")

    for token in [
        "JDIFF_ESC",
        "JDIFF_MOD",
        "JDIFF_INS",
        "JDIFF_DEL",
        "JDIFF_EQL",
        "JDIFF_BKT",
        "JANPATCH_IO_BUFFER_SIZE 128U",
        "JanpatchPort_Apply",
        "EXTERNAL_FLASH_PARTITION_INCOMING",
        "EXTERNAL_FLASH_PARTITION_RECONSTRUCTED",
        "DELTA_PATCH_HEADER_SIZE + ctx->patch_position",
    ]:
        if token not in patch_port:
            fail(f"embedded patch port missing {token}")

    if "malloc" in patch_port or "free(" in patch_port:
        fail("embedded patch port must not use heap allocation")

    for token in [
        "UPDATE_VERIFYING_CONTAINER",
        "UPDATE_VERIFYING_BASE",
        "UPDATE_PATCHING",
        "UPDATE_IMAGE_READY",
        "ValidatePatchCrc",
        "ValidateBase",
        "EraseReconstructed",
        "VerifyTarget",
        "JanpatchPort_Apply",
        "DELTA_PATCHER_SOURCE_REJECTED",
    ]:
        if token not in delta_patcher:
            fail(f"delta patcher missing {token}")

    if "PATCHING recovery is restart-from-scratch" not in delta_patcher:
        fail("Phase-13 patch recovery policy is not documented in code")

    for token in [
        "BOOT_ACTION_RESTART_VALIDATION",
        "BOOT_ACTION_RESTART_PATCH",
        "DeltaPatcher_Process",
        "DELTA_ERROR_PULSES",
    ]:
        if token not in boot_manager:
            fail(f"boot manager delta integration missing {token}")

    for token in [
        "EXTERNAL_FLASH_PARTITION_RECONSTRUCTED",
        "CandidateSource_t",
        "LoadCandidateSource",
        "ValidateExternalSource",
        "ResumePageCheckpointedCopy",
        "VerifyInstalledApplication",
    ]:
        if token not in installer:
            fail(f"shared installer delta integration missing {token}")

    for token in [
        "OTA_CAP_DELTA_IMAGE",
        "FW_IMAGE_DELTA",
        "DELTA_PATCH_HEADER_SIZE",
        "IncomingLooksLikeDelta",
        "ValidateDeltaInstallRequest",
        "PersistDeltaInstallRequest",
        "OTA_STATUS_BASE_MISMATCH",
    ]:
        if token not in receiver:
            fail(f"application delta receiver missing {token}")

    for token in [
        'sub.add_parser("delta-ota")',
        "parse_phase13_delta_artifact",
        "CAP_DELTA_IMAGE",
        "FW_IMAGE_DELTA",
        "container_header_size=PHASE13_DELTA_HEADER_SIZE",
        "Phase 13 Delta OTA PASS",
    ]:
        if token not in sender:
            fail(f"PC delta sender missing {token}")

    for token in [
        "phase13-base:",
        "phase13-target:",
        "phase13-delta:",
        "phase13-baseline:",
        "phase13-check:",
        "phase13-hw-test:",
    ]:
        if token not in makefile:
            fail(f"Makefile missing {token}")

    # Keep signing/authenticity out of Phase 13.
    if "signature" in delta_patcher.lower():
        fail("Phase 13 delta patcher must not claim signature verification")

    for token in [
        "def wait_for_baseline_uart(",
        "command=CMD_QUERY",
        "link.timeout = 1.5",
        "link.retries = 5",
        "P13_UART_SYNC",
        "wait_seconds=25.0",
    ]:
        if token not in phase13_hw:
            fail(f"Phase-13 hardware UART sync regression missing: {token}")

    run([
        "python3",
        "-m",
        "py_compile",
        "tools/jojodiff_patch.py",
        "tools/phase13_delta_artifact.py",
        "tools/ota_uart_protocol.py",
        "tools/uart_ota_sender.py",
        "scripts/phase13_check.py",
        "scripts/phase13_hw_test.py",
    ])

    # Retain the Phase-12 generator property tests.
    run(["python3", "tests/unit/test_phase12_jojodiff.py"])

    tc = toolchain()

    run(["make", f"TOOLCHAIN={tc}", "clean"], cwd=BOOT)
    run(["make", f"TOOLCHAIN={tc}", "all"], cwd=BOOT)

    boot_size = (BOOT / "out/bootloader.bin").stat().st_size
    if boot_size > 24 * 1024:
        fail(
            f"Phase-13 bootloader={boot_size} bytes exceeds 24 KiB"
        )

    for build_dir, out_dir, version in [
        ("build-phase13-base", "out-phase13-base", 1),
        ("build-phase13-target", "out-phase13-target", 2),
    ]:
        flags = f"-DAPPLICATION_VERSION=0x{version:08X}UL"

        run([
            "make",
            f"TOOLCHAIN={tc}",
            f"BUILD_DIR={build_dir}",
            f"OUT_DIR={out_dir}",
            f"PROJECT_CFLAGS={flags}",
            "clean",
        ], cwd=APP)

        run([
            "make",
            f"TOOLCHAIN={tc}",
            f"BUILD_DIR={build_dir}",
            f"OUT_DIR={out_dir}",
            f"PROJECT_CFLAGS={flags}",
            "all",
        ], cwd=APP)

    base_size, base_msp, base_reset = validate_application(
        BASE,
        "Phase-13 base v1",
    )
    target_size, target_msp, target_reset = validate_application(
        TARGET,
        "Phase-13 target v2",
    )

    run([
        "python3",
        "tools/jojodiff_patch.py",
        "generate",
        str(BASE),
        str(TARGET),
        str(PATCH),
    ])

    run([
        "python3",
        "tools/phase13_delta_artifact.py",
        "--base", str(BASE),
        "--target", str(TARGET),
        "--patch", str(PATCH),
        "--base-version", "1",
        "--target-version", "2",
        "--output", str(ARTIFACT),
    ])

    meta = json.loads(
        ARTIFACT_META.read_text(encoding="utf-8")
    )

    if meta["base_size"] != base_size:
        fail("D13P metadata base size mismatch")
    if meta["target_size"] != target_size:
        fail("D13P metadata target size mismatch")
    if meta["artifact_size"] != ARTIFACT.stat().st_size:
        fail("D13P artifact size mismatch")
    if meta["base_sha256"] != sha256(BASE):
        fail("D13P base SHA-256 mismatch")
    if meta["target_sha256"] != sha256(TARGET):
        fail("D13P target SHA-256 mismatch")
    if meta["patch_sha256"] != sha256(PATCH):
        fail("D13P patch SHA-256 mismatch")

    artifact_crc = zlib.crc32(ARTIFACT.read_bytes()) & 0xFFFFFFFF
    if meta["artifact_crc32"] != f"0x{artifact_crc:08X}":
        fail("D13P artifact CRC mismatch")

    if ARTIFACT.stat().st_size >= target_size:
        fail("Phase-13 delta artifact is not smaller than full target")

    # Compile the exact embedded patch implementation on the host with only
    # flash primitives mocked by the test harness.
    cc = (
        shutil.which("cc")
        or shutil.which("gcc")
        or shutil.which("clang")
    )
    if cc is None:
        fail("host C compiler missing")

    host_test = ROOT / "build-host/phase13_janpatch_port"
    host_test.parent.mkdir(parents=True, exist_ok=True)

    run([
        cc,
        "-std=c11",
        "-O2",
        "-Wall",
        "-Wextra",
        "-Wpedantic",
        "-Werror",
        "-DJANPATCH_PORT_HOST_TEST=1",
        "-Ishared/include",
        "-Inode-stm32f103/common/include",
        "-Inode-stm32f103/bootloader/patch",
        "shared/src/crc32.c",
        "shared/src/delta_patch.c",
        "node-stm32f103/bootloader/patch/janpatch_port.c",
        "tests/host/test_phase13_janpatch_port.c",
        "-o",
        str(host_test),
    ])

    run([
        str(host_test),
        str(BASE),
        str(ARTIFACT),
        str(TARGET),
    ])

    # The original host generator must also reconstruct the patch bytes that
    # the embedded engine consumed.
    with tempfile.TemporaryDirectory(prefix="phase13-python-") as td:
        reconstructed = Path(td) / "target.bin"
        run([
            "python3",
            "tools/jojodiff_patch.py",
            "apply",
            str(BASE),
            str(PATCH),
            str(reconstructed),
        ])
        if reconstructed.read_bytes() != TARGET.read_bytes():
            fail("Python Phase-13 patch reconstruction mismatch")

    run([
        "python3",
        "tools/merge_images.py",
        "--bootloader",
        str(BOOT / "out/bootloader.bin"),
        "--application",
        str(BASE),
        "--output",
        str(BASELINE),
        "--label",
        "Phase 13",
    ])

    print("Secure Delta OTA Phase 13 STM32 delta check: PASS")
    print(
        f"Bootloader: {boot_size} bytes / {24 * 1024} bytes"
    )
    print(
        f"Base v1: {base_size} bytes "
        f"MSP=0x{base_msp:08X} reset=0x{base_reset:08X}"
    )
    print(
        f"Target v2: {target_size} bytes "
        f"MSP=0x{target_msp:08X} reset=0x{target_reset:08X}"
    )
    print(
        f"Patch: {PATCH.stat().st_size} bytes; "
        f"D13P artifact: {ARTIFACT.stat().st_size} bytes; "
        f"artifact CRC32=0x{artifact_crc:08X}"
    )
    print(
        "Embedded JanpatchPort reconstruction: PASS byte-for-byte"
    )
    print(
        "Hardware: make phase13-hw-test PORT=/dev/ttyUSB0"
    )


if __name__ == "__main__":
    main()
