# Phase 7 Report — Power-loss Recovery

Phase 7 adds persistent recovery to the full-image OTA path already validated in
Phase 6.

## Recovery points

### UART download

The application stores a redundant CRC-protected download checkpoint in
external Metadata A/B every 4 KiB. After reset, a matching PC sender receives
the persisted offset through QUERY/RESUME and retransmits from that sector
boundary.

The 4 KiB cadence matches W25Q erase sectors and avoids rewriting metadata once
per 256-byte DATA packet.

### Internal installation

The bootloader changes from whole-image restart to page-checkpointed
installation. `BootMetadata.copy_offset` advances only after a 1 KiB STM32
Flash page has been fully programmed and byte-verified.

A torn first-unverified page is always erased again before retry.

## A/B ordering

Both internal boot metadata and external download/handoff records retain an
older valid copy while a replacement copy is erased/programmed/verified.
Progress is therefore conservative after a reset: it may move backward to an
older checkpoint, but it must not skip unverified data.

## Validation

Host tests exercise:

- download checkpoint CRC, A/B selection and generation wrap;
- legal/illegal install offsets;
- boot decisions for partial invalid applications in `INSTALLING`;
- every internal mid-page reset position in a non-page-aligned image;
- persistent download recovery and retransmission;
- torn external A/B checkpoint commits, where the older valid slot stays selected.

Hardware test command:

```bash
python3 -m pip install -r tools/requirements-phase5.txt
make phase7-hw-test PORT=/dev/ttyUSB0
```

The hardware test intentionally resets once after runtime download progress
reaches 4608 bytes and expects a persistent restart at 4096 bytes. It also uses
a test-only bootloader that resets once at internal copy offset 1536, then
proves application v2 boots and restores the normal bootloader.

Phase 7 does not add rollback or trial boot, and CRC32 remains integrity only.

## Measured validation build

```text
bootloader.bin                8512 bytes / 24 KiB
application-v1.bin           11300 bytes / 38 KiB
application-v2.bin           11312 bytes / 38 KiB
fault-injection bootloader   8680 bytes / 24 KiB
combined Phase 7             35876 bytes
```

Repository validation with Clang/LLD:

```text
Phase 0: PASS
Phase 1: PASS
Phase 2: PASS
Phase 3: PASS
Phase 4: PASS
Phase 5: PASS
Phase 6: PASS
Phase 7 host/build check: PASS
```

Physical Phase-7 fault-injection validation remains pending until
`make phase7-hw-test PORT=...` is run on the board.
