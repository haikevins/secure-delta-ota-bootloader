#!/usr/bin/env python3
from __future__ import annotations

import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
APP_DIR = ROOT / "node-stm32f103/application"

REQUIRED = [
    "shared/include/ext_flash_layout.h",
    "shared/src/ext_flash_layout.c",
    "node-stm32f103/common/include/spi_flash.h",
    "node-stm32f103/common/drivers/spi_flash.c",
    "node-stm32f103/common/include/external_flash_storage.h",
    "node-stm32f103/common/storage/external_flash_storage.c",
    "node-stm32f103/application/include/phase4_flash_selftest.h",
    "node-stm32f103/application/src/phase4_flash_selftest.c",
    "tests/unit/test_phase4_ext_flash.c",
    "docs/external-spi-flash.md",
    "docs/phase-4-checklist.md",
    "scripts/phase4_hw_test.py",
]

def fail(message: str) -> None:
    print(f"Phase 4 check: FAIL: {message}")
    raise SystemExit(1)

def run(command: list[str], cwd: Path = ROOT, echo: bool = True) -> str:
    result = subprocess.run(command, cwd=cwd, check=False, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            env=os.environ.copy())
    if echo:
        print(result.stdout, end="")
    if result.returncode != 0:
        fail(f"command returned {result.returncode}: {' '.join(command)}")
    return result.stdout

def detect_toolchain() -> str:
    requested = os.environ.get("TOOLCHAIN", "").strip()
    if requested:
        if requested not in {"gcc", "clang"}:
            fail("TOOLCHAIN must be gcc or clang")
        return requested
    if shutil.which("arm-none-eabi-gcc"):
        return "gcc"
    if shutil.which("clang") and shutil.which("ld.lld") and shutil.which("llvm-objcopy"):
        return "clang"
    fail("install GNU Arm Embedded GCC or Clang/LLD/llvm-objcopy")
    return ""

def compile_host_tests() -> None:
    compiler = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
    if not compiler:
        fail("native C compiler is required")
    output = ROOT / "build-host/phase4_ext_flash_tests"
    output.parent.mkdir(parents=True, exist_ok=True)
    run([compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
         "-Ishared/include",
         "shared/src/ext_flash_layout.c",
         "tests/unit/test_phase4_ext_flash.c",
         "-o", str(output)])
    run([str(output)])

def read_symbols(elf: Path) -> dict[str, int]:
    readelf = (shutil.which("arm-none-eabi-readelf") or
               shutil.which("llvm-readelf") or shutil.which("readelf"))
    if not readelf:
        fail("readelf is required")
    output = run([readelf, "-sW", str(elf)], echo=False)
    table: dict[str, int] = {}
    pattern = re.compile(r"^\s*\d+:\s+([0-9a-fA-F]+)\s+\d+\s+\S+\s+\S+\s+\S+\s+\S+\s+(.+)$")
    for line in output.splitlines():
        match = pattern.match(line)
        if match:
            table[match.group(2).strip()] = int(match.group(1), 16)
    return table

def check_source() -> None:
    text = (ROOT / "node-stm32f103/common/drivers/spi_flash.c").read_text()
    for token in [
        "SPI_FLASH_CMD_WRITE_ENABLE       0x06U",
        "SPI_FLASH_CMD_READ_STATUS1       0x05U",
        "SPI_FLASH_CMD_READ_DATA          0x03U",
        "SPI_FLASH_CMD_PAGE_PROGRAM       0x02U",
        "SPI_FLASH_CMD_SECTOR_ERASE_4K    0x20U",
        "SPI_FLASH_CMD_JEDEC_ID           0x9FU",
        "SPI_BaudRatePrescaler_4",
        "SPI_CPOL_Low",
        "SPI_CPHA_1Edge",
        "ExtFlash_PageChunkLength",
        "SPI_FLASH_STATUS_NEEDS_ERASE",
        "SpiFlash_Verify(address, data, length)",
    ]:
        if token not in text:
            fail(f"driver missing required operation: {token}")

def check_hw_runner_profile() -> None:
    text = (ROOT / "scripts/phase4_hw_test.py").read_text()
    for token in [
        "source [find interface/stlink.cfg]",
        "transport select hla_swd",
        "source [find target/stm32f1x.cfg]",
        "reset_config none",
        "adapter speed 1000",
    ]:
        if token not in text:
            fail(f"hardware runner missing OpenOCD profile item: {token}")

    for token in [
        "0x00EF4016",
        "0x00EF4017",
        "verify exit 0x08000000",
        "internal Flash program/verify",
        "Phase 4 test execution",
    ]:
        if token not in text:
            fail(f"hardware runner missing required item: {token}")

    if "flash write_image erase" in text:
        fail("hardware runner must use OpenOCD program helper, not the old one-shot flash write_image chain")

def build_forced_image(toolchain: str) -> Path:
    build_dir = "build-phase4-check"
    out_dir = "out-phase4-check"
    base = ["make", f"TOOLCHAIN={toolchain}",
            f"BUILD_DIR={build_dir}", f"OUT_DIR={out_dir}",
            "PROJECT_CFLAGS=-DPHASE4_HW_TEST=1"]
    run(base + ["clean"], cwd=APP_DIR)
    run(base + ["all"], cwd=APP_DIR)

    elf = APP_DIR / out_dir / "application.elf"
    binary = APP_DIR / out_dir / "application.bin"
    if binary.stat().st_size > 38 * 1024:
        fail("Phase 4 test application exceeds 38 KiB")

    symbols = read_symbols(elf)
    for name in [
        "SpiFlash_Init", "SpiFlash_IsSupportedJedecId", "SpiFlash_Read", "SpiFlash_Write",
        "SpiFlash_EraseSector", "SpiFlash_Verify", "SpiFlash_IsErased",
        "ExternalFlashStorage_Init", "ExternalFlashStorage_Read",
        "ExternalFlashStorage_Write", "ExternalFlashStorage_ErasePartition",
        "Phase4FlashSelfTest_Run", "g_phase4_flash_test_status",
        "g_phase4_flash_jedec_id",
    ]:
        if name not in symbols:
            fail(f"forced Phase 4 image missing symbol: {name}")
    return binary

def main() -> None:
    missing = [p for p in REQUIRED if not (ROOT / p).is_file()]
    if missing:
        fail("missing required file(s): " + ", ".join(missing))

    compile_host_tests()
    check_source()
    check_hw_runner_profile()

    toolchain = detect_toolchain()
    binary = build_forced_image(toolchain)

    if not (ROOT / "node-stm32f103/bootloader/build/spi_flash.o").is_file():
        fail("bootloader did not compile common SPI Flash driver")

    run(["python3", "tools/merge_images.py"])
    print("Secure Delta OTA Phase 4 external SPI Flash check: PASS")
    print("Supported JEDEC IDs: 0xEF4016 (W25Q32), 0xEF4017 (W25Q64)")
    print("SPI1: PA5/PA6/PA7, CS=PB0, mode 0, prescaler /4")
    print(f"Forced hardware-test application size: {binary.stat().st_size} bytes")
    print("Hardware validation command: make phase4-hw-test")

if __name__ == "__main__":
    main()
