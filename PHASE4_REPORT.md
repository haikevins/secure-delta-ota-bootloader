# Phase 4 Report — External SPI Flash

Phase 4 adds the storage foundation required by later Full OTA and Delta OTA
without changing the validated Phase 2 handoff or Phase 3 A/B internal
metadata protocol.

Implemented:

- one common SPL SPI1 driver supporting W25Q32 and W25Q64 basic Phase 4 operations;
- JEDEC identification;
- read, page program, sector erase, busy polling;
- verify-after-write and erased-range checking;
- preflight NOR `0 -> 1` protection;
- overflow-safe geometry helpers;
- partition-relative external storage API;
- host unit tests;
- a destructive hardware self-test isolated to sector `0x3FF000`.

`make phase4-check` validates all prior phases plus Phase 4 source/build
invariants and forces the driver into a linked test application.

`make phase4-hw-test` performs the physical SPI NOR test through OpenOCD.

## OpenOCD connection profile

The physical test runner uses `hla_swd`, `reset_config none`, and adapter speed
1000 kHz. Phase 4 accepts JEDEC `0xEF4016` (W25Q32) and `0xEF4017` (W25Q64).
The logical OTA partition map remains 4 MiB, so the upper half of W25Q64 is not
used by this project.
