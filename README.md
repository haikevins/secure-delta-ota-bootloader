# Secure Delta OTA Bootloader

Reference project for **STM32F103C8T6 + ESP32 + external SPI NOR Flash** using
**STM32 SPL/CMSIS**, not HAL.

## Status

- Phase 0 — Specification: complete.
- Phase 1 — Build foundation: complete.
- Phase 2 — Application handoff: complete + hardware verified.
- Phase 3 — Metadata/boot decision: complete + hardware verified.
- Phase 4 — External SPI Flash: complete + hardware verified.
- Phase 5 — UART protocol with PC Python: complete + hardware verified.
- Phase 6 — Basic Full OTA: implemented; end-to-end hardware test provided.

## Phase 6 path

```text
PC Python
  -> USART1 115200 8-N-1
  -> COBS + packet CRC32
  -> running STM32 application
  -> W25Q Incoming Artifact
  -> INSTALL
  -> reset to bootloader
  -> validate / erase / program / verify
  -> boot updated application
```

Phase 6 uses a raw application binary and CRC32. It deliberately does not claim
cryptographic security yet.

## Build

```bash
make phase6-check
make combined
make flash-combined
```

Combined image:

```text
dist/secure-delta-ota-phase6.bin
```

## UART wiring

```text
PA9  TX -> USB-UART RX
PA10 RX <- USB-UART TX
GND     -> USB-UART GND
```

Use 3.3 V UART logic.

## Phase 5 protocol regression

```bash
python3 -m pip install -r tools/requirements-phase5.txt
make phase5-hw-test PORT=/dev/ttyUSB0
```

## Phase 6 end-to-end OTA

Prepare a clean normal-v1 baseline:

```bash
make erase-metadata
make flash-combined
```

Then build/send a v2 candidate:

```bash
make phase6-hw-test PORT=/dev/ttyUSB0
```

See:

- `docs/phase-6-full-ota-basic.md`
- `docs/phase-6-checklist.md`
- `PHASE6_REPORT.md`
