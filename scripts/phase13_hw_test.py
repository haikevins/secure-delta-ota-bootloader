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
APP = ROOT / "node-stm32f103/application"

BASELINE = ROOT / "dist/secure-delta-ota-phase13.bin"
BASE = APP / "out-phase13-base/application.bin"
TARGET = APP / "out-phase13-target/application.bin"
DELTA = ROOT / "dist/phase13/application-v1-to-v2.d13"

METADATA_ADDRESS = 0x0800F800
METADATA_SIZE = 0x800
METADATA_PAGE = 1024
METADATA_RECORD_SIZE = 52
METADATA_MAGIC = 0x424D4554
METADATA_VERSION = 1

UPDATE_IDLE = 0
BASE_VERSION = 1
TARGET_VERSION = 2
UPDATE_ID = 0xD0130001

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
    CMD_NACK,
    CMD_QUERY,
    CMD_START,
    FW_IMAGE_DELTA,
    Packet,
    STATUS_BASE_MISMATCH,
    UPDATE_IDLE as OTA_UPDATE_IDLE,
    build_start_payload,
    crc32,
    parse_ack,
    parse_hello,
)
from uart_ota_sender import (  # noqa: E402
    PHASE13_DELTA_HEADER_SIZE,
    SerialLink,
    delta_ota,
    parse_phase13_delta_artifact,
    wait_for_application_version,
)


def fail(message: str) -> None:
    print(f"Phase 13 hardware test: FAIL: {message}")
    raise SystemExit(1)


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
    result: list[Path] = []
    candidates: list[Path] = []

    if explicit:
        candidates.append(Path(explicit))

    candidates.extend([
        Path("/usr/share/openocd/scripts"),
        Path("/usr/local/share/openocd/scripts"),
        openocd.parent.parent / "share/openocd/scripts",
    ])

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
            if _valid_script_root(scripts):
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
            f"metadata dump size={len(data)}, "
            f"expected={METADATA_SIZE}"
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



def wait_for_baseline_uart(port: str,
                           expected_version: int,
                           wait_seconds: float = 25.0):
    """Synchronize with the freshly flashed baseline application.

    Unlike wait_for_application_version(), which is optimized for polling an
    already-open UART while the MCU reboots during OTA, this helper handles
    the initial PC serial open immediately after OpenOCD activity.  Some USB
    UART adapters need a slightly longer first transaction or benefit from a
    close/reopen cycle.
    """
    deadline = time.monotonic() + wait_seconds
    attempt = 0
    last_observation = "no valid QUERY response"

    while time.monotonic() < deadline:
        attempt += 1
        link = None

        try:
            link = open_link(port)

            # Match the successful interactive CLI timing much more closely
            # than the fast reboot poller: 1.5 s response window, 5 retries.
            link.timeout = 1.5
            link.retries = 5

            response = link.request(
                Packet(
                    command=CMD_QUERY,
                    update_id=0,
                    sequence=0,
                )
            )
            info = parse_hello(response)

            last_observation = (
                f"app=v{info.application_version} "
                f"state={info.update_state} "
                f"caps=0x{info.capability_flags:08X}"
            )

            print(
                f"P13_UART_SYNC attempt={attempt} "
                f"{last_observation}"
            )

            if (
                info.application_version == expected_version
                and info.update_state == OTA_UPDATE_IDLE
            ):
                return link, info

        except (OSError, TimeoutError, ValueError) as exc:
            last_observation = (
                f"{type(exc).__name__}: {exc}"
            )
            print(
                f"P13_UART_SYNC_RETRY attempt={attempt} "
                f"detail={last_observation}"
            )

        if link is not None:
            link.close()

        # Give the bootloader/application and USB-UART driver time to settle.
        time.sleep(0.50)

    raise TimeoutError(
        f"baseline application v{expected_version} IDLE not observed "
        f"within {wait_seconds:.1f}s; last={last_observation}"
    )


def verify_final_stm32(openocd: str,
                       scripts: str,
                       expected: bytes) -> None:
    with tempfile.TemporaryDirectory(prefix="phase13-final-") as td:
        td_path = Path(td)
        metadata_path = td_path / "metadata.bin"
        application_path = td_path / "application.bin"

        run_openocd(
            openocd,
            scripts,
            "init; halt; "
            f"dump_image {tcl_path(metadata_path)} "
            f"0x{METADATA_ADDRESS:08X} 0x{METADATA_SIZE:X}; "
            f"dump_image {tcl_path(application_path)} "
            f"0x08006000 0x{len(expected):X}; "
            "resume; shutdown",
            "Phase-13 final ST-Link verification",
        )

        fields = select_metadata(metadata_path.read_bytes())

        print(
            f"P13_STM32_METADATA generation={fields[1]} "
            f"state={fields[3]} active_version={fields[4]} "
            f"pending_version={fields[5]} "
            f"boot_attempts={fields[10]} "
            f"last_error=0x{fields[11]:08X}"
        )

        if fields[3] != UPDATE_IDLE:
            fail(f"final metadata state={fields[3]}, expected IDLE")
        if fields[4] != TARGET_VERSION:
            fail(
                f"active_version={fields[4]}, "
                f"expected {TARGET_VERSION}"
            )
        if any((
            fields[5],   # pending_version
            fields[6],   # active_update_id
            fields[7],   # received_size
            fields[8],   # expected_size
            fields[9],   # copy_offset
            fields[10],  # boot_attempts
        )):
            fail("final metadata cleanup fields are not zero")
        if fields[11] != 0:
            fail(
                f"final last_error=0x{fields[11]:08X}, expected 0"
            )

        actual = application_path.read_bytes()
        if actual != expected:
            mismatch = next(
                (
                    i
                    for i, (left, right)
                    in enumerate(zip(actual, expected))
                    if left != right
                ),
                None,
            )
            fail(
                "installed target byte mismatch at offset "
                f"{mismatch}"
            )

        print(
            "STM32 delta-installed target byte-for-byte "
            "verification: PASS"
        )


def verify_wrong_base_rejected(port: str,
                               artifact: bytes) -> None:
    info = parse_phase13_delta_artifact(artifact)
    link = open_link(port)

    try:
        hello = wait_for_application_version(
            link,
            TARGET_VERSION,
            wait_seconds=8.0,
            required_state=OTA_UPDATE_IDLE,
        )

        if hello.application_version != TARGET_VERSION:
            fail("negative base test did not start from confirmed v2")

        response = link.request(
            Packet(
                command=CMD_START,
                update_id=UPDATE_ID + 1,
                payload=build_start_payload(
                    len(artifact),
                    crc32(artifact),
                    artifact_type=FW_IMAGE_DELTA,
                    base_version=info["base_version"],
                    target_version=info["target_version"],
                    container_header_size=PHASE13_DELTA_HEADER_SIZE,
                ),
            )
        )

        nack = parse_ack(response)

        if (
            response.command != CMD_NACK
            or nack.status != STATUS_BASE_MISMATCH
        ):
            fail(
                "wrong-base delta was not rejected with "
                f"BASE_MISMATCH: cmd=0x{response.command:02X} "
                f"status=0x{nack.status:02X}"
            )

        after = parse_hello(
            link.request(Packet(command=CMD_QUERY))
        )
        if (
            after.application_version != TARGET_VERSION
            or after.update_state != OTA_UPDATE_IDLE
        ):
            fail("wrong-base rejection changed confirmed v2 state")

        print(
            "Wrong-base delta rejection after v2 confirmation: PASS"
        )

    finally:
        link.close()


def main() -> int:
    port = (
        os.environ.get("PORT", "").strip()
        or os.environ.get("STM32_PORT", "").strip()
    )

    if not port:
        print(
            "Usage: make phase13-hw-test "
            "PORT=/dev/ttyUSB0"
        )
        return 2

    openocd, scripts = resolve_stm32_openocd()

    for path in (BASELINE, BASE, TARGET, DELTA):
        if not path.is_file():
            fail(f"missing Phase-13 artifact: {path}")

    base = BASE.read_bytes()
    target = TARGET.read_bytes()
    artifact = DELTA.read_bytes()
    delta_info = parse_phase13_delta_artifact(artifact)

    if delta_info["base_version"] != BASE_VERSION:
        fail("D13P base version is not v1")
    if delta_info["target_version"] != TARGET_VERSION:
        fail("D13P target version is not v2")
    if delta_info["base_size"] != len(base):
        fail("D13P base size mismatch")
    if delta_info["target_size"] != len(target):
        fail("D13P target size mismatch")

    print(
        "Phase 13 test: PC UART delta artifact -> STM32 external "
        "Flash -> bootloader streaming patch -> reconstructed image "
        "-> backup/install/trial/confirm"
    )
    print(
        "Wiring: USB-UART TX -> STM32 PA10 RX, "
        "USB-UART RX <- STM32 PA9 TX, GND common"
    )
    print(
        "Disconnect ESP32 from PA9/PA10 during this direct PC-UART test."
    )
    print(f"STM32 OpenOCD: {openocd}")
    print(f"STM32 OpenOCD scripts: {scripts}")
    print(
        f"P13_ARTIFACT size={len(artifact)} "
        f"patch={delta_info['patch_size']} "
        f"target={len(target)} "
        f"crc32=0x{crc32(artifact):08X}"
    )

    # Deterministic Phase-13 baseline: bootloader + exact v1 application.
    run_openocd(
        openocd,
        scripts,
        f"program {tcl_path(BASELINE)} verify exit 0x08000000",
        "Phase-13 baseline program/verify",
    )

    run_openocd(
        openocd,
        scripts,
        "init; reset halt; "
        "flash erase_address 0x0800F800 0x800; "
        "reset run; sleep 4500; shutdown",
        "Phase-13 metadata erase/baseline boot",
    )

    link = None
    try:
        link, initial = wait_for_baseline_uart(
            port,
            BASE_VERSION,
            wait_seconds=25.0,
        )

        if (initial.capability_flags & CAP_DELTA_IMAGE) == 0:
            fail("STM32 v1 does not advertise OTA_CAP_DELTA_IMAGE")

        print(
            f"P13_BASELINE=PASS app=v{initial.application_version} "
            f"caps=0x{initial.capability_flags:08X}"
        )

        delta_ota(link, artifact, UPDATE_ID)

    except (OSError, TimeoutError, ValueError) as exc:
        fail(f"delta OTA: {exc}")
    finally:
        if link is not None:
            link.close()

    verify_final_stm32(openocd, scripts, target)

    # Base binding is part of Phase 13 acceptance: the same v1->v2 patch
    # must not be accepted once the node already runs v2.
    verify_wrong_base_rejected(port, artifact)

    print(
        "Phase 13 STM32 delta patch hardware test: PASS "
        f"(artifact={len(artifact)} bytes, "
        f"patch={delta_info['patch_size']} bytes, "
        f"target={len(target)} bytes)"
    )
    print(
        "Final board state: STM32 confirmed application v2; "
        "wrong-base delta rejected."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
