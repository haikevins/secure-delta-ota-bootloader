# Phase 2 Build Report

## Result

`Phase 2 — Bootloader Jump to Application` is complete at source, build and
static-verification level.

## Implemented

- Application vector/MSP/reset-handler validation.
- Two-second bootloader startup indication.
- SysTick and NVIC cleanup.
- RCC reset-like deinitialization before handoff.
- VTOR relocation to `0x08006000`.
- Stackless assembly MSP/CONTROL/PRIMASK handoff.
- Direct branch to the application reset handler.
- Application SysTick heartbeat proving relocated interrupts.
- Linker assertions for vector origins and partition bounds.
- Combined bootloader/application binary generation.
- OpenOCD helper scripts for separate or combined flashing.
- Automated `phase2-check` validation.

## Validation performed in the archive-generation environment

```text
Secure Delta OTA Phase 0 specification check: PASS
Secure Delta OTA Phase 1 repository/build check: PASS
Secure Delta OTA Phase 2 bootloader jump check: PASS
```

The archive-generation environment used Clang/LLD. The same Makefiles support
GNU Arm Embedded GCC, which is the preferred toolchain on the target developer
machine.

## Hardware boundary

No physical STM32 target is connected to the archive-generation environment.
The PC13 LED sequence and repeated-reset behavior must therefore be verified on
the user's Blue Pill using `make flash-combined` and the procedure in
`docs/phase-2-checklist.md`.

## Next phase

Phase 3 adds redundant, CRC-protected boot metadata and metadata-driven boot
decisions. It must not introduce OTA installation yet.

## Toolchain propagation fix

The Phase 2 flash entry points were hardened against an inherited empty
`TOOLCHAIN` environment variable. Empty values are now treated as unset, so the
common build rules auto-detect GNU Arm GCC or Clang/LLD. A regression check in
`scripts/phase1_check.py` verifies this behavior.
