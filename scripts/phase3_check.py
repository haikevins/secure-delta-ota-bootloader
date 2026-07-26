#!/usr/bin/env python3
"""Build and structurally verify Phase 3 metadata and boot decisions."""
from __future__ import annotations

import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
APP_START = 0x08006000
META_A = 0x0800F800
META_B = 0x0800FC00

REQUIRED = [
    "shared/include/crc32.h",
    "shared/src/crc32.c",
    "shared/include/boot_metadata.h",
    "shared/src/boot_metadata.c",
    "node-stm32f103/bootloader/include/metadata_storage.h",
    "node-stm32f103/bootloader/storage/metadata_storage.c",
    "node-stm32f103/bootloader/include/boot_decision.h",
    "node-stm32f103/bootloader/src/boot_decision.c",
    "tests/unit/test_phase3_metadata.c",
    "docs/metadata-and-boot-decision.md",
    "docs/phase-3-checklist.md",
    "tools/inspect_metadata.py",
    "scripts/dump_metadata.sh",
    "scripts/erase_metadata.sh",
]


def fail(message: str) -> None:
    print(f"Phase 3 check: FAIL: {message}")
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


def symbol_table(elf: Path) -> dict[str, int]:
    tool = (shutil.which("arm-none-eabi-readelf") or
            shutil.which("llvm-readelf") or shutil.which("readelf"))
    if not tool:
        fail("readelf is required")
    output = run([tool, "-sW", str(elf)], echo=False)
    result: dict[str, int] = {}
    pattern = re.compile(
        r"^\s*\d+:\s+([0-9a-fA-F]+)\s+\d+\s+\S+\s+\S+\s+\S+\s+\S+\s+(.+)$"
    )
    for line in output.splitlines():
        match = pattern.match(line)
        if match:
            result[match.group(2).strip()] = int(match.group(1), 16)
    return result


def require_symbol(symbols: dict[str, int], name: str) -> None:
    value = symbols.get(name)
    if value is None:
        fail(f"missing bootloader symbol: {name}")
    if not (0x08000000 <= (value & ~1) < APP_START):
        fail(f"symbol {name}=0x{value:08X} outside bootloader")


def compile_host_tests() -> None:
    compiler = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
    if not compiler:
        fail("native C compiler is required for metadata unit tests")
    out = ROOT / "build-host/phase3_metadata_tests"
    out.parent.mkdir(parents=True, exist_ok=True)
    run([
        compiler,
        "-std=c11", "-O2", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
        "-Ishared/include",
        "-Inode-stm32f103/bootloader/include",
        "shared/src/crc32.c",
        "shared/src/boot_metadata.c",
        "node-stm32f103/bootloader/src/boot_decision.c",
        "tests/unit/test_phase3_metadata.c",
        "-o", str(out),
    ])
    run([str(out)])


def check_memory_map() -> None:
    text = (ROOT / "shared/include/memory_map.h").read_text(encoding="utf-8")
    required = {
        "BOOT_METADATA_A_ADDRESS": "0x0800F800UL",
        "BOOT_METADATA_B_ADDRESS": "0x0800FC00UL",
        "APPLICATION_MAX_SIZE": "(38UL * 1024UL)",
    }
    for name, value in required.items():
        if f"#define {name}" not in text or value not in text:
            fail(f"memory_map.h does not freeze {name}={value}")

    linker = (ROOT / "node-stm32f103/application/linker/application.ld").read_text(
        encoding="utf-8"
    )
    if "LENGTH = 38K" not in linker or "0x0800F800" not in linker:
        fail("application linker does not stop before metadata A")


def check_storage_protocol() -> None:
    text = (ROOT / "node-stm32f103/bootloader/storage/metadata_storage.c").read_text(
        encoding="utf-8"
    )
    for token in (
        "BootMetadata_SelectWriteSlot",
        "FLASH_ErasePage",
        "FLASH_ProgramHalfWord",
        "BootMetadata_Validate(&verification)",
    ):
        if token not in text:
            fail(f"metadata storage missing safety operation: {token}")


def main() -> None:
    missing = [path for path in REQUIRED if not (ROOT / path).is_file()]
    if missing:
        fail("missing required file(s): " + ", ".join(missing))

    run(["python3", "scripts/phase2_check.py"])
    compile_host_tests()
    check_memory_map()
    check_storage_protocol()

    boot_elf = ROOT / "node-stm32f103/bootloader/out/bootloader.elf"
    symbols = symbol_table(boot_elf)
    for name in (
        "Crc32_Calculate",
        "BootMetadata_Validate",
        "BootMetadata_SelectNewestSlot",
        "MetadataStorage_Load",
        "MetadataStorage_Commit",
        "BootDecision_Evaluate",
    ):
        require_symbol(symbols, name)

    run(["python3", "tools/merge_images.py"])

    print("Secure Delta OTA Phase 3 metadata/boot-decision check: PASS")
    print("Internal metadata A: 0x0800F800 (1 KiB page)")
    print("Internal metadata B: 0x0800FC00 (1 KiB page)")
    print("Application budget: 38 KiB")
    print("Host CRC/validation/redundancy/decision tests: PASS")


if __name__ == "__main__":
    main()
