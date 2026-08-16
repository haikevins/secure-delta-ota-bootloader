#!/usr/bin/env python3
from __future__ import annotations

import ipaddress
import json
import os
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import zlib

ROOT = Path(__file__).resolve().parents[1]
GATEWAY = ROOT / "gateway-esp32"
STM32_BASELINE = ROOT / "dist/secure-delta-ota-baseline.bin"
CANDIDATE = (
    ROOT
    / "node-stm32f103/application/out-gateway-candidate/application.bin"
)
RUNTIME_CONFIG = GATEWAY / "main/include/runtime_config.h"
TEST_CA = GATEWAY / "main/test_ca.pem"
HTTPS_SERVER = ROOT / "tools/https_test_server.py"
MQTT_BROKER = ROOT / "tools/mqtt_protocol.py"

METADATA_ADDRESS = 0x0800F800
METADATA_SIZE = 0x800
METADATA_PAGE = 1024
METADATA_RECORD_SIZE = 52
METADATA_MAGIC = 0x424D4554
METADATA_VERSION = 1
UPDATE_IDLE = 0
TARGET_VERSION = 2

TOPIC_BASE = "sdota"
DEVICE_ID = "bluepill-001"
UPDATE_ID = 0xB00B0001

OPENOCD_SETUP = (
    "source [find interface/stlink.cfg]; "
    "transport select hla_swd; "
    "source [find target/stm32f1x.cfg]; "
    "reset_config none; "
    "adapter speed 1000; "
)

PASS_MARKER = "GATEWAY_HIL=PASS"
FAIL_MARKER = "GATEWAY_HIL=FAIL"
REQUIRED_SERIAL_MARKERS = {
    "MQTT=PASS",
    "OTA_COMMAND=PASS",
    "OTA_HTTPS=PASS",
    "OTA_FINAL=PASS",
    "GATEWAY_PIPELINE=PASS",
    PASS_MARKER,
}


def fail(message: str) -> None:
    print(f"MQTT orchestration gateway HIL support: FAIL: {message}")
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
        "No STM32-capable OpenOCD found. Set "
        "STM32_OPENOCD=/usr/bin/openocd and "
        "STM32_OPENOCD_SCRIPTS=/usr/share/openocd/scripts."
    )
    raise AssertionError


def run_openocd(openocd: str,
                 scripts: str,
                 commands: str,
                 stage: str) -> str:
    env = os.environ.copy()
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


def detect_host_ip() -> str:
    explicit = (
        os.environ.get("SDOTA_HOST_IP", "").strip()
        or os.environ.get("HTTPS_HOST_IP", "").strip()
    )
    if explicit:
        try:
            return str(ipaddress.IPv4Address(explicit))
        except ipaddress.AddressValueError:
            fail(f"host IP is not a valid IPv4 address: {explicit}")

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.connect(("1.1.1.1", 9))
        address = sock.getsockname()[0]
    except OSError as exc:
        fail(
            "could not auto-detect PC LAN IPv4; set "
            f"SDOTA_HOST_IP manually ({exc})"
        )
    finally:
        sock.close()

    try:
        return str(ipaddress.IPv4Address(address))
    except ipaddress.AddressValueError:
        fail(f"detected invalid host address: {address}")
    raise AssertionError


def run_checked(cmd: list[str],
                cwd: Path | None = None,
                stage: str = "command",
                timeout: float | None = None) -> str:
    result = subprocess.run(
        cmd,
        cwd=cwd or ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env=os.environ.copy(),
        timeout=timeout,
    )
    print(result.stdout, end="")
    if result.returncode != 0:
        fail(f"{stage} returned {result.returncode}")
    return result.stdout


def generate_test_pki(directory: Path,
                      host_ip: str) -> tuple[Path, Path, Path]:
    openssl = shutil.which("openssl")
    if not openssl:
        fail("openssl is required for MQTT orchestration TLS hardware test")

    ca_key = directory / "ca.key"
    ca_pem = directory / "ca.pem"
    server_key = directory / "server.key"
    server_csr = directory / "server.csr"
    server_pem = directory / "server.pem"
    server_ext = directory / "server.ext"

    run_checked([
        openssl,
        "req",
        "-x509",
        "-newkey", "rsa:2048",
        "-nodes",
        "-sha256",
        "-days", "2",
        "-subj", "/CN=Secure Delta OTA MQTT orchestration Test CA",
        "-keyout", str(ca_key),
        "-out", str(ca_pem),
    ], stage="generate MQTT orchestration test CA")

    run_checked([
        openssl,
        "req",
        "-new",
        "-newkey", "rsa:2048",
        "-nodes",
        "-sha256",
        "-subj", f"/CN={host_ip}",
        "-keyout", str(server_key),
        "-out", str(server_csr),
    ], stage="generate MQTT orchestration TLS server CSR")

    server_ext.write_text(
        "\n".join([
            f"subjectAltName=IP:{host_ip}",
            "basicConstraints=CA:FALSE",
            "keyUsage=digitalSignature,keyEncipherment",
            "extendedKeyUsage=serverAuth",
            "",
        ]),
        encoding="utf-8",
    )

    run_checked([
        openssl,
        "x509",
        "-req",
        "-in", str(server_csr),
        "-CA", str(ca_pem),
        "-CAkey", str(ca_key),
        "-CAcreateserial",
        "-days", "2",
        "-sha256",
        "-extfile", str(server_ext),
        "-out", str(server_pem),
    ], stage="sign MQTT orchestration TLS server certificate")

    return ca_pem, server_pem, server_key


def c_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def write_hardware_runtime_config(ssid: str,
                                  password: str,
                                  mqtt_uri: str,
                                  epoch: int) -> None:
    RUNTIME_CONFIG.write_text(
        "\n".join([
            "#ifndef SDOTA_RUNTIME_CONFIG_H",
            "#define SDOTA_RUNTIME_CONFIG_H",
            "",
            "#define SDOTA_RUNTIME_OVERRIDE          1",
            f"#define SDOTA_RUNTIME_WIFI_SSID         {c_string(ssid)}",
            f"#define SDOTA_RUNTIME_WIFI_PASSWORD     {c_string(password)}",
            f"#define SDOTA_RUNTIME_MQTT_URI          {c_string(mqtt_uri)}",
            "#define SDOTA_RUNTIME_USE_TEST_CA       1",
            f"#define SDOTA_RUNTIME_TEST_EPOCH        {epoch}LL",
            "#define SDOTA_RUNTIME_SINGLE_SHOT       1",
            "",
            "#endif",
            "",
        ]),
        encoding="utf-8",
    )


def start_process(cmd: list[str],
                  name: str) -> subprocess.Popen[str]:
    process = subprocess.Popen(
        cmd,
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env=os.environ.copy(),
    )

    time.sleep(0.7)
    if process.poll() is not None:
        output = process.stdout.read() if process.stdout else ""
        fail(f"{name} exited early:\n{output}")

    return process


def stop_process(process: subprocess.Popen[str],
                 name: str) -> str:
    if process.poll() is None:
        process.terminate()
        try:
            output, _ = process.communicate(timeout=4)
        except subprocess.TimeoutExpired:
            process.kill()
            output, _ = process.communicate(timeout=4)
    else:
        output = process.stdout.read() if process.stdout else ""

    if output:
        print(output, end="")
    return output


def monitor_esp32(port: str, timeout_s: float = 150.0) -> set[str]:
    try:
        import serial  # type: ignore
    except ImportError:
        fail(
            "pyserial unavailable. Activate the ESP-IDF environment "
            "or install pyserial in a virtual environment."
        )

    observed: set[str] = set()
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

            for marker in REQUIRED_SERIAL_MARKERS:
                if marker in line:
                    observed.add(marker)

            if FAIL_MARKER in line:
                fail("ESP32 gateway reported FAIL")

            if PASS_MARKER in line:
                missing = REQUIRED_SERIAL_MARKERS - observed
                if missing:
                    fail(
                        "MQTT orchestration PASS arrived without required markers: "
                        f"{sorted(missing)}"
                    )
                return observed

    fail("timed out waiting for ESP32 MQTT orchestration PASS marker")
    raise AssertionError


def verify_stm32_final(openocd: str,
                       scripts: str,
                       candidate: bytes) -> None:
    with tempfile.TemporaryDirectory(prefix="MQTT orchestration-verify-") as td:
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
            "STM32 final MQTT orchestration verification",
        )

        fields = select_metadata(metadata_path.read_bytes())
        state = fields[3]
        active_version = fields[4]
        pending_version = fields[5]
        active_update_id = fields[6]
        copy_offset = fields[9]
        attempts = fields[10]
        last_error = fields[11]

        print(
            f"STM32_METADATA generation={fields[1]} "
            f"state={state} active_version={active_version} "
            f"pending_version={pending_version} "
            f"boot_attempts={attempts} "
            f"last_error=0x{last_error:08X}"
        )

        if state != UPDATE_IDLE:
            fail(f"STM32 state={state}, expected IDLE")
        if active_version != TARGET_VERSION:
            fail(
                f"STM32 active_version={active_version}, "
                f"expected {TARGET_VERSION}"
            )
        if any((
            pending_version,
            active_update_id,
            copy_offset,
            attempts,
        )):
            fail("STM32 final metadata cleanup fields are not zero")
        if last_error != 0:
            fail(f"STM32 last_error=0x{last_error:08X}")

        actual = app_path.read_bytes()
        if actual != candidate:
            mismatch = next(
                (
                    i
                    for i, (a, b)
                    in enumerate(zip(actual, candidate))
                    if a != b
                ),
                None,
            )
            fail(f"STM32 candidate mismatch at offset {mismatch}")

        print(
            "STM32 MQTT-orchestrated candidate "
            "byte-for-byte verification: PASS"
        )


def validate_broker_output(output: str) -> None:
    required = [
        "MQTT_BROKER_TLS=PASS",
        "MQTT_BROKER_CONNECT=PASS",
        "MQTT_BROKER_SUBSCRIBE=PASS",
        "MQTT_BROKER_COMMAND_SENT=PASS",
        "MQTT_BROKER_COMMAND_ACK=PASS",
        '"state":"online"',
        '"state":"accepted"',
        '"state":"downloaded"',
        '"state":"installing"',
        '"state":"confirmed"',
        '"stage":"https"',
        '"stage":"uart"',
        "MQTT_BROKER_CONFIRMED=PASS",
        "MQTT_BROKER_RESULT=PASS",
    ]

    missing = [token for token in required if token not in output]
    if missing:
        fail(
            "MQTT broker transcript missing: "
            + ", ".join(missing)
        )

    print("MQTT orchestration transcript verification: PASS")


def main() -> int:
    esp32_port = (
        os.environ.get("ESP32_PORT", "").strip()
        or os.environ.get("PORT", "").strip()
    )
    wifi_ssid = os.environ.get("WIFI_SSID", "")
    wifi_password = os.environ.get("WIFI_PASSWORD", "")

    if not esp32_port or not wifi_ssid:
        print(
            "Usage: make MQTT orchestration-hw-test "
            "ESP32_PORT=/dev/ttyUSB0 "
            'WIFI_SSID="your-ssid" WIFI_PASSWORD="your-password"'
        )
        return 2

    try:
        https_port = int(
            os.environ.get("HTTPS_PORT", "").strip() or "8443"
        )
        mqtt_port = int(
            os.environ.get("MQTT_PORT", "").strip() or "8883"
        )
    except ValueError:
        fail("HTTPS_PORT and MQTT_PORT must be integers")

    for name, port in (
        ("HTTPS_PORT", https_port),
        ("MQTT_PORT", mqtt_port),
    ):
        if not 1 <= port <= 65535:
            fail(f"{name} must be between 1 and 65535")

    stm32_openocd, stm32_openocd_scripts = resolve_stm32_openocd()
    idf = shutil.which("idf.py")

    if not idf or not os.environ.get("IDF_PATH"):
        fail(
            "ESP-IDF environment is not active. "
            "Source your ESP-IDF export.sh before MQTT orchestration."
        )

    for path in (
        STM32_BASELINE,
        CANDIDATE,
        RUNTIME_CONFIG,
        TEST_CA,
        HTTPS_SERVER,
        MQTT_BROKER,
    ):
        if not path.is_file():
            fail(f"missing MQTT orchestration artifact: {path}")

    candidate = CANDIDATE.read_bytes()
    candidate_crc = zlib.crc32(candidate) & 0xFFFFFFFF
    host_ip = detect_host_ip()

    https_url = (
        f"https://{host_ip}:{https_port}/gateway_candidate.bin"
    )
    mqtt_uri = f"mqtts://{host_ip}:{mqtt_port}"

    print(
        "MQTT orchestration test: MQTT command -> HTTPS -> ESP32 cache "
        "-> UART -> STM32"
    )
    print(
        "Wiring: ESP32 GPIO17 TX -> STM32 PA10 RX, "
        "ESP32 GPIO16 RX <- STM32 PA9 TX, GND common"
    )
    print(f"MQTT broker URI: {mqtt_uri}")
    print(f"HTTPS artifact URL: {https_url}")
    print(f"STM32 OpenOCD: {stm32_openocd}")
    print(f"STM32 OpenOCD scripts: {stm32_openocd_scripts}")
    print(f"ESP-IDF idf.py: {idf}")

    runtime_original = RUNTIME_CONFIG.read_bytes()
    ca_original = TEST_CA.read_bytes()

    https_process: subprocess.Popen[str] | None = None
    mqtt_process: subprocess.Popen[str] | None = None
    https_output = ""
    mqtt_output = ""

    try:
        with tempfile.TemporaryDirectory(prefix="MQTT orchestration-hil-") as td:
            test_dir = Path(td)
            ca_pem, server_pem, server_key = generate_test_pki(
                test_dir,
                host_ip,
            )

            TEST_CA.write_bytes(ca_pem.read_bytes())

            command = {
                "schema": 1,
                "cmd": "update",
                "update_id": UPDATE_ID,
                "target_version": TARGET_VERSION,
                "size": len(candidate),
                "crc32": candidate_crc,
                "url": https_url,
            }
            command_file = test_dir / "command.json"
            command_file.write_text(
                json.dumps(command, separators=(",", ":")),
                encoding="utf-8",
            )

            test_epoch = int(time.time()) + 60
            write_hardware_runtime_config(
                wifi_ssid,
                wifi_password,
                mqtt_uri,
                test_epoch,
            )

            run_checked(
                [
                    sys.executable,
                    str(ROOT / "scripts/esp32_build_guard.py"),
                ],
                stage="ESP32 build-directory guard",
            )

            run_checked(
                [idf, "build"],
                cwd=GATEWAY,
                stage="ESP32 MQTT orchestration build",
                timeout=600,
            )

            run_openocd(
                stm32_openocd,
                stm32_openocd_scripts,
                f"program {tcl_path(STM32_BASELINE)} "
                "verify exit 0x08000000",
                "STM32 MQTT orchestration baseline program/verify",
            )

            run_openocd(
                stm32_openocd,
                stm32_openocd_scripts,
                "init; reset halt; "
                "flash erase_address 0x0800F800 0x800; "
                "reset run; shutdown",
                "STM32 metadata erase/baseline boot",
            )

            https_process = start_process(
                [
                    sys.executable,
                    str(HTTPS_SERVER),
                    "--bind", "0.0.0.0",
                    "--port", str(https_port),
                    "--cert", str(server_pem),
                    "--key", str(server_key),
                    "--file", str(CANDIDATE),
                    "--path", "/gateway_candidate.bin",
                ],
                "MQTT orchestration HTTPS server",
            )

            mqtt_process = start_process(
                [
                    sys.executable,
                    str(MQTT_BROKER),
                    "--bind", "0.0.0.0",
                    "--port", str(mqtt_port),
                    "--cert", str(server_pem),
                    "--key", str(server_key),
                    "--topic-base", TOPIC_BASE,
                    "--device-id", DEVICE_ID,
                    "--command-file", str(command_file),
                ],
                "MQTT test broker",
            )

            run_checked(
                [idf, "-p", esp32_port, "flash"],
                cwd=GATEWAY,
                stage="ESP32 MQTT orchestration flash",
                timeout=240,
            )

            monitor_esp32(esp32_port)
            verify_stm32_final(
                stm32_openocd,
                stm32_openocd_scripts,
                candidate,
            )

            if mqtt_process is not None:
                mqtt_output = stop_process(
                    mqtt_process,
                    "MQTT test broker",
                )
                mqtt_process = None

            if https_process is not None:
                https_output = stop_process(
                    https_process,
                    "MQTT orchestration HTTPS server",
                )
                https_process = None

            if (
                'GET /gateway_candidate.bin HTTP/1.1" 200'
                not in https_output
            ):
                fail(
                    "local HTTPS server did not observe the "
                    "MQTT orchestration firmware GET"
                )

            print("Local HTTPS server firmware GET: PASS")
            validate_broker_output(mqtt_output)

    finally:
        if mqtt_process is not None:
            stop_process(mqtt_process, "MQTT test broker")
        if https_process is not None:
            stop_process(https_process, "MQTT orchestration HTTPS server")

        RUNTIME_CONFIG.write_bytes(runtime_original)
        TEST_CA.write_bytes(ca_original)

    print(
        "Gateway HIL hardware test: PASS "
        f"(candidate={len(candidate)} bytes, "
        f"crc32=0x{candidate_crc:08X})"
    )
    print("Final board state: STM32 confirmed application v2.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
