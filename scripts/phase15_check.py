#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import ssl
import struct
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "node-stm32f103/application"

sys.path.insert(0, str(ROOT))

from server.app.services.firmware_service import build_mqtt_command  # noqa: E402
from server.app.services.manifest_service import (  # noqa: E402
    ManifestError,
    verify_release_directory,
)


def fail(message: str) -> None:
    print(f"Phase 15 check: FAIL: {message}")
    raise SystemExit(1)


def run(
    command: list[str],
    *,
    cwd: Path = ROOT,
    timeout: int = 180,
    expect_success: bool = True,
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
    if expect_success and result.returncode != 0:
        fail(
            f"command returned {result.returncode}: "
            + " ".join(command)
        )
    if not expect_success and result.returncode == 0:
        fail("negative regression unexpectedly succeeded: " + " ".join(command))
    return result.stdout


def choose_toolchain() -> str:
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
    fail("no supported STM32 ARM toolchain found")
    raise AssertionError


def build_application(
    toolchain: str,
    version: int,
    build_dir: str,
    out_dir: str,
) -> Path:
    flags = f"-DAPPLICATION_VERSION=0x{version:08X}UL"
    run(
        [
            "make",
            f"TOOLCHAIN={toolchain}",
            f"BUILD_DIR={build_dir}",
            f"OUT_DIR={out_dir}",
            f"PROJECT_CFLAGS={flags}",
            "clean",
        ],
        cwd=APP,
    )
    run(
        [
            "make",
            f"TOOLCHAIN={toolchain}",
            f"BUILD_DIR={build_dir}",
            f"OUT_DIR={out_dir}",
            f"PROJECT_CFLAGS={flags}",
            "all",
        ],
        cwd=APP,
    )
    image = APP / out_dir / "application.bin"
    if not image.is_file():
        fail(f"application build missing: {image}")
    return image


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def generate_test_pki(directory: Path) -> tuple[Path, Path, Path]:
    openssl = shutil.which("openssl")
    if openssl is None:
        fail("openssl is required")

    ca_key = directory / "ca.key"
    ca_pem = directory / "ca.pem"
    server_key = directory / "server.key"
    server_csr = directory / "server.csr"
    server_pem = directory / "server.pem"
    ext = directory / "server.ext"

    run([
        openssl,
        "req",
        "-x509",
        "-newkey", "rsa:2048",
        "-nodes",
        "-sha256",
        "-days", "2",
        "-subj", "/CN=Secure Delta OTA Phase15 Test CA",
        "-addext", "basicConstraints=critical,CA:TRUE",
        "-addext", "keyUsage=critical,keyCertSign,cRLSign",
        "-addext", "subjectKeyIdentifier=hash",
        "-keyout", str(ca_key),
        "-out", str(ca_pem),
    ])

    run([
        openssl,
        "req",
        "-new",
        "-newkey", "rsa:2048",
        "-nodes",
        "-sha256",
        "-subj", "/CN=127.0.0.1",
        "-keyout", str(server_key),
        "-out", str(server_csr),
    ])

    ext.write_text(
        "\n".join([
            "subjectAltName=IP:127.0.0.1",
            "basicConstraints=CA:FALSE",
            "keyUsage=digitalSignature,keyEncipherment",
            "extendedKeyUsage=serverAuth",
            "",
        ]),
        encoding="utf-8",
    )

    run([
        openssl,
        "x509",
        "-req",
        "-in", str(server_csr),
        "-CA", str(ca_pem),
        "-CAkey", str(ca_key),
        "-CAcreateserial",
        "-days", "2",
        "-sha256",
        "-extfile", str(ext),
        "-out", str(server_pem),
    ])

    return ca_pem, server_pem, server_key


def wait_https(url: str, cafile: Path, timeout: float = 30.0) -> bytes:
    context = ssl.create_default_context(cafile=str(cafile))
    deadline = time.monotonic() + timeout
    last: Exception | None = None

    while time.monotonic() < deadline:
        try:
            with urllib.request.urlopen(
                url,
                context=context,
                timeout=1.0,
            ) as response:
                if response.status == 200:
                    return response.read()
        except Exception as exc:
            last = exc
            time.sleep(0.1)

    fail(f"HTTPS server did not become ready: {last}")
    raise AssertionError


def recv_exact(sock: ssl.SSLSocket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise EOFError("MQTT peer closed")
        data.extend(chunk)
    return bytes(data)


def recv_mqtt(sock: ssl.SSLSocket) -> tuple[int, bytes]:
    first = recv_exact(sock, 1)[0]
    remaining = 0
    multiplier = 1
    for _ in range(4):
        digit = recv_exact(sock, 1)[0]
        remaining += (digit & 0x7F) * multiplier
        if (digit & 0x80) == 0:
            return first, recv_exact(sock, remaining)
        multiplier *= 128
    raise ValueError("bad MQTT remaining length")


def read_utf8(body: bytes, offset: int) -> tuple[str, int]:
    if offset + 2 > len(body):
        raise ValueError("truncated MQTT string")
    length = struct.unpack_from(">H", body, offset)[0]
    offset += 2
    end = offset + length
    if end > len(body):
        raise ValueError("truncated MQTT payload")
    return body[offset:end].decode("utf-8"), end


def mqtt_broker_once(
    port: int,
    cert: Path,
    key: Path,
    received: dict[str, object],
    ready: threading.Event,
) -> None:
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(str(cert), str(key))

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(("127.0.0.1", port))
        listener.listen(1)
        ready.set()

        raw, _peer = listener.accept()
        with raw:
            with context.wrap_socket(raw, server_side=True) as sock:
                sock.settimeout(8.0)

                first, _body = recv_mqtt(sock)
                if first >> 4 != 1:
                    raise ValueError("first MQTT packet is not CONNECT")
                sock.sendall(b"\x20\x02\x00\x00")

                first, body = recv_mqtt(sock)
                if first >> 4 != 3:
                    raise ValueError("second MQTT packet is not PUBLISH")

                topic, offset = read_utf8(body, 0)
                qos = (first >> 1) & 0x03
                if qos != 1:
                    raise ValueError("PUBLISH is not QoS1")
                if offset + 2 > len(body):
                    raise ValueError("missing PUBLISH packet id")
                packet_id = struct.unpack_from(">H", body, offset)[0]
                offset += 2

                received["topic"] = topic
                received["payload"] = body[offset:].decode("utf-8")

                sock.sendall(
                    b"\x40\x02" + struct.pack(">H", packet_id)
                )

                first, _body = recv_mqtt(sock)
                if first >> 4 != 14:
                    raise ValueError("MQTT client did not DISCONNECT")


def compile_gateway_contract(temp: Path) -> None:
    cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
    if cc is None:
        fail("host C compiler is required")

    output = temp / "phase15-gateway-contract"
    run([
        cc,
        "-std=c11",
        "-O2",
        "-Wall",
        "-Wextra",
        "-Wpedantic",
        "-Werror",
        "-Igateway-esp32/components/secure_container_meta/include",
        "-Igateway-esp32/components/uart_ota/include",
        "gateway-esp32/components/secure_container_meta/secure_container_meta.c",
        "gateway-esp32/components/uart_ota/uart_ota_protocol.c",
        "tests/host/test_phase15_gateway_contract.c",
        "-o",
        str(output),
    ])
    run([str(output)])


def static_contract_checks() -> None:
    required = [
        "tools/phase15_release.py",
        "server/app/main.py",
        "server/app/models/sdot.py",
        "server/app/models/release.py",
        "server/app/services/signing_service.py",
        "server/app/services/manifest_service.py",
        "server/app/services/firmware_service.py",
        "server/app/services/mqtt_service.py",
        "server/app/services/https_service.py",
        "server/schemas/phase15-release-manifest.schema.json",
        "gateway-esp32/components/secure_container_meta/secure_container_meta.c",
        "gateway-esp32/components/secure_container_meta/include/secure_container_meta.h",
        "tests/host/test_phase15_gateway_contract.c",
        ".github/workflows/firmware-release.yml",
        "docs/phase-15-server-release-pipeline.md",
        "docs/phase-15-checklist.md",
        "PHASE15_REPORT.md",
    ]
    missing = [rel for rel in required if not (ROOT / rel).is_file()]
    if missing:
        fail("missing Phase-15 files: " + ", ".join(missing))

    gateway = (ROOT / "gateway-esp32/main/gateway_manager.c").read_text()
    mqtt = (
        ROOT
        / "gateway-esp32/components/mqtt_orchestrator/mqtt_orchestrator.c"
    ).read_text()
    uart = (
        ROOT
        / "gateway-esp32/components/uart_ota/uart_ota_client.c"
    ).read_text()

    for token in [
        "STM32_INCOMING_ARTIFACT_MAX_SIZE (128UL * 1024UL)",
        "download_config.max_image_size = STM32_INCOMING_ARTIFACT_MAX_SIZE",
    ]:
        if token not in gateway:
            fail(f"ESP32 signed-container size integration missing: {token}")

    if "128UL * 1024UL" not in mqtt:
        fail("MQTT command size limit was not raised to incoming partition size")

    for token in [
        "artifact->artifact_type",
        "artifact->base_version",
        "artifact->container_header_size",
        "UART_OTA_CAP_SIGNATURE_VERIFY",
    ]:
        if token not in uart:
            fail(f"ESP32 UART secure START integration missing: {token}")

    server_main = (ROOT / "server/app/main.py").read_text(encoding="utf-8")
    manifest_service = (
        ROOT / "server/app/services/manifest_service.py"
    ).read_text(encoding="utf-8")
    release_workflow = (
        ROOT / ".github/workflows/firmware-release.yml"
    ).read_text(encoding="utf-8")
    for token in [
        "PHASE15_TRUSTED_KEY_SHA256",
        "trusted_key_hash(args, required=True)",
        "--trusted-key-sha256",
    ]:
        if token not in server_main:
            fail(f"release-server trust pin missing: {token}")
    if "pinned server trust anchor" not in manifest_service:
        fail("manifest verifier does not enforce external release-key pin")

    for token in [
        "environment: firmware-production",
        "PHASE15_SIGNING_KEY_PEM",
        "PHASE15_TRUSTED_KEY_SHA256",
        'gh release download "fw-v${BASE_VERSION}"',
        'gh release create "$RELEASE_ID"',
        "make phase15-check TOOLCHAIN=gcc",
    ]:
        if token not in release_workflow:
            fail(f"protected release workflow contract missing: {token}")


def main() -> int:
    wifi_station_source = (
        ROOT / "gateway-esp32/components/wifi_station/wifi_station.c"
    ).read_text(encoding="utf-8")

    for token in [
        "esp_wifi_set_ps(WIFI_PS_NONE)",
        "esp_wifi_get_ps(&power_save)",
        "P15_WIFI_PS=PASS mode=none",
    ]:
        if token not in wifi_station_source:
            fail(f"Phase-15 Wi-Fi power-save regression: missing {token}")

    print("Phase 15 Wi-Fi power-save regression: PASS (WIFI_PS_NONE)")

    uart_client = (
        ROOT / "gateway-esp32/components/uart_ota/uart_ota_client.c"
    ).read_text(encoding="utf-8")
    broker_source = (
        ROOT / "tools/phase11_mqtt_broker.py"
    ).read_text(encoding="utf-8")
    phase15_hw_source = (
        ROOT / "scripts/phase15_hw_test.py"
    ).read_text(encoding="utf-8")

    if "#define UART_OTA_FINAL_TIMEOUT_MS    120000UL" not in uart_client:
        fail("Phase-15 secure-wait timeout regression: STM32 final wait")
    if "conn.settimeout(args.idle_timeout)" not in broker_source:
        fail("Phase-15 secure-wait timeout regression: MQTT broker idle timeout")
    if '"--idle-timeout", "150"' not in phase15_hw_source:
        fail("Phase-15 secure-wait timeout regression: HIL broker configuration")

    print(
        "Phase 15 secure-wait timeout regression: PASS "
        "(STM32=120s MQTT-idle=150s)"
    )


    phase15_hw = (
        ROOT / "scripts/phase15_hw_test.py"
    ).read_text(encoding="utf-8")
    gateway_manager = (
        ROOT / "gateway-esp32/main/gateway_manager.c"
    ).read_text(encoding="utf-8")

    for token in [
        "def verify_runtime_mqtt_uri(",
        "def verify_built_gateway_mqtt_uri(",
        "P15_RUNTIME_CONFIG=PASS",
        "P15_COMPILED_MQTT_URI=PASS",
        "build/secure_delta_ota_gateway.bin",
    ]:
        if token not in phase15_hw:
            fail(f"Phase-15 MQTT URI build guard missing: {token}")

    if "P15_RUNTIME_MQTT_URI=%s" not in gateway_manager:
        fail("ESP32 runtime MQTT URI diagnostic marker missing")

    static_contract_checks()

    for tool in ("openssl",):
        if shutil.which(tool) is None:
            fail(f"required tool missing: {tool}")

    run([
        "python3",
        "-m",
        "py_compile",
        "tools/phase15_release.py",
        "server/app/main.py",
        "server/app/models/sdot.py",
        "server/app/models/release.py",
        "server/app/services/signing_service.py",
        "server/app/services/manifest_service.py",
        "server/app/services/firmware_service.py",
        "server/app/services/mqtt_service.py",
        "server/app/services/https_service.py",
        "scripts/phase15_check.py",
        "scripts/phase15_hw_test.py",
        "tools/merge_images.py",
    ])

    toolchain = choose_toolchain()

    with tempfile.TemporaryDirectory(prefix="phase15-check-") as td:
        temp = Path(td)
        compile_gateway_contract(temp)

        key = temp / "release-private.pem"
        run([
            "openssl",
            "genpkey",
            "-algorithm",
            "EC",
            "-pkeyopt",
            "ec_paramgen_curve:P-256",
            "-out",
            str(key),
        ])
        key.chmod(0o600)

        base = build_application(
            toolchain,
            1,
            "build-phase15-base",
            "out-phase15-base",
        )
        target = build_application(
            toolchain,
            2,
            "build-phase15-target",
            "out-phase15-target",
        )

        release_root = temp / "releases"
        base_url = "https://127.0.0.1:9443"

        run([
            "python3",
            "tools/phase15_release.py",
            "--target", str(target),
            "--target-version", "2",
            "--base", str(base),
            "--base-version", "1",
            "--key", str(key),
            "--key-id", "0x15000001",
            "--base-url", base_url,
            "--release-id", "fw-v2",
            "--output-root", str(release_root),
            "--created-utc", "2026-01-01T00:00:00Z",
            "--source-revision", "phase15-host-check",
            "--min-delta-savings-percent", "20",
        ])

        directory = release_root / "fw-v2"
        manifest = verify_release_directory(directory)

        delta, command = build_mqtt_command(
            manifest,
            1,
            update_id=0xD0150001,
        )
        if delta.kind != "delta":
            fail("exact-base device did not select delta artifact")
        if command["url"] != delta.url:
            fail("delta command URL mismatch")

        full, _full_command = build_mqtt_command(
            manifest,
            0,
            update_id=0xD0150002,
        )
        if full.kind != "full":
            fail("device without exact delta base did not select full artifact")

        print(
            f"Phase 15 release selection: PASS "
            f"delta={delta.size} full={full.size}"
        )

        try:
            verify_release_directory(
                directory,
                trusted_public_key_sha256="0" * 64,
            )
        except ManifestError as exc:
            if "pinned server trust anchor" not in str(exc):
                fail(f"unexpected release-key pin rejection: {exc}")
        else:
            fail("wrong pinned release public key was accepted")
        verify_release_directory(
            directory,
            trusted_public_key_sha256=manifest.public_key_sha256,
        )
        print("Phase 15 pinned release-key authorization: PASS")

        # Immutable release: duplicate creation must fail before overwrite.
        duplicate = subprocess.run(
            [
                "python3",
                "tools/phase15_release.py",
                "--target", str(target),
                "--target-version", "2",
                "--base", str(base),
                "--base-version", "1",
                "--key", str(key),
                "--key-id", "0x15000001",
                "--base-url", base_url,
                "--release-id", "fw-v2",
                "--output-root", str(release_root),
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        if duplicate.returncode == 0 or "immutable" not in duplicate.stdout:
            fail("duplicate immutable release was not rejected")
        print("Phase 15 immutable-release overwrite rejection: PASS")

        # Key custody: a private key inside the repository must be rejected.
        repo_key = ROOT / ".phase15-private-key-negative.pem"
        try:
            shutil.copyfile(key, repo_key)
            repo_key.chmod(0o600)
            negative_root = temp / "negative-releases"
            negative = subprocess.run(
                [
                    "python3",
                    "tools/phase15_release.py",
                    "--target", str(target),
                    "--target-version", "2",
                    "--key", str(repo_key),
                    "--key-id", "0x15000001",
                    "--base-url", base_url,
                    "--release-id", "negative",
                    "--output-root", str(negative_root),
                ],
                cwd=ROOT,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                check=False,
            )
            if (
                negative.returncode == 0
                or "outside the repository" not in negative.stdout
            ):
                fail("repository-local private signing key was not rejected")
        finally:
            repo_key.unlink(missing_ok=True)
        print("Phase 15 private-key custody policy: PASS")

        ca, server_cert, server_key = generate_test_pki(temp)
        https_port = free_port()

        server = subprocess.Popen(
            [
                "python3",
                "server/app/main.py",
                "serve",
                "--release-root", str(release_root),
                "--bind", "127.0.0.1",
                "--port", str(https_port),
                "--cert", str(server_cert),
                "--key", str(server_key),
                "--trusted-key-sha256", manifest.public_key_sha256,
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            env=os.environ.copy(),
        )
        try:
            manifest_url = (
                f"https://127.0.0.1:{https_port}/"
                "releases/fw-v2/manifest.json"
            )
            served_manifest = wait_https(manifest_url, ca)
            if hashlib.sha256(served_manifest).hexdigest() != hashlib.sha256(
                (directory / "manifest.json").read_bytes()
            ).hexdigest():
                fail("HTTPS manifest byte comparison failed")

            artifact_url = (
                f"https://127.0.0.1:{https_port}/"
                f"releases/fw-v2/{delta.filename}"
            )
            served_artifact = wait_https(artifact_url, ca)
            if served_artifact != (directory / delta.filename).read_bytes():
                fail("HTTPS artifact byte comparison failed")
            print("Phase 15 verified TLS release serving: PASS")
        finally:
            server.terminate()
            try:
                output, _ = server.communicate(timeout=4)
            except subprocess.TimeoutExpired:
                server.kill()
                output, _ = server.communicate(timeout=4)
            if output:
                print(output, end="")

        mqtt_port = free_port()
        received: dict[str, object] = {}
        ready = threading.Event()
        broker = threading.Thread(
            target=mqtt_broker_once,
            args=(
                mqtt_port,
                server_cert,
                server_key,
                received,
                ready,
            ),
            daemon=True,
        )
        broker.start()
        if not ready.wait(timeout=3):
            fail("test MQTT broker did not start")

        publish_output = run([
            "python3",
            "server/app/main.py",
            "publish",
            "--release-root", str(release_root),
            "--release-id", "fw-v2",
            "--current-version", "1",
            "--update-id", "0xD0150003",
            "--device-id", "bluepill-001",
            "--broker-uri", f"mqtts://127.0.0.1:{mqtt_port}",
            "--ca", str(ca),
            "--trusted-key-sha256", manifest.public_key_sha256,
        ])
        broker.join(timeout=5)
        if broker.is_alive():
            fail("test MQTT broker did not finish")

        expected_topic = "sdota/bluepill-001/command"
        if received.get("topic") != expected_topic:
            fail(f"MQTT topic mismatch: {received.get('topic')}")
        try:
            published = json.loads(str(received.get("payload")))
        except json.JSONDecodeError as exc:
            fail(f"published MQTT payload invalid JSON: {exc}")
        if (
            published.get("url") != delta.url
            or published.get("size") != delta.size
            or published.get("crc32") != delta.crc32
            or published.get("target_version") != 2
        ):
            fail("published MQTT command does not match selected delta")
        if "PHASE15_MQTT_PUBLISH=PASS" not in publish_output:
            fail("MQTT publish PASS marker missing")
        print("Phase 15 MQTTS command publication QoS1/PUBACK: PASS")

        # Tamper one release artifact and ensure publication verification fails.
        artifact_path = directory / delta.filename
        original = artifact_path.read_bytes()
        tampered = bytearray(original)
        tampered[-1] ^= 0x01
        artifact_path.write_bytes(tampered)
        try:
            try:
                verify_release_directory(directory)
            except ManifestError:
                pass
            else:
                fail("tampered release artifact was accepted")
        finally:
            artifact_path.write_bytes(original)
        verify_release_directory(directory)
        print("Phase 15 tampered-release verification rejection: PASS")

        print("Secure Delta OTA Phase 15 server/release pipeline check: PASS")
        print(f"Release target: v{manifest.target_version}")
        print(f"Release key_id: 0x{manifest.key_id:08X}")
        print(
            f"Release artifacts: "
            + ", ".join(
                f"{artifact.kind}={artifact.size}"
                for artifact in manifest.artifacts
            )
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
