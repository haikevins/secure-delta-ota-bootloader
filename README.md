# Secure Delta OTA Bootloader

Secure Delta OTA reference project for **STM32F103C8T6 + ESP32 + external SPI NOR Flash**.

## Project status

- **Phase 0 — Specification Freeze:** complete.
- **Phase 1 — Repository and Build Foundation:** complete.
- **Phase 2 — Bootloader Jump to Application:** complete.
- **Phase 3 — Metadata and Boot Decision:** complete.
- **Phase 4 — External SPI Flash:** not implemented yet.

STM32 code uses **SPL/CMSIS**, not HAL.

## Current boot flow

```text
Reset at 0x08000000
        |
        v
Read Metadata A (0x0800F800) and B (0x0800FC00)
        |
        +-- neither valid --> create IDLE defaults, commit generation 1
        |
        v
Select newest valid CRC-protected generation
        |
        v
Validate application vectors at 0x08006000
        |
        v
Map persisted update state to explicit boot action
        |
        +-- IDLE / RECEIVING / allowed TRIAL_BOOT --> Phase 2 handoff
        |
        +-- later-phase recovery action --> remain in bootloader (9 pulses)
```

## Build and verify Phase 3

```bash
make phase3-check
```

Explicit toolchain selection remains supported:

```bash
make phase3-check TOOLCHAIN=gcc
make phase3-check TOOLCHAIN=clang
```

Useful targets:

```bash
make bootloader
make application
make combined
make flash-combined
make dump-metadata
make erase-metadata
make test
make clean
```

`make combined` creates:

```text
dist/secure-delta-ota-phase3.bin
dist/secure-delta-ota-phase3.txt
```

The combined image does not contain metadata bytes. On a fresh/erased device, first boot initializes slot A and then jumps to the application. Use `make erase-metadata` when you intentionally need a clean Phase 3 metadata test; normal combined flashing preserves existing metadata. Use `make dump-metadata` to read and decode both pages.

## Internal Flash map

```text
0x08000000 +---------------------------+
           | Bootloader: 24 KiB        |
0x08006000 +---------------------------+
           | Application: 38 KiB       |
0x0800F800 +---------------------------+
           | Metadata A: 1 KiB         |
0x0800FC00 +---------------------------+
           | Metadata B: 1 KiB         |
0x08010000 +---------------------------+
```

## Expected hardware behavior

With default `UPDATE_IDLE` metadata, Phase 2 behavior remains:

1. bootloader: five fast PC13 flashes;
2. one-second pause;
3. application: one short PC13 flash each second.

A valid state requiring future install/rollback logic remains in bootloader and
shows nine pulses repeatedly.

## Phase 3 documents

- `docs/metadata-and-boot-decision.md`
- `docs/phase-3-checklist.md`
- `PHASE3_REPORT.md`

The complete specification remains under `docs/`.
