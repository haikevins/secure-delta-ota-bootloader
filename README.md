# Secure Delta OTA Bootloader

Secure Delta OTA reference project for **STM32F103C8T6 + ESP32 + external SPI NOR Flash**.

## Project status

- **Phase 0 — Specification Freeze:** complete.
- **Phase 1 — Repository and Build Foundation:** complete.
- **Phase 2 — Bootloader Jump to Application:** not implemented yet.

The STM32 code uses the **STM32 Standard Peripheral Library (SPL)** and CMSIS,
not HAL. The current bootloader and application are independent, buildable
heartbeat images used to verify the toolchain, startup code, linker layout and
SPL integration.

## Chosen architecture

```text
Firmware Server / CI
    | MQTT notification + HTTPS download
    v
ESP32 Gateway
    | Custom UART OTA protocol: COBS + CRC32 + ACK/NACK + resume
    v
STM32F103C8T6 Application
    | stores update artifact
    v
External W25Q32 SPI NOR Flash
    | reset into bootloader
    v
STM32F103C8T6 Secure Bootloader
    | verify -> patch/full reconstruction -> backup -> install -> trial boot -> rollback
    v
STM32 Application
```

## Phase 1 build

The build automatically prefers `arm-none-eabi-gcc`. When GNU Arm Embedded GCC
is unavailable, it can use `clang`, `ld.lld` and `llvm-objcopy`.

```bash
make phase1-check
```

Other targets:

```bash
make bootloader
make application
make firmware
make toolchain-info
make clean
```

Explicit toolchain selection:

```bash
make phase1-check TOOLCHAIN=gcc
make phase1-check TOOLCHAIN=clang
```

Each STM32 image produces ELF, BIN, HEX, MAP and size-report artifacts in its
local `out/` directory.

## Internal Flash map

```text
0x08000000 +---------------------------+
           | Bootloader: 24 KiB        |
0x08006000 +---------------------------+
           | Application: 39 KiB       |
0x0800FC00 +---------------------------+
           | Boot metadata: 1 KiB      |
0x08010000 +---------------------------+
```

## Documentation

- `docs/architecture.md`
- `docs/memory-map.md`
- `docs/uart-ota-protocol.md`
- `docs/firmware-container.md`
- `docs/boot-state-machine.md`
- `docs/threat-model.md`
- `docs/release-process.md`
- `docs/test-plan.md`
- `docs/phase-0-checklist.md`
- `docs/phase-1-checklist.md`

## Current hardware behavior

The Phase 1 bootloader and application each configure PC13 as an active-low
status LED and blink with different patterns. They are intended to be flashed
and tested independently. The bootloader does not jump to the application until
Phase 2.
