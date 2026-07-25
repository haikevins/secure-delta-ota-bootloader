# Phase 2 — Bootloader Jump to Application

## Scope

Phase 2 implements a safe handoff from the bootloader image at `0x08000000` to
the relocated application at `0x08006000`. Update metadata, OTA transport,
external Flash and image authentication remain outside this phase.

## Completed

- [x] Bootloader reads the first two application vector words.
- [x] Application vector table address must be aligned to `0x200` bytes.
- [x] Initial MSP must lie in STM32F103C8T6 SRAM and be 8-byte aligned.
- [x] Reset handler must lie inside the application partition.
- [x] Reset handler must set the Cortex-M Thumb-state bit.
- [x] Bootloader uses a visible two-second startup window before handoff.
- [x] SysTick is stopped and cleared before the jump.
- [x] All NVIC enable and pending banks are cleared.
- [x] Pending SysTick and PendSV exceptions are cleared.
- [x] RCC is returned to its reset-like HSI configuration.
- [x] `SCB->VTOR` is set to `0x08006000`.
- [x] A dedicated assembly helper changes MSP without using a C stack frame.
- [x] CONTROL, PSP, BASEPRI, FAULTMASK and PRIMASK are restored to reset-like values.
- [x] The helper branches directly to the application reset handler.
- [x] Application uses its own SysTick handler to prove vector relocation.
- [x] Linker assertions pin both vector tables to their partition origins.
- [x] A combined flash image can be generated for hardware testing.
- [x] Automated build/static handoff validation is available as `make phase2-check`.

## Build and verification

```bash
make phase2-check
```

Explicit toolchain:

```bash
make phase2-check TOOLCHAIN=gcc
make phase2-check TOOLCHAIN=clang
```

The check validates:

- both Phase 1 images still build;
- application MSP and reset vector contents;
- bootloader and application SysTick vector entries;
- required handoff symbols;
- handoff assembly instructions;
- merged-image placement and erased gap.

## Hardware test

```bash
make flash-combined
```

Expected PC13 LED sequence:

1. five fast bootloader flashes;
2. approximately one second with the LED off;
3. application heartbeat: about 100 ms on every one second.

The application heartbeat depends on its relocated SysTick vector. A steady
error blink indicates that the application observed an unexpected VTOR value or
could not configure SysTick.

## Invalid-application LED codes

The bootloader repeats a pulse group followed by a pause:

| Pulses | Validation failure |
|---:|---|
| 1 | vector-table alignment |
| 2 | vector table outside the application partition |
| 3 | initial MSP outside SRAM |
| 4 | initial MSP alignment |
| 5 | reset handler Thumb bit |
| 6 | reset handler outside application partition |
| 7 | internal bootloader/SysTick initialization failure |

## Phase boundary

Phase 3 will persist redundant boot metadata and use it to make the boot
decision. Phase 2 always attempts the active application after validation.
