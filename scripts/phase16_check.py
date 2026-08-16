#!/usr/bin/env python3
"""Phase 16 host/static gate for deterministic fault-injection HIL."""
from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BOOT = ROOT / "node-stm32f103/bootloader"
MATRIX = ROOT / "tests/fault/phase16_fault_matrix.json"

REQUIRED_SCENARIOS = {
    "control-secure-delta",
    "patch-reset",
    "backup-reset",
    "install-midpage-reset",
    "mqtt-drop-after-accepted",
    "https-truncate",
    "tampered-signature",
    "rollback-control",
    "rollback-reset",
}

BOOT_VARIANTS = {
    "control": "",
    "patch": "-DPHASE16_FAULT_PATCH_RESET=1",
    "backup": "-DPHASE16_FAULT_BACKUP_RESET_OFFSET=4096UL",
    "install": "-DPHASE16_FAULT_INSTALL_OFFSET=1536UL",
    "rollback": "-DPHASE16_FAULT_ROLLBACK_RESET_OFFSET=1024UL",
}


def fail(message: str) -> None:
    print(f"Phase 16 check: FAIL: {message}")
    raise SystemExit(1)


def choose_toolchain() -> str:
    explicit = os.environ.get("TOOLCHAIN", "").strip()
    if explicit:
        if explicit not in {"gcc", "clang"}:
            fail(f"unsupported TOOLCHAIN={explicit!r}")
        return explicit

    if shutil.which("arm-none-eabi-gcc"):
        return "gcc"
    if shutil.which("clang") and shutil.which("llvm-objcopy"):
        return "clang"
    fail("no supported STM32 toolchain found")
    raise AssertionError


def run(command: list[str], *, cwd: Path = ROOT, timeout: int = 240) -> str:
    result = subprocess.run(
        command,
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env=os.environ.copy(),
        timeout=timeout,
        check=False,
    )
    if result.returncode != 0:
        print(result.stdout, end="")
        fail(
            f"command returned {result.returncode}: "
            + " ".join(command)
        )
    return result.stdout


def validate_matrix() -> None:
    try:
        raw = json.loads(MATRIX.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        fail(f"cannot load Phase-16 fault matrix: {exc}")

    if raw.get("schema") != 1 or raw.get("phase") != 16:
        fail("fault matrix schema/phase mismatch")

    scenarios = raw.get("scenarios")
    if not isinstance(scenarios, list):
        fail("fault matrix scenarios must be an array")

    ids: list[str] = []
    for scenario in scenarios:
        if not isinstance(scenario, dict):
            fail("fault matrix scenario must be an object")
        scenario_id = scenario.get("id")
        if not isinstance(scenario_id, str) or not scenario_id:
            fail("fault matrix scenario has invalid id")
        ids.append(scenario_id)

        if scenario.get("expected_gateway") not in {"pass", "fail"}:
            fail(f"{scenario_id}: expected_gateway must be pass/fail")
        if not isinstance(scenario.get("invariant"), str):
            fail(f"{scenario_id}: invariant missing")

    if len(ids) != len(set(ids)):
        fail("fault matrix contains duplicate scenario ids")

    missing = REQUIRED_SCENARIOS - set(ids)
    if missing:
        fail("fault matrix missing: " + ", ".join(sorted(missing)))

    print(
        f"Phase 16 fault matrix: PASS scenarios={len(ids)} "
        "security+transport+reset+rollback"
    )


def validate_source_contracts() -> None:
    secure = (
        BOOT / "src/secure_container.c"
    ).read_text(encoding="utf-8")
    installer = (
        BOOT / "src/image_installer.c"
    ).read_text(encoding="utf-8")
    uart = (
        ROOT / "gateway-esp32/components/uart_ota/uart_ota_client.c"
    ).read_text(encoding="utf-8")
    gateway_manager = (
        ROOT / "gateway-esp32/main/gateway_manager.c"
    ).read_text(encoding="utf-8")
    broker = (
        ROOT / "tools/phase16_mqtt_broker.py"
    ).read_text(encoding="utf-8")
    https_fault = (
        ROOT / "tools/phase16_fault_https_server.py"
    ).read_text(encoding="utf-8")
    hil = (
        ROOT / "scripts/phase16_hw_test.py"
    ).read_text(encoding="utf-8")
    application_main = (
        ROOT / "node-stm32f103/application/src/main.c"
    ).read_text(encoding="utf-8")

    sanitizer_tokens = [
        "PHASE16_HIL_SANITIZE_EXTERNAL",
        "EXTERNAL_FLASH_PARTITION_METADATA_A",
        "EXTERNAL_FLASH_PARTITION_METADATA_B",
        "EXTERNAL_FLASH_PARTITION_INCOMING",
        "EXT_INCOMING_SIZE",
        "ExternalFlashStorage_ErasePartition",
        "ExternalFlashStorage_IsErased",
    ]
    for token in sanitizer_tokens:
        if token not in application_main:
            fail(f"Phase-16 external sanitizer missing: {token}")

    secure_tokens = [
        "PHASE16_FAULT_PATCH_RESET",
        "PHASE16_FAULT_PATCH_MARKER",
        "Phase16InjectPatchReset",
        "NVIC_SystemReset",
    ]
    installer_tokens = [
        "PHASE16_FAULT_BACKUP_RESET_OFFSET",
        "PHASE16_FAULT_INSTALL_OFFSET",
        "PHASE16_FAULT_ROLLBACK_RESET_OFFSET",
        "PHASE16_FAULT_BACKUP_MARKER",
        "PHASE16_FAULT_INSTALL_MARKER",
        "PHASE16_FAULT_ROLLBACK_TAG",
        "Phase16CommitMarkerAndReset",
    ]
    uart_tokens = [
        "candidate rejected before trial",
        "candidate rolled back to app",
        "UART_OTA_FINAL_TIMEOUT_MS    120000UL",
    ]
    gateway_failure_tokens = [
        "MqttOrchestrator_PublishStatusAndWait(",
        '"failed",',
        "failed-status PUBACK wait failed",
        "5000UL",
    ]
    broker_tokens = [
        "--disconnect-on-state",
        "P16_MQTT_FAULT=DISCONNECT",
        "P16_BROKER_RESULT=PASS",
    ]
    https_tokens = [
        "P16_HTTPS_FAULT=TRUNCATE",
        "Content-Length",
        "connection.shutdown",
    ]
    hil_tokens = [
        "P16_FAULT_WITNESS=PASS",
        "P16_ROLLBACK_FAULT_WITNESS=PASS",
        "tampered-signature",
        "https-truncate",
        "rollback-reset",
        "baseline_region=baseline_region",
        "P16_EXTFLASH_SANITIZE=PASS",
        "incoming=erased_verified",
        "sleep 18000",
        "P16_SANITIZER_BUILD=PASS",
        "sanitizer=sanitizer",
    ]

    for token in secure_tokens:
        if token not in secure:
            fail(f"secure-container fault hook missing: {token}")
    for token in installer_tokens:
        if token not in installer:
            fail(f"installer fault hook missing: {token}")
    for token in uart_tokens:
        if token not in uart:
            fail(f"gateway negative-state hardening missing: {token}")
    for token in gateway_failure_tokens:
        if token not in gateway_manager:
            fail(f"gateway final-failure PUBACK contract missing: {token}")

    publish_failure_start = gateway_manager.find(
        "static esp_err_t PublishFailure("
    )
    process_command_start = gateway_manager.find(
        "static esp_err_t ProcessCommand("
    )
    if publish_failure_start < 0 or process_command_start <= publish_failure_start:
        fail("cannot locate gateway PublishFailure() contract")
    publish_failure_body = gateway_manager[
        publish_failure_start:process_command_start
    ]
    if "MqttOrchestrator_PublishStatus(" in publish_failure_body:
        fail(
            "gateway PublishFailure() regressed to asynchronous final status"
        )

    print("Phase 16 negative-final QoS1/PUBACK regression: PASS")

    for token in broker_tokens:
        if token not in broker:
            fail(f"Phase-16 broker contract missing: {token}")
    for token in https_tokens:
        if token not in https_fault:
            fail(f"Phase-16 HTTPS fault server missing: {token}")
    for token in hil_tokens:
        if token not in hil:
            fail(f"Phase-16 HIL invariant missing: {token}")

    print("Phase 16 fault-injection source contracts: PASS")


def validate_python() -> None:
    run([
        sys.executable,
        "-m",
        "py_compile",
        "scripts/phase16_check.py",
        "scripts/phase16_hw_test.py",
        "tools/phase16_mqtt_broker.py",
        "tools/phase16_fault_https_server.py",
    ])
    print("Phase 16 Python syntax: PASS")


def build_fault_variants(toolchain: str) -> None:
    sizes: dict[str, int] = {}

    try:
        for label, flags in BOOT_VARIANTS.items():
            build_dir = f"build-phase16-check-{label}"
            out_dir = f"out-phase16-check-{label}"

            for target in ("clean", "all"):
                run(
                    [
                        "make",
                        f"TOOLCHAIN={toolchain}",
                        f"BUILD_DIR={build_dir}",
                        f"OUT_DIR={out_dir}",
                        f"PROJECT_CFLAGS={flags}",
                        target,
                    ],
                    cwd=BOOT,
                )

            image = BOOT / out_dir / "bootloader.bin"
            if not image.is_file():
                fail(f"{label}: bootloader output missing")
            size = image.stat().st_size
            if size > 24 * 1024:
                fail(
                    f"{label}: bootloader size={size} exceeds 24 KiB"
                )
            sizes[label] = size

    finally:
        for label in BOOT_VARIANTS:
            shutil.rmtree(
                BOOT / f"build-phase16-check-{label}",
                ignore_errors=True,
            )
            shutil.rmtree(
                BOOT / f"out-phase16-check-{label}",
                ignore_errors=True,
            )

    rendered = " ".join(
        f"{label}={size}"
        for label, size in sizes.items()
    )
    print(
        f"Phase 16 STM32 fault-build matrix: PASS toolchain={toolchain} "
        f"{rendered}"
    )



def build_external_sanitizer(toolchain: str) -> None:
    app = ROOT / "node-stm32f103/application"
    build_dir = "build-phase16-check-ext-sanitizer"
    out_dir = "out-phase16-check-ext-sanitizer"
    flags = (
        "-DAPPLICATION_VERSION=0x00000001UL "
        "-DPHASE16_HIL_SANITIZE_EXTERNAL=1"
    )

    try:
        for target in ("clean", "all"):
            run(
                [
                    "make",
                    f"TOOLCHAIN={toolchain}",
                    f"BUILD_DIR={build_dir}",
                    f"OUT_DIR={out_dir}",
                    f"PROJECT_CFLAGS={flags}",
                    target,
                ],
                cwd=app,
            )

        image = app / out_dir / "application.bin"
        if not image.is_file():
            fail("external sanitizer application output missing")

        if image.stat().st_size > 38 * 1024:
            fail(
                "external sanitizer application exceeds 38 KiB: "
                f"{image.stat().st_size}"
            )

        print(
            "Phase 16 external-metadata sanitizer build: PASS "
            f"size={image.stat().st_size}"
        )
    finally:
        shutil.rmtree(app / build_dir, ignore_errors=True)
        shutil.rmtree(app / out_dir, ignore_errors=True)


def validate_packaging_boundary() -> None:
    trust = (
        BOOT / "include/phase14_trusted_key.h"
    ).read_text(encoding="utf-8")
    runtime = (
        ROOT / "gateway-esp32/main/include/phase11_runtime_config.h"
    ).read_text(encoding="utf-8")

    if "#define PHASE14_TRUSTED_KEY_PROVISIONED 0U" not in trust:
        fail("packaged bootloader trust anchor must remain unprovisioned")
    if "#define PHASE14_TRUSTED_KEY_ID          0UL" not in trust:
        fail("packaged bootloader key id must remain zero")
    if "#define SDOTA_PHASE11_HW_OVERRIDE          0" not in runtime:
        fail("packaged ESP32 runtime config must not contain HIL credentials")
    if '#define SDOTA_PHASE11_HW_WIFI_PASSWORD     ""' not in runtime:
        fail("packaged ESP32 Wi-Fi password must be empty")

    private_markers = {
        "-----BEGIN PRIVATE KEY-----",
        "-----BEGIN EC PRIVATE KEY-----",
        "-----BEGIN ENCRYPTED PRIVATE KEY-----",
        "-----BEGIN RSA PRIVATE KEY-----",
    }
    for path in ROOT.rglob("*"):
        if not path.is_file():
            continue
        try:
            text = path.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        first = next(
            (line.strip() for line in text.splitlines() if line.strip()),
            "",
        )
        if first in private_markers:
            fail(f"private key material present in repository: {path}")

    print(
        "Phase 16 packaging security boundary: PASS "
        "(unprovisioned + credential-free)"
    )


def main() -> int:
    uart_ota_source = (
        ROOT / "gateway-esp32/components/uart_ota/uart_ota_client.c"
    ).read_text(encoding="utf-8")

    broken_log_patterns = [
        "ESP_LOGW(TAG,\n                     saw_trial",
        'saw_trial\n                         ? "candidate rolled back',
    ]
    for token in broken_log_patterns:
        if token in uart_ota_source:
            fail(
                "Phase 16 ESP-IDF log-format regression: "
                "ESP_LOG format argument must be a string literal"
            )

    required_log_tokens = [
        'if (saw_trial)',
        '"candidate rolled back to app v%" PRIu32',
        '"candidate rejected before trial; app v%" PRIu32',
    ]
    for token in required_log_tokens:
        if token not in uart_ota_source:
            fail(
                "Phase 16 ESP-IDF log-format regression: "
                f"missing {token}"
            )

    print("Phase 16 ESP-IDF log-format regression: PASS")

    secure_container_source = (
        ROOT / "node-stm32f103/bootloader/src/secure_container.c"
    ).read_text(encoding="utf-8")

    host_safe_include = (
        "#if defined(PHASE16_FAULT_PATCH_RESET)\n"
        "#include \"stm32f10x.h\"\n"
        "#endif"
    )
    if host_safe_include not in secure_container_source:
        fail(
            "Phase 16 host-safe fault-hook include regression: "
            "stm32f10x.h must be guarded by PHASE16_FAULT_PATCH_RESET"
        )

    print("Phase 16 host-safe fault-hook include regression: PASS")

    required = [
        ROOT / "scripts/phase16_hw_test.py",
        ROOT / "tools/phase16_mqtt_broker.py",
        ROOT / "tools/phase16_fault_https_server.py",
        ROOT / "tests/fault/phase16_fault_matrix.json",
        ROOT / "docs/phase-16-fault-injection-hil.md",
        ROOT / "docs/phase-16-checklist.md",
        ROOT / "PHASE16_REPORT.md",
        ROOT / "PHASE16_VALIDATION.txt",
    ]
    missing = [
        str(path.relative_to(ROOT))
        for path in required
        if not path.is_file()
    ]
    if missing:
        fail("missing Phase-16 files: " + ", ".join(missing))

    validate_matrix()
    validate_source_contracts()
    print(
        "Phase 16 incoming-partition clean-baseline regression: PASS "
        "(Metadata A/B + Incoming erase/verify)"
    )
    validate_python()
    toolchain = choose_toolchain()
    build_fault_variants(toolchain)
    build_external_sanitizer(toolchain)
    validate_packaging_boundary()

    print(
        "Secure Delta OTA Phase 16 fault injection/HIL check: PASS"
    )
    print(
        "Physical Phase-16 fault matrix remains the final hardware gate: "
        "make phase16-hw-test ..."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
