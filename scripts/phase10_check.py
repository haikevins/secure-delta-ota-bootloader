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
    print(f"Phase 10 check: FAIL: {message}")
    raise SystemExit(1)


def run(cmd, cwd=ROOT, echo=True, timeout=180):
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
    if (
        shutil.which("clang")
        and shutil.which("ld.lld")
        and shutil.which("llvm-objcopy")
    ):
        return "clang"
    fail("no supported STM32 ARM toolchain")
    return ""


def validate_application(path: Path) -> tuple[int, int, int]:
    data = path.read_bytes()
    if not 8 <= len(data) <= APP_MAX:
        fail(f"{path} size={len(data)} outside application range")

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
        "gateway-esp32/components/https_download/CMakeLists.txt",
        "gateway-esp32/components/https_download/include/https_download.h",
        "gateway-esp32/components/https_download/include/https_download_policy.h",
        "gateway-esp32/components/https_download/https_download.c",
        "gateway-esp32/components/https_download/https_download_policy.c",
        "gateway-esp32/components/wifi_station/CMakeLists.txt",
        "gateway-esp32/components/wifi_station/include/wifi_station.h",
        "gateway-esp32/components/wifi_station/wifi_station.c",
        "gateway-esp32/components/time_sync/CMakeLists.txt",
        "gateway-esp32/components/time_sync/include/time_sync.h",
        "gateway-esp32/components/time_sync/time_sync.c",
        "gateway-esp32/main/include/phase10_runtime_config.h",
        "gateway-esp32/main/phase10_test_ca.pem",
        "tests/unit/test_phase10_https_policy.c",
        "tests/unit/test_phase10_https_cache_model.py",
        "tools/phase10_https_server.py",
        "scripts/phase10_hw_test.py",
        "scripts/esp32_build_guard.py",
        "docs/phase-10-https-download.md",
        "docs/phase-10-checklist.md",
        "PHASE10_REPORT.md",
    ]

    missing = [p for p in required if not (ROOT / p).is_file()]
    if missing:
        fail("missing: " + ", ".join(missing))

    root_makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
    phase9_checker = (
        ROOT / "scripts/phase9_check.py"
    ).read_text(encoding="utf-8")
    phase10_checker = (
        ROOT / "scripts/phase10_check.py"
    ).read_text(encoding="utf-8")
    phase10_hw = (
        ROOT / "scripts/phase10_hw_test.py"
    ).read_text(encoding="utf-8")
    sdk_defaults = (
        GATEWAY / "sdkconfig.defaults"
    ).read_text(encoding="utf-8")

    if 'CONFIG_IDF_TARGET="esp32"' not in sdk_defaults:
        fail("sdkconfig.defaults must pin CONFIG_IDF_TARGET=esp32")

    for source_name, source_text in [
        ("Makefile", root_makefile),
        ("phase9_check.py", phase9_checker),
        ("phase10_hw_test.py", phase10_hw),
    ]:
        if 'set-target", "esp32"' in source_text or "idf.py set-target esp32" in source_text:
            fail(f"{source_name} must not auto-run destructive idf.py set-target")

    if "phase10-check: phase9-check" in root_makefile:
        fail("phase10-check must not chain through Phase9 real ESP-IDF build")

    downloader = (
        GATEWAY / "components/https_download/https_download.c"
    ).read_text(encoding="utf-8")
    cache = (
        GATEWAY / "components/artifact_cache/artifact_cache.c"
    ).read_text(encoding="utf-8")
    manager = (
        GATEWAY / "main/gateway_manager.c"
    ).read_text(encoding="utf-8")
    app_main = (
        GATEWAY / "main/app_main.c"
    ).read_text(encoding="utf-8")
    main_kconfig = (
        GATEWAY / "main/Kconfig.projbuild"
    ).read_text(encoding="utf-8")
    wifi = (
        GATEWAY / "components/wifi_station/wifi_station.c"
    ).read_text(encoding="utf-8")
    time_sync = (
        GATEWAY / "components/time_sync/time_sync.c"
    ).read_text(encoding="utf-8")
    hw_runner = (
        ROOT / "scripts/phase10_hw_test.py"
    ).read_text(encoding="utf-8")

    for token in [
        "esp_http_client_init",
        "esp_http_client_open",
        "esp_http_client_fetch_headers",
        "esp_http_client_read",
        "esp_http_client_get_status_code",
        "esp_http_client_is_complete_data_received",
        "esp_crt_bundle_attach",
        "ArtifactCache_BeginWrite",
        "ArtifactCache_Commit",
    ]:
        if token not in downloader:
            fail(f"HTTPS downloader missing {token}")

    for forbidden in [
        "skip_cert_common_name_check",
        "CONFIG_ESP_TLS_INSECURE",
        "CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY",
    ]:
        if forbidden in downloader:
            fail(f"insecure TLS option present: {forbidden}")

    for token in [
        "esp_partition_erase_range",
        "ArtifactCache_BeginWrite",
        "ArtifactCache_Write",
        "ArtifactCache_Commit",
        "ArtifactCache_Abort",
        "CalculateStoredImageCrc",
    ]:
        if token not in cache:
            fail(f"transactional artifact cache missing {token}")

    if cache.find("esp_partition_erase_range(partition,\n                                       0UL") < 0:
        fail("cache writer must invalidate header before data publication")

    for token in [
        "esp_wifi_init",
        "esp_wifi_set_mode",
        "esp_wifi_set_config",
        "esp_wifi_start",
        "IP_EVENT_STA_GOT_IP",
    ]:
        if token not in wifi:
            fail(f"Wi-Fi station missing {token}")

    for token in [
        "esp_netif_sntp_init",
        "esp_netif_sntp_sync_wait",
        "esp_netif_sntp_deinit",
    ]:
        if token not in time_sync:
            fail(f"time sync missing {token}")

    if "config = ESP_NETIF_SNTP_DEFAULT_CONFIG(" in time_sync:
        fail(
            "ESP_NETIF_SNTP_DEFAULT_CONFIG is a brace initializer and "
            "must not be used as a post-declaration assignment"
        )

    if not re.search(
        r"esp_sntp_config_t\s+config\s*=\s*"
        r"ESP_NETIF_SNTP_DEFAULT_CONFIG\(",
        time_sync,
        flags=re.MULTILINE,
    ):
        fail(
            "SNTP config must be initialized at declaration for ESP-IDF 6.x"
        )

    for token in [
        "P10_WIFI=PASS",
        "P10_TIME=PASS",
        "P10_HTTPS=PASS",
        "P10_HELLO=PASS",
        "P10_FINAL=PASS",
    ]:
        if token not in manager:
            fail(f"gateway manager missing marker {token}")

    for token in [
        "xTaskCreate",
        "Phase10GatewayTask",
        "CONFIG_SDOTA_PHASE10_GATEWAY_TASK_STACK_SIZE",
        "P10_STACK=PASS",
        "uxTaskGetStackHighWaterMark",
    ]:
        if token not in app_main:
            fail(f"Phase-10 app_main missing dedicated-worker token {token}")

    if not re.search(
        r"config\s+SDOTA_PHASE10_GATEWAY_TASK_STACK_SIZE.*?"
        r"default\s+16384",
        main_kconfig,
        flags=re.MULTILINE | re.DOTALL,
    ):
        fail("Phase-10 worker stack must default to 16384 bytes")

    for token in [
        "STM32_OPENOCD",
        "STM32_OPENOCD_SCRIPTS",
        "WIFI_SSID",
        "WIFI_PASSWORD",
        "generate_test_pki",
        "phase10_https_server.py",
        "GET /phase10_candidate.bin",
    ]:
        if token not in hw_runner:
            fail(f"hardware runner missing {token}")

    # Verify component dependency propagation for public headers.
    sdkconfig_defaults = (
        GATEWAY / "sdkconfig.defaults"
    ).read_text(encoding="utf-8")

    if "CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y" not in sdkconfig_defaults:
        fail("certificate bundle must be enabled in sdkconfig.defaults")
    if (
        "CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL=y"
        not in sdkconfig_defaults
    ):
        fail("full default certificate bundle selection is missing")

    https_cmake = (
        GATEWAY / "components/https_download/CMakeLists.txt"
    ).read_text(encoding="utf-8")
    cache_cmake = (
        GATEWAY / "components/artifact_cache/CMakeLists.txt"
    ).read_text(encoding="utf-8")

    if not re.search(
        r"REQUIRES\s+artifact_cache(?:\s|\))",
        https_cmake,
        flags=re.MULTILINE,
    ):
        fail(
            "https_download public header exposes artifact_cache, "
            "so artifact_cache must be in REQUIRES"
        )

    if not re.search(
        r"REQUIRES\s+uart_ota(?:\s|\))",
        cache_cmake,
        flags=re.MULTILINE,
    ):
        fail(
            "artifact_cache public header exposes uart_ota, "
            "so uart_ota must be in REQUIRES"
        )

    cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
    if not cc:
        fail("host C compiler missing")

    host = ROOT / "build-host/phase10_https_policy"
    host.parent.mkdir(parents=True, exist_ok=True)

    run([
        cc,
        "-std=c11",
        "-O2",
        "-Wall",
        "-Wextra",
        "-Wpedantic",
        "-Werror",
        "-Igateway-esp32/components/https_download/include",
        "gateway-esp32/components/https_download/https_download_policy.c",
        "tests/unit/test_phase10_https_policy.c",
        "-o",
        str(host),
    ])
    run([str(host)])

    run(["python3", "tests/unit/test_phase10_https_cache_model.py"])

    run([
        "python3",
        "-m",
        "py_compile",
        "scripts/phase10_check.py",
        "scripts/phase10_hw_test.py",
        "scripts/esp32_build_guard.py",
        "tools/phase10_https_server.py",
    ])

    tc = toolchain()

    run(["make", f"TOOLCHAIN={tc}", "clean"], cwd=BOOT)
    run(["make", f"TOOLCHAIN={tc}", "all"], cwd=BOOT)
    run(["make", f"TOOLCHAIN={tc}", "clean"], cwd=APP)
    run(["make", f"TOOLCHAIN={tc}", "all"], cwd=APP)

    build_dir = "build-phase10-candidate"
    out_dir = "out-phase10-candidate"
    flags = "-DAPPLICATION_VERSION=0x00000002UL"

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

    candidate = APP / out_dir / "application.bin"
    size, msp, reset = validate_application(candidate)

    if (BOOT / "out/bootloader.bin").stat().st_size > 24 * 1024:
        fail("bootloader exceeds 24 KiB")
    if (APP / "out/application.bin").stat().st_size > APP_MAX:
        fail("application exceeds 38 KiB")

    run([
        "python3",
        "tools/merge_images.py",
        "--output",
        "dist/secure-delta-ota-phase10.bin",
        "--label",
        "Phase 10",
    ])

    idf = shutil.which("idf.py")
    if idf and os.environ.get("IDF_PATH"):
        run(["python3", "scripts/esp32_build_guard.py"])
        run([idf, "build"], cwd=GATEWAY, timeout=360)
        print("ESP-IDF Phase-10 gateway build: PASS")
    else:
        print(
            "ESP-IDF Phase-10 gateway build: SKIPPED "
            "(activate ESP-IDF environment to run it)"
        )

    print("Secure Delta OTA Phase 10 HTTPS download check: PASS")
    print(
        f"STM32 Phase-10 candidate: {size} bytes, "
        f"crc32=0x{zlib.crc32(candidate.read_bytes()) & 0xFFFFFFFF:08X}"
    )
    print(f"Candidate MSP=0x{msp:08X} reset=0x{reset:08X}")
    print(
        "Hardware: make phase10-hw-test ESP32_PORT=/dev/ttyUSB0 "
        'WIFI_SSID="..." WIFI_PASSWORD="..."'
    )


if __name__ == "__main__":
    main()
