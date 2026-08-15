#!/usr/bin/env python3
from __future__ import annotations

import math
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
FAULT_IMAGE = ROOT / "dist/secure-delta-ota-phase7-fault.bin"
CANDIDATE = APP / "out-phase7-candidate/application.bin"
NORMAL_BOOTLOADER = BOOT / "out/bootloader.bin"

METADATA_ADDRESS = 0x0800F800
METADATA_SIZE = 0x800
PAGE_SIZE = 1024
RECORD_SIZE = 52
METADATA_MAGIC = 0x424D4554
METADATA_VERSION = 1
UPDATE_IDLE = 0
TARGET_VERSION = 2
UPDATE_ID = 0x70070001
FAULT_OFFSET = 1536
DOWNLOAD_CUT_OFFSET = 4608
DOWNLOAD_CHECKPOINT = 4096
UART_CHUNK = 256

OPENOCD_SETUP = (
    "source [find interface/stlink.cfg]; "
    "transport select hla_swd; "
    "source [find target/stm32f1x.cfg]; "
    "reset_config none; "
    "adapter speed 1000; "
)

sys.path.insert(0, str(ROOT / "tools"))
from ota_uart_protocol import (  # noqa: E402
    CMD_ABORT,
    CMD_DATA,
    CMD_FINISH,
    CMD_INSTALL,
    CMD_QUERY,
    CMD_RESUME,
    CMD_START,
    Packet,
    ProtocolError,
    UPDATE_ARTIFACT_READY,
    UPDATE_IDLE as OTA_UPDATE_IDLE,
    UPDATE_RECEIVING,
    build_start_payload,
    crc32,
    parse_hello,
)
from uart_ota_sender import (  # noqa: E402
    SerialLink,
    require_ack,
    wait_for_application_version,
)

def fail(message: str) -> None:
    print(f"Phase 7 hardware test: FAIL: {message}")
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
    fail("both metadata slots invalid after recovery")
    raise AssertionError

def verify_final_metadata(dump: Path, candidate_size: int) -> None:
    fields = selected_metadata(dump.read_bytes())
    generation = fields[1]
    state = fields[3]
    active_version = fields[4]
    pending_version = fields[5]
    update_id = fields[6]
    copy_offset = fields[9]
    last_error = fields[11]

    pages = math.ceil(candidate_size / PAGE_SIZE)

    # Deterministic clean sequence:
    # gen1 default IDLE after internal metadata erase
    # gen2 ARTIFACT_READY from application INSTALL handoff
    # gen3 INSTALLING offset 0
    # +1 commit for each verified internal 1 KiB page
    # +1 one-shot fault marker before reset in the second page
    # +1 VERIFYING_INSTALL
    # +1 final IDLE
    expected_generation = pages + 6

    print(
        f"P7_METADATA generation={generation} expected={expected_generation} "
        f"state={state} active_version={active_version} "
        f"copy_offset={copy_offset} last_error=0x{last_error:08X}"
    )

    if generation != expected_generation:
        fail(
            "metadata generation does not prove the one-shot install reset; "
            f"expected {expected_generation}, got {generation}"
        )
    if state != UPDATE_IDLE:
        fail(f"final metadata state={state}, expected IDLE")
    if active_version != TARGET_VERSION:
        fail(
            f"final active_version=0x{active_version:08X}, "
            f"expected 0x{TARGET_VERSION:08X}"
        )
    if pending_version != 0 or update_id != 0 or copy_offset != 0:
        fail("final metadata cleanup fields are not zero")
    if last_error != 0:
        fail(f"final last_error=0x{last_error:08X}, expected 0")

def open_link(port: str) -> SerialLink:
    return SerialLink(port=port, baud=115200, timeout=1.5, retries=5)

def query(link: SerialLink):
    return parse_hello(link.request(Packet(command=CMD_QUERY)))

def clear_stale_download(link: SerialLink) -> None:
    info = query(link)
    if info.update_state == OTA_UPDATE_IDLE:
        return
    response = link.request(
        Packet(command=CMD_ABORT, update_id=info.active_update_id)
    )
    require_ack(response, "ABORT stale Phase-7 session")
    final = query(link)
    if final.update_state != OTA_UPDATE_IDLE:
        fail(f"ABORT left UART state={final.update_state}")

def begin_download(link: SerialLink, data: bytes) -> None:
    response = link.request(
        Packet(
            command=CMD_START,
            update_id=UPDATE_ID,
            payload=build_start_payload(
                len(data),
                crc32(data),
                target_version=TARGET_VERSION,
            ),
        )
    )
    info = require_ack(response, "Phase-7 START")
    if info.next_expected_offset != 0:
        fail(f"START returned next offset {info.next_expected_offset}, expected 0")

def send_range(link: SerialLink, data: bytes, start: int, end: int) -> None:
    if start % UART_CHUNK:
        fail(f"send start {start} is not {UART_CHUNK}-byte aligned")
    offset = start
    sequence = (start // UART_CHUNK) & 0xFFFF

    while offset < end:
        chunk_end = min(offset + UART_CHUNK, end, len(data))
        chunk = data[offset:chunk_end]
        response = link.request(
            Packet(
                command=CMD_DATA,
                update_id=UPDATE_ID,
                offset=offset,
                sequence=sequence,
                payload=chunk,
            )
        )
        ack = require_ack(response, f"DATA offset={offset}")
        expected = chunk_end
        if ack.next_expected_offset != expected:
            fail(
                f"DATA offset={offset} ACK next={ack.next_expected_offset}, "
                f"expected {expected}"
            )
        offset = expected
        sequence = (sequence + 1) & 0xFFFF

def finish_download(link: SerialLink, data: bytes) -> None:
    sequence = (len(data) + UART_CHUNK - 1) // UART_CHUNK
    packet = Packet(
        command=CMD_FINISH,
        update_id=UPDATE_ID,
        offset=len(data),
        sequence=sequence & 0xFFFF,
    )
    info = require_ack(link.request(packet), "FINISH after download recovery")
    if info.update_state != UPDATE_ARTIFACT_READY:
        fail(f"FINISH state={info.update_state}, expected ARTIFACT_READY")

    retry = require_ack(link.request(packet), "FINISH retry")
    if retry.update_state != UPDATE_ARTIFACT_READY:
        fail("FINISH retry lost ARTIFACT_READY state")

def install_and_wait(link: SerialLink, candidate_size: int):
    install = Packet(
        command=CMD_INSTALL,
        update_id=UPDATE_ID,
        offset=candidate_size,
        sequence=0,
    )
    try:
        require_ack(link.request(install), "INSTALL")
        print("INSTALL ACK: PASS; fault-injection bootloader will reset mid-page")
    except TimeoutError:
        # Reset may happen immediately after the response. Success is proven by
        # the final application version and metadata generation below.
        print("INSTALL ACK not observed; waiting for recovered application")

    return wait_for_application_version(link, TARGET_VERSION, wait_seconds=25.0)

def main() -> int:
    port = os.environ.get("PORT", "").strip()
    if not port:
        print("Usage: make phase7-hw-test PORT=/dev/ttyUSB0")
        return 2

    openocd = os.environ.get("OPENOCD", "openocd")
    if not shutil.which(openocd):
        fail(f"OpenOCD not found: {openocd}")

    for path in (FAULT_IMAGE, CANDIDATE, NORMAL_BOOTLOADER):
        if not path.is_file():
            fail(f"missing build artifact: {path}")

    data = CANDIDATE.read_bytes()
    if len(data) <= DOWNLOAD_CUT_OFFSET:
        fail("candidate is too small for persistent-download reset test")
    if len(data) <= FAULT_OFFSET:
        fail("candidate is too small for install fault injection")

    print(
        "Phase 7 test: persistent UART resume + page-checkpointed install "
        f"(download cut={DOWNLOAD_CUT_OFFSET}, install fault={FAULT_OFFSET})"
    )

    # Stage 1: install test-only bootloader + baseline application.
    run_openocd(
        openocd,
        f"program {tcl_path(FAULT_IMAGE)} verify exit 0x08000000",
        "fault-image program/verify",
    )

    # Stage 2: clear internal A/B metadata so generation counting is
    # deterministic, then boot the baseline application.
    run_openocd(
        openocd,
        "init; reset halt; "
        "flash erase_address 0x0800F800 0x800; "
        "reset run; sleep 3000; shutdown",
        "metadata erase/baseline boot",
    )

    # Stage 3: establish a persistent UART download, checkpoint 4096 bytes,
    # accept another 512 bytes without a new checkpoint, then reset.
    link = open_link(port)
    try:
        clear_stale_download(link)
        begin_download(link, data)
        send_range(link, data, 0, DOWNLOAD_CUT_OFFSET)
        before_reset = query(link)
        if (
            before_reset.update_state != UPDATE_RECEIVING
            or before_reset.next_expected_offset != DOWNLOAD_CUT_OFFSET
        ):
            fail(
                "pre-reset runtime progress mismatch: "
                f"state={before_reset.update_state}, "
                f"next={before_reset.next_expected_offset}"
            )
    finally:
        link.close()

    run_openocd(
        openocd,
        "init; reset run; sleep 3000; shutdown",
        "download power-loss reset",
    )

    # Stage 4: after reboot the runtime 4608-byte progress must fall back to
    # the newest persisted 4 KiB checkpoint. RESUME then retransmits the torn
    # sector from 4096 and completes the artifact.
    link = open_link(port)
    try:
        recovered = query(link)
        if recovered.update_state != UPDATE_RECEIVING:
            fail(
                f"download reboot state={recovered.update_state}, "
                "expected RECEIVING"
            )
        if recovered.active_update_id != UPDATE_ID:
            fail(
                f"download reboot update_id=0x{recovered.active_update_id:08X}, "
                f"expected 0x{UPDATE_ID:08X}"
            )
        if recovered.next_expected_offset != DOWNLOAD_CHECKPOINT:
            fail(
                "persistent checkpoint mismatch: "
                f"runtime before reset={DOWNLOAD_CUT_OFFSET}, "
                f"restored={recovered.next_expected_offset}, "
                f"expected={DOWNLOAD_CHECKPOINT}"
            )

        resume = require_ack(
            link.request(Packet(command=CMD_RESUME, update_id=UPDATE_ID)),
            "RESUME after reset",
        )
        if resume.next_expected_offset != DOWNLOAD_CHECKPOINT:
            fail(
                f"RESUME next={resume.next_expected_offset}, "
                f"expected={DOWNLOAD_CHECKPOINT}"
            )

        print(
            "Download checkpoint recovery: PASS "
            f"({DOWNLOAD_CUT_OFFSET} -> {DOWNLOAD_CHECKPOINT})"
        )

        send_range(link, data, DOWNLOAD_CHECKPOINT, len(data))
        finish_download(link, data)

        final_download = query(link)
        if (
            final_download.update_state != UPDATE_ARTIFACT_READY
            or final_download.next_expected_offset != len(data)
        ):
            fail("artifact did not reach ARTIFACT_READY after resume")

        # Stage 5: INSTALL hands off to the bootloader. The test-only bootloader
        # resets once during the second internal page. Persistent internal A/B
        # metadata must resume from the previous verified 1 KiB page.
        app_info = install_and_wait(link, len(data))
        if app_info.update_state != OTA_UPDATE_IDLE:
            fail(
                f"updated application returned state={app_info.update_state}, "
                "expected IDLE"
            )
    except (ProtocolError, TimeoutError, OSError) as exc:
        fail(str(exc))
    finally:
        link.close()

    # Stage 6: generation proves the deterministic mid-page reset path was
    # executed once and final metadata was cleaned after successful verify.
    with tempfile.TemporaryDirectory(prefix="phase7-") as td:
        dump = Path(td) / "metadata.bin"
        run_openocd(
            openocd,
            "init; halt; "
            f"dump_image {tcl_path(dump)} 0x{METADATA_ADDRESS:08X} "
            f"0x{METADATA_SIZE:X}; "
            "resume; shutdown",
            "final metadata dump",
        )
        verify_final_metadata(dump, len(data))

    print(
        "Install mid-page recovery: PASS "
        f"(one-shot reset at offset {FAULT_OFFSET})"
    )

    # Stage 7: remove the test-only fault injector without touching application
    # v2 or internal metadata. Only bootloader pages are rewritten.
    run_openocd(
        openocd,
        f"program {tcl_path(NORMAL_BOOTLOADER)} verify reset exit 0x08000000",
        "restore normal bootloader",
    )
    time.sleep(3.0)

    link = open_link(port)
    try:
        info = parse_hello(link.request(Packet(command=CMD_QUERY)))
        if info.application_version != TARGET_VERSION:
            fail(
                f"application=0x{info.application_version:08X} after restore, "
                f"expected 0x{TARGET_VERSION:08X}"
            )
        if info.update_state != OTA_UPDATE_IDLE:
            fail(
                f"post-restore update state={info.update_state}, expected IDLE"
            )
    except (ProtocolError, TimeoutError, OSError) as exc:
        fail(f"post-restore HELLO/QUERY: {exc}")
    finally:
        link.close()

    print(
        "Phase 7 power-loss recovery hardware test: PASS "
        f"(candidate={len(data)} bytes)"
    )
    print("Normal Phase-7 bootloader restored; application v2 preserved.")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
