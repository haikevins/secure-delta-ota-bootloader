#!/usr/bin/env python3
from __future__ import annotations

import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zlib

ROOT = Path(__file__).resolve().parents[1]
GATEWAY = ROOT / "gateway-esp32"
STM32_BASELINE = ROOT / "dist/secure-delta-ota-phase9.bin"
CANDIDATE = (
    ROOT
    / "node-stm32f103/application/out-phase9-candidate/application.bin"
)

METADATA_ADDRESS = 0x0800F800
METADATA_SIZE = 0x800
METADATA_PAGE = 1024
METADATA_RECORD_SIZE = 52
METADATA_MAGIC = 0x424D4554
METADATA_VERSION = 1
UPDATE_IDLE = 0
TARGET_VERSION = 2

OPENOCD_SETUP = (
    "source [find interface/stlink.cfg]; "
    "transport select hla_swd; "
    "source [find target/stm32f1x.cfg]; "
    "reset_config none; "
    "adapter speed 1000; "
)

PASS_MARKER = "P9_GATEWAY_HW_TEST=PASS"
FAIL_MARKER = "P9_GATEWAY_HW_TEST=FAIL"

def fail(message: str) -> None:
    print(f"Phase 9 hardware test: FAIL: {message}")
    raise SystemExit(1)

def tcl_path(path: Path) -> str:
    value = str(path.resolve())
    if "}" in value:
        fail("path containing '}' is not supported")
    return "{" + value + "}"

def _path_openocd_candidates() -> list[Path]:
    candidates: list[Path] = []
    seen: set[Path] = set()

    for directory in os.environ.get("PATH", "").split(os.pathsep):
        if not directory:
            continue
        candidate = Path(directory) / "openocd"
        try:
            resolved = candidate.resolve()
        except OSError:
            resolved = candidate

        if resolved in seen:
            continue
        seen.add(resolved)

        if candidate.is_file() and os.access(candidate, os.X_OK):
            candidates.append(candidate)

    return candidates


def _script_candidates(openocd: Path) -> list[Path]:
    explicit = os.environ.get("STM32_OPENOCD_SCRIPTS", "").strip()
    candidates: list[Path] = []

    if explicit:
        candidates.append(Path(explicit))

    # Common upstream OpenOCD package/install locations.
    candidates.extend([
        Path("/usr/share/openocd/scripts"),
        Path("/usr/local/share/openocd/scripts"),
        openocd.parent.parent / "share/openocd/scripts",
    ])

    result: list[Path] = []
    seen: set[Path] = set()
    for candidate in candidates:
        try:
            resolved = candidate.resolve()
        except OSError:
            resolved = candidate
        if resolved not in seen:
            seen.add(resolved)
            result.append(candidate)
    return result


def _is_stm32_script_root(path: Path) -> bool:
    return (
        (path / "interface/stlink.cfg").is_file()
        and (path / "target/stm32f1x.cfg").is_file()
    )


def resolve_stm32_openocd() -> tuple[str, str]:
    explicit = os.environ.get("STM32_OPENOCD", "").strip()
    candidates: list[Path] = []

    if explicit:
        candidates.append(Path(explicit))
    else:
        # Prefer system/upstream installations before PATH because sourcing
        # ESP-IDF prepends Espressif's openocd-esp32 to PATH.
        candidates.extend([
            Path("/usr/bin/openocd"),
            Path("/usr/local/bin/openocd"),
        ])
        candidates.extend(_path_openocd_candidates())

    seen: set[Path] = set()

    for candidate in candidates:
        try:
            resolved = candidate.resolve()
        except OSError:
            resolved = candidate

        if resolved in seen:
            continue
        seen.add(resolved)

        if not candidate.is_file() or not os.access(candidate, os.X_OK):
            continue

        # Never auto-select Espressif's OpenOCD for the STM32 ST-Link path.
        path_text = str(resolved).lower()
        if not explicit and (
            "openocd-esp32" in path_text
            or "/.espressif/" in path_text
        ):
            continue

        for scripts in _script_candidates(candidate):
            if _is_stm32_script_root(scripts):
                return str(candidate), str(scripts)

    fail(
        "No STM32-capable upstream/system OpenOCD found. "
        "ESP-IDF's openocd-esp32 must not be used for ST-Link. "
        "Set STM32_OPENOCD=/path/to/openocd and "
        "STM32_OPENOCD_SCRIPTS=/path/to/share/openocd/scripts."
    )
    raise AssertionError


def run_openocd(openocd: str,
                 scripts: str,
                 commands: str,
                 stage: str) -> str:
    env = os.environ.copy()

    # ESP-IDF export.sh sets OPENOCD_SCRIPTS for openocd-esp32. Do not let
    # that script tree leak into the STM32 OpenOCD process.
    env.pop("OPENOCD_SCRIPTS", None)
    env.pop("OPENOCD_COMMANDS", None)

    result = subprocess.run(
        [openocd, "-s", scripts, "-c", OPENOCD_SETUP + commands],
        cwd=ROOT,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env=env,
    )
    print(result.stdout, end="")
    if result.returncode != 0:
        fail(f"{stage}: OpenOCD returned {result.returncode}")
    return result.stdout

def decode_record(page: bytes):
    fields = struct.unpack("<13I", page[:METADATA_RECORD_SIZE])
    stored_crc = fields[12]
    computed_crc = zlib.crc32(
        page[:METADATA_RECORD_SIZE - 4]
    ) & 0xFFFFFFFF
    valid = (
        fields[0] == METADATA_MAGIC
        and fields[1] != 0
        and fields[2] == METADATA_VERSION
        and fields[3] <= 13
        and stored_crc == computed_crc
    )
    return fields, valid

def newer(candidate: int, reference: int) -> bool:
    diff = (candidate - reference) & 0xFFFFFFFF
    return diff != 0 and diff < 0x80000000

def select_metadata(data: bytes):
    a, valid_a = decode_record(data[:METADATA_PAGE])
    b, valid_b = decode_record(data[METADATA_PAGE:])

    if valid_a and valid_b:
        return b if newer(b[1], a[1]) else a
    if valid_a:
        return a
    if valid_b:
        return b
    fail("both STM32 metadata slots invalid")
    raise AssertionError

def monitor_esp32(port: str, timeout_s: float = 100.0) -> None:
    try:
        import serial  # type: ignore
    except ImportError:
        fail(
            "pyserial unavailable. Activate ESP-IDF environment first "
            "(source export.sh), then rerun."
        )

    deadline = time.monotonic() + timeout_s
    with serial.Serial(
        port=port,
        baudrate=115200,
        timeout=0.25,
    ) as ser:
        while time.monotonic() < deadline:
            raw = ser.readline()
            if not raw:
                continue

            line = raw.decode("utf-8", errors="replace").rstrip()
            print(line)

            if FAIL_MARKER in line:
                fail("ESP32 gateway reported FAIL")
            if PASS_MARKER in line:
                return

    fail("timed out waiting for ESP32 Phase-9 PASS marker")

def verify_stm32_final(openocd: str, scripts: str, candidate: bytes) -> None:
    with tempfile.TemporaryDirectory(prefix="phase9-verify-") as td:
        td_path = Path(td)
        metadata_path = td_path / "metadata.bin"
        app_path = td_path / "application.bin"

        run_openocd(
            openocd,
            scripts,
            "init; halt; "
            f"dump_image {tcl_path(metadata_path)} "
            f"0x{METADATA_ADDRESS:08X} 0x{METADATA_SIZE:X}; "
            f"dump_image {tcl_path(app_path)} "
            f"0x08006000 0x{len(candidate):X}; "
            "resume; shutdown",
            "STM32 final verification",
        )

        fields = select_metadata(metadata_path.read_bytes())
        state = fields[3]
        active_version = fields[4]
        pending_version = fields[5]
        update_id = fields[6]
        copy_offset = fields[9]
        attempts = fields[10]
        last_error = fields[11]

        print(
            f"P9_STM32_METADATA generation={fields[1]} "
            f"state={state} active_version={active_version} "
            f"pending_version={pending_version} boot_attempts={attempts} "
            f"last_error=0x{last_error:08X}"
        )

        if state != UPDATE_IDLE:
            fail(f"STM32 state={state}, expected IDLE")
        if active_version != TARGET_VERSION:
            fail(
                f"STM32 active_version={active_version}, "
                f"expected {TARGET_VERSION}"
            )
        if any((pending_version, update_id, copy_offset, attempts)):
            fail("STM32 final metadata cleanup fields are not zero")
        if last_error != 0:
            fail(f"STM32 last_error=0x{last_error:08X}")

        actual = app_path.read_bytes()
        if actual != candidate:
            mismatch = next(
                (i for i, (a, b) in enumerate(zip(actual, candidate))
                 if a != b),
                None,
            )
            fail(f"STM32 candidate mismatch at offset {mismatch}")

        print("STM32 installed candidate byte-for-byte verification: PASS")

def main() -> int:
    esp32_port = (
        os.environ.get("ESP32_PORT", "").strip()
        or os.environ.get("PORT", "").strip()
    )
    if not esp32_port:
        print(
            "Usage: make phase9-hw-test "
            "ESP32_PORT=/dev/ttyUSB0"
        )
        return 2

    stm32_openocd, stm32_openocd_scripts = resolve_stm32_openocd()
    idf = shutil.which("idf.py")

    if not idf or not os.environ.get("IDF_PATH"):
        fail(
            "ESP-IDF environment is not active. "
            "Source your ESP-IDF export.sh before running Phase 9."
        )

    for path in (STM32_BASELINE, CANDIDATE):
        if not path.is_file():
            fail(f"missing build artifact: {path}")

    candidate = CANDIDATE.read_bytes()

    print("Phase 9 test: ESP32 UART Gateway -> STM32 full OTA")
    print(
        "Wiring: ESP32 GPIO17 TX -> STM32 PA10 RX, "
        "ESP32 GPIO16 RX <- STM32 PA9 TX, GND common"
    )

    print(f"STM32 OpenOCD: {stm32_openocd}")
    print(f"STM32 OpenOCD scripts: {stm32_openocd_scripts}")
    print(f"ESP-IDF idf.py: {idf}")

    # Deterministic STM32 baseline v1, while external W25Q state may remain.
    # The gateway protocol will ABORT any foreign stale download checkpoint.
    run_openocd(
        stm32_openocd,
        stm32_openocd_scripts,
        f"program {tcl_path(STM32_BASELINE)} verify exit 0x08000000",
        "STM32 Phase-9 baseline program/verify",
    )
    run_openocd(
        stm32_openocd,
        stm32_openocd_scripts,
        "init; reset halt; "
        "flash erase_address 0x0800F800 0x800; "
        "reset run; shutdown",
        "STM32 metadata erase/baseline boot",
    )

    # Build is a Make dependency, but flash here so the test can print/validate
    # the exact ESP32 port it is using.
    result = subprocess.run(
        [idf, "-p", esp32_port, "flash"],
        cwd=GATEWAY,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env=os.environ.copy(),
    )
    print(result.stdout, end="")
    if result.returncode != 0:
        fail(f"ESP32 idf.py flash returned {result.returncode}")

    monitor_esp32(esp32_port)
    verify_stm32_final(stm32_openocd, stm32_openocd_scripts, candidate)

    print(
        "Phase 9 ESP32 UART Gateway hardware test: PASS "
        f"(candidate={len(candidate)} bytes)"
    )
    print("Final board state: STM32 confirmed application v2.")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
