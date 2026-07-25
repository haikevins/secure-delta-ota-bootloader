# Phase 0 — Specification Freeze Checklist

## Decisions

- [x] MCU fixed: STM32F103C8T6.
- [x] STM32 library fixed: SPL.
- [x] Gateway fixed: ESP32 with ESP-IDF.
- [x] Network path fixed: MQTT notification + HTTPS download.
- [x] Node transport fixed: custom UART, no LoRa protocol.
- [x] UART pins fixed: USART1 PA9/PA10.
- [x] Initial UART mode fixed: 115200 8-N-1.
- [x] Framing fixed: COBS + 0x00 delimiter.
- [x] DATA payload fixed: maximum 256 bytes.
- [x] Packet integrity fixed: CRC32.
- [x] External storage fixed: W25Q32.
- [x] Delta host concept fixed: JojoDiff-compatible.
- [x] Delta target concept fixed: janpatch-compatible streaming adapter.
- [x] Internal application address fixed: 0x08006000.
- [x] Internal metadata address fixed: 0x0800FC00.
- [x] Boot/update states fixed for protocol version 1.
- [x] Container header fields fixed for format version 1.
- [x] Full-image fallback policy defined.
- [x] Trial boot and rollback policy defined.
- [x] Trust boundary defined: bootloader is final verifier.

## Validation

- [x] Internal Flash regions sum to 64 KiB.
- [x] External Flash regions do not overlap.
- [x] External regions are 4 KiB sector aligned.
- [x] Incoming/reconstructed/backup partitions exceed application maximum.
- [x] Protocol commands and status codes are documented.
- [x] Power-loss expectations are documented.
- [x] Phase 0 out-of-scope functionality is documented.

## Exit condition

Phase 0 is complete. Phase 1 may begin with repository build foundation, linker scripts, startup files, SPL/CMSIS integration and size checks.
