# Secure Delta OTA Bootloader

Reference project for **STM32F103C8T6 + ESP32 + external SPI NOR Flash**.

STM32 uses **Standard Peripheral Library + CMSIS**, not HAL. ESP32 uses
**ESP-IDF**.

## Status

- Phase 0 — Specification: complete.
- Phase 1 — Build foundation: complete.
- Phase 2 — Application handoff: complete + hardware verified.
- Phase 3 — Metadata/boot decision: complete + hardware verified.
- Phase 4 — External SPI Flash: complete + hardware verified.
- Phase 5 — UART protocol with PC Python: complete + hardware verified.
- Phase 6 — Basic Full OTA: complete + hardware verified.
- Phase 7 — Power-loss recovery: complete + hardware fault-injection verified.
- Phase 8 — Trial boot and rollback: complete + hardware verified.
- Phase 9 — ESP32 UART Gateway: implemented; hardware test provided.

## Phase 9

```text
ESP32 embedded/cache artifact
      |
      v
UART2 GPIO17/GPIO16
      |
COBS + 0x00 + CRC32
      |
STM32 USART1 PA10/PA9
      |
W25Q -> backup -> install -> trial -> confirm
```

Default wiring:

```text
ESP32 GPIO17 TX -> STM32 PA10 RX
ESP32 GPIO16 RX <- STM32 PA9 TX
ESP32 GND       -> STM32 GND
```

Use 3.3 V logic.

## STM32 validation

```bash
make phase9-check
make combined
make flash-combined
```

Combined STM32 image:

```text
dist/secure-delta-ota-phase9.bin
```

## ESP32 build

Activate your ESP-IDF environment first:

```bash
make phase9-gateway-build
```

Phase 9 targets classic `esp32` and uses UART2 while UART0 remains the console.

## End-to-end hardware test

Keep ST-Link attached to STM32 and use the ESP32 USB serial port as
`ESP32_PORT`:

```bash
make phase9-hw-test ESP32_PORT=/dev/ttyUSB0
```

See:

- `docs/phase-9-esp32-uart-gateway.md`
- `docs/phase-9-checklist.md`
- `PHASE9_REPORT.md`
