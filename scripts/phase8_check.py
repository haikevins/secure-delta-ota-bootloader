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

def fail(message: str) -> None:
    print(f"Phase 8 check: FAIL: {message}")
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

def symbol_names(elf: Path) -> set[str]:
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

def check_vector(path: Path) -> None:
    data = path.read_bytes()
    if not 8 <= len(data) <= APP_MAX:
        fail(f"{path.name} invalid size {len(data)}")
    msp, reset = struct.unpack_from("<II", data)
    if not SRAM_BASE <= msp <= SRAM_END or (msp & 7):
        fail(f"{path.name} invalid MSP 0x{msp:08X}")
    if (reset & 1) == 0:
        fail(f"{path.name} reset is not Thumb 0x{reset:08X}")
    code = reset & ~1
    if not APP_START <= code < APP_START + len(data):
        fail(f"{path.name} reset outside image 0x{reset:08X}")

def build_candidate(tc: str, build: str, out: str, flags: str) -> Path:
    run(["make", f"TOOLCHAIN={tc}",
         f"BUILD_DIR={build}", f"OUT_DIR={out}",
         f"PROJECT_CFLAGS={flags}", "clean"], cwd=APP)
    run(["make", f"TOOLCHAIN={tc}",
         f"BUILD_DIR={build}", f"OUT_DIR={out}",
         f"PROJECT_CFLAGS={flags}", "all"], cwd=APP)
    path = APP / out / "application.bin"
    check_vector(path)
    return path

def main() -> None:
    required = [
        "shared/include/backup_image.h",
        "shared/src/backup_image.c",
        "shared/include/backup_progress.h",
        "shared/src/backup_progress.c",
        "node-stm32f103/common/include/backup_image_storage.h",
        "node-stm32f103/common/storage/backup_image_storage.c",
        "node-stm32f103/bootloader/include/trial_boot.h",
        "node-stm32f103/bootloader/src/trial_boot.c",
        "node-stm32f103/application/include/trial_confirmation.h",
        "node-stm32f103/application/src/trial_confirmation.c",
        "tests/unit/test_phase8_trial_rollback.c",
        "tests/unit/test_phase8_trial_rollback_model.py",
        "scripts/phase8_hw_test.py",
        "docs/phase-8-trial-boot-rollback.md",
        "docs/phase-8-checklist.md",
        "PHASE8_REPORT.md",
    ]
    missing = [p for p in required if not (ROOT / p).is_file()]
    if missing:
        fail("missing: " + ", ".join(missing))

    installer = (
        ROOT / "node-stm32f103/bootloader/src/image_installer.c"
    ).read_text(encoding="utf-8")
    for token in [
        "UPDATE_BACKING_UP",
        "BackupImageStorage_CommitHeader",
        "APPLICATION_MAX_SIZE",
        "UPDATE_TRIAL_BOOT",
        "ImageInstaller_ProcessRollback",
        "metadata->copy_offset = next_offset",
        "FLASH_ErasePage(APPLICATION_START_ADDRESS + page_offset)",
        "PHASE7_FAULT_INJECT_OFFSET",
    ]:
        if token not in installer:
            fail(f"installer missing Phase-8 token: {token}")

    trial_boot = (
        ROOT / "node-stm32f103/bootloader/src/trial_boot.c"
    ).read_text(encoding="utf-8")
    for token in [
        "++working.boot_attempts",
        "IWDG_Prescaler_64",
        "IWDG_Enable",
        "UPDATE_CONFIRMED",
    ]:
        if token not in trial_boot:
            fail(f"trial controller missing token: {token}")

    cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
    if not cc:
        fail("host C compiler missing")

    host = ROOT / "build-host/phase8_trial_rollback"
    host.parent.mkdir(parents=True, exist_ok=True)
    run([
        cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
        "-Ishared/include", "-Inode-stm32f103/bootloader/include",
        "shared/src/crc32.c",
        "shared/src/backup_image.c",
        "shared/src/backup_progress.c",
        "shared/src/boot_metadata.c",
        "node-stm32f103/bootloader/src/boot_decision.c",
        "tests/unit/test_phase8_trial_rollback.c",
        "-o", str(host),
    ])
    run([str(host)])
    run(["python3", "tests/unit/test_phase8_trial_rollback_model.py"])
    run(["python3", "-m", "py_compile",
         "scripts/phase8_check.py",
         "scripts/phase8_hw_test.py",
         "tools/uart_ota_sender.py",
         "tools/ota_uart_protocol.py"])

    tc = toolchain()

    run(["make", f"TOOLCHAIN={tc}", "all"], cwd=BOOT)
    run(["make", f"TOOLCHAIN={tc}", "all"], cwd=APP)

    boot_bin = BOOT / "out/bootloader.bin"
    app_bin = APP / "out/application.bin"
    if boot_bin.stat().st_size > 24 * 1024:
        fail("bootloader exceeds 24 KiB")
    if app_bin.stat().st_size > APP_MAX:
        fail("application exceeds 38 KiB")

    boot_symbols = symbol_names(BOOT / "out/bootloader.elf")
    for name in [
        "ImageInstaller_ProcessBasicFull",
        "ImageInstaller_ProcessRollback",
        "BackupImageStorage_LoadHeader",
        "BackupImageStorage_CommitHeader",
        "TrialBoot_PrepareAttempt",
        "TrialBoot_FinalizeConfirmation",
        "TrialBoot_StartWatchdog",
        "IWDG_Enable",
    ]:
        if name not in boot_symbols:
            fail(f"bootloader missing symbol {name}")

    app_symbols = symbol_names(APP / "out/application.elf")
    for name in [
        "TrialConfirmation_Init",
        "TrialConfirmation_Process",
        "TrialConfirmation_ConfirmNow",
        "OtaReceiver_ProcessPacket",
    ]:
        if name not in app_symbols:
            fail(f"application missing symbol {name}")

    good = build_candidate(
        tc,
        "build-phase8-good",
        "out-phase8-good",
        "-DAPPLICATION_VERSION=0x00000002UL",
    )
    bad = build_candidate(
        tc,
        "build-phase8-bad",
        "out-phase8-bad",
        "-DAPPLICATION_VERSION=0x00000003UL "
        "-DPHASE8_DISABLE_TRIAL_CONFIRM=1",
    )

    if good.read_bytes() == app_bin.read_bytes():
        fail("healthy v2 candidate is byte-identical to baseline v1")
    if bad.read_bytes() == good.read_bytes():
        fail("unhealthy v3 candidate is byte-identical to healthy v2")

    run([
        "python3", "tools/merge_images.py",
        "--output", "dist/secure-delta-ota-phase8.bin",
        "--label", "Phase 8",
    ])

    print("Secure Delta OTA Phase 8 trial boot/rollback check: PASS")
    print(f"Bootloader size: {boot_bin.stat().st_size} bytes / 24 KiB")
    print(f"Application v1 size: {app_bin.stat().st_size} bytes / 38 KiB")
    print(f"Healthy candidate v2 size: {good.stat().st_size} bytes / 38 KiB")
    print(f"Unhealthy candidate v3 size: {bad.stat().st_size} bytes / 38 KiB")
    print("Trial attempts: 3; watchdog: IWDG")
    print("Hardware: make phase8-hw-test PORT=/dev/ttyUSB0")

if __name__ == "__main__":
    main()
