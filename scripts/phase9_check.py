#!/usr/bin/env python3
from __future__ import annotations

import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import zlib

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "node-stm32f103/application"
BOOT = ROOT / "node-stm32f103/bootloader"
GATEWAY = ROOT / "gateway-esp32"

APP_START = 0x08006000
APP_MAX = 38 * 1024
SRAM_BASE = 0x20000000
SRAM_END = 0x20005000

def fail(message: str) -> None:
    print(f"Phase 9 check: FAIL: {message}")
    raise SystemExit(1)

def run(cmd, cwd=ROOT, echo=True, timeout=120):
    result = subprocess.run(
        cmd,
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env=os.environ.copy(),
        timeout=timeout,
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
    fail("no supported STM32 ARM toolchain")
    return ""

def validate_application(path: Path) -> tuple[int, int, int]:
    data = path.read_bytes()
    if not 8 <= len(data) <= APP_MAX:
        fail(f"{path} size={len(data)} outside app range")

    msp, reset = struct.unpack_from("<II", data)
    if not SRAM_BASE <= msp <= SRAM_END or (msp & 7):
        fail(f"{path.name} invalid MSP 0x{msp:08X}")
    if (reset & 1) == 0:
        fail(f"{path.name} reset handler not Thumb")
    if not APP_START <= (reset & ~1) < APP_START + len(data):
        fail(f"{path.name} reset handler outside binary")

    return len(data), msp, reset

def main() -> None:
    required = [
        "gateway-esp32/CMakeLists.txt",
        "gateway-esp32/partitions.csv",
        "gateway-esp32/main/CMakeLists.txt",
        "gateway-esp32/main/Kconfig.projbuild",
        "gateway-esp32/main/app_main.c",
        "gateway-esp32/main/gateway_manager.c",
        "gateway-esp32/components/uart_ota/CMakeLists.txt",
        "gateway-esp32/components/uart_ota/include/uart_ota.h",
        "gateway-esp32/components/uart_ota/include/uart_ota_protocol.h",
        "gateway-esp32/components/uart_ota/include/uart_ota_plan.h",
        "gateway-esp32/components/uart_ota/uart_ota_protocol.c",
        "gateway-esp32/components/uart_ota/uart_ota_plan.c",
        "gateway-esp32/components/uart_ota/uart_ota_client.c",
        "gateway-esp32/components/artifact_cache/CMakeLists.txt",
        "gateway-esp32/components/artifact_cache/include/artifact_cache.h",
        "gateway-esp32/components/artifact_cache/artifact_cache.c",
        "tests/unit/test_phase9_gateway_protocol.c",
        "tests/unit/test_phase9_gateway_model.py",
        "scripts/phase9_prepare_gateway.py",
        "scripts/phase9_hw_test.py",
        "docs/phase-9-esp32-uart-gateway.md",
        "docs/phase-9-checklist.md",
        "PHASE9_REPORT.md",
    ]
    missing = [p for p in required if not (ROOT / p).is_file()]
    if missing:
        fail("missing: " + ", ".join(missing))

    # Relocatable release ZIPs must not contain compiler dependency/object
    # files carrying absolute paths from the packaging machine.
    stale_build_files = []
    for build_root in [
        ROOT / "node-stm32f103/bootloader",
        ROOT / "node-stm32f103/application",
    ]:
        for path in build_root.glob("build*"):
            if path.is_dir():
                stale_build_files.extend(path.rglob("*.d"))
                stale_build_files.extend(path.rglob("*.o"))

    # This checker can itself run in an active local build tree, so only reject
    # dependency files whose text contains a foreign absolute /mnt/data path.
    for dep in stale_build_files:
        if dep.suffix == ".d":
            text = dep.read_text(encoding="utf-8", errors="ignore")
            if "/mnt/data/" in text and str(ROOT) not in text:
                fail(f"foreign absolute path in dependency file: {dep}")

    protocol = (
        GATEWAY / "components/uart_ota/uart_ota_protocol.c"
    ).read_text(encoding="utf-8")
    client = (
        GATEWAY / "components/uart_ota/uart_ota_client.c"
    ).read_text(encoding="utf-8")
    cache = (
        GATEWAY / "components/artifact_cache/artifact_cache.c"
    ).read_text(encoding="utf-8")

    hw_runner = (
        ROOT / "scripts/phase9_hw_test.py"
    ).read_text(encoding="utf-8")

    for token in [
        "STM32_OPENOCD",
        "STM32_OPENOCD_SCRIPTS",
        'env.pop("OPENOCD_SCRIPTS", None)',
        '"-s", scripts',
        "resolve_stm32_openocd",
    ]:
        if token not in hw_runner:
            fail(f"Phase-9 hardware runner missing OpenOCD isolation token: {token}")

    if 'os.environ.get("OPENOCD", "openocd")' in hw_runner:
        fail(
            "Phase-9 hardware runner must not use generic openocd from ESP-IDF PATH"
        )

    uart_cmake = (
        GATEWAY / "components/uart_ota/CMakeLists.txt"
    ).read_text(encoding="utf-8")
    cache_cmake = (
        GATEWAY / "components/artifact_cache/CMakeLists.txt"
    ).read_text(encoding="utf-8")

    # Public headers must propagate the components whose headers they expose.
    # uart_ota.h includes driver/uart.h, and artifact_cache.h includes uart_ota.h.
    if not re.search(
        r"REQUIRES\s+esp_driver_uart(?:\s|\))",
        uart_cmake,
        flags=re.MULTILINE,
    ):
        fail(
            "uart_ota must declare esp_driver_uart in REQUIRES "
            "because public uart_ota.h includes driver/uart.h"
        )

    if not re.search(
        r"REQUIRES\s+uart_ota(?:\s|\))",
        cache_cmake,
        flags=re.MULTILINE,
    ):
        fail(
            "artifact_cache must declare uart_ota in REQUIRES "
            "because public artifact_cache.h includes uart_ota.h"
        )

    for token in [
        "UART_OTA_MAX_PAYLOAD",
        "UartOta_CobsEncode",
        "UartOta_Crc32",
        "UART_OTA_CMD_RESUME",
        "UART_OTA_CMD_INSTALL",
    ]:
        if token not in protocol and token not in client:
            fail(f"UART gateway missing token {token}")

    for token in [
        "uart_driver_install",
        "uart_param_config",
        "uart_set_pin",
        "uart_read_bytes",
        "uart_write_bytes",
        "esp_timer_get_time",
        "INSTALL ACK uncertain",
        "UartOta_SelectPlan",
    ]:
        if token not in client:
            fail(f"ESP-IDF UART client missing {token}")

    for token in [
        "esp_partition_find_first",
        "esp_partition_read",
        "esp_partition_write",
        "esp_partition_erase_range",
        "ARTIFACT_CACHE_DATA_OFFSET",
    ]:
        if token not in cache:
            fail(f"artifact cache missing {token}")

    cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
    if not cc:
        fail("host C compiler missing")

    host = ROOT / "build-host/phase9_gateway_protocol"
    host.parent.mkdir(parents=True, exist_ok=True)

    run([
        cc,
        "-std=c11",
        "-O2",
        "-Wall",
        "-Wextra",
        "-Wpedantic",
        "-Werror",
        "-Igateway-esp32/components/uart_ota/include",
        "gateway-esp32/components/uart_ota/uart_ota_protocol.c",
        "gateway-esp32/components/uart_ota/uart_ota_plan.c",
        "tests/unit/test_phase9_gateway_protocol.c",
        "-o",
        str(host),
    ])
    run([str(host)])
    run(["python3", "tests/unit/test_phase9_gateway_model.py"])
    run([
        "python3", "-m", "py_compile",
        "scripts/phase9_check.py",
        "scripts/phase9_prepare_gateway.py",
        "scripts/phase9_hw_test.py",
    ])

    tc = toolchain()

    # ZIPs from previous phases may contain .d files with absolute build paths.
    # Always clean normal STM32 outputs before validating this checkout.
    run(["make", f"TOOLCHAIN={tc}", "clean"], cwd=BOOT)
    run(["make", f"TOOLCHAIN={tc}", "all"], cwd=BOOT)
    run(["make", f"TOOLCHAIN={tc}", "clean"], cwd=APP)
    run(["make", f"TOOLCHAIN={tc}", "all"], cwd=APP)

    # Build the healthy v2 artifact that the ESP32 embeds and caches.
    build_dir = "build-phase9-candidate"
    out_dir = "out-phase9-candidate"
    flags = "-DAPPLICATION_VERSION=0x00000002UL"
    run([
        "make", f"TOOLCHAIN={tc}",
        f"BUILD_DIR={build_dir}",
        f"OUT_DIR={out_dir}",
        f"PROJECT_CFLAGS={flags}",
        "clean",
    ], cwd=APP)
    run([
        "make", f"TOOLCHAIN={tc}",
        f"BUILD_DIR={build_dir}",
        f"OUT_DIR={out_dir}",
        f"PROJECT_CFLAGS={flags}",
        "all",
    ], cwd=APP)

    candidate = APP / out_dir / "application.bin"
    size, msp, reset = validate_application(candidate)
    run(["python3", "scripts/phase9_prepare_gateway.py"])

    embedded = GATEWAY / "main/phase9_candidate.bin"
    if embedded.read_bytes() != candidate.read_bytes():
        fail("ESP32 embedded candidate differs from STM32 candidate")

    if (BOOT / "out/bootloader.bin").stat().st_size > 24 * 1024:
        fail("bootloader exceeds 24 KiB")
    if (APP / "out/application.bin").stat().st_size > APP_MAX:
        fail("application exceeds 38 KiB")

    run([
        "python3", "tools/merge_images.py",
        "--output", "dist/secure-delta-ota-phase9.bin",
        "--label", "Phase 9",
    ])

    # If an ESP-IDF environment is active, validate the real gateway build too.
    idf = shutil.which("idf.py")
    if idf and os.environ.get("IDF_PATH"):
        run(["python3", "scripts/esp32_build_guard.py"])
        run([idf, "build"], cwd=GATEWAY, timeout=300)
        print("ESP-IDF gateway build: PASS")
    else:
        print(
            "ESP-IDF gateway build: SKIPPED "
            "(activate ESP-IDF environment to run it)"
        )

    print("Secure Delta OTA Phase 9 ESP32 UART Gateway check: PASS")
    print(
        f"STM32 Phase-9 candidate: {size} bytes, "
        f"crc32=0x{zlib.crc32(candidate.read_bytes()) & 0xFFFFFFFF:08X}"
    )
    print(f"Candidate MSP=0x{msp:08X} reset=0x{reset:08X}")
    print("ESP32 UART2 default: GPIO17 TX -> PA10, GPIO16 RX <- PA9")
    print("Hardware: make phase9-hw-test ESP32_PORT=/dev/ttyUSB0")

if __name__ == "__main__":
    main()
