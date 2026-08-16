#!/usr/bin/env python3
"""Phase 16 HIL: deterministic faults across secure release -> ESP32 -> STM32."""
from __future__ import annotations

import ipaddress
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import zlib

ROOT = Path(__file__).resolve().parents[1]
BOOT = ROOT / "node-stm32f103/bootloader"
APP = ROOT / "node-stm32f103/application"
GATEWAY = ROOT / "gateway-esp32"
TRUSTED_HEADER = BOOT / "include/phase14_trusted_key.h"
RUNTIME_CONFIG = GATEWAY / "main/include/phase11_runtime_config.h"
TEST_CA = GATEWAY / "main/phase11_test_ca.pem"

BASE_VERSION = 1
TARGET_VERSION = 2
KEY_ID_DEFAULT = 0x16000001
TOPIC_BASE = "sdota"
DEVICE_ID = "bluepill-001"

METADATA_ADDRESS = 0x0800F800
METADATA_SIZE = 0x800
APPLICATION_START = 0x08006000
APPLICATION_REGION_SIZE = 38 * 1024
UPDATE_IDLE = 0
ROLLBACK_DIAGNOSTIC = 0x0008B003

sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "scripts"))

import phase11_hw_test as p11  # noqa: E402
import phase14_hw_test as p14  # noqa: E402
import phase15_hw_test as p15  # noqa: E402
from server.app.services.firmware_service import (  # noqa: E402
    build_mqtt_command,
    encode_command,
)
from server.app.services.manifest_service import (  # noqa: E402
    verify_release_directory,
)


def fail(message: str) -> None:
    print(f"Phase 16 hardware test: FAIL: {message}")
    raise SystemExit(1)


def run(
    command: list[str],
    *,
    cwd: Path = ROOT,
    timeout: int = 900,
) -> str:
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
    print(result.stdout, end="")
    if result.returncode != 0:
        fail(
            f"command returned {result.returncode}: "
            + " ".join(command)
        )
    return result.stdout


def detect_host_ip() -> str:
    explicit = os.environ.get("PHASE16_HOST_IP", "").strip()
    if not explicit:
        explicit = os.environ.get("PHASE15_HOST_IP", "").strip()

    if explicit:
        try:
            return str(ipaddress.IPv4Address(explicit))
        except ipaddress.AddressValueError:
            fail(f"PHASE16_HOST_IP is not valid IPv4: {explicit}")

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.connect(("1.1.1.1", 9))
        address = sock.getsockname()[0]
    except OSError as exc:
        fail(
            "could not auto-detect PC LAN IPv4; set PHASE16_HOST_IP "
            f"manually ({exc})"
        )
    finally:
        sock.close()

    return str(ipaddress.IPv4Address(address))


def parse_port(name: str, default: int) -> int:
    raw = os.environ.get(name, "").strip()
    try:
        value = int(raw or str(default))
    except ValueError:
        fail(f"{name} must be an integer")
    if not 1 <= value <= 65535:
        fail(f"{name} must be in 1..65535")
    return value


def build_application(
    toolchain: str,
    version: int,
    label: str,
    extra_flags: str = "",
) -> Path:
    build_dir = f"build-phase16-{label}"
    out_dir = f"out-phase16-{label}"
    flags = f"-DAPPLICATION_VERSION=0x{version:08X}UL"
    if extra_flags.strip():
        flags += " " + extra_flags.strip()

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
            cwd=APP,
        )

    image = APP / out_dir / "application.bin"
    if not image.is_file():
        fail(f"application build missing: {image}")
    return image


def build_bootloader(
    toolchain: str,
    label: str,
    cflags: str,
) -> Path:
    build_dir = f"build-phase16-{label}"
    out_dir = f"out-phase16-{label}"

    for target in ("clean", "all"):
        run(
            [
                "make",
                f"TOOLCHAIN={toolchain}",
                f"BUILD_DIR={build_dir}",
                f"OUT_DIR={out_dir}",
                f"PROJECT_CFLAGS={cflags}",
                target,
            ],
            cwd=BOOT,
        )

    image = BOOT / out_dir / "bootloader.bin"
    if not image.is_file():
        fail(f"bootloader build missing: {image}")
    if image.stat().st_size > 24 * 1024:
        fail(
            f"Phase-16 {label} bootloader exceeds 24 KiB: "
            f"{image.stat().st_size}"
        )
    print(
        f"P16_BOOTLOADER=PASS label={label} "
        f"size={image.stat().st_size} cflags={cflags or '<none>'}"
    )
    return image


def create_release(
    target: Path,
    base: Path,
    private_key: Path,
    key_id: int,
    base_url: str,
    release_root: Path,
    release_id: str,
    source_revision: str,
) -> tuple[object, object]:
    run([
        sys.executable,
        "tools/phase15_release.py",
        "--target", str(target),
        "--target-version", str(TARGET_VERSION),
        "--base", str(base),
        "--base-version", str(BASE_VERSION),
        "--key", str(private_key),
        "--key-id", f"0x{key_id:08X}",
        "--base-url", base_url,
        "--release-id", release_id,
        "--output-root", str(release_root),
        "--created-utc", "2026-01-01T00:00:00Z",
        "--source-revision", source_revision,
    ])

    manifest = verify_release_directory(release_root / release_id)
    artifact, command = build_mqtt_command(
        manifest,
        BASE_VERSION,
        update_id=0xD0160000,
    )
    if artifact.kind != "delta":
        fail(
            f"{release_id} did not select a delta artifact; "
            "Phase-16 PATCHING fault requires delta coverage"
        )
    return manifest, artifact


def write_command(
    path: Path,
    *,
    update_id: int,
    target_version: int,
    size: int,
    crc32: int,
    url: str,
) -> None:
    payload = {
        "schema": 1,
        "cmd": "update",
        "update_id": update_id,
        "target_version": target_version,
        "size": size,
        "crc32": crc32,
        "url": url,
    }
    path.write_text(
        json.dumps(payload, separators=(",", ":")),
        encoding="utf-8",
    )


def monitor_gateway(
    port: str,
    *,
    expected_pass: bool,
    timeout_s: float,
    required: set[str],
) -> str:
    try:
        import serial  # type: ignore
    except ImportError:
        fail(
            "pyserial unavailable. Activate the ESP-IDF environment "
            "or install pyserial in a virtual environment."
        )

    deadline = time.monotonic() + timeout_s
    lines: list[str] = []
    observed: set[str] = set()

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
            lines.append(line)

            for marker in required:
                if marker in line:
                    observed.add(marker)

            if "P11_GATEWAY_HW_TEST=PASS" in line:
                if not expected_pass:
                    fail("gateway unexpectedly PASSed a negative fault scenario")
                missing = required - observed
                if missing:
                    fail(
                        "gateway PASS missing Phase-16 markers: "
                        + ", ".join(sorted(missing))
                    )
                return "\n".join(lines)

            if "P11_GATEWAY_HW_TEST=FAIL" in line:
                if expected_pass:
                    fail("gateway reported FAIL in a recovery scenario")
                missing = required - observed
                if missing:
                    fail(
                        "gateway FAIL arrived before required negative markers: "
                        + ", ".join(sorted(missing))
                    )
                return "\n".join(lines)

    fail(
        f"timed out after {timeout_s:.0f}s waiting for expected "
        f"{'PASS' if expected_pass else 'FAIL'} marker"
    )
    raise AssertionError


def dump_stm32(
    openocd: str,
    scripts: str,
    directory: Path,
    label: str,
) -> tuple[tuple[int, ...], bytes]:
    metadata_path = directory / f"{label}-metadata.bin"
    app_path = directory / f"{label}-application-region.bin"

    p14.run_openocd(
        openocd,
        scripts,
        "init; halt; "
        f"dump_image {p14.tcl_path(metadata_path)} "
        f"0x{METADATA_ADDRESS:08X} 0x{METADATA_SIZE:X}; "
        f"dump_image {p14.tcl_path(app_path)} "
        f"0x{APPLICATION_START:08X} 0x{APPLICATION_REGION_SIZE:X}; "
        "resume; shutdown",
        f"Phase-16 {label} STM32 snapshot",
    )

    fields = p11.select_metadata(metadata_path.read_bytes())
    region = app_path.read_bytes()
    if len(region) != APPLICATION_REGION_SIZE:
        fail(
            f"{label}: STM32 application-region dump size={len(region)}, "
            f"expected={APPLICATION_REGION_SIZE}"
        )

    print(
        f"P16_STM32_METADATA label={label} generation={fields[1]} "
        f"state={fields[3]} active_version={fields[4]} "
        f"pending_version={fields[5]} boot_attempts={fields[10]} "
        f"last_error=0x{fields[11]:08X}"
    )
    return fields, region


def verify_state(
    label: str,
    fields: tuple[int, ...],
    region: bytes,
    *,
    expected_version: int,
    expected_prefix: bytes,
    expected_last_error: int | None,
    baseline_region: bytes | None = None,
) -> None:
    if fields[3] != UPDATE_IDLE:
        fail(f"{label}: state={fields[3]}, expected IDLE")
    if fields[4] != expected_version:
        fail(
            f"{label}: active_version={fields[4]}, "
            f"expected={expected_version}"
        )
    if any((fields[5], fields[6], fields[7], fields[8], fields[9], fields[10])):
        fail(f"{label}: persistent update fields were not cleaned")
    if expected_last_error is not None and fields[11] != expected_last_error:
        fail(
            f"{label}: last_error=0x{fields[11]:08X}, "
            f"expected=0x{expected_last_error:08X}"
        )

    if region[:len(expected_prefix)] != expected_prefix:
        mismatch = next(
            (
                i
                for i, (left, right)
                in enumerate(zip(region, expected_prefix))
                if left != right
            ),
            None,
        )
        fail(f"{label}: application prefix mismatch at offset {mismatch}")

    if baseline_region is not None and region != baseline_region:
        mismatch = next(
            (
                i
                for i, (left, right)
                in enumerate(zip(region, baseline_region))
                if left != right
            ),
            None,
        )
        fail(
            f"{label}: 38 KiB fail-safe/rollback region mismatch "
            f"at offset {mismatch}"
        )

    print(f"P16_STM32_VERIFY=PASS label={label} app=v{expected_version}")


def validate_broker_output(
    output: str,
    *,
    expected_final: str,
    expect_disconnect_fault: bool,
) -> None:
    required = [
        "P16_BROKER_TLS=PASS",
        "P16_BROKER_CONNECT=PASS",
        "P16_BROKER_SUBSCRIBE=PASS",
        "P16_BROKER_COMMAND_SENT=PASS",
        "P16_BROKER_COMMAND_ACK=PASS",
        f"P16_BROKER_FINAL=PASS state={expected_final}",
        f"P16_BROKER_RESULT=PASS final={expected_final}",
    ]
    if expect_disconnect_fault:
        required.append("P16_MQTT_FAULT=DISCONNECT")

    missing = [token for token in required if token not in output]
    if missing:
        fail(
            "Phase-16 MQTT transcript missing: "
            + ", ".join(missing)
        )


def program_baseline(
    openocd: str,
    scripts: str,
    bootloader: Path,
    sanitizer: Path,
    base: Path,
    temp: Path,
    label: str,
) -> bytes:
    sanitizer_image = temp / f"{label}-extflash-sanitizer.bin"
    baseline = temp / f"{label}-baseline.bin"

    run([
        sys.executable,
        "tools/merge_images.py",
        "--bootloader", str(bootloader),
        "--application", str(sanitizer),
        "--output", str(sanitizer_image),
        "--label", f"Phase 16 {label} external sanitizer",
    ])

    p14.run_openocd(
        openocd,
        scripts,
        f"program {p14.tcl_path(sanitizer_image)} verify exit 0x08000000",
        f"Phase-16 {label} external-metadata sanitizer program/verify",
    )
    p14.run_openocd(
        openocd,
        scripts,
        "init; reset halt; "
        "flash erase_address 0x0800F800 0x800; "
        "reset run; sleep 18000; shutdown",
        f"Phase-16 {label} external-metadata/incoming sanitizer run",
    )
    print(
        f"P16_EXTFLASH_SANITIZE=PASS scenario={label} "
        "metadata_a_b=erased incoming=erased_verified"
    )

    run([
        sys.executable,
        "tools/merge_images.py",
        "--bootloader", str(bootloader),
        "--application", str(base),
        "--output", str(baseline),
        "--label", f"Phase 16 {label}",
    ])

    p14.run_openocd(
        openocd,
        scripts,
        f"program {p14.tcl_path(baseline)} verify exit 0x08000000",
        f"Phase-16 {label} baseline program/verify",
    )
    p14.run_openocd(
        openocd,
        scripts,
        "init; reset halt; "
        "flash erase_address 0x0800F800 0x800; "
        "reset run; sleep 3500; shutdown",
        f"Phase-16 {label} metadata erase/baseline boot",
    )

    _, region = dump_stm32(
        openocd,
        scripts,
        temp,
        f"{label}-baseline",
    )
    return region


def start_production_https(
    release_root: Path,
    host_ip: str,
    port: int,
    server_pem: Path,
    server_key: Path,
    trusted_hash: str,
) -> subprocess.Popen[str]:
    return p11.start_process(
        [
            sys.executable,
            "server/app/main.py",
            "serve",
            "--release-root", str(release_root),
            "--bind", "0.0.0.0",
            "--port", str(port),
            "--cert", str(server_pem),
            "--key", str(server_key),
            "--trusted-key-sha256", trusted_hash,
        ],
        "Phase-16 production HTTPS server",
    )


def start_fault_https(
    artifact: Path,
    host_ip: str,
    port: int,
    server_pem: Path,
    server_key: Path,
    mode: str,
) -> subprocess.Popen[str]:
    _ = host_ip
    return p11.start_process(
        [
            sys.executable,
            "tools/phase16_fault_https_server.py",
            "--bind", "0.0.0.0",
            "--port", str(port),
            "--cert", str(server_pem),
            "--key", str(server_key),
            "--artifact", str(artifact),
            "--route", "/phase16/artifact.sdot",
            "--mode", mode,
            "--truncate-after", "512",
        ],
        f"Phase-16 {mode} HTTPS fault server",
    )


def run_scenario(
    *,
    scenario: dict[str, object],
    update_id: int,
    toolchain: str,
    openocd: str,
    openocd_scripts: str,
    idf: str,
    esp32_port: str,
    host_ip: str,
    https_port: int,
    mqtt_port: int,
    release_root: Path,
    healthy_manifest: object,
    healthy_artifact: object,
    bad_manifest: object,
    bad_artifact: object,
    healthy_target: Path,
    bad_target: Path,
    sanitizer: Path,
    base: Path,
    server_pem: Path,
    server_key: Path,
    temp: Path,
    tampered_artifact: Path,
) -> tuple[tuple[int, ...], bytes]:
    label = str(scenario["id"])
    print(f"\n===== P16_SCENARIO_BEGIN id={label} =====")

    boot_flags = str(scenario.get("bootloader_cflag", ""))
    bootloader = build_bootloader(toolchain, label, boot_flags)
    baseline_region = program_baseline(
        openocd,
        openocd_scripts,
        bootloader,
        sanitizer,
        base,
        temp,
        label,
    )

    command_file = temp / f"{label}-command.json"
    https_process: subprocess.Popen[str] | None = None
    mqtt_process: subprocess.Popen[str] | None = None
    https_output = ""
    mqtt_output = ""

    expected_pass = scenario["expected_gateway"] == "pass"
    expected_final = "confirmed" if expected_pass else "failed"
    disconnect_state = str(
        scenario.get("mqtt_disconnect_on_state", "none")
    )

    if label in ("rollback-control", "rollback-reset"):
        manifest = bad_manifest
        artifact = bad_artifact
        _, command = build_mqtt_command(
            manifest,
            BASE_VERSION,
            update_id=update_id,
        )
        command_file.write_bytes(encode_command(command).rstrip(b"\n"))
        https_process = start_production_https(
            release_root,
            host_ip,
            https_port,
            server_pem,
            server_key,
            manifest.public_key_sha256,
        )
        expected_prefix = base.read_bytes()

    elif label == "tampered-signature":
        data = tampered_artifact.read_bytes()
        write_command(
            command_file,
            update_id=update_id,
            target_version=TARGET_VERSION,
            size=len(data),
            crc32=zlib.crc32(data) & 0xFFFFFFFF,
            url=f"https://{host_ip}:{https_port}/phase16/artifact.sdot",
        )
        https_process = start_fault_https(
            tampered_artifact,
            host_ip,
            https_port,
            server_pem,
            server_key,
            "normal",
        )
        expected_prefix = base.read_bytes()

    elif label == "https-truncate":
        valid = release_root / "fw-v2" / healthy_artifact.filename
        data = valid.read_bytes()
        write_command(
            command_file,
            update_id=update_id,
            target_version=TARGET_VERSION,
            size=len(data),
            crc32=zlib.crc32(data) & 0xFFFFFFFF,
            url=f"https://{host_ip}:{https_port}/phase16/artifact.sdot",
        )
        https_process = start_fault_https(
            valid,
            host_ip,
            https_port,
            server_pem,
            server_key,
            "truncate",
        )
        expected_prefix = base.read_bytes()

    else:
        _, command = build_mqtt_command(
            healthy_manifest,
            BASE_VERSION,
            update_id=update_id,
        )
        command_file.write_bytes(encode_command(command).rstrip(b"\n"))
        https_process = start_production_https(
            release_root,
            host_ip,
            https_port,
            server_pem,
            server_key,
            healthy_manifest.public_key_sha256,
        )
        expected_prefix = healthy_target.read_bytes()

    mqtt_process = p11.start_process(
        [
            sys.executable,
            "tools/phase16_mqtt_broker.py",
            "--bind", "0.0.0.0",
            "--port", str(mqtt_port),
            "--cert", str(server_pem),
            "--key", str(server_key),
            "--topic-base", TOPIC_BASE,
            "--device-id", DEVICE_ID,
            "--command-file", str(command_file),
            "--idle-timeout", "180",
            "--accept-timeout", "220",
            "--max-connections", "4",
            "--expected-final", expected_final,
            "--disconnect-on-state", disconnect_state,
        ],
        f"Phase-16 {label} MQTTS broker",
    )

    try:
        # Re-flashing an unchanged gateway is intentionally used as a reliable
        # USB auto-reset between single-shot scenarios; esptool --skip-flashed
        # keeps this fast when the image is unchanged.
        run(
            [idf, "-p", esp32_port, "flash"],
            cwd=GATEWAY,
            timeout=300,
        )

        required = {
            "P15_WIFI_PS=PASS mode=none",
            "P11_MQTT=PASS",
            "P11_COMMAND=PASS",
        }
        if label != "https-truncate":
            required.add("P11_HTTPS=PASS")
        if expected_pass:
            required.update({
                "P11_FINAL=PASS",
                "P11_PIPELINE=PASS",
                "P11_GATEWAY_HW_TEST=PASS",
            })
        else:
            required.add("P11_GATEWAY_HW_TEST=FAIL")

        monitor_gateway(
            esp32_port,
            expected_pass=expected_pass,
            timeout_s=210.0,
            required=required,
        )

        # Both confirmed and failed terminal states are QoS-1/PUBACK
        # synchronized. Give the HIL broker a bounded interval to observe
        # its terminal state and exit before transcript collection.
        if mqtt_process is not None:
            try:
                mqtt_process.wait(timeout=8.0)
            except subprocess.TimeoutExpired:
                time.sleep(0.5)

    finally:
        if mqtt_process is not None:
            mqtt_output = p11.stop_process(
                mqtt_process,
                f"Phase-16 {label} MQTTS broker",
            )
        if https_process is not None:
            https_output = p11.stop_process(
                https_process,
                f"Phase-16 {label} HTTPS server",
            )

    validate_broker_output(
        mqtt_output,
        expected_final=expected_final,
        expect_disconnect_fault=(disconnect_state != "none"),
    )

    if label == "https-truncate":
        if "P16_HTTPS_FAULT=TRUNCATE" not in https_output:
            fail("HTTPS truncation scenario did not inject the cut")
    elif label == "tampered-signature":
        if "P16_HTTPS_ARTIFACT=PASS" not in https_output:
            fail("tampered SDOT was not served byte-for-byte")
    else:
        expected_get = f'GET /releases/'
        if expected_get not in https_output:
            fail(f"{label}: production HTTPS server saw no release GET")

    fields, region = dump_stm32(
        openocd,
        openocd_scripts,
        temp,
        label,
    )

    if expected_pass:
        verify_state(
            label,
            fields,
            region,
            expected_version=TARGET_VERSION,
            expected_prefix=expected_prefix,
            expected_last_error=0,
        )
    elif label == "https-truncate":
        verify_state(
            label,
            fields,
            region,
            expected_version=BASE_VERSION,
            expected_prefix=base.read_bytes(),
            expected_last_error=0,
            baseline_region=baseline_region,
        )
    elif label == "tampered-signature":
        verify_state(
            label,
            fields,
            region,
            expected_version=BASE_VERSION,
            expected_prefix=base.read_bytes(),
            expected_last_error=None,
            baseline_region=baseline_region,
        )
        if fields[11] == 0:
            fail("tampered-signature: expected a persisted security diagnostic")
    else:
        verify_state(
            label,
            fields,
            region,
            expected_version=BASE_VERSION,
            expected_prefix=base.read_bytes(),
            expected_last_error=ROLLBACK_DIAGNOSTIC,
            baseline_region=baseline_region,
        )

    print(
        f"P16_SCENARIO=PASS id={label} generation={fields[1]} "
        f"gateway={'PASS' if expected_pass else 'EXPECTED_FAIL'}"
    )
    return fields, region


def main() -> int:
    esp32_port = (
        os.environ.get("ESP32_PORT", "").strip()
        or os.environ.get("PORT", "").strip()
    )
    wifi_ssid = os.environ.get("WIFI_SSID", "")
    wifi_password = os.environ.get("WIFI_PASSWORD", "")

    if not esp32_port or not wifi_ssid:
        print(
            "Usage: make phase16-hw-test "
            "ESP32_PORT=/dev/ttyUSB0 "
            'WIFI_SSID="your-ssid" WIFI_PASSWORD="your-password" '
            "PHASE16_HOST_IP=<PC-LAN-IP>"
        )
        return 2

    idf = shutil.which("idf.py")
    if idf is None or not os.environ.get("IDF_PATH"):
        fail("ESP-IDF environment is not active; source export.sh first")
    if shutil.which("openssl") is None:
        fail("openssl is required")

    toolchain = p14.choose_toolchain()
    openocd, openocd_scripts = p14.resolve_stm32_openocd()
    host_ip = detect_host_ip()
    https_port = parse_port("HTTPS_PORT", 8443)
    mqtt_port = parse_port("MQTT_PORT", 8883)

    raw_key_id = os.environ.get("PHASE16_KEY_ID", "").strip()
    if not raw_key_id:
        raw_key_id = f"0x{KEY_ID_DEFAULT:08X}"
    try:
        key_id = int(raw_key_id, 0)
    except ValueError:
        fail(f"PHASE16_KEY_ID invalid: {raw_key_id!r}")
    if not 1 <= key_id <= 0xFFFFFFFF:
        fail("PHASE16_KEY_ID must fit non-zero uint32")

    matrix = json.loads(
        (ROOT / "tests/fault/phase16_fault_matrix.json").read_text(
            encoding="utf-8"
        )
    )
    scenarios = matrix["scenarios"]

    print(
        "Phase 16 HIL: deterministic secure OTA fault injection "
        f"({len(scenarios)} scenarios)"
    )
    print(
        "Wiring remains Phase 15: ESP32 GPIO17 TX -> STM32 PA10 RX, "
        "GPIO16 RX <- PA9 TX, common GND, ST-Link on SWD."
    )
    print(
        f"host={host_ip} https={https_port} mqtts={mqtt_port} "
        f"key_id=0x{key_id:08X}"
    )

    original_trust = TRUSTED_HEADER.read_bytes()
    original_runtime = RUNTIME_CONFIG.read_bytes()
    original_ca = TEST_CA.read_bytes()

    results: dict[str, tuple[int, ...]] = {}

    try:
        with tempfile.TemporaryDirectory(prefix="phase16-hil-") as td:
            temp = Path(td)
            private_key = temp / "phase16-release-private.pem"

            run([
                "openssl",
                "genpkey",
                "-algorithm", "EC",
                "-pkeyopt", "ec_paramgen_curve:P-256",
                "-out", str(private_key),
            ])
            private_key.chmod(0o600)

            run([
                sys.executable,
                "tools/phase14_keytool.py",
                str(private_key),
                "--key-id", f"0x{key_id:08X}",
                "--output", str(TRUSTED_HEADER),
            ])

            sanitizer = build_application(
                toolchain,
                BASE_VERSION,
                "external-sanitizer-v1",
                "-DPHASE16_HIL_SANITIZE_EXTERNAL=1",
            )
            print(
                "P16_SANITIZER_BUILD=PASS "
                "mode=metadata-a-b-plus-incoming"
            )

            base = build_application(
                toolchain,
                BASE_VERSION,
                "base-v1",
            )
            healthy_target = build_application(
                toolchain,
                TARGET_VERSION,
                "healthy-v2",
            )
            bad_target = build_application(
                toolchain,
                TARGET_VERSION,
                "bad-v2",
                "-DPHASE8_DISABLE_TRIAL_CONFIRM=1",
            )

            ca_pem, server_pem, server_key = p11.generate_test_pki(
                temp,
                host_ip,
            )
            TEST_CA.write_bytes(ca_pem.read_bytes())

            base_url = f"https://{host_ip}:{https_port}"
            release_root = temp / "releases"

            healthy_manifest, healthy_artifact = create_release(
                healthy_target,
                base,
                private_key,
                key_id,
                base_url,
                release_root,
                "fw-v2",
                "phase16-healthy",
            )
            bad_manifest, bad_artifact = create_release(
                bad_target,
                base,
                private_key,
                key_id,
                base_url,
                release_root,
                "fw-v2-bad",
                "phase16-unconfirmed",
            )

            # Create a transport-valid but signature-invalid SDOT outside the
            # immutable verified release directory. The Phase-16 fault server
            # deliberately serves it without production release verification.
            healthy_path = (
                release_root / "fw-v2" / healthy_artifact.filename
            )
            tampered = bytearray(healthy_path.read_bytes())
            if len(tampered) < 64:
                fail("signed SDOT unexpectedly too small")
            tampered[-1] ^= 0x01
            tampered_artifact = temp / "tampered-signature.sdot"
            tampered_artifact.write_bytes(tampered)

            mqtt_uri = f"mqtts://{host_ip}:{mqtt_port}"
            test_epoch = int(time.time()) + 60
            p11.write_hardware_runtime_config(
                wifi_ssid,
                wifi_password,
                mqtt_uri,
                test_epoch,
            )
            p15.verify_runtime_mqtt_uri(mqtt_uri)

            run([
                sys.executable,
                "scripts/esp32_build_guard.py",
            ])
            run([idf, "build"], cwd=GATEWAY, timeout=900)
            p15.verify_built_gateway_mqtt_uri(mqtt_uri)

            for index, scenario in enumerate(scenarios, start=1):
                update_id = 0xD0160000 + index
                fields, _ = run_scenario(
                    scenario=scenario,
                    update_id=update_id,
                    toolchain=toolchain,
                    openocd=openocd,
                    openocd_scripts=openocd_scripts,
                    idf=idf,
                    esp32_port=esp32_port,
                    host_ip=host_ip,
                    https_port=https_port,
                    mqtt_port=mqtt_port,
                    release_root=release_root,
                    healthy_manifest=healthy_manifest,
                    healthy_artifact=healthy_artifact,
                    bad_manifest=bad_manifest,
                    bad_artifact=bad_artifact,
                    healthy_target=healthy_target,
                    bad_target=bad_target,
                    sanitizer=sanitizer,
                    base=base,
                    server_pem=server_pem,
                    server_key=server_key,
                    temp=temp,
                    tampered_artifact=tampered_artifact,
                )
                results[str(scenario["id"])] = fields

            control_generation = results["control-secure-delta"][1]
            for label in (
                "patch-reset",
                "backup-reset",
                "install-midpage-reset",
            ):
                actual = results[label][1]
                if actual != control_generation + 1:
                    fail(
                        f"{label}: fault witness generation={actual}, "
                        f"expected control+1={control_generation + 1}"
                    )
                print(
                    f"P16_FAULT_WITNESS=PASS id={label} "
                    f"generation={actual} control={control_generation}"
                )

            mqtt_generation = results["mqtt-drop-after-accepted"][1]
            if mqtt_generation != control_generation:
                fail(
                    "MQTT disconnect changed STM32 metadata generation: "
                    f"control={control_generation} mqtt={mqtt_generation}"
                )
            print(
                "P16_MQTT_ISOLATION=PASS "
                f"generation={mqtt_generation}"
            )

            rollback_control = results["rollback-control"][1]
            rollback_fault = results["rollback-reset"][1]
            if rollback_fault != rollback_control + 1:
                fail(
                    "rollback reset witness generation mismatch: "
                    f"control={rollback_control} fault={rollback_fault}"
                )
            print(
                "P16_ROLLBACK_FAULT_WITNESS=PASS "
                f"generation={rollback_fault} control={rollback_control}"
            )

    finally:
        TRUSTED_HEADER.write_bytes(original_trust)
        RUNTIME_CONFIG.write_bytes(original_runtime)
        TEST_CA.write_bytes(original_ca)

        # Never persist a HIL private key, provisioned trust anchor, Wi-Fi
        # credentials/test CA build, or fault-injection binary in the package.
        shutil.rmtree(GATEWAY / "build", ignore_errors=True)
        for path in (GATEWAY / "sdkconfig", GATEWAY / "sdkconfig.old"):
            path.unlink(missing_ok=True)

        for pattern in ("build-phase16-*", "out-phase16-*"):
            for path in APP.glob(pattern):
                shutil.rmtree(path, ignore_errors=True)
            for path in BOOT.glob(pattern):
                shutil.rmtree(path, ignore_errors=True)

    print(
        "Phase 16 fault injection/HIL hardware test: PASS "
        f"({len(scenarios)} deterministic scenarios)"
    )
    print(
        "Final board state: rollback-reset scenario restores confirmed "
        "application v1; no HIL signing private key persisted."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
