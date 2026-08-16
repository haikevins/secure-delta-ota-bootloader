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
    print(f"Phase 11 check: FAIL: {message}")
    raise SystemExit(1)


def run(cmd, cwd=ROOT, timeout=180):
    result = subprocess.run(
        cmd,
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env=os.environ.copy(),
        timeout=timeout,
    )
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
        "gateway-esp32/main/idf_component.yml",
        "gateway-esp32/main/include/phase11_runtime_config.h",
        "gateway-esp32/main/phase11_test_ca.pem",
        "gateway-esp32/components/mqtt_orchestrator/CMakeLists.txt",
        "gateway-esp32/components/mqtt_orchestrator/include/mqtt_orchestrator.h",
        "gateway-esp32/components/mqtt_orchestrator/include/mqtt_orchestration_contract.h",
        "gateway-esp32/components/mqtt_orchestrator/mqtt_orchestrator.c",
        "gateway-esp32/components/mqtt_orchestrator/mqtt_orchestration_contract.c",
        "tests/unit/test_phase11_mqtt_contract.c",
        "tests/unit/test_phase11_mqtt_model.py",
        "tools/phase11_mqtt_broker.py",
        "scripts/phase11_hw_test.py",
        "scripts/esp32_build_guard.py",
        "docs/phase-11-mqtt-orchestration.md",
        "docs/phase-11-checklist.md",
        "PHASE11_REPORT.md",
    ]

    missing = [p for p in required if not (ROOT / p).is_file()]
    if missing:
        fail("missing: " + ", ".join(missing))

    manifest = (
        GATEWAY / "main/idf_component.yml"
    ).read_text(encoding="utf-8")
    orchestrator = (
        GATEWAY / "components/mqtt_orchestrator/mqtt_orchestrator.c"
    ).read_text(encoding="utf-8")
    contract = (
        GATEWAY
        / "components/mqtt_orchestrator/mqtt_orchestration_contract.c"
    ).read_text(encoding="utf-8")
    manager = (
        GATEWAY / "main/gateway_manager.c"
    ).read_text(encoding="utf-8")
    gateway_config = (
        GATEWAY / "main/gateway_config.c"
    ).read_text(encoding="utf-8")
    app_main = (
        GATEWAY / "main/app_main.c"
    ).read_text(encoding="utf-8")
    kconfig = (
        GATEWAY / "main/Kconfig.projbuild"
    ).read_text(encoding="utf-8")
    hw_runner = (
        ROOT / "scripts/phase11_hw_test.py"
    ).read_text(encoding="utf-8")
    broker = (
        ROOT / "tools/phase11_mqtt_broker.py"
    ).read_text(encoding="utf-8")
    runtime = (
        GATEWAY / "main/include/phase11_runtime_config.h"
    ).read_text(encoding="utf-8")
    sdk_defaults = (
        GATEWAY / "sdkconfig.defaults"
    ).read_text(encoding="utf-8")
    root_makefile = (
        ROOT / "Makefile"
    ).read_text(encoding="utf-8")

    if 'espressif/mqtt: "1.0.0"' not in manifest:
        fail("ESP-IDF 6.x managed espressif/mqtt dependency missing")
    if "espressif/cjson:" not in manifest:
        fail("ESP-IDF 6.x managed espressif/cjson dependency missing")


    # Kconfig bools set to 'n' are not emitted as CONFIG_* C macros.
    # They must be consumed with preprocessor guards, not direct C expressions.
    for forbidden in [
        ".single_shot = CONFIG_SDOTA_PHASE11_SINGLE_SHOT",
        ".autorun = CONFIG_SDOTA_PHASE11_AUTORUN",
    ]:
        if forbidden in gateway_config:
            fail(
                "Kconfig bool used as direct C expression: "
                + forbidden
            )

    for required_bool_guard in [
        "#if defined(CONFIG_SDOTA_PHASE11_AUTORUN)",
        "#if defined(CONFIG_SDOTA_PHASE11_SINGLE_SHOT)",
    ]:
        if required_bool_guard not in gateway_config:
            fail(
                "gateway_config missing Kconfig bool guard: "
                + required_bool_guard
            )

    if 'CONFIG_IDF_TARGET="esp32"' not in sdk_defaults:
        fail("sdkconfig.defaults must pin CONFIG_IDF_TARGET=esp32")
    if "CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y" not in sdk_defaults:
        fail("production certificate bundle must remain enabled")

    if "idf.py set-target esp32" in root_makefile:
        fail("Phase 11 must not auto-run destructive idf.py set-target")

    if "phase11-check: phase10-check" in root_makefile:
        fail("Phase 11 check must not trigger a second legacy gateway build")

    if "SDOTA_PHASE11_HW_WIFI_PASSWORD" not in runtime:
        fail("Phase-11 runtime hardware override missing")
    if '#define SDOTA_PHASE11_HW_OVERRIDE          0' not in runtime:
        fail("packaged hardware override must be disabled")
    if '#define SDOTA_PHASE11_HW_WIFI_PASSWORD     ""' not in runtime:
        fail("packaged Phase-11 runtime must not contain Wi-Fi credentials")

    for token in [
        "esp_mqtt_client_init",
        "esp_mqtt_client_register_event",
        "esp_mqtt_client_start",
        "esp_mqtt_client_subscribe_single",
        "esp_mqtt_client_enqueue",
        "MQTT_EVENT_CONNECTED",
        "MQTT_EVENT_SUBSCRIBED",
        "MQTT_EVENT_DATA",
        "current_data_offset",
        "total_data_len",
        "xQueueCreate",
        "xQueueSend",
        "cJSON_ParseWithLength",
        "esp_crt_bundle_attach",
    ]:
        if token not in orchestrator:
            fail(f"MQTT orchestrator missing {token}")

    for forbidden in [
        "mqtt://",
        "skip_cert_common_name_check",
        "CONFIG_ESP_TLS_INSECURE",
        "CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY",
    ]:
        if forbidden in orchestrator:
            fail(f"insecure MQTT/TLS option present: {forbidden}")

    for token in [
        "https://",
        "MQTT_ORCH_SCHEMA_VERSION",
        "MqttOrchestration_ValidateCommand",
        "MqttOrchestration_BuildTopics",
    ]:
        if token not in contract:
            fail(f"MQTT contract missing {token}")

    for token in [
        "P11_WIFI=PASS",
        "P11_TIME=PASS",
        "P11_MQTT=PASS",
        "P11_COMMAND=PASS",
        "P11_HTTPS=PASS",
        "P11_HELLO=PASS",
        "P11_FINAL=PASS",
        "MqttOrchestrator_WaitCommand",
        "HttpsDownload_ToCache",
        "UartOta_TransferInstallAndWait",
        "mqtt_size_mismatch",
        "mqtt_crc_mismatch",
    ]:
        if token not in manager:
            fail(f"Phase-11 gateway manager missing {token}")

    if "MQTT_EVENT_PUBLISHED" not in orchestrator:
        fail("MQTT QoS-1 PUBACK event handling missing")

    if "MqttOrchestrator_PublishStatusAndWait" not in orchestrator:
        fail("broker-ACKed final status API missing")

    if "esp_mqtt_client_publish(" not in orchestrator:
        fail("direct MQTT publish path missing")

    progress_start = orchestrator.find(
        "esp_err_t MqttOrchestrator_PublishProgress"
    )
    status_wait_start = orchestrator.find(
        "esp_err_t MqttOrchestrator_PublishStatusAndWait"
    )
    if progress_start < 0 or status_wait_start < 0:
        fail("cannot locate Phase-11 MQTT progress/final publish functions")

    progress_body = orchestrator[progress_start:status_wait_start]
    if "esp_mqtt_client_enqueue" in progress_body:
        fail(
            "QoS-0 progress must not use enqueue(store=false); "
            "publish it immediately"
        )
    if "esp_mqtt_client_publish" not in progress_body:
        fail("QoS-0 progress must use direct esp_mqtt_client_publish")

    if '"confirmed"' not in manager or \
       "MqttOrchestrator_PublishStatusAndWait" not in manager:
        fail("final confirmed status must wait for broker PUBACK")

    # Long HTTPS/UART operations must never execute in the MQTT event callback.
    event_handler_start = orchestrator.find("static void MqttEventHandler")
    start_fn = orchestrator.find("esp_err_t MqttOrchestrator_Start")
    if event_handler_start < 0 or start_fn < 0:
        fail("cannot locate MQTT event handler boundaries")
    event_handler_body = orchestrator[event_handler_start:start_fn]
    for forbidden in [
        "HttpsDownload_ToCache",
        "UartOta_TransferInstallAndWait",
        "esp_partition_",
    ]:
        if forbidden in event_handler_body:
            fail(f"MQTT callback performs forbidden long operation {forbidden}")

    for token in [
        "xTaskCreate",
        "Phase11GatewayTask",
        "CONFIG_SDOTA_PHASE11_GATEWAY_TASK_STACK_SIZE",
        "P11_STACK=PASS",
        "uxTaskGetStackHighWaterMark",
    ]:
        if token not in app_main:
            fail(f"Phase-11 app_main missing {token}")

    if not re.search(
        r"config\s+SDOTA_PHASE11_GATEWAY_TASK_STACK_SIZE.*?"
        r"default\s+16384",
        kconfig,
        flags=re.MULTILINE | re.DOTALL,
    ):
        fail("Phase-11 worker stack must default to 16384 bytes")

    for token in [
        "PHASE11_MQTT_BROKER_READY",
        "P11_BROKER_TLS=PASS",
        "P11_BROKER_COMMAND_SENT=PASS",
        "P11_BROKER_COMMAND_ACK=PASS",
        "P11_BROKER_CONFIRMED=PASS",
        "P11_BROKER_RESULT=PASS",
    ]:
        if token not in broker:
            fail(f"Phase-11 test broker missing {token}")

    for token in [
        "STM32_OPENOCD",
        "STM32_OPENOCD_SCRIPTS",
        "WIFI_SSID",
        "WIFI_PASSWORD",
        "PHASE11_HOST_IP",
        "MQTT_PORT",
        "HTTPS_PORT",
        "phase11_mqtt_broker.py",
        "phase11_candidate.bin",
        "validate_broker_output",
    ]:
        if token not in hw_runner:
            fail(f"Phase-11 hardware runner missing {token}")

    cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
    if not cc:
        fail("host C compiler missing")

    host = ROOT / "build-host/phase11_mqtt_contract"
    host.parent.mkdir(parents=True, exist_ok=True)

    run([
        cc,
        "-std=c11",
        "-O2",
        "-Wall",
        "-Wextra",
        "-Wpedantic",
        "-Werror",
        "-Igateway-esp32/components/mqtt_orchestrator/include",
        "gateway-esp32/components/mqtt_orchestrator/mqtt_orchestration_contract.c",
        "tests/unit/test_phase11_mqtt_contract.c",
        "-o",
        str(host),
    ])
    run([str(host)])

    run(["python3", "tests/unit/test_phase11_mqtt_model.py"])

    # Preserve Phase-10 HTTPS/cache portable regressions.
    https_host = ROOT / "build-host/phase10_https_policy"
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
        str(https_host),
    ])
    run([str(https_host)])
    run(["python3", "tests/unit/test_phase10_https_cache_model.py"])

    # Preserve Phase-9 UART gateway portable regressions.
    uart_host = ROOT / "build-host/phase9_gateway_protocol"
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
        str(uart_host),
    ])
    run([str(uart_host)])
    run(["python3", "tests/unit/test_phase9_gateway_model.py"])

    run([
        "python3",
        "-m",
        "py_compile",
        "scripts/phase11_check.py",
        "scripts/phase11_hw_test.py",
        "scripts/esp32_build_guard.py",
        "tools/phase11_mqtt_broker.py",
        "tools/phase10_https_server.py",
        "tests/unit/test_phase11_mqtt_model.py",
    ])

    tc = toolchain()

    run(["make", f"TOOLCHAIN={tc}", "clean"], cwd=BOOT)
    run(["make", f"TOOLCHAIN={tc}", "all"], cwd=BOOT)
    run(["make", f"TOOLCHAIN={tc}", "clean"], cwd=APP)
    run(["make", f"TOOLCHAIN={tc}", "all"], cwd=APP)

    build_dir = "build-phase11-candidate"
    out_dir = "out-phase11-candidate"
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
        "dist/secure-delta-ota-phase11.bin",
        "--label",
        "Phase 11",
    ])

    idf = shutil.which("idf.py")
    if idf and os.environ.get("IDF_PATH"):
        run(["python3", "scripts/esp32_build_guard.py"])
        run([idf, "build"], cwd=GATEWAY, timeout=600)
        print("ESP-IDF Phase-11 gateway build: PASS")
    else:
        print(
            "ESP-IDF Phase-11 gateway build: SKIPPED "
            "(activate ESP-IDF environment to run it)"
        )

    print("Secure Delta OTA Phase 11 MQTT orchestration check: PASS")
    print(
        f"STM32 Phase-11 candidate: {size} bytes, "
        f"crc32=0x{zlib.crc32(candidate.read_bytes()) & 0xFFFFFFFF:08X}"
    )
    print(f"Candidate MSP=0x{msp:08X} reset=0x{reset:08X}")
    print(
        "Hardware: make phase11-hw-test ESP32_PORT=/dev/ttyUSB0 "
        'WIFI_SSID="..." WIFI_PASSWORD="..." '
        "PHASE11_HOST_IP=192.168.1.x"
    )


if __name__ == "__main__":
    main()
