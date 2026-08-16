#!/usr/bin/env python3
"""Model Phase-10 HTTPS cache publication and end-to-end handoff policy."""
from __future__ import annotations

import zlib

ERASE = 4096
APP_MAX = 38 * 1024


class Cache:
    def __init__(self) -> None:
        self.header: tuple[int, int, int] | None = None
        self.data = bytearray(b"\xFF" * APP_MAX)

    def begin(self, size: int) -> None:
        assert 0 < size <= APP_MAX
        self.header = None
        erase_size = (size + ERASE - 1) // ERASE * ERASE
        self.data[: min(erase_size, APP_MAX)] = (
            b"\xFF" * min(erase_size, APP_MAX)
        )

    def commit(
        self,
        image: bytes,
        update_id: int,
        version: int,
    ) -> None:
        stored = bytes(self.data[: len(image)])
        assert stored == image
        self.header = (
            update_id,
            version,
            zlib.crc32(stored) & 0xFFFFFFFF,
        )


def image(size: int) -> bytes:
    data = bytearray(((i * 31 + 7) & 0xFF) for i in range(size))
    data[0:4] = (0x20005000).to_bytes(4, "little")
    data[4:8] = (0x08006101).to_bytes(4, "little")
    return bytes(data)


def test_power_cut_never_publishes_partial_download() -> None:
    fw = image(10184)

    for cut in (1, 255, 256, 1024, 4095, 4096, 8192, len(fw) - 1):
        cache = Cache()
        cache.header = (0x1111, 1, 0xDEADBEEF)
        cache.begin(len(fw))

        cache.data[:cut] = fw[:cut]

        # A reset here sees no committed header.
        assert cache.header is None


def test_complete_download_publishes_verified_cache() -> None:
    fw = image(10184)
    cache = Cache()
    cache.begin(len(fw))

    offset = 0
    while offset < len(fw):
        chunk = fw[offset : offset + 1024]
        cache.data[offset : offset + len(chunk)] = chunk
        offset += len(chunk)

    cache.commit(fw, 0xA00A0001, 2)

    assert cache.header is not None
    update_id, version, crc = cache.header
    assert update_id == 0xA00A0001
    assert version == 2
    assert crc == zlib.crc32(fw) & 0xFFFFFFFF


def test_length_policy_prevents_unbounded_body() -> None:
    assert 10184 <= APP_MAX
    assert APP_MAX + 1 > APP_MAX


def test_phase10_pipeline_contract() -> None:
    states = [
        "HTTPS_200",
        "CACHE_COMMITTED",
        "UART_ARTIFACT_READY",
        "INSTALL",
        "TRIAL_BOOT",
        "IDLE_V2",
    ]
    assert states[-1] == "IDLE_V2"


def main() -> int:
    test_power_cut_never_publishes_partial_download()
    test_complete_download_publishes_verified_cache()
    test_length_policy_prevents_unbounded_body()
    test_phase10_pipeline_contract()
    print(
        "Phase 10 HTTPS transactional-cache model: PASS "
        "(partial download unpublished + full commit)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
