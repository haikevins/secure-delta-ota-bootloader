# Secure Delta OTA Bootloader

Secure Delta OTA reference project for **STM32F103C8T6 + ESP32 + external SPI NOR Flash**.

## Project status

- **Phase 0 — Specification Freeze:** complete.
- **Phase 1 — Repository and Build Foundation:** complete.
- **Phase 2 — Bootloader Jump to Application:** complete.
- **Phase 3 — Metadata and Boot Decision:** not implemented yet.

The STM32 code uses the **STM32 Standard Peripheral Library (SPL)** and CMSIS,
not HAL.

## Current boot flow

```text
Reset at 0x08000000
        |
        v
Bootloader clock + PC13 startup indication
        |
        v
Validate application vectors at 0x08006000
        |
        +-- invalid --> repeat LED error code
        |
        v
Stop SysTick, clear NVIC, deinit RCC
        |
        v
Set VTOR and MSP, branch to application Reset_Handler
        |
        v
Application configures its own clock and SysTick
        |
        v
100 ms PC13 heartbeat every second
```

## Build and verify Phase 2

The build prefers `arm-none-eabi-gcc` and falls back to Clang/LLD.

```bash
make phase2-check
```

Explicit toolchain:

```bash
make phase2-check TOOLCHAIN=gcc
make phase2-check TOOLCHAIN=clang
```

An unset or empty `TOOLCHAIN` value uses automatic detection. This means both
commands below are valid when GNU Arm GCC is installed:

```bash
make flash-combined
TOOLCHAIN="" make flash-combined
```

See `docs/toolchain-selection.md` for the propagation rules and troubleshooting.

Other useful targets:

```bash
make bootloader
make application
make firmware
make combined
make flash-bootloader
make flash-application
make flash-combined
make clean
```

`make combined` creates:

```text
dist/secure-delta-ota-phase2.bin
dist/secure-delta-ota-phase2.txt
```

The combined binary is flashed at `0x08000000`; its application bytes are
placed at offset `0x6000`. The metadata page is left erased.

## Expected hardware behavior

After `make flash-combined`:

1. bootloader: five fast PC13 flashes;
2. one-second pause;
3. application: one short PC13 flash each second.

The application pattern uses SysTick and therefore verifies that the relocated
vector table works after the jump.

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
- `docs/boot-jump.md`
- `docs/boot-state-machine.md`
- `docs/uart-ota-protocol.md`
- `docs/firmware-container.md`
- `docs/threat-model.md`
- `docs/release-process.md`
- `docs/test-plan.md`
- `docs/toolchain-selection.md`
- `docs/phase-0-checklist.md`
- `docs/phase-1-checklist.md`
- `docs/phase-2-checklist.md`
