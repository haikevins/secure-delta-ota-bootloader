# Secure Delta OTA Bootloader

Secure Delta OTA reference project for **STM32F103C8T6 + ESP32 + external SPI NOR Flash**.

## Project status

This archive completes **Phase 0 — Specification Freeze** only. It intentionally contains a repository skeleton and interface placeholders; production firmware is not implemented yet.

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

## Fixed Phase 0 decisions

- MCU: STM32F103C8T6, Cortex-M3, 64 KiB internal Flash, 20 KiB SRAM.
- STM32 framework: Standard Peripheral Library (SPL), not HAL.
- Gateway: ESP32 using ESP-IDF.
- OTA network path: MQTT notification and HTTPS artifact download.
- Gateway-to-node transport: custom UART protocol; LoRa is not used.
- UART framing: COBS with `0x00` delimiter.
- UART initial configuration: USART1, 115200 baud, 8-N-1, no flow control.
- OTA payload per DATA packet: 256 bytes maximum.
- External storage: W25Q32, 4 MiB SPI NOR Flash.
- Delta generation: JojoDiff-compatible host tool.
- Delta application: janpatch-compatible streaming patch adapter.
- Integrity progression: packet CRC32, artifact CRC32, SHA-256.
- Authenticity target: signed container verified by bootloader; no AES in the first secure release.
- Internal application start: `0x08006000`.
- Boot metadata page: `0x0800FC00`–`0x0800FFFF`.

## Phase 0 documents

- `docs/architecture.md`
- `docs/memory-map.md`
- `docs/uart-ota-protocol.md`
- `docs/firmware-container.md`
- `docs/boot-state-machine.md`
- `docs/threat-model.md`
- `docs/release-process.md`
- `docs/test-plan.md`
- `docs/phase-0-checklist.md`

## Build status

The Makefiles are orchestration placeholders. Phase 1 will add startup code, linker scripts, SPL/CMSIS sources, compiler configuration and reproducible builds.
