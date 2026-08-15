#!/usr/bin/env python3
"""Phase 14 physical HIL test: signed delta + signature rejection + signed full."""
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
BOOT = ROOT / "node-stm32f103/bootloader"
APP = ROOT / "node-stm32f103/application"
TRUSTED_HEADER = BOOT / "include/phase14_trusted_key.h"

APP_ADDRESS = 0x08006000
METADATA_ADDRESS = 0x0800F800
METADATA_SIZE = 0x800
METADATA_PAGE = 1024
METADATA_RECORD_SIZE = 52
METADATA_MAGIC = 0x424D4554
METADATA_VERSION = 1

UPDATE_IDLE = 0
BASE_VERSION = 1
DELTA_TARGET_VERSION = 2
FULL_TARGET_VERSION = 3
KEY_ID_DEFAULT = 0x14000001

UPDATE_ID_DELTA = 0xD0140001
UPDATE_ID_TAMPER = 0xD0140002
UPDATE_ID_FULL = 0xD0140003
UPDATE_ID_UNSIGNED = 0xD0140004
UPDATE_ID_DOWNGRADE = 0xD0140005

SECURE_CONTAINER_ERROR_BASE = 0x00140000
SECURE_CONTAINER_SIGNATURE_INVALID_REASON = 12
EXPECTED_SIGNATURE_ERROR = (
    SECURE_CONTAINER_ERROR_BASE
    | SECURE_CONTAINER_SIGNATURE_INVALID_REASON
)

OPENOCD_SETUP = (
    "source [find interface/stlink.cfg]; "
    "transport select hla_swd; "
    "source [find target/stm32f1x.cfg]; "
    "reset_config none; "
    "adapter speed 1000; "
)

sys.path.insert(0, str(ROOT / "tools"))
from ota_uart_protocol import (  # noqa: E402
    CAP_DELTA_IMAGE,
    CAP_SIGNATURE_VERIFY,
    CMD_INSTALL,
    CMD_NACK,
    CMD_QUERY,
    CMD_START,
    FW_IMAGE_FULL,
    Packet,
    STATUS_SIGNATURE_ERROR,
    STATUS_VERSION_REJECTED,
    UPDATE_IDLE as OTA_UPDATE_IDLE,
    build_start_payload,
    crc32,
    parse_ack,
    parse_hello,
)
from uart_ota_sender import (  # noqa: E402
    PHASE14_CONTAINER_HEADER_SIZE,
    SerialLink,
    parse_phase14_secure_container,
    require_ack,
    secure_ota,
    transfer,
    wait_for_application_version,
)


def fail(message: str) -> None:
    print(f"Phase 14 hardware test: FAIL: {message}")
    raise SystemExit(1)


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


def run(
    command: list[str],
    *,
    cwd: Path = ROOT,
    timeout: int = 240,
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


def tcl_path(path: Path) -> str:
    value = str(path.resolve())
    if "}" in value:
        fail("path containing '}' is not supported")
    return "{" + value + "}"


def _path_openocd_candidates() -> list[Path]:
    result: list[Path] = []
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
            result.append(candidate)
    return result


def _script_candidates(openocd: Path) -> list[Path]:
    explicit = os.environ.get("STM32_OPENOCD_SCRIPTS", "").strip()
    candidates: list[Path] = []
    if explicit:
        candidates.append(Path(explicit))
    candidates.extend(
        [
            Path("/usr/share/openocd/scripts"),
            Path("/usr/local/share/openocd/scripts"),
            openocd.parent.parent / "share/openocd/scripts",
        ]
    )
    return candidates


def _valid_script_root(path: Path) -> bool:
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
        candidates.extend(
            [Path("/usr/bin/openocd"), Path("/usr/local/bin/openocd")]
        )
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

        lower = str(resolved).lower()
        if not explicit and (
            "openocd-esp32" in lower or "/.espressif/" in lower
        ):
            continue

        for scripts in _script_candidates(candidate):
            if _valid_script_root(scripts):
                return str(candidate), str(scripts)

    fail(
        "No STM32-capable OpenOCD found. Set "
        "STM32_OPENOCD=/usr/bin/openocd and "
        "STM32_OPENOCD_SCRIPTS=/usr/share/openocd/scripts."
    )
    raise AssertionError


def run_openocd(
    openocd: str,
    scripts: str,
    commands: str,
    stage: str,
) -> str:
    env = os.environ.copy()
    env.pop("OPENOCD_SCRIPTS", None)
    env.pop("OPENOCD_COMMANDS", None)

    result = subprocess.run(
        [openocd, "-s", scripts, "-c", OPENOCD_SETUP + commands],
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env=env,
        check=False,
    )
    print(result.stdout, end="")
    if result.returncode != 0:
        fail(f"{stage}: OpenOCD returned {result.returncode}")
    return result.stdout


def decode_record(page: bytes):
    fields = struct.unpack("<13I", page[:METADATA_RECORD_SIZE])
    stored_crc = fields[12]
    computed_crc = (
        zlib.crc32(page[:METADATA_RECORD_SIZE - 4])
        & 0xFFFFFFFF
    )
    valid = (
        fields[0] == METADATA_MAGIC
        and fields[1] != 0
        and fields[2] == METADATA_VERSION
        and fields[3] <= 13
        and stored_crc == computed_crc
    )
    return fields, valid


def newer(candidate: int, reference: int) -> bool:
    difference = (candidate - reference) & 0xFFFFFFFF
    return difference != 0 and difference < 0x80000000


def select_metadata(data: bytes):
    if len(data) != METADATA_SIZE:
        fail(
            f"metadata dump size={len(data)}, expected={METADATA_SIZE}"
        )

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


def open_link(port: str) -> SerialLink:
    return SerialLink(
        port=port,
        baud=115200,
        timeout=1.5,
        retries=5,
    )


def wait_for_baseline_uart(
    port: str,
    expected_version: int,
    wait_seconds: float = 25.0,
):
    deadline = time.monotonic() + wait_seconds
    attempt = 0
    last = "no valid QUERY response"

    while time.monotonic() < deadline:
        attempt += 1
        link = None
        try:
            link = open_link(port)
            response = link.request(
                Packet(command=CMD_QUERY, update_id=0, sequence=0)
            )
            info = parse_hello(response)
            last = (
                f"app=v{info.application_version} "
                f"state={info.update_state} "
                f"caps=0x{info.capability_flags:08X}"
            )
            print(f"P14_UART_SYNC attempt={attempt} {last}")
            if (
                info.application_version == expected_version
                and info.update_state == OTA_UPDATE_IDLE
            ):
                return link, info
        except (OSError, TimeoutError, ValueError) as exc:
            last = f"{type(exc).__name__}: {exc}"
            print(
                f"P14_UART_SYNC_RETRY attempt={attempt} detail={last}"
            )

        if link is not None:
            link.close()
        time.sleep(0.5)

    raise TimeoutError(
        f"baseline application v{expected_version} IDLE not observed "
        f"within {wait_seconds:.1f}s; last={last}"
    )


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


def verify_board(
    openocd: str,
    scripts: str,
    expected_version: int,
    expected_image: bytes,
    *,
    expected_last_error: int,
    label: str,
) -> None:
    with tempfile.TemporaryDirectory(prefix="phase14-stlink-") as td:
        temp = Path(td)
        metadata_path = temp / "metadata.bin"
        application_path = temp / "application.bin"

        run_openocd(
            openocd,
            scripts,
            "init; halt; "
            f"dump_image {tcl_path(metadata_path)} "
            f"0x{METADATA_ADDRESS:08X} 0x{METADATA_SIZE:X}; "
            f"dump_image {tcl_path(application_path)} "
            f"0x{APP_ADDRESS:08X} 0x{len(expected_image):X}; "
            "resume; shutdown",
            label,
        )

        fields = select_metadata(metadata_path.read_bytes())
        print(
            f"P14_STM32_METADATA label={label} "
            f"generation={fields[1]} state={fields[3]} "
            f"active_version={fields[4]} pending_version={fields[5]} "
            f"boot_attempts={fields[10]} "
            f"last_error=0x{fields[11]:08X}"
        )

        if fields[3] != UPDATE_IDLE:
            fail(f"{label}: state={fields[3]}, expected IDLE")
        if fields[4] != expected_version:
            fail(
                f"{label}: active_version={fields[4]}, "
                f"expected={expected_version}"
            )
        if any(
            (
                fields[5],
                fields[6],
                fields[7],
                fields[8],
                fields[9],
                fields[10],
            )
        ):
            fail(f"{label}: persistent cleanup fields are not zero")
        if fields[11] != expected_last_error:
            fail(
                f"{label}: last_error=0x{fields[11]:08X}, "
                f"expected=0x{expected_last_error:08X}"
            )

        if application_path.read_bytes() != expected_image:
            fail(f"{label}: internal application byte comparison failed")

        print(
            f"STM32 secure application byte-for-byte verification: PASS "
            f"label={label} app=v{expected_version}"
        )


def reject_unsigned_full(
    port: str,
    application: bytes,
    target_version: int,
) -> None:
    link = open_link(port)
    try:
        response = link.request(
            Packet(
                command=CMD_START,
                update_id=UPDATE_ID_UNSIGNED,
                payload=build_start_payload(
                    len(application),
                    crc32(application),
                    artifact_type=FW_IMAGE_FULL,
                    base_version=0,
                    target_version=target_version,
                    container_header_size=0,
                ),
            )
        )
        info = parse_ack(response)

        if (
            response.command != CMD_NACK
            or info.status != STATUS_SIGNATURE_ERROR
        ):
            fail(
                "unsigned full artifact was not rejected with "
                f"SIGNATURE_ERROR: command=0x{response.command:02X} "
                f"status=0x{info.status:02X}"
            )

        after = parse_hello(
            link.request(Packet(command=CMD_QUERY))
        )
        if (
            after.application_version != DELTA_TARGET_VERSION
            or after.update_state != OTA_UPDATE_IDLE
        ):
            fail("unsigned artifact rejection changed confirmed v2 state")

        print("P14_UNSIGNED_REJECT=PASS status=SIGNATURE_ERROR")
    finally:
        link.close()


def send_tampered_container(
    port: str,
    tampered: bytes,
) -> None:
    info = parse_phase14_secure_container(tampered)
    link = open_link(port)
    try:
        transfer(
            link,
            tampered,
            UPDATE_ID_TAMPER,
            target_version=info["target_version"],
            artifact_type=info["image_type"],
            base_version=info["base_version"],
            container_header_size=PHASE14_CONTAINER_HEADER_SIZE,
        )

        require_ack(
            link.request(
                Packet(
                    command=CMD_INSTALL,
                    update_id=UPDATE_ID_TAMPER,
                    offset=len(tampered),
                    sequence=0,
                )
            ),
            "tampered secure INSTALL",
        )

        final = wait_for_application_version(
            link,
            DELTA_TARGET_VERSION,
            wait_seconds=45.0,
            required_state=OTA_UPDATE_IDLE,
        )
        if final.application_version != DELTA_TARGET_VERSION:
            fail("tampered signature changed active application")

        print(
            "P14_SIGNATURE_REJECT=PASS "
            "tampered_signature_preserved_v2"
        )
    finally:
        link.close()


def reject_downgrade(port: str, signed_v2: bytes) -> None:
    info = parse_phase14_secure_container(signed_v2)
    link = open_link(port)
    try:
        hello = parse_hello(link.request(Packet(command=CMD_QUERY)))
        if (
            hello.application_version != FULL_TARGET_VERSION
            or hello.update_state != OTA_UPDATE_IDLE
        ):
            fail("downgrade test did not start from confirmed v3")

        response = link.request(
            Packet(
                command=CMD_START,
                update_id=UPDATE_ID_DOWNGRADE,
                payload=build_start_payload(
                    len(signed_v2),
                    crc32(signed_v2),
                    artifact_type=info["image_type"],
                    base_version=info["base_version"],
                    target_version=info["target_version"],
                    container_header_size=PHASE14_CONTAINER_HEADER_SIZE,
                ),
            )
        )
        nack = parse_ack(response)

        if (
            response.command != CMD_NACK
            or nack.status != STATUS_VERSION_REJECTED
        ):
            fail(
                "signed downgrade was not rejected with VERSION_REJECTED: "
                f"command=0x{response.command:02X} "
                f"status=0x{nack.status:02X}"
            )

        print("P14_DOWNGRADE_REJECT=PASS status=VERSION_REJECTED")
    finally:
        link.close()


def main() -> int:
    port = (
        os.environ.get("PORT", "").strip()
        or os.environ.get("STM32_PORT", "").strip()
    )
    if not port:
        print("Usage: make phase14-hw-test PORT=/dev/ttyUSB0")
        return 2

    if shutil.which("openssl") is None:
        fail("openssl is required")

    openocd, scripts = resolve_stm32_openocd()
    toolchain = choose_toolchain()
    raw_key_id = os.environ.get("PHASE14_KEY_ID", "").strip()
    if not raw_key_id:
        raw_key_id = f"0x{KEY_ID_DEFAULT:08X}"

    try:
        key_id = int(raw_key_id, 0)
    except ValueError:
        fail(
            "PHASE14_KEY_ID must be a valid integer, for example "
            f"0x{KEY_ID_DEFAULT:08X}; got {raw_key_id!r}"
        )

    if not 1 <= key_id <= 0xFFFFFFFF:
        fail("PHASE14_KEY_ID must fit non-zero uint32")

    original_trust = TRUSTED_HEADER.read_bytes()

    print(
        "Phase 14 HIL: signed delta v1->v2, unsigned rejection, "
        "tampered signature rejection, signed full v2->v3, downgrade rejection"
    )
    print(
        "Wiring: USB-UART TX -> STM32 PA10 RX, "
        "USB-UART RX <- STM32 PA9 TX, GND common"
    )
    print("Disconnect ESP32 from STM32 PA9/PA10 during this direct PC-UART test.")
    print(f"STM32 OpenOCD: {openocd}")
    print(f"STM32 OpenOCD scripts: {scripts}")

    external_private = os.environ.get(
        "PHASE14_PRIVATE_KEY", ""
    ).strip()

    try:
        with tempfile.TemporaryDirectory(prefix="phase14-hw-") as td:
            temp = Path(td)

            if external_private:
                private_key = Path(external_private).expanduser().resolve()
                if not private_key.is_file():
                    fail(
                        "PHASE14_PRIVATE_KEY does not exist: "
                        f"{private_key}"
                    )
                key_mode = "user-supplied"
            else:
                private_key = temp / "ephemeral-private.pem"
                run(
                    [
                        "openssl",
                        "genpkey",
                        "-algorithm",
                        "EC",
                        "-pkeyopt",
                        "ec_paramgen_curve:P-256",
                        "-out",
                        str(private_key),
                    ]
                )
                key_mode = "ephemeral-test"

            run(
                [
                    "python3",
                    "tools/phase14_keytool.py",
                    str(private_key),
                    "--key-id",
                    f"0x{key_id:08X}",
                    "--output",
                    str(TRUSTED_HEADER),
                ]
            )
            print(
                f"P14_KEY=PASS mode={key_mode} "
                f"key_id=0x{key_id:08X}"
            )

            v1_path = build_application(
                toolchain, 1, "build-phase14-hw-v1", "out-phase14-hw-v1"
            )
            v2_path = build_application(
                toolchain, 2, "build-phase14-hw-v2", "out-phase14-hw-v2"
            )
            v3_path = build_application(
                toolchain, 3, "build-phase14-hw-v3", "out-phase14-hw-v3"
            )

            run(
                ["make", f"TOOLCHAIN={toolchain}", "clean"],
                cwd=BOOT,
            )
            run(
                ["make", f"TOOLCHAIN={toolchain}", "all"],
                cwd=BOOT,
            )

            bootloader = BOOT / "out/bootloader.bin"
            if bootloader.stat().st_size > 24 * 1024:
                fail(
                    "provisioned Phase-14 bootloader exceeds 24 KiB: "
                    f"{bootloader.stat().st_size}"
                )

            v1 = v1_path.read_bytes()
            v2 = v2_path.read_bytes()
            v3 = v3_path.read_bytes()

            patch = temp / "application-v1-to-v2.jdiff"
            signed_delta = temp / "application-v1-to-v2.sdot"
            signed_v2 = temp / "application-v2-full.sdot"
            signed_v3 = temp / "application-v3-full.sdot"
            baseline = temp / "secure-delta-ota-phase14-baseline.bin"

            run(
                [
                    "python3",
                    "tools/jojodiff_patch.py",
                    "generate",
                    str(v1_path),
                    str(v2_path),
                    str(patch),
                ]
            )

            common = [
                "--key",
                str(private_key),
                "--key-id",
                f"0x{key_id:08X}",
            ]

            run(
                [
                    "python3",
                    "tools/phase14_secure_container.py",
                    "build",
                    "--type",
                    "delta",
                    "--payload",
                    str(patch),
                    "--base",
                    str(v1_path),
                    "--target",
                    str(v2_path),
                    "--base-version",
                    "1",
                    "--target-version",
                    "2",
                    *common,
                    "--output",
                    str(signed_delta),
                ]
            )
            run(
                [
                    "python3",
                    "tools/phase14_secure_container.py",
                    "build",
                    "--type",
                    "full",
                    "--payload",
                    str(v2_path),
                    "--target",
                    str(v2_path),
                    "--target-version",
                    "2",
                    *common,
                    "--output",
                    str(signed_v2),
                ]
            )
            run(
                [
                    "python3",
                    "tools/phase14_secure_container.py",
                    "build",
                    "--type",
                    "full",
                    "--payload",
                    str(v3_path),
                    "--target",
                    str(v3_path),
                    "--target-version",
                    "3",
                    *common,
                    "--output",
                    str(signed_v3),
                ]
            )

            run(
                [
                    "python3",
                    "tools/merge_images.py",
                    "--bootloader",
                    str(bootloader),
                    "--application",
                    str(v1_path),
                    "--output",
                    str(baseline),
                    "--label",
                    "Phase 14 secure HIL baseline",
                ]
            )

            delta_bytes = signed_delta.read_bytes()
            full_v2_bytes = signed_v2.read_bytes()
            full_v3_bytes = signed_v3.read_bytes()
            tampered_v3 = bytearray(full_v3_bytes)
            tampered_v3[-1] ^= 0x01

            print(
                f"P14_ARTIFACT delta={len(delta_bytes)} "
                f"patch={patch.stat().st_size} "
                f"full_v3={len(full_v3_bytes)} "
                f"bootloader={bootloader.stat().st_size}"
            )

            run_openocd(
                openocd,
                scripts,
                f"program {tcl_path(baseline)} verify exit 0x08000000",
                "Phase-14 baseline program/verify",
            )
            run_openocd(
                openocd,
                scripts,
                "init; reset halt; "
                "flash erase_address 0x0800F800 0x800; "
                "reset run; sleep 4500; shutdown",
                "Phase-14 metadata erase/baseline boot",
            )

            link = None
            try:
                link, initial = wait_for_baseline_uart(
                    port, BASE_VERSION, wait_seconds=25.0
                )

                required_caps = (
                    CAP_DELTA_IMAGE | CAP_SIGNATURE_VERIFY
                )
                if (
                    initial.capability_flags & required_caps
                ) != required_caps:
                    fail(
                        "baseline lacks DELTA/SIGNATURE capabilities: "
                        f"0x{initial.capability_flags:08X}"
                    )

                print(
                    f"P14_BASELINE=PASS app=v{initial.application_version} "
                    f"caps=0x{initial.capability_flags:08X}"
                )

                secure_ota(link, delta_bytes, UPDATE_ID_DELTA)
                print("P14_SECURE_DELTA=PASS target=v2")
            except (OSError, TimeoutError, ValueError) as exc:
                fail(f"secure delta OTA: {exc}")
            finally:
                if link is not None:
                    link.close()

            verify_board(
                openocd,
                scripts,
                DELTA_TARGET_VERSION,
                v2,
                expected_last_error=0,
                label="secure-delta-v2",
            )

            reject_unsigned_full(port, v3, FULL_TARGET_VERSION)

            send_tampered_container(port, bytes(tampered_v3))
            verify_board(
                openocd,
                scripts,
                DELTA_TARGET_VERSION,
                v2,
                expected_last_error=EXPECTED_SIGNATURE_ERROR,
                label="tampered-signature-rejected",
            )

            link = open_link(port)
            try:
                secure_ota(link, full_v3_bytes, UPDATE_ID_FULL)
                print("P14_SECURE_FULL=PASS target=v3")
            except (OSError, TimeoutError, ValueError) as exc:
                fail(f"secure full OTA: {exc}")
            finally:
                link.close()

            verify_board(
                openocd,
                scripts,
                FULL_TARGET_VERSION,
                v3,
                expected_last_error=0,
                label="secure-full-v3",
            )

            reject_downgrade(port, full_v2_bytes)

            print(
                "Phase 14 secure container hardware test: PASS "
                f"(delta={len(delta_bytes)} bytes, "
                f"full_v3={len(full_v3_bytes)} bytes, "
                f"key_id=0x{key_id:08X})"
            )
            print(
                "Final board state: STM32 confirmed application v3; "
                "unsigned, tampered-signature and downgrade paths rejected."
            )

    finally:
        TRUSTED_HEADER.write_bytes(original_trust)

        # Do not leave build products compiled with the ephemeral public key.
        subprocess.run(
            ["make", f"TOOLCHAIN={toolchain}", "clean"],
            cwd=BOOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )

        for name in (
            "build-phase14-hw-v1",
            "out-phase14-hw-v1",
            "build-phase14-hw-v2",
            "out-phase14-hw-v2",
            "build-phase14-hw-v3",
            "out-phase14-hw-v3",
        ):
            path = APP / name
            if path.exists():
                shutil.rmtree(path, ignore_errors=True)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
