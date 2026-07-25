# Phase 1 Build Report

## Result

`Phase 1 — Repository and Build Foundation` is complete.

## Implemented

- Independent bootloader and application builds.
- STM32F103C8T6 medium-density CMSIS startup/vector table.
- SPL configuration and GPIO/RCC linkage proof.
- 72 MHz HSE clock setup with HSI fallback.
- Relocated application vector table at `0x08006000`.
- Full linker scripts with Flash/RAM budget assertions.
- Automatic GNU Arm GCC or Clang/LLD toolchain selection.
- ELF, BIN, HEX, MAP, dependency and size outputs.
- Automated Phase 1 structural and build validation.

## Validation performed in the archive-generation environment

```text
Secure Delta OTA Phase 0 specification check: PASS
Secure Delta OTA Phase 1 repository/build check: PASS
Bootloader vector table: 0x08000000
Application vector table: 0x08006000
```

The validation environment used the Clang/LLD fallback because GNU Arm
Embedded GCC was not installed there. The Makefiles also support
`arm-none-eabi-gcc` when available on the development machine.

## Phase boundary

The bootloader does not jump to the application yet. Application validation,
MSP/VTOR handoff and the actual jump are Phase 2 work.
