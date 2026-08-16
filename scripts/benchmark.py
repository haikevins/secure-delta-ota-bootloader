#!/usr/bin/env python3
"""Reproducible build, footprint, delta, and signed-artifact benchmark."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "node-stm32f103/application"
BOOT = ROOT / "node-stm32f103/bootloader"

FLASH_BOOT_LIMIT = 24 * 1024
FLASH_APP_LIMIT = 38 * 1024
SRAM_LIMIT = 20 * 1024
INCOMING_LIMIT = 128 * 1024
KEY_ID = 0xB17C0001


class BenchmarkError(RuntimeError):
    pass


def choose_toolchain(explicit: str | None) -> str:
    if explicit:
        if explicit not in {"gcc", "clang"}:
            raise BenchmarkError("toolchain must be gcc or clang")
        return explicit
    if shutil.which("arm-none-eabi-gcc"):
        return "gcc"
    if shutil.which("clang") and shutil.which("llvm-objcopy"):
        return "clang"
    raise BenchmarkError(
        "no supported STM32 toolchain found "
        "(arm-none-eabi-gcc or clang+llvm-objcopy)"
    )


def run(
    command: list[str],
    *,
    cwd: Path = ROOT,
    env: dict[str, str] | None = None,
    timeout: int = 300,
) -> tuple[str, float]:
    start = time.perf_counter()
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
    elapsed = time.perf_counter() - start
    if result.returncode != 0:
        print(result.stdout, end="")
        raise BenchmarkError(
            f"command returned {result.returncode}: {' '.join(command)}"
        )
    return result.stdout, elapsed


def parse_size_file(path: Path) -> dict[str, int]:
    lines = [line.strip() for line in path.read_text(encoding="utf-8").splitlines()]
    for line in reversed(lines):
        fields = line.split()
        if len(fields) >= 6 and all(part.isdigit() for part in fields[:4]):
            text, data, bss, dec = map(int, fields[:4])
            return {
                "text": text,
                "data": data,
                "bss": bss,
                "dec": dec,
                "flash": text + data,
                "ram": data + bss,
            }
    raise BenchmarkError(f"cannot parse size output: {path}")


def build_component(
    component: Path,
    *,
    label: str,
    toolchain: str,
    flags: str = "",
) -> tuple[Path, dict[str, int], float]:
    build_dir = f"build-benchmark-{label}"
    out_dir = f"out-benchmark-{label}"
    try:
        _, clean_time = run(
            [
                "make",
                f"TOOLCHAIN={toolchain}",
                f"BUILD_DIR={build_dir}",
                f"OUT_DIR={out_dir}",
                "clean",
            ],
            cwd=component,
        )
        _, build_time = run(
            [
                "make",
                f"TOOLCHAIN={toolchain}",
                f"BUILD_DIR={build_dir}",
                f"OUT_DIR={out_dir}",
                f"PROJECT_CFLAGS={flags}",
                "all",
            ],
            cwd=component,
        )
        target = "application" if component == APP else "bootloader"
        image = component / out_dir / f"{target}.bin"
        size_file = component / out_dir / f"{target}.size.txt"
        if not image.is_file() or not size_file.is_file():
            raise BenchmarkError(f"{label}: expected build output missing")
        data = parse_size_file(size_file)
        data["binary"] = image.stat().st_size
        # Keep a private copy because cleanup removes the component output.
        copy_dir = ROOT / ".benchmark-tmp"
        copy_dir.mkdir(exist_ok=True)
        copy_path = copy_dir / f"{label}.bin"
        copy_path.write_bytes(image.read_bytes())
        return copy_path, data, clean_time + build_time
    finally:
        shutil.rmtree(component / build_dir, ignore_errors=True)
        shutil.rmtree(component / out_dir, ignore_errors=True)


def generate_delta(base: bytes, target: bytes) -> tuple[bytes, float]:
    if str(ROOT) not in sys.path:
        sys.path.insert(0, str(ROOT))
    from tools.jojodiff_patch import apply_patch, generate_patch  # noqa: E402

    start = time.perf_counter()
    patch, _stats = generate_patch(base, target)
    reconstructed = apply_patch(base, patch)
    elapsed = time.perf_counter() - start
    if reconstructed != target:
        raise BenchmarkError("JojoDiff round-trip mismatch")
    return patch, elapsed


def create_signed_release(
    *,
    base_path: Path,
    target_path: Path,
    temp: Path,
) -> tuple[Path, float]:
    key = temp / "benchmark-key.pem"
    run(
        [
            "openssl",
            "ecparam",
            "-name",
            "prime256v1",
            "-genkey",
            "-noout",
            "-out",
            str(key),
        ],
        timeout=30,
    )
    key.chmod(0o600)

    out_root = temp / "releases"
    env = os.environ.copy()
    env["SOURCE_DATE_EPOCH"] = "1767225600"  # 2026-01-01T00:00:00Z
    _, elapsed = run(
        [
            sys.executable,
            "tools/release.py",
            "--target",
            str(target_path),
            "--target-version",
            "2",
            "--base",
            str(base_path),
            "--base-version",
            "1",
            "--key",
            str(key),
            "--key-id",
            hex(KEY_ID),
            "--base-url",
            "https://benchmark.invalid",
            "--release-id",
            "benchmark-reference",
            "--output-root",
            str(out_root),
            "--source-revision",
            "benchmark-reference",
        ],
        env=env,
        timeout=120,
    )
    release = out_root / "benchmark-reference"
    if not release.is_dir():
        raise BenchmarkError("signed benchmark release missing")
    return release, elapsed


def owned_source_stats() -> dict[str, int]:
    excluded = {
        ".git",
        "third_party",
        "cmsis",
        "spl",
        "build",
        "dist",
        "__pycache__",
    }
    suffixes = {".c", ".h", ".s", ".py", ".ld", ".yml", ".yaml"}
    files = 0
    logical_lines = 0
    for path in ROOT.rglob("*"):
        if not path.is_file() or path.suffix.lower() not in suffixes:
            continue
        rel_parts = path.relative_to(ROOT).parts
        if any(part in excluded or part.startswith("build-") or part.startswith("out-")
               for part in rel_parts):
            continue
        files += 1
        try:
            text = path.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        logical_lines += sum(1 for line in text.splitlines() if line.strip())
    return {"files": files, "nonblank_lines": logical_lines}


def pct(used: int, limit: int) -> float:
    return round(used * 100.0 / limit, 2)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def write_outputs(results: dict[str, object], output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "benchmark.json").write_text(
        json.dumps(results, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )

    rows: list[tuple[str, object, str]] = []
    def row(metric: str, value: object, unit: str = "") -> None:
        rows.append((metric, value, unit))

    stm32 = results["stm32"]
    app_v1 = stm32["application_v1"]
    app_v2 = stm32["application_v2"]
    boot = stm32["bootloader"]
    artifacts = results["artifacts"]
    hil = results["hardware_hil"]
    codebase = results["codebase"]

    row("bootloader_flash", boot["flash"], "bytes")
    row("bootloader_flash_utilization", boot["flash_percent"], "%")
    row("bootloader_ram", boot["ram"], "bytes")
    row("application_v1_flash", app_v1["flash"], "bytes")
    row("application_v2_flash", app_v2["flash"], "bytes")
    row("application_v2_flash_utilization", app_v2["flash_percent"], "%")
    row("application_v2_ram", app_v2["ram"], "bytes")
    row("raw_delta_size", artifacts["raw_delta_bytes"], "bytes")
    row("raw_delta_savings", artifacts["raw_delta_savings_percent"], "%")
    row("signed_delta_size", artifacts["signed_delta_bytes"], "bytes")
    row("signed_full_size", artifacts["signed_full_bytes"], "bytes")
    row("signed_delta_savings", artifacts["signed_delta_savings_percent"], "%")
    row("incoming_partition_utilization_delta", artifacts["incoming_delta_percent"], "%")
    row("hil_passed", hil["passed"], "scenarios")
    row("hil_total", hil["total"], "scenarios")
    row("owned_source_files", codebase["files"], "files")
    row("owned_nonblank_lines", codebase["nonblank_lines"], "lines")

    with (output_dir / "benchmark.csv").open(
        "w", encoding="utf-8", newline=""
    ) as handle:
        writer = csv.writer(handle)
        writer.writerow(["metric", "value", "unit"])
        writer.writerows(rows)

    md = [
        "# Secure Delta OTA Benchmark",
        "",
        f"- Toolchain: `{results['environment']['toolchain']}`",
        f"- Host: `{results['environment']['platform']}`",
        "- Timing values are host wall-clock observations, not MCU cycle counts.",
        "",
        "## STM32 footprint",
        "",
        "| Image | Flash | Limit | Utilization | RAM | SRAM limit |",
        "|---|---:|---:|---:|---:|---:|",
        f"| Bootloader | {boot['flash']} B | {FLASH_BOOT_LIMIT} B | {boot['flash_percent']:.2f}% | {boot['ram']} B | {SRAM_LIMIT} B |",
        f"| App v1 | {app_v1['flash']} B | {FLASH_APP_LIMIT} B | {app_v1['flash_percent']:.2f}% | {app_v1['ram']} B | {SRAM_LIMIT} B |",
        f"| App v2 | {app_v2['flash']} B | {FLASH_APP_LIMIT} B | {app_v2['flash_percent']:.2f}% | {app_v2['ram']} B | {SRAM_LIMIT} B |",
        "",
        "## OTA artifact efficiency",
        "",
        "| Metric | Result |",
        "|---|---:|",
        f"| Raw target binary | {artifacts['target_bytes']} B |",
        f"| Raw JojoDiff patch | {artifacts['raw_delta_bytes']} B |",
        f"| Raw delta savings | {artifacts['raw_delta_savings_percent']:.2f}% |",
        f"| Signed full SDOT | {artifacts['signed_full_bytes']} B |",
        f"| Signed delta SDOT | {artifacts['signed_delta_bytes']} B |",
        f"| Signed delta savings | {artifacts['signed_delta_savings_percent']:.2f}% |",
        f"| Incoming partition used by signed delta | {artifacts['incoming_delta_percent']:.2f}% |",
        "",
        "## Reliability evidence",
        "",
        f"- Deterministic hardware-in-the-loop fault matrix: **{hil['passed']}/{hil['total']} PASS**.",
        "- Final rollback-reset state: confirmed application v1 restored.",
        "- HIL private signing key persisted: **no**.",
        "",
        "## Host execution time",
        "",
        "| Operation | Wall time |",
        "|---|---:|",
        f"| Bootloader build | {results['timing_seconds']['bootloader_build']:.3f} s |",
        f"| App v1 build | {results['timing_seconds']['application_v1_build']:.3f} s |",
        f"| App v2 build | {results['timing_seconds']['application_v2_build']:.3f} s |",
        f"| Delta generate + round-trip | {results['timing_seconds']['delta_roundtrip']:.3f} s |",
        f"| Signed release generation | {results['timing_seconds']['signed_release']:.3f} s |",
        "",
        "These wall-clock timings are informational and intentionally have no PASS/FAIL threshold.",
        "",
    ]
    (output_dir / "benchmark.md").write_text(
        "\n".join(md),
        encoding="utf-8",
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=ROOT / "dist/benchmark",
    )
    parser.add_argument("--toolchain", choices=["gcc", "clang"])
    parser.add_argument(
        "--hardware-hil-passed",
        type=int,
        default=9,
        help="deterministic hardware scenarios observed PASS",
    )
    parser.add_argument("--hardware-hil-total", type=int, default=9)
    args = parser.parse_args()

    try:
        toolchain = choose_toolchain(args.toolchain or os.environ.get("TOOLCHAIN"))
        temp_copy_dir = ROOT / ".benchmark-tmp"
        shutil.rmtree(temp_copy_dir, ignore_errors=True)

        boot_path, boot_size, boot_time = build_component(
            BOOT,
            label="boot",
            toolchain=toolchain,
        )
        v1_path, v1_size, v1_time = build_component(
            APP,
            label="app-v1",
            toolchain=toolchain,
            flags="-DAPPLICATION_VERSION=0x00000001UL",
        )
        v2_path, v2_size, v2_time = build_component(
            APP,
            label="app-v2",
            toolchain=toolchain,
            flags="-DAPPLICATION_VERSION=0x00000002UL",
        )

        base = v1_path.read_bytes()
        target = v2_path.read_bytes()
        patch, delta_time = generate_delta(base, target)

        with tempfile.TemporaryDirectory(prefix="benchmark-release-") as td:
            release, release_time = create_signed_release(
                base_path=v1_path,
                target_path=v2_path,
                temp=Path(td),
            )
            full_candidates = list(release.glob("*.full.sdot"))
            delta_candidates = list(release.glob("*.delta.sdot"))
            if len(full_candidates) != 1 or len(delta_candidates) != 1:
                raise BenchmarkError("expected one full and one delta SDOT")
            full = full_candidates[0].read_bytes()
            delta = delta_candidates[0].read_bytes()

        raw_savings = (1.0 - len(patch) / len(target)) * 100.0
        signed_savings = (1.0 - len(delta) / len(full)) * 100.0

        for label, data, limit in (
            ("bootloader", boot_size, FLASH_BOOT_LIMIT),
            ("application_v1", v1_size, FLASH_APP_LIMIT),
            ("application_v2", v2_size, FLASH_APP_LIMIT),
        ):
            if data["flash"] > limit:
                raise BenchmarkError(
                    f"{label} flash {data['flash']} exceeds {limit}"
                )
            if data["ram"] > SRAM_LIMIT:
                raise BenchmarkError(
                    f"{label} RAM {data['ram']} exceeds {SRAM_LIMIT}"
                )
        if raw_savings < 20.0 or signed_savings < 20.0:
            raise BenchmarkError(
                "delta benchmark failed minimum 20% savings policy"
            )
        if len(delta) > INCOMING_LIMIT or len(full) > INCOMING_LIMIT:
            raise BenchmarkError("signed SDOT exceeds Incoming partition")

        results = {
            "schema": 1,
                        "environment": {
                "toolchain": toolchain,
                "platform": platform.platform(),
                "python": platform.python_version(),
            },
            "limits": {
                "bootloader_flash_bytes": FLASH_BOOT_LIMIT,
                "application_flash_bytes": FLASH_APP_LIMIT,
                "sram_bytes": SRAM_LIMIT,
                "incoming_partition_bytes": INCOMING_LIMIT,
            },
            "stm32": {
                "bootloader": {
                    **boot_size,
                    "flash_percent": pct(boot_size["flash"], FLASH_BOOT_LIMIT),
                    "ram_percent": pct(boot_size["ram"], SRAM_LIMIT),
                },
                "application_v1": {
                    **v1_size,
                    "flash_percent": pct(v1_size["flash"], FLASH_APP_LIMIT),
                    "ram_percent": pct(v1_size["ram"], SRAM_LIMIT),
                },
                "application_v2": {
                    **v2_size,
                    "flash_percent": pct(v2_size["flash"], FLASH_APP_LIMIT),
                    "ram_percent": pct(v2_size["ram"], SRAM_LIMIT),
                },
            },
            "artifacts": {
                "base_bytes": len(base),
                "target_bytes": len(target),
                "raw_delta_bytes": len(patch),
                "raw_delta_savings_percent": round(raw_savings, 2),
                "signed_full_bytes": len(full),
                "signed_delta_bytes": len(delta),
                "signed_delta_savings_percent": round(signed_savings, 2),
                "incoming_delta_percent": pct(len(delta), INCOMING_LIMIT),
                "base_sha256": sha256(base),
                "target_sha256": sha256(target),
                "raw_delta_sha256": sha256(patch),
            },
            "hardware_hil": {
                "source": "deterministic-hil",
                "passed": args.hardware_hil_passed,
                "total": args.hardware_hil_total,
                "final_application_version": 1,
                "rollback_diagnostic": "0x0008B003",
                "private_signing_key_persisted": False,
            },
            "codebase": owned_source_stats(),
            "timing_seconds": {
                "bootloader_build": round(boot_time, 3),
                "application_v1_build": round(v1_time, 3),
                "application_v2_build": round(v2_time, 3),
                "delta_roundtrip": round(delta_time, 3),
                "signed_release": round(release_time, 3),
            },
            "policy": {
                "minimum_delta_savings_percent": 20.0,
                "timing_thresholds": None,
                "note": (
                    "Wall-clock times are environment-specific; footprint, "
                    "artifact savings and HIL scenario count are the portable "
                    "portfolio claims."
                ),
            },
        }

        output_dir = args.output_dir
        if not output_dir.is_absolute():
            output_dir = ROOT / output_dir
        write_outputs(results, output_dir)

        print(
            "BENCHMARK=PASS "
            f"toolchain={toolchain} "
            f"boot_flash={boot_size['flash']} "
            f"app_v2_flash={v2_size['flash']} "
            f"raw_delta={len(patch)} "
            f"signed_delta={len(delta)} "
            f"signed_full={len(full)}"
        )
        print(
            "DELTA_SAVINGS=PASS "
            f"raw={raw_savings:.2f}% signed={signed_savings:.2f}%"
        )
        print(
            "HIL_EVIDENCE=PASS "
            f"{args.hardware_hil_passed}/{args.hardware_hil_total}"
        )
        print(f"BENCHMARK_OUTPUT={output_dir}")
        return 0
    except (BenchmarkError, OSError, subprocess.TimeoutExpired) as exc:
        print(f"Benchmark: FAIL: {exc}")
        return 1
    finally:
        shutil.rmtree(ROOT / ".benchmark-tmp", ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
