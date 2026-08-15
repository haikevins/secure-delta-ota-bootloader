#!/usr/bin/env python3
from __future__ import annotations

import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
APP_DIR = ROOT / "node-stm32f103/application"
BOOT_DIR = ROOT / "node-stm32f103/bootloader"
OUTPUT = ROOT / "dist/secure-delta-ota-phase4-hwtest.bin"
PASS_VALUE = 0x50415353
SUPPORTED_JEDEC = {
    0x00EF4016: "W25Q32",
    0x00EF4017: "W25Q64",
}

def fail(message: str) -> None:
    print(f"Phase 4 hardware test: FAIL: {message}")
    raise SystemExit(1)

def run(command: list[str], cwd: Path = ROOT) -> str:
    result = subprocess.run(command, cwd=cwd, check=False, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            env=os.environ.copy())
    print(result.stdout, end="")
    if result.returncode != 0:
        fail(f"command returned {result.returncode}: {' '.join(command)}")
    return result.stdout


OPENOCD_SETUP = (
    "source [find interface/stlink.cfg]; "
    "transport select hla_swd; "
    "source [find target/stm32f1x.cfg]; "
    "reset_config none; "
    "adapter speed 1000; "
)

def run_openocd(openocd: str, commands: str, stage: str) -> str:
    """
    Run one OpenOCD stage with the fixed Phase 4 connection profile.

    Keeping programming and test execution in separate OpenOCD processes makes
    failures unambiguous: an internal-Flash programming failure is reported
    separately from an external-SPI-Flash self-test failure.
    """
    result = subprocess.run(
        [openocd, "-c", OPENOCD_SETUP + commands],
        cwd=ROOT,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env=os.environ.copy(),
    )
    print(result.stdout, end="")
    if result.returncode != 0:
        fail(f"{stage} failed: OpenOCD returned {result.returncode}")
    return result.stdout

def tcl_path(path: Path) -> str:
    # Tcl braces preserve an absolute path as one argument.
    value = str(path.resolve())
    if "}" in value:
        fail("project path containing '}' is not supported by the hardware-test runner")
    return "{" + value + "}"


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
    fail("no supported ARM toolchain found")
    return ""

def symbols(elf: Path) -> dict[str, int]:
    readelf = (shutil.which("arm-none-eabi-readelf") or
               shutil.which("llvm-readelf") or shutil.which("readelf"))
    if not readelf:
        fail("readelf is required")
    result = subprocess.run([readelf, "-sW", str(elf)], cwd=ROOT, check=False,
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode != 0:
        fail("cannot inspect application ELF")
    table: dict[str, int] = {}
    pattern = re.compile(r"^\s*\d+:\s+([0-9a-fA-F]+)\s+\d+\s+\S+\s+\S+\s+\S+\s+\S+\s+(.+)$")
    for line in result.stdout.splitlines():
        m = pattern.match(line)
        if m:
            table[m.group(2).strip()] = int(m.group(1), 16)
    return table

def parse(output: str, name: str) -> int:
    m = re.search(rf"{re.escape(name)}=0x([0-9A-Fa-f]+)", output)
    if not m:
        fail(f"OpenOCD output missing {name}")
    return int(m.group(1), 16)

def main() -> None:
    openocd = os.environ.get("OPENOCD", "openocd")
    if not shutil.which(openocd):
        fail(f"OpenOCD not found: {openocd}")

    toolchain = detect_toolchain()
    run(["make", f"TOOLCHAIN={toolchain}", "all"], cwd=BOOT_DIR)

    vars = [f"TOOLCHAIN={toolchain}",
            "BUILD_DIR=build-phase4-hwtest",
            "OUT_DIR=out-phase4-hwtest",
            "PROJECT_CFLAGS=-DPHASE4_HW_TEST=1"]
    run(["make", *vars, "clean"], cwd=APP_DIR)
    run(["make", *vars, "all"], cwd=APP_DIR)

    app_elf = APP_DIR / "out-phase4-hwtest/application.elf"
    app_bin = APP_DIR / "out-phase4-hwtest/application.bin"

    run(["python3", "tools/merge_images.py",
         "--bootloader", str(BOOT_DIR / "out/bootloader.bin"),
         "--application", str(app_bin),
         "--output", str(OUTPUT)])

    table = symbols(app_elf)
    names = ["g_phase4_flash_test_status",
             "g_phase4_flash_jedec_id",
             "g_phase4_flash_driver_status"]
    for name in names:
        if name not in table:
            fail(f"missing symbol: {name}")

    status_addr, jedec_addr, driver_addr = [table[n] for n in names]

    # Stage 1: use OpenOCD's documented standalone programmer helper.
    # For a raw binary the address is supplied as the final offset argument.
    # "program" performs init/reset-init, erase/write, and verify.
    program_commands = (
        f"program {tcl_path(OUTPUT)} verify exit 0x08000000"
    )
    run_openocd(openocd, program_commands, "internal Flash program/verify")

    # Stage 2: erase only the two internal metadata pages, boot the test image,
    # then read the retained self-test result from SRAM.
    test_commands = (
        f"init; reset halt; "
        f"flash erase_address 0x0800F800 0x800; "
        f"reset run; sleep 6000; halt; "
        f"set p4s [mrw 0x{status_addr:08X}]; "
        f"set p4j [mrw 0x{jedec_addr:08X}]; "
        f"set p4d [mrw 0x{driver_addr:08X}]; "
        'echo [format "P4_STATUS=0x%08X" $p4s]; '
        'echo [format "P4_JEDEC=0x%08X" $p4j]; '
        'echo [format "P4_DRIVER_STATUS=0x%08X" $p4d]; shutdown'
    )
    output = run_openocd(openocd, test_commands, "Phase 4 test execution")

    status = parse(output, "P4_STATUS")
    jedec = parse(output, "P4_JEDEC")
    driver = parse(output, "P4_DRIVER_STATUS")

    if status != PASS_VALUE:
        fail(f"self-test status=0x{status:08X}, JEDEC=0x{jedec:08X}, driver={driver}")
    if jedec not in SUPPORTED_JEDEC:
        supported = ", ".join(f"0x{x:08X}" for x in SUPPORTED_JEDEC)
        fail(f"JEDEC=0x{jedec:08X}; supported IDs: {supported}")
    if driver != 0:
        fail(f"driver status={driver}; expected 0")

    print(f"Phase 4 hardware SPI Flash test: PASS ({SUPPORTED_JEDEC[jedec]}, JEDEC=0x{jedec:08X})")
    print("Only external sector 0x3FF000-0x3FFFFF was used destructively.")
    print("Restore normal firmware with: make flash-combined")

if __name__ == "__main__":
    main()
