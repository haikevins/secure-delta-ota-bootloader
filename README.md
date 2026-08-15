# Secure Delta OTA Bootloader

Reference project for **STM32F103C8T6 + ESP32 + external SPI NOR Flash** using
**SPL/CMSIS**, not HAL.

## Status

- Phase 0 — Specification: complete.
- Phase 1 — Build foundation: complete.
- Phase 2 — Application handoff: complete + hardware verified.
- Phase 3 — Metadata/boot decision: complete + hardware verified.
- Phase 4 — External SPI Flash: complete + hardware verified.
- Phase 5 — UART protocol with PC Python: implemented; hardware test provided.

## Phase 5 path

```text
PC Python
  -> USART1 115200 8-N-1
  -> COBS + 0x00 + packet CRC32
  -> STM32 application
  -> Incoming Artifact partition
```

Phase 5 stops at `UPDATE_ARTIFACT_READY`; installation starts in Phase 6.

## Build

```bash
make phase5-check
make flash-combined
```

`make combined` creates:

```text
dist/secure-delta-ota-phase5.bin
dist/secure-delta-ota-phase5.txt
```

## UART wiring

```text
PA9  TX -> USB-UART RX
PA10 RX <- USB-UART TX
GND     -> USB-UART GND
```

Use 3.3 V UART logic.

Install PC dependency and test:

```bash
python3 -m pip install -r tools/requirements-phase5.txt
python3 tools/uart_ota_sender.py --port /dev/ttyUSB0 hello
make phase5-hw-test PORT=/dev/ttyUSB0
```

See `docs/phase-5-uart-pc-protocol.md` and `PHASE5_REPORT.md`.
