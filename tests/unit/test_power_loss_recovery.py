#!/usr/bin/env python3
"""Host model of power-loss recovery page-checkpoint recovery.

The model injects one reset at every byte position of a non-page-aligned image.
A reset may leave the current internal page partially programmed, while the
persistent checkpoint still points to the previous fully verified page.
Recovery must re-erase that page and converge to the exact source image.
"""
from __future__ import annotations

import struct
import zlib

PAGE = 1024
IMAGE_SIZE = 3500

DOWNLOAD_SECTOR = 4096
DOWNLOAD_IMAGE_SIZE = 10000

def download_source() -> bytes:
    return bytes(((i * 41 + 7) & 0xFF) for i in range(DOWNLOAD_IMAGE_SIZE))

def run_download_with_single_cut(cut_offset: int) -> tuple[bytes, int]:
    """Model a reset at an arbitrary byte during UART reception.

    Only complete 4 KiB receive sectors are checkpointed. After reboot the
    receiver resumes from the newest checkpoint and erases the first
    uncheckpointed sector before accepting retransmitted DATA.
    """
    source = download_source()
    external = bytearray(b"\xFF" * (3 * DOWNLOAD_SECTOR))
    persisted_offset = 0

    # First boot: receive until power is lost.
    for index in range(cut_offset):
        external[index] = source[index]
        if (index + 1) % DOWNLOAD_SECTOR == 0:
            persisted_offset = index + 1

    assert 0 <= persisted_offset <= cut_offset
    assert persisted_offset % DOWNLOAD_SECTOR == 0

    # Reboot: sectors below the checkpoint remain accepted. The sector at the
    # checkpoint may contain a torn partial write, so it is erased and resent.
    resume_offset = persisted_offset
    if resume_offset < len(source):
        sector_start = (resume_offset // DOWNLOAD_SECTOR) * DOWNLOAD_SECTOR
        sector_end = min(sector_start + DOWNLOAD_SECTOR, len(external))
        external[sector_start:sector_end] = b"\xFF" * (sector_end - sector_start)

    index = resume_offset
    while index < len(source):
        external[index] = source[index]
        index += 1

    return bytes(external[:len(source)]), persisted_offset

def test_download_checkpoint_recovery_all_cut_positions() -> None:
    source = download_source()

    for cut in range(1, len(source)):
        recovered, checkpoint = run_download_with_single_cut(cut)
        assert recovered == source
        assert checkpoint == (cut // DOWNLOAD_SECTOR) * DOWNLOAD_SECTOR

    # A reset immediately after the full artifact arrived is also harmless:
    # the latest complete 4 KiB checkpoint is used and the tail is resent.
    recovered, checkpoint = run_download_with_single_cut(len(source))
    assert recovered == source
    assert checkpoint == 2 * DOWNLOAD_SECTOR



CHECKPOINT_MAGIC = 0x37504344
CHECKPOINT_VERSION = 1
CHECKPOINT_RECORD_SIZE = 40

def checkpoint_record(generation: int, state: int, next_offset: int) -> bytes:
    if state == 0:
        values = (
            CHECKPOINT_MAGIC, generation, CHECKPOINT_VERSION, 0,
            0, 0, 0, 0, 0,
        )
    else:
        values = (
            CHECKPOINT_MAGIC, generation, CHECKPOINT_VERSION, state,
            0x70070001, 2, DOWNLOAD_IMAGE_SIZE,
            zlib.crc32(download_source()) & 0xFFFFFFFF, next_offset,
        )
    prefix = struct.pack("<9I", *values)
    return prefix + struct.pack("<I", zlib.crc32(prefix) & 0xFFFFFFFF)

def checkpoint_valid(record: bytes) -> bool:
    if len(record) != CHECKPOINT_RECORD_SIZE:
        return False
    fields = struct.unpack("<10I", record)
    if fields[0] != CHECKPOINT_MAGIC or fields[1] == 0:
        return False
    if fields[2] != CHECKPOINT_VERSION or fields[3] > 2:
        return False
    if fields[9] != (zlib.crc32(record[:36]) & 0xFFFFFFFF):
        return False
    if fields[3] == 0:
        return fields[4:9] == (0, 0, 0, 0, 0)
    if fields[4] == 0 or fields[6] == 0:
        return False
    if fields[3] == 1:
        return fields[8] <= fields[6] and fields[8] % DOWNLOAD_SECTOR == 0
    return fields[8] == fields[6]

def generation_newer(candidate: int, reference: int) -> bool:
    diff = (candidate - reference) & 0xFFFFFFFF
    return diff != 0 and diff < 0x80000000

def select_checkpoint(a: bytes, b: bytes) -> bytes | None:
    va = checkpoint_valid(a)
    vb = checkpoint_valid(b)
    if va and vb:
        ga = struct.unpack_from("<I", a, 4)[0]
        gb = struct.unpack_from("<I", b, 4)[0]
        return b if generation_newer(gb, ga) else a
    if va:
        return a
    if vb:
        return b
    return None

def test_download_checkpoint_ab_torn_commit() -> None:
    old = checkpoint_record(7, 1, DOWNLOAD_SECTOR)
    new = checkpoint_record(8, 1, 2 * DOWNLOAD_SECTOR)
    erased = bytearray(b"\xFF" * CHECKPOINT_RECORD_SIZE)

    assert checkpoint_valid(old)
    assert checkpoint_valid(new)

    # CRC is the final 4 bytes. Any interrupted target write must leave the old
    # A/B slot selected; only the complete target record may become newest.
    for written in range(CHECKPOINT_RECORD_SIZE):
        target = bytearray(erased)
        target[:written] = new[:written]
        selected = select_checkpoint(old, bytes(target))
        assert selected == old

    assert select_checkpoint(old, new) == new


def source_image() -> bytes:
    data = bytearray(((i * 73 + 19) & 0xFF) for i in range(IMAGE_SIZE))
    # A plausible STM32 vector table so the model resembles a real app image.
    data[0:4] = (0x20005000).to_bytes(4, "little")
    data[4:8] = (0x08006101).to_bytes(4, "little")
    return bytes(data)

def next_checkpoint(size: int, offset: int) -> int:
    return min(size, offset + PAGE)

def run_with_single_cut(cut_offset: int) -> tuple[bytes, int, int]:
    source = source_image()
    internal = bytearray([0x35] * (4 * PAGE))
    copy_offset = 0
    reset_count = 0
    page_restarts = 0
    cut_done = False

    while copy_offset < len(source):
        page_start = copy_offset
        page_end = next_checkpoint(len(source), page_start)

        # Idempotent retry rule: always erase the first unverified page.
        internal[page_start:page_start + PAGE] = b"\xFF" * PAGE

        index = page_start
        while index < page_end:
            internal[index] = source[index]
            index += 1

            if (not cut_done and index >= cut_offset and
                    page_start < cut_offset < page_end):
                # Power loss: no checkpoint was committed for this page.
                cut_done = True
                reset_count += 1
                page_restarts += 1
                break
        else:
            # Compare before committing the persistent copy_offset.
            assert internal[page_start:page_end] == source[page_start:page_end]
            copy_offset = page_end
            continue

        # Reboot. copy_offset still points to page_start.
        assert copy_offset == page_start

    return bytes(internal[:len(source)]), reset_count, page_restarts

def test_all_mid_page_cut_positions() -> None:
    source = source_image()
    exercised = 0

    for cut in range(1, len(source)):
        if cut % PAGE == 0:
            # Exactly at a checkpoint is covered separately below.
            continue
        installed, resets, restarts = run_with_single_cut(cut)
        assert installed == source
        assert resets == 1
        assert restarts == 1
        exercised += 1

    assert exercised > 3000

def test_reset_after_verified_page_before_metadata_commit() -> None:
    source = source_image()
    internal = bytearray([0xA5] * (4 * PAGE))
    copy_offset = 0

    # Page 0 is fully written and verified, but power dies before metadata A/B
    # commits 1024. The old checkpoint remains 0.
    internal[0:PAGE] = source[0:PAGE]
    assert internal[0:PAGE] == source[0:PAGE]
    assert copy_offset == 0

    # Recovery re-erases/reprograms page 0. This is safe and idempotent.
    internal[0:PAGE] = b"\xFF" * PAGE
    internal[0:PAGE] = source[0:PAGE]
    copy_offset = PAGE

    while copy_offset < len(source):
        end = next_checkpoint(len(source), copy_offset)
        internal[copy_offset:copy_offset + PAGE] = b"\xFF" * PAGE
        internal[copy_offset:end] = source[copy_offset:end]
        assert internal[copy_offset:end] == source[copy_offset:end]
        copy_offset = end

    assert internal[:len(source)] == source

def test_final_unaligned_checkpoint() -> None:
    source = source_image()
    assert len(source) % PAGE != 0
    final_page = (len(source) // PAGE) * PAGE
    assert next_checkpoint(len(source), final_page) == len(source)

def main() -> int:
    test_all_mid_page_cut_positions()
    test_reset_after_verified_page_before_metadata_commit()
    test_final_unaligned_checkpoint()
    test_download_checkpoint_recovery_all_cut_positions()
    test_download_checkpoint_ab_torn_commit()
    print(
        "power-loss recovery power-loss model: PASS "
        "(install mid-page + persistent download cuts)"
    )
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
