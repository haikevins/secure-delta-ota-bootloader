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
    print(f"Phase 6 check: FAIL: {message}")
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
    if shutil.which("clang") and shutil.which("ld.lld") and shutil.which("llvm-objcopy"):
        return "clang"
    fail("no supported ARM toolchain")
    return ""

def symbols(elf: Path):
    readelf = (shutil.which("arm-none-eabi-readelf") or
               shutil.which("llvm-readelf") or shutil.which("readelf"))
    out = run([readelf, "-sW", str(elf)], echo=False)
    names = set()
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
        "shared/include/update_handoff.h",
        "shared/src/update_handoff.c",
        "shared/include/full_image_validation.h",
        "shared/src/full_image_validation.c",
        "node-stm32f103/common/storage/update_handoff_storage.c",
        "node-stm32f103/application/src/ota_receiver.c",
        "node-stm32f103/bootloader/src/image_installer.c",
        "tools/uart_ota_sender.py",
        "scripts/phase6_hw_test.py",
        "tests/unit/test_phase6_full_ota.c",
        "tests/unit/test_phase6_pc_preflight.py",
        "docs/phase-6-full-ota-basic.md",
        "docs/phase-6-checklist.md",
        "PHASE6_REPORT.md",
    ]
    missing = [p for p in required if not (ROOT / p).is_file()]
    if missing:
        fail("missing: " + ", ".join(missing))

    cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
    host = ROOT / "build-host/phase6_full_ota"
    host.parent.mkdir(parents=True, exist_ok=True)
    run([
        cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
        "-Ishared/include", "-Inode-stm32f103/bootloader/include",
        "shared/src/crc32.c",
        "shared/src/boot_metadata.c",
        "shared/src/full_image_validation.c",
        "shared/src/update_handoff.c",
        "node-stm32f103/bootloader/src/boot_decision.c",
        "tests/unit/test_phase6_full_ota.c",
        "-o", str(host),
    ])
    run([str(host)])
    run(["python3", "tests/unit/test_phase6_pc_preflight.py"])
    run(["python3", "-m", "py_compile",
         "tools/ota_uart_protocol.py", "tools/uart_ota_sender.py",
         "scripts/phase6_hw_test.py"])

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
    app_symbols = symbols(APP / "out/application.elf")

    for name in [
        "ImageInstaller_ProcessBasicFull",
        "UpdateHandoffStorage_Load",
        "UpdateHandoff_Validate",
        "FullImage_ValidateVector",
    ]:
        if name not in boot_symbols:
            fail(f"bootloader missing symbol {name}")

    for name in [
        "MetadataStorage_Commit",
        "UpdateHandoffStorage_Commit",
        "OtaReceiver_ShouldReset",
    ]:
        if name not in app_symbols:
            fail(f"application missing symbol {name}")

    candidate_build = "build-phase6-candidate"
    candidate_out = "out-phase6-candidate"
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

    run(["python3", "tools/merge_images.py",
         "--output", "dist/secure-delta-ota-phase6.bin",
         "--label", "Phase 6"])

    print("Secure Delta OTA Phase 6 basic full OTA check: PASS")
    print(f"Bootloader size: {boot_bin.stat().st_size} bytes / 24 KiB")
    print(f"Application size: {app_bin.stat().st_size} bytes / 38 KiB")
    print(f"Candidate v2 size: {candidate.stat().st_size} bytes / 38 KiB")
    print("Hardware: make phase6-hw-test PORT=/dev/ttyUSB0")

if __name__ == "__main__":
    main()
