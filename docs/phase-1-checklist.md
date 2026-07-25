# Phase 1 — Repository and Build Foundation

## Scope

Phase 1 establishes reproducible, independent STM32F103C8T6 builds for the
bootloader and relocated application. It does **not** implement application
validation or bootloader-to-application transfer; those belong to Phase 2.

## Completed

- [x] Imported STM32F10x SPL V3.5.0 and CMSIS device/core files are wired into the build.
- [x] Shared Make rules select GNU Arm Embedded GCC or Clang/LLD.
- [x] Bootloader and application have independent Makefiles.
- [x] Bootloader linker region is `0x08000000` with a 24 KiB budget.
- [x] Application linker region is `0x08006000` with a 39 KiB budget.
- [x] The final 1 KiB page at `0x0800FC00` remains excluded for metadata.
- [x] Official medium-density startup/vector source is present in both images.
- [x] `SystemInit()` configures 72 MHz from an 8 MHz HSE and falls back to HSI.
- [x] Each image sets `SCB->VTOR` to its own vector-table address.
- [x] SPL GPIO/RCC integration is proven by a minimal PC13 heartbeat image.
- [x] Builds generate `.elf`, `.bin`, `.hex`, `.map`, dependency and size files.
- [x] Strict warnings are enabled for project code.
- [x] `make phase1-check` rebuilds both images and verifies vector addresses and budgets.

## Build commands

```bash
make phase1-check
make bootloader
make application
make firmware
make clean
```

Explicit toolchain selection:

```bash
make phase1-check TOOLCHAIN=gcc
make phase1-check TOOLCHAIN=clang
```

## Expected output paths

```text
node-stm32f103/bootloader/out/
├── bootloader.elf
├── bootloader.bin
├── bootloader.hex
├── bootloader.map
└── bootloader.size.txt

node-stm32f103/application/out/
├── application.elf
├── application.bin
├── application.hex
├── application.map
└── application.size.txt
```

## Phase boundary

The bootloader currently remains in its own heartbeat loop. Phase 2 will add:

- application vector and stack validation;
- interrupt/SysTick cleanup;
- MSP and VTOR handoff;
- jump to the application reset handler;
- repeatable hardware reset/jump tests.
