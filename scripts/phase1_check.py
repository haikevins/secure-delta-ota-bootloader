#!/usr/bin/env python3
"""Build and structurally validate the Phase 1 STM32 firmware foundation."""
from __future__ import annotations

import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

REQUIRED = [
    "mk/stm32f103.mk",
    "node-stm32f103/config/stm32f10x_conf.h",
    "node-stm32f103/bootloader/Makefile",
    "node-stm32f103/bootloader/linker/bootloader.ld",
    "node-stm32f103/bootloader/startup/startup_stm32f10x_md.s",
    "node-stm32f103/bootloader/platform/system_clock.c",
    "node-stm32f103/application/Makefile",
    "node-stm32f103/application/linker/application.ld",
    "node-stm32f103/application/startup/startup_stm32f10x_md.s",
    "node-stm32f103/application/platform/system_clock.c",
    "node-stm32f103/spl/inc/stm32f10x_gpio.h",
    "node-stm32f103/spl/src/stm32f10x_gpio.c",
    "node-stm32f103/cmsis/CM3/CoreSupport/core_cm3.h",
    "node-stm32f103/cmsis/CM3/DeviceSupport/ST/STM32F10x/stm32f10x.h",
]

EXPECTED = {
    "bootloader": {
        "vector": 0x08000000,
        "budget": 24 * 1024,
    },
    "application": {
        "vector": 0x08006000,
        "budget": 39 * 1024,
    },
}


def fail(message: str) -> None:
    print(f"Phase 1 check: FAIL: {message}")
    raise SystemExit(1)


def run(command: list[str], cwd: Path = ROOT) -> str:
    result = subprocess.run(
        command,
        cwd=cwd,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env=os.environ.copy(),
    )
    print(result.stdout, end="")
    if result.returncode != 0:
        fail(f"command returned {result.returncode}: {' '.join(command)}")
    return result.stdout


def detect_toolchain() -> str:
    requested = os.environ.get("TOOLCHAIN")
    if requested:
        if requested not in {"gcc", "clang"}:
            fail("TOOLCHAIN must be 'gcc' or 'clang'")
        return requested
    if shutil.which("arm-none-eabi-gcc"):
        return "gcc"
    if shutil.which("clang") and shutil.which("ld.lld") and shutil.which("llvm-objcopy"):
        return "clang"
    fail("install GNU Arm Embedded GCC or Clang + LLD + llvm-objcopy")
    return ""  # unreachable


def section_address(elf: Path, section: str) -> int:
    readelf = shutil.which("arm-none-eabi-readelf") or shutil.which("readelf")
    if readelf is None:
        fail("readelf is required for ELF validation")
    result = subprocess.run(
        [readelf, "-W", "-S", str(elf)],
        cwd=ROOT,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if result.returncode != 0:
        print(result.stdout, end="")
        fail(f"readelf failed for {elf}")
    output = result.stdout
    pattern = re.compile(rf"\]\s+{re.escape(section)}\s+\S+\s+([0-9a-fA-F]+)\s+")
    for line in output.splitlines():
        match = pattern.search(line)
        if match:
            return int(match.group(1), 16)
    fail(f"section {section} not found in {elf}")
    return 0


def binary_size(path: Path) -> int:
    return path.stat().st_size


def verify_unique_basenames(image: str) -> None:
    project = ROOT / "node-stm32f103" / image
    selected = [project / "src/main.c", project / "platform/system_clock.c"]
    selected.extend(
        [
            ROOT / "node-stm32f103/spl/src/misc.c",
            ROOT / "node-stm32f103/spl/src/stm32f10x_gpio.c",
            ROOT / "node-stm32f103/spl/src/stm32f10x_rcc.c",
        ]
    )
    names = [path.name for path in selected]
    duplicates = sorted({name for name in names if names.count(name) > 1})
    if duplicates:
        fail(f"flat object naming collision in {image}: {', '.join(duplicates)}")


def main() -> None:
    missing = [item for item in REQUIRED if not (ROOT / item).is_file()]
    if missing:
        fail("missing required file(s): " + ", ".join(missing))

    toolchain = detect_toolchain()
    print(f"Phase 1 toolchain: {toolchain}")

    for image in EXPECTED:
        verify_unique_basenames(image)
        image_dir = ROOT / "node-stm32f103" / image
        run(["make", f"TOOLCHAIN={toolchain}", "clean"], cwd=image_dir)
        run(["make", f"TOOLCHAIN={toolchain}", "all"], cwd=image_dir)

        out = image_dir / "out"
        elf = out / f"{image}.elf"
        bin_file = out / f"{image}.bin"
        hex_file = out / f"{image}.hex"
        map_file = out / f"{image}.map"
        size_file = out / f"{image}.size.txt"
        for artifact in [elf, bin_file, hex_file, map_file, size_file]:
            if not artifact.is_file() or artifact.stat().st_size == 0:
                fail(f"missing or empty artifact: {artifact.relative_to(ROOT)}")

        actual_vector = section_address(elf, ".isr_vector")
        expected_vector = EXPECTED[image]["vector"]
        if actual_vector != expected_vector:
            fail(
                f"{image} vector address is 0x{actual_vector:08X}; "
                f"expected 0x{expected_vector:08X}"
            )

        size = binary_size(bin_file)
        budget = EXPECTED[image]["budget"]
        if size > budget:
            fail(f"{image}.bin uses {size} bytes, over {budget}-byte budget")

    print("Secure Delta OTA Phase 1 repository/build check: PASS")
    print("Generated artifacts per image: ELF, BIN, HEX, MAP, size report")
    print("Bootloader vector table: 0x08000000")
    print("Application vector table: 0x08006000")


if __name__ == "__main__":
    main()
