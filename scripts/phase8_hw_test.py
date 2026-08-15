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
BOOT = ROOT / "node-stm32f103/bootloader"
APP = ROOT / "node-stm32f103/application"

BASE_IMAGE = ROOT / "dist/secure-delta-ota-phase8.bin"
GOOD_CANDIDATE = APP / "out-phase8-good/application.bin"
BAD_CANDIDATE = APP / "out-phase8-bad/application.bin"

METADATA_ADDRESS = 0x0800F800
METADATA_SIZE = 0x800
PAGE_SIZE = 1024
RECORD_SIZE = 52
METADATA_MAGIC = 0x424D4554
METADATA_VERSION = 1

UPDATE_IDLE = 0
UPDATE_TRIAL_BOOT = 10

BASE_VERSION = 1
GOOD_VERSION = 2
BAD_VERSION = 3
GOOD_UPDATE_ID = 0x80080001
BAD_UPDATE_ID = 0x80080002

ROLLBACK_DIAGNOSTIC = 0x0008B003
APPLICATION_REGION_SIZE = 38 * 1024

OPENOCD_SETUP = (
    "source [find interface/stlink.cfg]; "
    "transport select hla_swd; "
    "source [find target/stm32f1x.cfg]; "
    "reset_config none; "
    "adapter speed 1000; "
)

sys.path.insert(0, str(ROOT / "tools"))
from ota_uart_protocol import (  # noqa: E402
    CMD_INSTALL,
    Packet,
    ProtocolError,
    UPDATE_IDLE as OTA_UPDATE_IDLE,
    UPDATE_TRIAL_BOOT as OTA_UPDATE_TRIAL_BOOT,
    parse_hello,
)
from uart_ota_sender import (  # noqa: E402
    SerialLink,
    full_ota,
    require_ack,
    transfer,
    wait_for_application_version,
)

def fail(message: str) -> None:
    print(f"Phase 8 hardware test: FAIL: {message}")
    raise SystemExit(1)

def tcl_path(path: Path) -> str:
    value = str(path.resolve())
    if "}" in value:
        fail("path containing '}' is not supported")
    return "{" + value + "}"

def run_openocd(openocd: str, commands: str, stage: str) -> str:
    result = subprocess.run(
        [openocd, "-c", OPENOCD_SETUP + commands],
        cwd=ROOT,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env=os.environ.copy(),
    )
    print(result.stdout, end="")
    if result.returncode != 0:
        fail(f"{stage}: OpenOCD returned {result.returncode}")
    return result.stdout

def open_link(port: str) -> SerialLink:
    return SerialLink(port=port, baud=115200, timeout=1.5, retries=5)

def decode_record(page: bytes):
    fields = struct.unpack("<13I", page[:RECORD_SIZE])
    stored_crc = fields[12]
    computed_crc = zlib.crc32(page[:RECORD_SIZE - 4]) & 0xFFFFFFFF
    valid = (
        fields[0] == METADATA_MAGIC
        and fields[1] != 0
        and fields[2] == METADATA_VERSION
        and fields[3] <= 13
        and stored_crc == computed_crc
    )
    return fields, valid

def is_newer(candidate: int, reference: int) -> bool:
    difference = (candidate - reference) & 0xFFFFFFFF
    return difference != 0 and difference < 0x80000000

def selected_metadata(data: bytes):
    if len(data) != METADATA_SIZE:
        fail(f"metadata dump size {len(data)} != {METADATA_SIZE}")

    a, valid_a = decode_record(data[:PAGE_SIZE])
    b, valid_b = decode_record(data[PAGE_SIZE:])

    if valid_a and valid_b:
        return b if is_newer(b[1], a[1]) else a
    if valid_a:
        return a
    if valid_b:
        return b
    fail("both metadata slots invalid")
    raise AssertionError

def dump_metadata(openocd: str, path: Path, stage: str):
    run_openocd(
        openocd,
        "init; halt; "
        f"dump_image {tcl_path(path)} 0x{METADATA_ADDRESS:08X} "
        f"0x{METADATA_SIZE:X}; "
        "resume; shutdown",
        stage,
    )
    return selected_metadata(path.read_bytes())

def verify_clean_metadata(fields, expected_version: int,
                          expected_last_error: int) -> None:
    generation = fields[1]
    state = fields[3]
    active_version = fields[4]
    pending_version = fields[5]
    update_id = fields[6]
    received = fields[7]
    expected = fields[8]
    copy_offset = fields[9]
    attempts = fields[10]
    last_error = fields[11]

    print(
        f"P8_METADATA generation={generation} state={state} "
        f"active_version={active_version} pending_version={pending_version} "
        f"boot_attempts={attempts} last_error=0x{last_error:08X}"
    )

    if state != UPDATE_IDLE:
        fail(f"metadata state={state}, expected IDLE")
    if active_version != expected_version:
        fail(
            f"active_version={active_version}, expected {expected_version}"
        )
    if any((pending_version, update_id, received, expected,
            copy_offset, attempts)):
        fail("final metadata cleanup fields are not zero")
    if last_error != expected_last_error:
        fail(
            f"last_error=0x{last_error:08X}, "
            f"expected 0x{expected_last_error:08X}"
        )

def wait_initial_version(link: SerialLink, version: int):
    info = wait_for_application_version(
        link, version, wait_seconds=12.0, required_state=OTA_UPDATE_IDLE
    )
    if info.application_version != version:
        fail("baseline version mismatch")
    return info

def send_bad_trial(link: SerialLink, data: bytes) -> None:
    transfer(
        link,
        data,
        BAD_UPDATE_ID,
        target_version=BAD_VERSION,
    )

    packet = Packet(
        command=CMD_INSTALL,
        update_id=BAD_UPDATE_ID,
        offset=len(data),
        sequence=0,
    )

    try:
        require_ack(link.request(packet), "bad-candidate INSTALL")
        print("Bad candidate INSTALL ACK: PASS")
    except TimeoutError:
        print("Bad candidate INSTALL ACK not observed; waiting for trial")

def dump_application_region(openocd: str, stage: str) -> bytes:
    with tempfile.TemporaryDirectory(prefix="phase8-app-") as td:
        dump = Path(td) / "application-region.bin"
        run_openocd(
            openocd,
            "init; halt; "
            f"dump_image {tcl_path(dump)} 0x08006000 "
            f"0x{APPLICATION_REGION_SIZE:X}; "
            "resume; shutdown",
            stage,
        )
        data = dump.read_bytes()
        if len(data) != APPLICATION_REGION_SIZE:
            fail(
                f"application-region dump size={len(data)}, "
                f"expected={APPLICATION_REGION_SIZE}"
            )
        return data

def verify_restored_bytes(expected_region: bytes,
                          actual_region: bytes,
                          good_image: bytes) -> None:
    if actual_region != expected_region:
        mismatch = next(
            (i for i, (a, b) in enumerate(zip(actual_region, expected_region))
             if a != b),
            None,
        )
        fail(f"rollback 38 KiB region mismatch at offset {mismatch}")

    if actual_region[:len(good_image)] != good_image:
        fail("restored application prefix does not match healthy v2 binary")

def main() -> int:
    port = os.environ.get("PORT", "").strip()
    if not port:
        print("Usage: make phase8-hw-test PORT=/dev/ttyUSB0")
        return 2

    openocd = os.environ.get("OPENOCD", "openocd")
    if not shutil.which(openocd):
        fail(f"OpenOCD not found: {openocd}")

    for path in (BASE_IMAGE, GOOD_CANDIDATE, BAD_CANDIDATE):
        if not path.is_file():
            fail(f"missing build artifact: {path}")

    good_data = GOOD_CANDIDATE.read_bytes()
    bad_data = BAD_CANDIDATE.read_bytes()

    print(
        "Phase 8 test: healthy trial confirmation + "
        "3-attempt watchdog rollback"
    )

    # 1. Deterministic baseline v1.
    run_openocd(
        openocd,
        f"program {tcl_path(BASE_IMAGE)} verify exit 0x08000000",
        "Phase-8 baseline program/verify",
    )
    run_openocd(
        openocd,
        "init; reset halt; "
        "flash erase_address 0x0800F800 0x800; "
        "reset run; sleep 3500; shutdown",
        "metadata erase/baseline boot",
    )

    link = open_link(port)
    try:
        wait_initial_version(link, BASE_VERSION)
        print("Baseline application v1: PASS")

        # 2. Healthy v2: backup v1 -> install -> trial -> auto-confirm -> IDLE.
        full_ota(
            link,
            good_data,
            GOOD_UPDATE_ID,
            GOOD_VERSION,
        )
        print("Healthy candidate v2 trial confirmation: PASS")
    except (ProtocolError, TimeoutError, OSError) as exc:
        fail(f"healthy trial: {exc}")
    finally:
        link.close()

    with tempfile.TemporaryDirectory(prefix="phase8-meta-good-") as td:
        fields = dump_metadata(
            openocd,
            Path(td) / "metadata.bin",
            "healthy-trial metadata dump",
        )
        verify_clean_metadata(fields, GOOD_VERSION, 0)

    confirmed_v2_region = dump_application_region(
        openocd,
        "confirmed-v2 38 KiB snapshot",
    )
    if confirmed_v2_region[:len(good_data)] != good_data:
        fail("confirmed v2 internal image does not match healthy candidate")
    print("Confirmed v2 38 KiB application-region snapshot: PASS")

    # 3. Unhealthy v3: it deliberately never confirms. IWDG should reset it
    # three times; on the fourth bootloader entry, attempt limit selects
    # ROLLBACK and restores the complete v2 backup.
    link = open_link(port)
    try:
        send_bad_trial(link, bad_data)

        trial_info = wait_for_application_version(
            link,
            BAD_VERSION,
            wait_seconds=20.0,
            required_state=OTA_UPDATE_TRIAL_BOOT,
        )
        if trial_info.update_state != UPDATE_TRIAL_BOOT:
            fail("v3 did not expose TRIAL_BOOT state")
        print("Unconfirmed candidate v3 observed in TRIAL_BOOT: PASS")

        restored = wait_for_application_version(
            link,
            GOOD_VERSION,
            wait_seconds=55.0,
            required_state=OTA_UPDATE_IDLE,
        )
        if restored.application_version != GOOD_VERSION:
            fail("rollback did not restore v2")
        print("Watchdog attempt-limit rollback to v2: PASS")
    except (ProtocolError, TimeoutError, OSError) as exc:
        fail(f"rollback trial: {exc}")
    finally:
        link.close()

    # 4. Final persistent metadata proves the reason and clean final state.
    with tempfile.TemporaryDirectory(prefix="phase8-meta-bad-") as td:
        fields = dump_metadata(
            openocd,
            Path(td) / "metadata.bin",
            "rollback metadata dump",
        )
        verify_clean_metadata(
            fields,
            GOOD_VERSION,
            ROLLBACK_DIAGNOSTIC,
        )

    # 5. Stronger proof than version reporting: compare the complete restored
    # 38 KiB application region against the snapshot taken after v2 confirm.
    restored_region = dump_application_region(
        openocd,
        "rollback 38 KiB application-region dump",
    )
    verify_restored_bytes(
        confirmed_v2_region,
        restored_region,
        good_data,
    )
    print("Rollback full 38 KiB byte-for-byte verification: PASS")

    print(
        "Phase 8 trial boot/rollback hardware test: PASS "
        f"(good={len(good_data)} bytes, bad={len(bad_data)} bytes)"
    )
    print("Final board state: normal Phase-8 bootloader + confirmed application v2.")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
