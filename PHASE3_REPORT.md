# Phase 3 Build Report

## Result

`Phase 3 — Metadata and Boot Decision` is complete at source, embedded-build,
host-unit-test and static-verification level.

## Implemented

- IEEE CRC32 shared module with the standard `123456789` test vector.
- 52-byte persistent `BootMetadata_t` format with magic/version/CRC checks.
- State and progress consistency validation.
- Modular generation comparison including wrap-around.
- Two independently erasable internal-Flash metadata pages.
- Inactive-slot erase/program/read-back/validate commit protocol.
- Default generation-1 initialization on first boot.
- Pure state-to-action boot-decision module.
- Metadata-driven boot manager integration.
- Phase 3 host tests and embedded symbol/storage checks.
- Current combined Phase 3 image generation.

## Intentional memory-map revision

The former 39 KiB application plus one 1 KiB metadata placeholder was changed
to a 38 KiB application plus two 1 KiB metadata pages. This is necessary for
power-loss-safe A/B commits on STM32F103, whose internal Flash erase page is
1 KiB.

## Validation boundary

The automated check validates builds, ELF symbols, linker bounds, CRC vectors,
record corruption, redundant selection, generation wrap and boot-decision
mapping. Physical Flash power-cut behavior still requires Blue Pill testing.

## Next phase

Phase 4 implements the W25Q32 SPI NOR driver and external storage partitions.
It must not add UART OTA transport yet.
