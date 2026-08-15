#!/usr/bin/env python3
from __future__ import annotations

import os
from pathlib import Path
import re
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "node-stm32f103/application"
BOOT = ROOT / "node-stm32f103/bootloader"
APP_START = 0x08006000
APP_MAX = 38 * 1024
SRAM_BASE = 0x20000000
SRAM_END = 0x20005000
FAULT_OFFSET = 1536

def fail(message: str) -> None:
    print(f"Phase 7 check: FAIL: {message}")
    raise SystemExit(1)

def run(cmd, cwd=ROOT, echo=True):
    result = subprocess.run(
        cmd, cwd=cwd, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        env=os.environ.copy()
    )
    if echo:
        print(result.stdout, end="")
    if result.returncode != 0:
        fail(f"command returned {result.returncode}: {' '.join(cmd)}")
    return result.stdout

def toolchain() -> str:
    requested = os.environ.get("TOOLCHAIN", "").strip()
    if requested:
        return requested
    if shutil.which("arm-none-eabi-gcc"):
        return "gcc"
    if (shutil.which("clang") and shutil.which("ld.lld") and
            shutil.which("llvm-objcopy")):
        return "clang"
    fail("no supported ARM toolchain")
    return ""

def symbols(elf: Path) -> set[str]:
    readelf = (shutil.which("arm-none-eabi-readelf") or
               shutil.which("llvm-readelf") or shutil.which("readelf"))
    if not readelf:
        fail("readelf not found")
    out = run([readelf, "-sW", str(elf)], echo=False)
    names: set[str] = set()
    pattern = re.compile(
        r"^\s*\d+:\s+[0-9a-fA-F]+\s+\d+\s+\S+\s+\S+\s+\S+\s+\S+\s+(.+)$"
    )
    for line in out.splitlines():
        match = pattern.match(line)
        if match:
            names.add(match.group(1).strip())
    return names

def check_candidate_vector(path: Path) -> None:
    data = path.read_bytes()
    if not 8 <= len(data) <= APP_MAX:
        fail(f"candidate size invalid: {len(data)}")
    msp, reset = struct.unpack_from("<II", data)
    if not SRAM_BASE <= msp <= SRAM_END or (msp & 7):
        fail(f"candidate MSP invalid: 0x{msp:08X}")
    if (reset & 1) == 0:
        fail(f"candidate Reset_Handler not Thumb: 0x{reset:08X}")
    code = reset & ~1
    if not APP_START <= code < APP_START + len(data):
        fail(f"candidate Reset_Handler outside candidate: 0x{reset:08X}")

def main() -> None:
    required = [
        "shared/include/install_progress.h",
        "shared/src/install_progress.c",
        "shared/include/download_checkpoint.h",
        "shared/src/download_checkpoint.c",
        "node-stm32f103/common/include/download_checkpoint_storage.h",
        "node-stm32f103/common/storage/download_checkpoint_storage.c",
        "node-stm32f103/bootloader/src/image_installer.c",
        "tests/unit/test_phase7_recovery.c",
        "tests/unit/test_phase7_power_loss_model.py",
        "scripts/phase7_hw_test.py",
        "docs/phase-7-power-loss-recovery.md",
        "docs/phase-7-checklist.md",
        "PHASE7_REPORT.md",
    ]
    missing = [p for p in required if not (ROOT / p).is_file()]
    if missing:
        fail("missing: " + ", ".join(missing))

    installer_text = (
        ROOT / "node-stm32f103/bootloader/src/image_installer.c"
    ).read_text(encoding="utf-8")
    for token in [
        "FLASH_ErasePage(APPLICATION_START_ADDRESS + page_offset)",
        "metadata->copy_offset = next_offset",
        "IMAGE_INSTALLER_PAGE_VERIFY_FAILED",
        "PHASE7_FAULT_INJECT_OFFSET",
        "IMAGE_INSTALLER_PHASE7_FAULT_MARKER",
    ]:
        if token not in installer_text:
            fail(f"installer missing recovery token: {token}")

    cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
    if not cc:
        fail("host C compiler missing")
    host = ROOT / "build-host/phase7_recovery"
    host.parent.mkdir(parents=True, exist_ok=True)
    run([
        cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
        "-Ishared/include", "-Inode-stm32f103/bootloader/include",
        "shared/src/crc32.c",
        "shared/src/boot_metadata.c",
        "shared/src/install_progress.c",
        "shared/src/download_checkpoint.c",
        "node-stm32f103/bootloader/src/boot_decision.c",
        "tests/unit/test_phase7_recovery.c",
        "-o", str(host),
    ])
    run([str(host)])
    run(["python3", "tests/unit/test_phase7_power_loss_model.py"])
    run(["python3", "-m", "py_compile",
         "scripts/phase7_hw_test.py",
         "scripts/phase7_check.py",
         "tools/uart_ota_sender.py"])

    tc = toolchain()

    run(["make", f"TOOLCHAIN={tc}", "all"], cwd=BOOT)
    run(["make", f"TOOLCHAIN={tc}", "all"], cwd=APP)

    boot_bin = BOOT / "out/bootloader.bin"
    app_bin = APP / "out/application.bin"
    if boot_bin.stat().st_size > 24 * 1024:
        fail("bootloader exceeds 24 KiB")
    if app_bin.stat().st_size > APP_MAX:
        fail("application exceeds 38 KiB")

    boot_symbols = symbols(BOOT / "out/bootloader.elf")
    for name in [
        "ImageInstaller_ProcessBasicFull",
        "InstallProgress_Validate",
        "InstallProgress_PageLength",
        "InstallProgress_NextCheckpoint",
        "MetadataStorage_Commit",
        "DownloadCheckpointStorage_Clear",
    ]:
        if name not in boot_symbols:
            fail(f"bootloader missing symbol {name}")

    app_symbols = symbols(APP / "out/application.elf")
    for name in [
        "OtaReceiver_ProcessPacket",
        "DownloadCheckpointStorage_Load",
        "DownloadCheckpointStorage_Commit",
        "DownloadCheckpointStorage_Clear",
    ]:
        if name not in app_symbols:
            fail(f"application missing symbol {name}")

    candidate_build = "build-phase7-candidate"
    candidate_out = "out-phase7-candidate"
    run(["make", f"TOOLCHAIN={tc}",
         f"BUILD_DIR={candidate_build}", f"OUT_DIR={candidate_out}",
         "PROJECT_CFLAGS=-DAPPLICATION_VERSION=0x00000002UL",
         "clean"], cwd=APP)
    run(["make", f"TOOLCHAIN={tc}",
         f"BUILD_DIR={candidate_build}", f"OUT_DIR={candidate_out}",
         "PROJECT_CFLAGS=-DAPPLICATION_VERSION=0x00000002UL",
         "all"], cwd=APP)

    candidate = APP / candidate_out / "application.bin"
    check_candidate_vector(candidate)
    if candidate.read_bytes() == app_bin.read_bytes():
        fail("candidate v2 is byte-identical to normal v1 image")
    if candidate.stat().st_size <= FAULT_OFFSET:
        fail("candidate too small for mid-page fault injection")

    fault_build = "build-phase7-fault"
    fault_out = "out-phase7-fault"
    run(["make", f"TOOLCHAIN={tc}",
         f"BUILD_DIR={fault_build}", f"OUT_DIR={fault_out}",
         f"PROJECT_CFLAGS=-DPHASE7_FAULT_INJECT_OFFSET={FAULT_OFFSET}UL",
         "clean"], cwd=BOOT)
    run(["make", f"TOOLCHAIN={tc}",
         f"BUILD_DIR={fault_build}", f"OUT_DIR={fault_out}",
         f"PROJECT_CFLAGS=-DPHASE7_FAULT_INJECT_OFFSET={FAULT_OFFSET}UL",
         "all"], cwd=BOOT)

    fault_bin = BOOT / fault_out / "bootloader.bin"
    if fault_bin.read_bytes() == boot_bin.read_bytes():
        fail("fault-injection bootloader is byte-identical to normal build")
    if fault_bin.stat().st_size > 24 * 1024:
        fail("fault-injection bootloader exceeds 24 KiB")

    run([
        "python3", "tools/merge_images.py",
        "--output", "dist/secure-delta-ota-phase7.bin",
        "--label", "Phase 7",
    ])
    run([
        "python3", "tools/merge_images.py",
        "--bootloader", str(fault_bin),
        "--application", str(app_bin),
        "--output", "dist/secure-delta-ota-phase7-fault.bin",
        "--label", "Phase 7 fault-injection",
    ])

    print("Secure Delta OTA Phase 7 power-loss recovery check: PASS")
    print(f"Bootloader size: {boot_bin.stat().st_size} bytes / 24 KiB")
    print(f"Application size: {app_bin.stat().st_size} bytes / 38 KiB")
    print(f"Candidate v2 size: {candidate.stat().st_size} bytes / 38 KiB")
    print(f"Fault injection offset: {FAULT_OFFSET} bytes (mid second page)")
    print("Hardware: make phase7-hw-test PORT=/dev/ttyUSB0")

if __name__ == "__main__":
    main()
