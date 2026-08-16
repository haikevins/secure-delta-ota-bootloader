#!/usr/bin/env python3
"""release pipeline HIL: signed release server -> MQTTS -> ESP32 -> HTTPS -> UART -> STM32."""
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

ROOT = Path(__file__).resolve().parents[1]
BOOT = ROOT / "node-stm32f103/bootloader"
APP = ROOT / "node-stm32f103/application"
GATEWAY = ROOT / "gateway-esp32"
TRUSTED_HEADER = BOOT / "include/trusted_key.h"
RUNTIME_CONFIG = GATEWAY / "main/include/runtime_config.h"
TEST_CA = GATEWAY / "main/test_ca.pem"

BASE_VERSION = 1
TARGET_VERSION = 2
UPDATE_ID = 0xD0150001
KEY_ID_DEFAULT = 0xC0DE0001
TOPIC_BASE = "sdota"
DEVICE_ID = "bluepill-001"

sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "scripts"))

import gateway_hil_support as gateway_support  # noqa: E402
import security_hil_support as security_support  # noqa: E402
from server.app.services.firmware_service import (  # noqa: E402
    build_mqtt_command,
    encode_command,
)
from server.app.services.manifest_service import (  # noqa: E402
    verify_release_directory,
)


def fail(message: str) -> None:
    print(f"release pipeline hardware test: FAIL: {message}")
    raise SystemExit(1)


def detect_host_ip() -> str:
    explicit = os.environ.get("SDOTA_HOST_IP", "").strip()
    if explicit:
        try:
            return str(ipaddress.IPv4Address(explicit))
        except ipaddress.AddressValueError:
            fail(f"SDOTA_HOST_IP is not valid IPv4: {explicit}")

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.connect(("1.1.1.1", 9))
        address = sock.getsockname()[0]
    except OSError as exc:
        fail(
            "could not auto-detect PC LAN IPv4; set SDOTA_HOST_IP "
            f"manually ({exc})"
        )
    finally:
        sock.close()

    try:
        return str(ipaddress.IPv4Address(address))
    except ipaddress.AddressValueError:
        fail(f"detected invalid host IPv4: {address}")
    raise AssertionError


def run(
    command: list[str],
    *,
    cwd: Path = ROOT,
    timeout: int = 600,
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


def parse_port(name: str, default: int) -> int:
    raw = os.environ.get(name, "").strip()
    try:
        value = int(raw or str(default))
    except ValueError:
        fail(f"{name} must be an integer")
    if not 1 <= value <= 65535:
        fail(f"{name} must be in 1..65535")
    return value



def verify_runtime_mqtt_uri(expected_uri: str) -> None:
    text = RUNTIME_CONFIG.read_text(encoding="utf-8")
    escaped = json.dumps(expected_uri, ensure_ascii=True)

    required = [
        "#define SDOTA_RUNTIME_OVERRIDE          1",
        f"#define SDOTA_RUNTIME_MQTT_URI          {escaped}",
        "#define SDOTA_RUNTIME_USE_TEST_CA       1",
        "#define SDOTA_RUNTIME_SINGLE_SHOT       1",
    ]

    for token in required:
        if token not in text:
            fail(
                "generated release pipeline ESP32 runtime config mismatch; "
                f"missing {token!r}"
            )

    print(f"RUNTIME_CONFIG=PASS mqtt_uri={expected_uri}")


def verify_built_gateway_mqtt_uri(expected_uri: str) -> None:
    binary = GATEWAY / "build/secure_delta_ota_gateway.bin"

    if not binary.is_file():
        fail(
            "ESP32 application binary missing after idf.py build: "
            f"{binary}"
        )

    data = binary.read_bytes()
    encoded = expected_uri.encode("ascii")

    if encoded not in data:
        fail(
            "ESP32 binary does not contain expected MQTT URI "
            f"{expected_uri!r}; stale/wrong runtime config build"
        )

    print(
        f"COMPILED_MQTT_URI=PASS mqtt_uri={expected_uri} "
        f"binary={binary.name}"
    )


def main() -> int:
    esp32_port = (
        os.environ.get("ESP32_PORT", "").strip()
        or os.environ.get("PORT", "").strip()
    )
    wifi_ssid = os.environ.get("WIFI_SSID", "")
    wifi_password = os.environ.get("WIFI_PASSWORD", "")

    if not esp32_port or not wifi_ssid:
        print(
            "Usage: make release pipeline-hw-test "
            "ESP32_PORT=/dev/ttyUSB0 "
            'WIFI_SSID="your-ssid" WIFI_PASSWORD="your-password" '
            "SDOTA_HOST_IP=<PC-LAN-IP>"
        )
        return 2

    idf = shutil.which("idf.py")
    if idf is None or not os.environ.get("IDF_PATH"):
        fail("ESP-IDF environment is not active; source export.sh first")
    if shutil.which("openssl") is None:
        fail("openssl is required")

    toolchain = security_support.choose_toolchain()
    stm32_openocd, stm32_scripts = security_support.resolve_stm32_openocd()
    host_ip = detect_host_ip()
    https_port = parse_port("HTTPS_PORT", 8443)
    mqtt_port = parse_port("MQTT_PORT", 8883)

    raw_key_id = os.environ.get("SDOTA_HIL_KEY_ID", "").strip()
    if not raw_key_id:
        raw_key_id = f"0x{KEY_ID_DEFAULT:08X}"
    try:
        key_id = int(raw_key_id, 0)
    except ValueError:
        fail(f"SDOTA_HIL_KEY_ID invalid: {raw_key_id!r}")
    if not 1 <= key_id <= 0xFFFFFFFF:
        fail("SDOTA_HIL_KEY_ID must fit non-zero uint32")

    print(
        "release pipeline HIL: signed release -> MQTTS command -> HTTPS cache -> "
        "ESP32 secure SDOT metadata -> UART -> STM32 signature/delta/install"
    )
    print(
        "Wiring: ESP32 GPIO17 TX -> STM32 PA10 RX, "
        "ESP32 GPIO16 RX <- STM32 PA9 TX, common GND; ST-Link remains on SWD."
    )
    print(f"PC release server IP: {host_ip}")
    print(f"HTTPS port: {https_port}; MQTTS port: {mqtt_port}")
    print(f"key_id=0x{key_id:08X}")

    original_trust = TRUSTED_HEADER.read_bytes()
    original_runtime = RUNTIME_CONFIG.read_bytes()
    original_ca = TEST_CA.read_bytes()

    https_process: subprocess.Popen[str] | None = None
    mqtt_process: subprocess.Popen[str] | None = None
    https_output = ""
    mqtt_output = ""

    try:
        with tempfile.TemporaryDirectory(prefix="release pipeline-hil-") as td:
            temp = Path(td)
            private_key = temp / "release-private.pem"

            run([
                "openssl",
                "genpkey",
                "-algorithm",
                "EC",
                "-pkeyopt",
                "ec_paramgen_curve:P-256",
                "-out",
                str(private_key),
            ])
            private_key.chmod(0o600)

            # Provision only the public point into the bootloader source.
            run([
                sys.executable,
                "tools/keytool.py",
                str(private_key),
                "--key-id",
                f"0x{key_id:08X}",
                "--output",
                str(TRUSTED_HEADER),
            ])

            run(["make", f"TOOLCHAIN={toolchain}", "clean"], cwd=BOOT)
            run(["make", f"TOOLCHAIN={toolchain}", "all"], cwd=BOOT)
            bootloader = BOOT / "out/bootloader.bin"
            if not bootloader.is_file():
                fail("provisioned bootloader build missing")
            if bootloader.stat().st_size > 24 * 1024:
                fail("provisioned bootloader exceeds 24 KiB")

            base = security_support.build_application(
                toolchain,
                BASE_VERSION,
                "build-release-hw-v1",
                "out-release-hw-v1",
            )
            target = security_support.build_application(
                toolchain,
                TARGET_VERSION,
                "build-release-hw-v2",
                "out-release-hw-v2",
            )

            baseline = temp / "secure-delta-ota-release-baseline.bin"
            run([
                sys.executable,
                "tools/merge_images.py",
                "--bootloader", str(bootloader),
                "--application", str(base),
                "--output", str(baseline),
                "--label", "release pipeline secure server HIL baseline",
            ])

            ca_pem, server_pem, server_key = gateway_support.generate_test_pki(
                temp,
                host_ip,
            )
            TEST_CA.write_bytes(ca_pem.read_bytes())

            base_url = f"https://{host_ip}:{https_port}"
            release_root = temp / "releases"

            run([
                sys.executable,
                "tools/release.py",
                "--target", str(target),
                "--target-version", str(TARGET_VERSION),
                "--base", str(base),
                "--base-version", str(BASE_VERSION),
                "--key", str(private_key),
                "--key-id", f"0x{key_id:08X}",
                "--base-url", base_url,
                "--release-id", f"fw-v{TARGET_VERSION}",
                "--output-root", str(release_root),
                "--created-utc", "2026-01-01T00:00:00Z",
                "--source-revision", "release pipeline-hil",
            ])

            manifest = verify_release_directory(
                release_root / f"fw-v{TARGET_VERSION}"
            )
            artifact, command = build_mqtt_command(
                manifest,
                BASE_VERSION,
                update_id=UPDATE_ID,
            )
            if artifact.kind != "delta":
                fail(
                    "HIL release did not select exact-base signed delta; "
                    "check delta savings policy"
                )

            command_file = temp / "command.json"
            command_file.write_bytes(encode_command(command).rstrip(b"\n"))

            mqtt_uri = f"mqtts://{host_ip}:{mqtt_port}"
            print(
                "SECURE_WAIT_CONFIG=PASS "
                "stm32_final_timeout_ms=120000 mqtt_idle_timeout_s=150"
            )
            test_epoch = int(time.time()) + 60
            gateway_support.write_hardware_runtime_config(
                wifi_ssid,
                wifi_password,
                mqtt_uri,
                test_epoch,
            )
            verify_runtime_mqtt_uri(mqtt_uri)

            run([
                sys.executable,
                "scripts/esp32_build_guard.py",
            ])
            run([idf, "build"], cwd=GATEWAY, timeout=900)
            verify_built_gateway_mqtt_uri(mqtt_uri)

            security_support.run_openocd(
                stm32_openocd,
                stm32_scripts,
                f"program {security_support.tcl_path(baseline)} "
                "verify exit 0x08000000",
                "STM32 release pipeline baseline program/verify",
            )
            security_support.run_openocd(
                stm32_openocd,
                stm32_scripts,
                "init; reset halt; "
                "flash erase_address 0x0800F800 0x800; "
                "reset run; shutdown",
                "STM32 release pipeline metadata erase/baseline boot",
            )

            https_process = gateway_support.start_process(
                [
                    sys.executable,
                    "server/app/main.py",
                    "serve",
                    "--release-root", str(release_root),
                    "--bind", "0.0.0.0",
                    "--port", str(https_port),
                    "--cert", str(server_pem),
                    "--key", str(server_key),
                    "--trusted-key-sha256", manifest.public_key_sha256,
                ],
                "release pipeline HTTPS release server",
            )

            mqtt_process = gateway_support.start_process(
                [
                    sys.executable,
                    "tools/mqtt_protocol.py",
                    "--bind", "0.0.0.0",
                    "--port", str(mqtt_port),
                    "--cert", str(server_pem),
                    "--key", str(server_key),
                    "--topic-base", TOPIC_BASE,
                    "--device-id", DEVICE_ID,
                    "--command-file", str(command_file),
                    "--idle-timeout", "150",
                ],
                "release pipeline MQTTS broker",
            )

            run(
                [idf, "-p", esp32_port, "flash"],
                cwd=GATEWAY,
                timeout=300,
            )

            # Gateway retains stable OTA log markers because the UART/MQTT transport
            # contract is unchanged; release pipeline changes the release artifact.
            gateway_support.monitor_esp32(esp32_port, timeout_s=180.0)

            gateway_support.verify_stm32_final(
                stm32_openocd,
                stm32_scripts,
                target.read_bytes(),
            )

            if mqtt_process is not None:
                mqtt_output = gateway_support.stop_process(
                    mqtt_process,
                    "release pipeline MQTTS broker",
                )
                mqtt_process = None
            if https_process is not None:
                https_output = gateway_support.stop_process(
                    https_process,
                    "release pipeline HTTPS release server",
                )
                https_process = None

            expected_get = (
                f'GET /releases/fw-v{TARGET_VERSION}/'
                f'{artifact.filename} HTTP/1.1" 200'
            )
            if expected_get not in https_output:
                fail(
                    "HTTPS release server did not observe selected SDOT GET: "
                    + expected_get
                )
            print("HTTPS_RELEASE_GET=PASS")

            gateway_support.validate_broker_output(mqtt_output)
            print(
                f"RELEASE_PIPELINE=PASS artifact={artifact.kind} "
                f"size={artifact.size} target=v{TARGET_VERSION} "
                f"key_id=0x{key_id:08X}"
            )

    finally:
        if mqtt_process is not None:
            gateway_support.stop_process(mqtt_process, "release pipeline MQTTS broker")
        if https_process is not None:
            gateway_support.stop_process(https_process, "release pipeline HTTPS release server")

        TRUSTED_HEADER.write_bytes(original_trust)
        RUNTIME_CONFIG.write_bytes(original_runtime)
        TEST_CA.write_bytes(original_ca)

        # Never leave an object/binary built with the ephemeral HIL key or a
        # gateway build containing temporary Wi-Fi credentials/test CA.
        subprocess.run(
            ["make", f"TOOLCHAIN={toolchain}", "clean"],
            cwd=BOOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        shutil.rmtree(GATEWAY / "build", ignore_errors=True)
        for path in (GATEWAY / "sdkconfig", GATEWAY / "sdkconfig.old"):
            path.unlink(missing_ok=True)

    print(
        "release pipeline server/release pipeline hardware test: PASS "
        "(signed release -> ESP32 -> STM32 confirmed v2)"
    )
    print(
        "Final board state: STM32 confirmed application v2 from "
        "the release pipeline signed release; release private key not persisted."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
