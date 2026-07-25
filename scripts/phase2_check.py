#!/usr/bin/env python3
"""Build and structurally verify Phase 2 bootloader-to-application handoff."""
from __future__ import annotations

import os
from pathlib import Path
import re
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
FLASH_BASE = 0x08000000
APPLICATION_ADDRESS = 0x08006000
METADATA_ADDRESS = 0x0800FC00
SRAM_BASE = 0x20000000
SRAM_END = 0x20005000

REQUIRED = [
    "node-stm32f103/bootloader/include/application_jump.h",
    "node-stm32f103/bootloader/src/application_jump.c",
    "node-stm32f103/bootloader/startup/application_handoff.s",
    "node-stm32f103/bootloader/include/boot_manager.h",
    "node-stm32f103/bootloader/src/boot_manager.c",
    "tools/merge_phase2_images.py",
    "docs/phase-2-checklist.md",
    "docs/boot-jump.md",
]


def fail(message: str) -> None:
    print(f"Phase 2 check: FAIL: {message}")
    raise SystemExit(1)


def run(command: list[str], cwd: Path = ROOT, echo: bool = True) -> str:
    result = subprocess.run(
        command,
        cwd=cwd,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env=os.environ.copy(),
    )
    if echo:
        print(result.stdout, end="")
    if result.returncode != 0:
        fail(f"command returned {result.returncode}: {' '.join(command)}")
    return result.stdout


def tool(name_gcc: str, name_llvm: str, generic: str | None = None) -> str:
    for candidate in (name_gcc, name_llvm, generic):
        if candidate and shutil.which(candidate):
            return candidate
    fail(f"required inspection tool not found: {name_gcc} or {name_llvm}")
    return ""


def symbols(elf: Path) -> dict[str, int]:
    readelf = tool("arm-none-eabi-readelf", "llvm-readelf", "readelf")
    output = run([readelf, "-sW", str(elf)], echo=False)
    result: dict[str, int] = {}
    pattern = re.compile(
        r"^\s*\d+:\s+([0-9a-fA-F]+)\s+\d+\s+\S+\s+\S+\s+\S+\s+\S+\s+(.+)$"
    )
    for line in output.splitlines():
        match = pattern.match(line)
        if match:
            result[match.group(2).strip()] = int(match.group(1), 16)
    return result


def vector_words(binary: Path, count: int = 16) -> tuple[int, ...]:
    data = binary.read_bytes()
    required = count * 4
    if len(data) < required:
        fail(f"{binary} is too small to contain {count} vector words")
    return struct.unpack("<" + "I" * count, data[:required])


def require_symbol(table: dict[str, int], name: str, low: int, high: int) -> int:
    value = table.get(name)
    if value is None:
        fail(f"missing symbol {name}")
    code_value = value & ~1
    if not (low <= code_value < high):
        fail(f"symbol {name}=0x{value:08X} outside expected range")
    return value


def verify_handoff_instructions(boot_elf: Path) -> None:
    objdump = tool("arm-none-eabi-objdump", "llvm-objdump", "objdump")
    output = run([objdump, "-d", str(boot_elf)], echo=False)
    marker = "<ApplicationJump_SetStackAndBranch>:"
    start = output.find(marker)
    if start < 0:
        fail("handoff assembly function missing from disassembly")
    fragment = output[start : start + 1000].lower()
    checks = ["msr\tmsp", "msr\tcontrol", "msr\tprimask", "bx\tr1"]
    for instruction in checks:
        if instruction not in fragment:
            fail(f"handoff disassembly missing '{instruction}'")


def verify_merged_image() -> None:
    run(["python3", "tools/merge_phase2_images.py"])
    boot = (ROOT / "node-stm32f103/bootloader/out/bootloader.bin").read_bytes()
    app = (ROOT / "node-stm32f103/application/out/application.bin").read_bytes()
    merged_path = ROOT / "dist/secure-delta-ota-phase2.bin"
    merged = merged_path.read_bytes()
    offset = APPLICATION_ADDRESS - FLASH_BASE

    if merged[: len(boot)] != boot:
        fail("combined image bootloader bytes do not match")
    if merged[len(boot) : offset] != b"\xFF" * (offset - len(boot)):
        fail("combined image gap is not erased 0xFF")
    if merged[offset : offset + len(app)] != app:
        fail("combined image application bytes do not match")


def main() -> None:
    missing = [path for path in REQUIRED if not (ROOT / path).is_file()]
    if missing:
        fail("missing required file(s): " + ", ".join(missing))

    # Reuse Phase 1's full build, vector placement and budget verification.
    run(["python3", "scripts/phase1_check.py"])

    boot_elf = ROOT / "node-stm32f103/bootloader/out/bootloader.elf"
    app_elf = ROOT / "node-stm32f103/application/out/application.elf"
    boot_bin = ROOT / "node-stm32f103/bootloader/out/bootloader.bin"
    app_bin = ROOT / "node-stm32f103/application/out/application.bin"

    boot_symbols = symbols(boot_elf)
    app_symbols = symbols(app_elf)

    for name in (
        "BootManager_Run",
        "ApplicationJump_Validate",
        "ApplicationJump_Execute",
        "ApplicationJump_SetStackAndBranch",
        "SysTick_Handler",
    ):
        require_symbol(boot_symbols, name, FLASH_BASE, APPLICATION_ADDRESS)

    app_reset = require_symbol(
        app_symbols, "Reset_Handler", APPLICATION_ADDRESS, METADATA_ADDRESS
    )
    app_systick = require_symbol(
        app_symbols, "SysTick_Handler", APPLICATION_ADDRESS, METADATA_ADDRESS
    )
    require_symbol(app_symbols, "main", APPLICATION_ADDRESS, METADATA_ADDRESS)

    boot_vectors = vector_words(boot_bin)
    app_vectors = vector_words(app_bin)

    initial_msp, reset_handler = app_vectors[0], app_vectors[1]
    if initial_msp != SRAM_END:
        fail(f"application MSP is 0x{initial_msp:08X}; expected 0x{SRAM_END:08X}")
    if not (SRAM_BASE <= initial_msp <= SRAM_END) or initial_msp % 8 != 0:
        fail("application MSP range/alignment is invalid")
    if (reset_handler & 1) == 0:
        fail("application reset vector does not set the Thumb bit")
    if not (APPLICATION_ADDRESS <= (reset_handler & ~1) < METADATA_ADDRESS):
        fail("application reset vector lies outside the application partition")
    if (reset_handler & ~1) != (app_reset & ~1):
        fail("application reset vector does not reference Reset_Handler")

    if (app_vectors[15] & ~1) != (app_systick & ~1):
        fail("application SysTick vector does not reference its handler")
    boot_systick = require_symbol(
        boot_symbols, "SysTick_Handler", FLASH_BASE, APPLICATION_ADDRESS
    )
    if (boot_vectors[15] & ~1) != (boot_systick & ~1):
        fail("bootloader SysTick vector does not reference its handler")

    verify_handoff_instructions(boot_elf)
    verify_merged_image()

    print("Secure Delta OTA Phase 2 bootloader jump check: PASS")
    print(f"Application initial MSP: 0x{initial_msp:08X}")
    print(f"Application reset handler: 0x{reset_handler:08X}")
    print("Handoff assembly: MSP/CONTROL/PRIMASK reset and BX verified")
    print("Combined flash image: dist/secure-delta-ota-phase2.bin")


if __name__ == "__main__":
    main()
