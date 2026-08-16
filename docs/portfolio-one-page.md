# Secure Delta OTA Bootloader — Portfolio One-Page

## Problem

Deliver reliable remote firmware updates to a constrained **STM32F103C8T6**
without trusting the network path and without bricking the device when power or
transport fails.

## System

```text
CI / release signer
      |
      | signed SDOT metadata + HTTPS URL over MQTTS
      v
    ESP32
      |
      | HTTPS download/cache
      | COBS-framed UART, ACK/NACK/resume
      v
 STM32F103C8T6
      |
      +--> W25Q SPI NOR: incoming / reconstructed / backup / checkpoints
      |
      +--> ECDSA P-256 + SHA-256 verification
      +--> JojoDiff-compatible streaming delta reconstruction
      +--> backup -> install -> verify -> trial -> confirm
      +--> rollback on unhealthy candidate
```

## Engineering highlights

- **Secure container:** SHA-256 + **ECDSA P-256**, key ID, signed metadata,
  anti-downgrade, unsigned images disabled.
- **Delta OTA:** JojoDiff-compatible host generator and project-owned streaming
  patch adapter; full image remains available as fallback.
- **Power-loss safety:** persistent checkpoints for patching, backup, internal
  flash install, trial boot, and rollback.
- **Transport:** HTTPS for bytes, MQTTS for orchestration/status, UART COBS
  frames with CRC32, sequence, offset, ACK/NACK/retry/resume.
- **External storage:** W25Q SPI NOR isolates incoming/reconstructed/backup data
  from the active internal application.
- **Release safety:** immutable signed releases, exact previous binary for delta,
  external private-key custody, key fingerprint pinning, QoS1 terminal status.

## Proof, not just design

Physical Phase 16 HIL finished **9/9** deterministic scenarios:

1. secure delta control;
2. reset during patch;
3. reset during backup;
4. reset during internal-flash install;
5. MQTT disconnect recovery;
6. truncated HTTPS body rejection;
7. transport-valid **tampered-signature** rejection;
8. automatic rollback;
9. reset during rollback.

Final rollback test restored exact confirmed v1 and preserved rollback
diagnostic `0x0008B003`.

## Benchmark

Run `make phase17-benchmark` for the final source-tree numbers. The checked-in
reference shows bootloader/application footprint, raw and signed delta savings,
and Incoming-partition occupancy. Absolute host build times are reported only
as environment-specific observations.

## Why this is portfolio-grade

The repository connects embedded flash semantics, cryptographic trust,
distributed transport, release engineering, deterministic fault injection and
hardware evidence in one end-to-end system. Each major claim maps to a
reproduction command in `docs/portfolio-evidence.md`.
