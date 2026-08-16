#!/usr/bin/env python3
"""Phase 17 benchmark/portfolio closure gate."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
REFERENCE = ROOT / "benchmarks/phase17_reference.json"

REQUIRED_PORTFOLIO_FILES = [
    "docs/phase-17-benchmark-portfolio.md",
    "docs/phase-17-checklist.md",
    "docs/portfolio-one-page.md",
    "docs/portfolio-demo.md",
    "docs/portfolio-evidence.md",
    "benchmarks/README.md",
    "PHASE17_REPORT.md",
    "PHASE17_VALIDATION.txt",
]

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


def fail(message: str) -> None:
    print(f"Phase 17 check: FAIL: {message}")
    raise SystemExit(1)


def run(command: list[str], *, timeout: int = 360) -> str:
    result = subprocess.run(
        command,
        cwd=ROOT,
        env=os.environ.copy(),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
        timeout=timeout,
    )
    if result.returncode != 0:
        print(result.stdout, end="")
        fail(f"command returned {result.returncode}: {' '.join(command)}")
    return result.stdout


def validate_phase16_closure() -> None:
    report = (ROOT / "PHASE16_REPORT.md").read_text(encoding="utf-8")
    validation = (ROOT / "PHASE16_VALIDATION.txt").read_text(encoding="utf-8")
    readme = (ROOT / "README.md").read_text(encoding="utf-8")

    required = [
        "COMPLETE + HARDWARE VERIFIED",
        "PASS (9 deterministic scenarios)",
        "rollback-reset",
        "0x0008B003",
    ]
    combined = report + "\n" + validation
    for token in required:
        if token not in combined:
            fail(f"Phase-16 hardware closure evidence missing: {token}")

    if (
        "Phase 16 — Fault injection and HIL: complete + hardware verified."
        not in readme
    ):
        fail("README does not close Phase 16 as hardware verified")

    matrix = json.loads(
        (ROOT / "tests/fault/phase16_fault_matrix.json").read_text(
            encoding="utf-8"
        )
    )
    ids = {item["id"] for item in matrix["scenarios"]}
    if ids != REQUIRED_SCENARIOS:
        fail("Phase-16 matrix changed while closing portfolio")

    print("Phase 17 Phase-16 hardware-evidence closure: PASS 9/9")


def validate_reference() -> None:
    try:
        data = json.loads(REFERENCE.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        fail(f"cannot read benchmark reference: {exc}")

    if data.get("schema") != 1 or data.get("phase") != 17:
        fail("benchmark reference schema mismatch")

    limits = data["limits"]
    stm32 = data["stm32"]
    artifacts = data["artifacts"]
    hil = data["hardware_hil"]

    if stm32["bootloader"]["flash"] > limits["bootloader_flash_bytes"]:
        fail("reference bootloader exceeds 24 KiB")
    for key in ("application_v1", "application_v2"):
        if stm32[key]["flash"] > limits["application_flash_bytes"]:
            fail(f"reference {key} exceeds 38 KiB")
        if stm32[key]["ram"] > limits["sram_bytes"]:
            fail(f"reference {key} exceeds SRAM")
    if stm32["bootloader"]["ram"] > limits["sram_bytes"]:
        fail("reference bootloader exceeds SRAM")

    if artifacts["raw_delta_savings_percent"] < 20.0:
        fail("reference raw delta savings below project threshold")
    if artifacts["signed_delta_savings_percent"] < 20.0:
        fail("reference signed delta savings below project threshold")
    if artifacts["signed_delta_bytes"] >= artifacts["signed_full_bytes"]:
        fail("signed delta is not smaller than signed full")
    if artifacts["signed_delta_bytes"] > limits["incoming_partition_bytes"]:
        fail("signed delta exceeds Incoming partition")

    if hil["passed"] != 9 or hil["total"] != 9:
        fail("benchmark reference does not record Phase-16 9/9 HIL")
    if hil["final_application_version"] != 1:
        fail("reference final rollback state is not v1")
    if hil["rollback_diagnostic"] != "0x0008B003":
        fail("reference rollback diagnostic mismatch")
    if hil["private_signing_key_persisted"]:
        fail("reference incorrectly records persisted HIL private key")

    print(
        "Phase 17 benchmark reference: PASS "
        f"toolchain={data['environment']['toolchain']} "
        f"signed_delta_savings={artifacts['signed_delta_savings_percent']:.2f}%"
    )


def validate_portfolio_docs() -> None:
    for rel in REQUIRED_PORTFOLIO_FILES:
        path = ROOT / rel
        if not path.is_file() or path.stat().st_size < 100:
            fail(f"portfolio deliverable missing/empty: {rel}")

    one_page = (ROOT / "docs/portfolio-one-page.md").read_text(
        encoding="utf-8"
    )
    demo = (ROOT / "docs/portfolio-demo.md").read_text(encoding="utf-8")
    evidence = (ROOT / "docs/portfolio-evidence.md").read_text(
        encoding="utf-8"
    )
    readme = (ROOT / "README.md").read_text(encoding="utf-8")

    for token in (
        "STM32F103C8T6",
        "ESP32",
        "W25Q",
        "ECDSA P-256",
        "JojoDiff",
        "9/9",
        "rollback",
    ):
        if token not in one_page:
            fail(f"one-page portfolio summary missing: {token}")

    for token in (
        "5-minute",
        "make phase17-check",
        "make phase16-hw-test",
        "tampered-signature",
    ):
        if token not in demo:
            fail(f"portfolio demo script missing: {token}")

    for token in (
        "Claim",
        "Evidence",
        "Reproduce",
        "Phase 16",
        "Phase 17",
    ):
        if token not in evidence:
            fail(f"portfolio evidence map missing: {token}")

    if "Phase 17 — Benchmark and portfolio: complete." not in readme:
        fail("README Phase-17 completion status missing")

    print("Phase 17 portfolio documentation contracts: PASS")



def validate_automation_contracts() -> None:
    makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
    build_workflow = (
        ROOT / ".github/workflows/build.yml"
    ).read_text(encoding="utf-8")
    test_workflow = (
        ROOT / ".github/workflows/test.yml"
    ).read_text(encoding="utf-8")
    release_workflow = (
        ROOT / ".github/workflows/firmware-release.yml"
    ).read_text(encoding="utf-8")

    for token in (
        "all: phase17-check",
        "phase17-check: phase16-check",
        "phase17-benchmark:",
        "test: phase17-check",
    ):
        if token not in makefile:
            fail(f"Makefile Phase-17 contract missing: {token}")

    if "make phase17-check TOOLCHAIN=gcc" not in build_workflow:
        fail("CI build workflow does not execute Phase-17 closure")

    for token in (
        "scripts/phase17_benchmark.py",
        "scripts/phase17_check.py",
    ):
        if token not in test_workflow:
            fail(f"CI syntax workflow missing: {token}")

    for token in (
        "make phase15-check TOOLCHAIN=gcc",
        "python3 scripts/phase16_check.py",
        "Phase 17 portfolio closure",
        "python3 scripts/phase17_check.py --static-only",
    ):
        if token not in release_workflow:
            fail(f"protected release workflow regression: {token}")

    print("Phase 17 Makefile/CI automation contracts: PASS")


def validate_python() -> None:
    run([
        sys.executable,
        "-m",
        "py_compile",
        "scripts/phase17_benchmark.py",
        "scripts/phase17_check.py",
    ])
    print("Phase 17 Python syntax: PASS")


def validate_security_boundary() -> None:
    trust = (
        ROOT
        / "node-stm32f103/bootloader/include/phase14_trusted_key.h"
    ).read_text(encoding="utf-8")
    runtime = (
        ROOT
        / "gateway-esp32/main/include/phase11_runtime_config.h"
    ).read_text(encoding="utf-8")

    if "#define PHASE14_TRUSTED_KEY_PROVISIONED 0U" not in trust:
        fail("packaged trust anchor must remain unprovisioned")
    if "#define PHASE14_TRUSTED_KEY_ID          0UL" not in trust:
        fail("packaged trusted key id must remain zero")
    if "#define SDOTA_PHASE11_HW_OVERRIDE          0" not in runtime:
        fail("packaged ESP32 HIL override must remain disabled")
    if '#define SDOTA_PHASE11_HW_WIFI_PASSWORD     ""' not in runtime:
        fail("packaged Wi-Fi password must remain empty")

    private_markers = {
        "-----BEGIN PRIVATE KEY-----",
        "-----BEGIN EC PRIVATE KEY-----",
        "-----BEGIN ENCRYPTED PRIVATE KEY-----",
        "-----BEGIN RSA PRIVATE KEY-----",
    }
    for path in ROOT.rglob("*"):
        if not path.is_file():
            continue
        if any(
            part == ".git"
            or part.startswith("build-")
            or part.startswith("out-")
            for part in path.relative_to(ROOT).parts
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
        if first in private_markers:
            fail(f"private key material present: {path}")

    print("Phase 17 packaging security boundary: PASS")


def run_live_benchmark() -> None:
    with tempfile.TemporaryDirectory(prefix="phase17-check-") as td:
        output = Path(td) / "benchmark"
        text = run(
            [
                sys.executable,
                "scripts/phase17_benchmark.py",
                "--output-dir",
                str(output),
            ],
            timeout=360,
        )
        expected = [
            "PHASE17_BENCHMARK=PASS",
            "PHASE17_DELTA_SAVINGS=PASS",
            "PHASE17_HIL_EVIDENCE=PASS 9/9",
        ]
        for token in expected:
            if token not in text:
                fail(f"live benchmark marker missing: {token}")
        result = json.loads(
            (output / "phase17_benchmark.json").read_text(encoding="utf-8")
        )
        if result["hardware_hil"]["passed"] != 9:
            fail("live benchmark lost 9/9 HIL evidence")

    print("Phase 17 live build/artifact benchmark: PASS")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--static-only",
        action="store_true",
        help="skip live STM32/release benchmark build",
    )
    args = parser.parse_args()

    validate_phase16_closure()
    validate_reference()
    validate_portfolio_docs()
    validate_automation_contracts()
    validate_python()
    validate_security_boundary()
    if not args.static_only:
        run_live_benchmark()

    print("Secure Delta OTA Phase 17 benchmark/portfolio check: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
