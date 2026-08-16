#!/usr/bin/env python3
"""trial boot and rollback host model: backup checkpoints, trial limit and rollback."""
from __future__ import annotations

APP_SIZE = 38 * 1024
EXT_SECTOR = 4 * 1024
INT_PAGE = 1024
MAX_ATTEMPTS = 3

def source_image(seed: int) -> bytes:
    data = bytearray(((i * 29 + seed) & 0xFF) for i in range(APP_SIZE))
    data[0:4] = (0x20005000).to_bytes(4, "little")
    data[4:8] = (0x08006101).to_bytes(4, "little")
    return bytes(data)

def backup_with_cut(active: bytes, cut: int) -> tuple[bytes, int]:
    external = bytearray(b"\xFF" * APP_SIZE)
    checkpoint = 0

    # First boot up to arbitrary cut.
    for i in range(cut):
        external[i] = active[i]
        if (i + 1) % EXT_SECTOR == 0:
            checkpoint = i + 1

    # Recovery always erases/replays the first uncheckpointed sector.
    if checkpoint < APP_SIZE:
        start = checkpoint
        end = min(start + EXT_SECTOR, APP_SIZE)
        external[start:end] = b"\xFF" * (end - start)

    offset = checkpoint
    while offset < APP_SIZE:
        end = min(offset + EXT_SECTOR, APP_SIZE)
        external[offset:end] = active[offset:end]
        offset = end
        checkpoint = offset

    return bytes(external), checkpoint

def rollback_with_cut(backup: bytes, candidate: bytes, cut: int) -> bytes:
    internal = bytearray(candidate)
    checkpoint = 0
    cut_done = False

    while checkpoint < APP_SIZE:
        page_start = checkpoint
        page_end = min(page_start + INT_PAGE, APP_SIZE)

        internal[page_start:page_start + INT_PAGE] = b"\xFF" * min(
            INT_PAGE, APP_SIZE - page_start
        )

        i = page_start
        while i < page_end:
            internal[i] = backup[i]
            i += 1
            if (not cut_done and
                    page_start < cut < page_end and
                    i >= cut):
                cut_done = True
                break
        else:
            assert internal[page_start:page_end] == backup[page_start:page_end]
            checkpoint = page_end
            continue

        # Reboot: checkpoint still points at page_start, so page is replayed.
        assert checkpoint == page_start

    return bytes(internal)

def test_backup_representative_cuts() -> None:
    active = source_image(17)
    cuts: set[int] = {1, APP_SIZE - 1, APP_SIZE}

    for sector_start in range(0, APP_SIZE, EXT_SECTOR):
        sector_end = min(sector_start + EXT_SECTOR, APP_SIZE)
        if sector_end - sector_start > 2:
            cuts.add(sector_start + 1)
            cuts.add(sector_start + (sector_end - sector_start) // 2)
            cuts.add(sector_end - 1)
        if sector_end < APP_SIZE:
            cuts.add(sector_end)

    for cut in sorted(cuts):
        recovered, checkpoint = backup_with_cut(active, cut)
        assert recovered == active
        assert checkpoint == APP_SIZE

def test_trial_attempt_limit() -> None:
    attempts = 0
    boots = 0

    while attempts < MAX_ATTEMPTS:
        attempts += 1       # persisted before jump
        boots += 1          # unhealthy candidate never confirms
        # watchdog reset

    assert boots == 3
    assert attempts == MAX_ATTEMPTS

def test_healthy_trial_promotes_only_after_confirmation() -> None:
    active_version = 1
    pending_version = 2
    attempts = 0

    attempts += 1
    # Candidate reaches health path.
    confirmed = True
    assert active_version == 1  # not promoted while merely trialing

    if confirmed:
        active_version = pending_version
        pending_version = 0
        attempts = 0

    assert active_version == 2
    assert pending_version == 0
    assert attempts == 0

def test_rollback_representative_mid_page_cuts() -> None:
    backup = source_image(31)
    bad_candidate = source_image(99)

    cuts: set[int] = set()
    for page_start in range(0, APP_SIZE, INT_PAGE):
        page_end = min(page_start + INT_PAGE, APP_SIZE)
        if page_end - page_start > 2:
            cuts.add(page_start + 1)
            cuts.add(page_start + (page_end - page_start) // 2)
            cuts.add(page_end - 1)

    for cut in sorted(cuts):
        restored = rollback_with_cut(backup, bad_candidate, cut)
        assert restored == backup

    assert len(cuts) >= 3 * (APP_SIZE // INT_PAGE)

def main() -> int:
    test_backup_representative_cuts()
    test_trial_attempt_limit()
    test_healthy_trial_promotes_only_after_confirmation()
    test_rollback_representative_mid_page_cuts()
    print(
        "trial boot and rollback trial/rollback model: PASS "
        "(backup cuts + 3 attempts + page-checkpointed rollback)"
    )
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
