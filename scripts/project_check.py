#!/usr/bin/env python3
"""Integrated closure gate for the Secure Delta OTA project."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BOOT = ROOT / "node-stm32f103/bootloader"
APP = ROOT / "node-stm32f103/application"

FLASH_BOOT_LIMIT = 24 * 1024
FLASH_APP_LIMIT = 38 * 1024
SRAM_LIMIT = 20 * 1024

REQUIRED_HIL_SCENARIOS = {
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

FIRST_PARTY_EXCLUDES = (
    "third_party/",
    "node-stm32f103/cmsis/",
    "node-stm32f103/spl/",
)

PYTHON_FILES = [
    "scripts/project_check.py",
    "scripts/benchmark.py",
    "scripts/hil_test.py",
    "scripts/gateway_hil_support.py",
    "scripts/security_hil_support.py",
    "scripts/release_hil_support.py",
    "scripts/esp32_build_guard.py",
    "tools/release.py",
    "tools/secure_container.py",
    "tools/keytool.py",
    "tools/delta.py",
    "tools/delta_artifact.py",
    "tools/jojodiff_patch.py",
    "tools/ota_uart_protocol.py",
    "tools/uart_ota_sender.py",
    "tools/mqtt_protocol.py",
    "tools/mqtt_test_broker.py",
    "tools/fault_https_server.py",
    "tools/https_test_server.py",
    "server/app/main.py",
    "server/app/services/firmware_service.py",
    "server/app/services/manifest_service.py",
    "server/app/services/signing_service.py",
    "server/app/services/https_service.py",
    "server/app/services/mqtt_service.py",
]


class CheckError(RuntimeError):
    pass


def fail(message: str) -> None:
    print(f"PROJECT_CHECK=FAIL {message}")
    raise SystemExit(1)


def run(
    command: list[str],
    *,
    cwd: Path = ROOT,
    timeout: int = 360,
    env: dict[str, str] | None = None,
) -> str:
    result = subprocess.run(
        command,
        cwd=cwd,
        env=env or os.environ.copy(),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
        timeout=timeout,
    )
    if result.returncode != 0:
        print(result.stdout, end="")
        raise CheckError(
            f"command returned {result.returncode}: {' '.join(command)}"
        )
    return result.stdout


def choose_toolchain() -> str:
    explicit = os.environ.get("TOOLCHAIN", "").strip()
    if explicit:
        if explicit not in {"gcc", "clang"}:
            raise CheckError("TOOLCHAIN must be gcc or clang")
        return explicit
    if shutil.which("arm-none-eabi-gcc"):
        return "gcc"
    if shutil.which("clang") and shutil.which("llvm-objcopy"):
        return "clang"
    raise CheckError(
        "no supported STM32 toolchain found "
        "(arm-none-eabi-gcc or clang+llvm-objcopy)"
    )


def validate_product_presentation() -> None:
    required = [
        "README.md",
        "PROJECT_REPORT.md",
        "VALIDATION.md",
        "docs/portfolio-one-page.md",
        "docs/portfolio-demo.md",
        "docs/portfolio-evidence.md",
        "docs/hil-results.md",
        "docs/benchmark-portfolio.md",
        "benchmarks/reference.json",
        "benchmarks/reference.csv",
        "benchmarks/reference.md",
    ]
    for rel in required:
        path = ROOT / rel
        if not path.is_file() or path.stat().st_size < 80:
            raise CheckError(f"required project deliverable missing: {rel}")

    # Legacy implementation-stage terminology is intentionally absent from
    # first-party source/docs. Vendor CMSIS/SPL/third-party files are excluded
    # because the ordinary electronics/algorithmic word can occur there
    # independently.
    for path in ROOT.rglob("*"):
        if not path.is_file():
            continue
        rel = path.relative_to(ROOT).as_posix()
        if rel.startswith(FIRST_PARTY_EXCLUDES):
            continue
        legacy_stage_word = "ph" + "ase"
        if legacy_stage_word in path.name.lower():
            raise CheckError(f"legacy stage-style filename remains: {rel}")
        try:
            text = path.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        if legacy_stage_word in text.lower():
            raise CheckError(f"legacy stage terminology remains in first-party file: {rel}")

    readme = (ROOT / "README.md").read_text(encoding="utf-8")
    for token in (
        "Secure Delta OTA Bootloader",
        "ECDSA P-256",
        "JojoDiff",
        "MQTTS",
        "HTTPS",
        "9/9 PASS",
        "make check",
        "make benchmark",
        "make hil-test",
    ):
        if token not in readme:
            raise CheckError(f"README missing integrated project claim: {token}")

    print("PROJECT_PRESENTATION=PASS product_layout=clean")


def validate_hardware_evidence() -> None:
    matrix_path = ROOT / "tests/fault/fault_matrix.json"
    matrix = json.loads(matrix_path.read_text(encoding="utf-8"))
    ids = {item["id"] for item in matrix["scenarios"]}
    if ids != REQUIRED_HIL_SCENARIOS:
        raise CheckError("deterministic HIL scenario matrix changed")

    evidence = (ROOT / "docs/hil-results.md").read_text(encoding="utf-8")
    for token in (
        "9/9 PASS",
        "rollback-reset",
        "active_version=1",
        "pending_version=0",
        "boot_attempts=0",
        "last_error=0x0008B003",
        "app=v1",
    ):
        if token not in evidence:
            raise CheckError(f"HIL evidence missing: {token}")

    print("HIL_EVIDENCE=PASS scenarios=9/9")


def validate_reference_benchmark() -> None:
    data = json.loads(
        (ROOT / "benchmarks/reference.json").read_text(encoding="utf-8")
    )
    if data.get("schema") != 1:
        raise CheckError("benchmark reference schema mismatch")

    limits = data["limits"]
    stm32 = data["stm32"]
    artifacts = data["artifacts"]
    hil = data["hardware_hil"]

    if limits["bootloader_flash_bytes"] != FLASH_BOOT_LIMIT:
        raise CheckError("bootloader reference limit mismatch")
    if limits["application_flash_bytes"] != FLASH_APP_LIMIT:
        raise CheckError("application reference limit mismatch")
    if limits["sram_bytes"] != SRAM_LIMIT:
        raise CheckError("SRAM reference limit mismatch")

    if stm32["bootloader"]["flash"] > FLASH_BOOT_LIMIT:
        raise CheckError("reference bootloader exceeds flash budget")
    if stm32["bootloader"]["ram"] > SRAM_LIMIT:
        raise CheckError("reference bootloader exceeds SRAM budget")
    for name in ("application_v1", "application_v2"):
        if stm32[name]["flash"] > FLASH_APP_LIMIT:
            raise CheckError(f"reference {name} exceeds flash budget")
        if stm32[name]["ram"] > SRAM_LIMIT:
            raise CheckError(f"reference {name} exceeds SRAM budget")

    if artifacts["raw_delta_savings_percent"] < 20.0:
        raise CheckError("reference raw delta savings below 20%")
    if artifacts["signed_delta_savings_percent"] < 20.0:
        raise CheckError("reference signed delta savings below 20%")
    if artifacts["signed_delta_bytes"] >= artifacts["signed_full_bytes"]:
        raise CheckError("reference signed delta is not smaller than signed full")
    if hil["passed"] != 9 or hil["total"] != 9:
        raise CheckError("reference does not preserve 9/9 HIL evidence")
    if hil["final_application_version"] != 1:
        raise CheckError("reference rollback final application is not v1")
    if hil["rollback_diagnostic"] != "0x0008B003":
        raise CheckError("reference rollback diagnostic mismatch")
    if hil["private_signing_key_persisted"]:
        raise CheckError("reference incorrectly records a persisted private key")

    print(
        "BENCHMARK_REFERENCE=PASS "
        f"toolchain={data['environment']['toolchain']} "
        f"signed_delta_savings={artifacts['signed_delta_savings_percent']:.2f}%"
    )


def validate_security_boundary() -> None:
    trust = (
        ROOT / "node-stm32f103/bootloader/include/trusted_key.h"
    ).read_text(encoding="utf-8")
    runtime = (
        ROOT / "gateway-esp32/main/include/runtime_config.h"
    ).read_text(encoding="utf-8")

    for token in (
        "#define TRUSTED_KEY_PROVISIONED 0U",
        "#define TRUSTED_KEY_ID          0UL",
    ):
        if token not in trust:
            raise CheckError(f"checked-in trust anchor boundary changed: {token}")

    for token in (
        "#define SDOTA_RUNTIME_OVERRIDE          0",
        '#define SDOTA_RUNTIME_WIFI_PASSWORD     ""',
        '#define SDOTA_RUNTIME_MQTT_URI          ""',
    ):
        if token not in runtime:
            raise CheckError(f"checked-in runtime config boundary changed: {token}")

    private_headers = {
        "-----BEGIN PRIVATE KEY-----",
        "-----BEGIN EC PRIVATE KEY-----",
        "-----BEGIN ENCRYPTED PRIVATE KEY-----",
        "-----BEGIN RSA PRIVATE KEY-----",
    }
    for path in ROOT.rglob("*"):
        if not path.is_file():
            continue
        rel = path.relative_to(ROOT)
        if any(
            part == ".git"
            or part.startswith("build-")
            or part.startswith("out-")
            or part == "dist"
            for part in rel.parts
        ):
            continue
        try:
            text = path.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        first = next(
            (line.strip() for line in text.splitlines() if line.strip()),
            "",
        )
        if first in private_headers:
            raise CheckError(f"private key material found in repository: {rel}")

    print("SECURITY_BOUNDARY=PASS unprovisioned_trust_anchor credential_free")


def validate_source_contracts() -> None:
    secure = (
        ROOT / "node-stm32f103/bootloader/src/secure_container.c"
    ).read_text(encoding="utf-8")
    installer = (
        ROOT / "node-stm32f103/bootloader/src/image_installer.c"
    ).read_text(encoding="utf-8")
    app = (
        ROOT / "node-stm32f103/application/src/main.c"
    ).read_text(encoding="utf-8")
    gateway = (
        ROOT / "gateway-esp32/main/gateway_manager.c"
    ).read_text(encoding="utf-8")
    makefile = (ROOT / "Makefile").read_text(encoding="utf-8")

    for token in (
        "TRUSTED_KEY_PROVISIONED",
        "TRUSTED_KEY_ID",
        "HIL_FAULT_PATCH_RESET",
        "g_trusted_public_key",
    ):
        if token not in secure:
            raise CheckError(f"secure-container contract missing: {token}")

    for token in (
        "HIL_FAULT_BACKUP_RESET_OFFSET",
        "HIL_FAULT_INSTALL_OFFSET",
        "HIL_FAULT_ROLLBACK_RESET_OFFSET",
        "IMAGE_INSTALLER_ROLLBACK_TRIAL_LIMIT_BASE",
    ):
        if token not in installer:
            raise CheckError(f"installer recovery contract missing: {token}")

    if "HIL_SANITIZE_EXTERNAL" not in app:
        raise CheckError("external-flash HIL sanitizer contract missing")
    if "GatewayManager_Run(" not in gateway:
        raise CheckError("gateway manager entry point missing")

    for token in (
        "all: check",
        "check:",
        "benchmark:",
        "hil-test:",
        "release:",
        "combined:",
    ):
        if token not in makefile:
            raise CheckError(f"Makefile integrated target missing: {token}")

    print("SOURCE_CONTRACTS=PASS secure_delta_recovery_gateway")


def validate_automation_contracts() -> None:
    build = (
        ROOT / ".github/workflows/build.yml"
    ).read_text(encoding="utf-8")
    tests = (
        ROOT / ".github/workflows/test.yml"
    ).read_text(encoding="utf-8")
    release = (
        ROOT / ".github/workflows/firmware-release.yml"
    ).read_text(encoding="utf-8")

    for token in ("make check TOOLCHAIN=gcc", "make benchmark TOOLCHAIN=gcc"):
        if token not in build:
            raise CheckError(f"build workflow missing: {token}")
    for token in ("scripts/project_check.py", "scripts/benchmark.py", "scripts/hil_test.py"):
        if token not in tests:
            raise CheckError(f"host-test workflow missing: {token}")
    for token in ("make check TOOLCHAIN=gcc", "tools/release.py", "SIGNING_KEY_PEM"):
        if token not in release:
            raise CheckError(f"release workflow missing: {token}")

    print("AUTOMATION_CONTRACTS=PASS ci_release")


def validate_python() -> None:
    missing = [rel for rel in PYTHON_FILES if not (ROOT / rel).is_file()]
    if missing:
        raise CheckError("Python entry points missing: " + ", ".join(missing))
    run([sys.executable, "-m", "py_compile", *PYTHON_FILES], timeout=60)

    # Keep the deterministic JojoDiff model as an executable host regression.
    jojo = ROOT / "tests/unit/test_jojodiff.py"
    if jojo.is_file():
        run([sys.executable, str(jojo)], timeout=60)

    print("PYTHON_HOST_CHECKS=PASS")


def build_fault_matrix(toolchain: str) -> None:
    variants = {
        "control": "",
        "patch": "-DHIL_FAULT_PATCH_RESET=1",
        "backup": "-DHIL_FAULT_BACKUP_RESET_OFFSET=4096UL",
        "install": "-DHIL_FAULT_INSTALL_OFFSET=1536UL",
        "rollback": "-DHIL_FAULT_ROLLBACK_RESET_OFFSET=1024UL",
    }
    sizes: dict[str, int] = {}
    try:
        for label, flags in variants.items():
            build_dir = f"build-check-{label}"
            out_dir = f"out-check-{label}"
            run(
                [
                    "make",
                    f"TOOLCHAIN={toolchain}",
                    f"BUILD_DIR={build_dir}",
                    f"OUT_DIR={out_dir}",
                    f"PROJECT_CFLAGS={flags}",
                    "clean",
                    "all",
                ],
                cwd=BOOT,
                timeout=180,
            )
            image = BOOT / out_dir / "bootloader.bin"
            if not image.is_file():
                raise CheckError(f"{label} bootloader output missing")
            size = image.stat().st_size
            if size > FLASH_BOOT_LIMIT:
                raise CheckError(
                    f"{label} bootloader {size} exceeds {FLASH_BOOT_LIMIT}"
                )
            sizes[label] = size

        run(
            [
                "make",
                f"TOOLCHAIN={toolchain}",
                "BUILD_DIR=build-check-sanitizer",
                "OUT_DIR=out-check-sanitizer",
                "PROJECT_CFLAGS=-DHIL_SANITIZE_EXTERNAL=1",
                "clean",
                "all",
            ],
            cwd=APP,
            timeout=180,
        )
        sanitizer = APP / "out-check-sanitizer/application.bin"
        if not sanitizer.is_file():
            raise CheckError("external-flash sanitizer build missing")
    finally:
        for component in (BOOT, APP):
            for pattern in ("build-check-*", "out-check-*"):
                for path in component.glob(pattern):
                    shutil.rmtree(path, ignore_errors=True)

    print(
        "FAULT_BUILD_MATRIX=PASS "
        + " ".join(f"{name}={size}" for name, size in sizes.items())
    )


def run_live_benchmark() -> None:
    with tempfile.TemporaryDirectory(prefix="sdota-check-") as td:
        output = Path(td) / "benchmark"
        text = run(
            [
                sys.executable,
                "scripts/benchmark.py",
                "--output-dir",
                str(output),
            ],
            timeout=360,
        )
        for token in (
            "BENCHMARK=PASS",
            "DELTA_SAVINGS=PASS",
            "HIL_EVIDENCE=PASS 9/9",
        ):
            if token not in text:
                raise CheckError(f"live benchmark marker missing: {token}")
        data = json.loads(
            (output / "benchmark.json").read_text(encoding="utf-8")
        )
        if data["hardware_hil"]["passed"] != 9:
            raise CheckError("live benchmark lost HIL evidence")

    print("LIVE_BENCHMARK=PASS")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--static-only",
        action="store_true",
        help="skip STM32 cross-builds and live benchmark",
    )
    args = parser.parse_args()

    try:
        validate_product_presentation()
        validate_hardware_evidence()
        validate_reference_benchmark()
        validate_security_boundary()
        validate_source_contracts()
        validate_automation_contracts()
        validate_python()

        if not args.static_only:
            toolchain = choose_toolchain()
            build_fault_matrix(toolchain)
            run_live_benchmark()

        print("SECURE_DELTA_OTA_PROJECT_CHECK=PASS")
        return 0
    except (CheckError, OSError, json.JSONDecodeError, subprocess.TimeoutExpired) as exc:
        fail(str(exc))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
