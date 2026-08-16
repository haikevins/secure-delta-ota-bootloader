# Secure Delta OTA Bootloader — One-Page Portfolio Summary

## Problem

Safely update an STM32F103C8T6 over an unreliable network and power environment while keeping the constrained MCU free of a network/TLS stack.

## Architecture

- **STM32F103C8T6:** owns the update trust boundary, persistent state machine, delta reconstruction, install, trial boot and rollback.
- **ESP32:** MQTTS orchestration, HTTPS artifact download/cache and UART forwarding.
- **W25Q SPI NOR:** incoming artifact, reconstructed image, validated backup and persistent OTA checkpoints.
- **Release tooling/server:** immutable signed full/delta releases and signed manifest verification.

## Security

- SHA-256 integrity.
- ECDSA P-256 signatures.
- signed SDOT container.
- key-ID trust anchor.
- anti-downgrade checks.
- private signing key never embedded in device firmware.
- unsigned secure-path artifacts rejected.

## Delta update

Host tooling generates a JojoDiff-compatible patch. STM32 reconstructs the target through a project-owned streaming adapter, validates the base image before patching, and validates the reconstructed target before installation.

## Recovery

The update process survives resets during:

- patching;
- backup;
- internal-flash installation;
- rollback.

A candidate must confirm health. Failure triggers restoration from a separately validated backup.

## Hardware evidence

Deterministic HIL matrix: **9/9 PASS**.

It includes MQTTS disconnect, truncated HTTPS transfer, signature tamper, rollback and reset-during-rollback cases.

## Measured GCC result

```text
Bootloader flash       9412 B / 24 KiB
Application v2 flash   9648 B / 38 KiB
Raw delta              1242 B
Signed delta           1446 B
Signed full            9852 B
Raw savings            87.13%
Signed savings         85.32%
```

## Skills demonstrated

Embedded C, Cortex-M3 boot flow, linker/memory layout, SPI NOR constraints, persistent metadata, COBS UART protocol, binary diff/patch, SHA-256, ECDSA P-256, ESP-IDF, HTTPS, MQTTS, release engineering, deterministic fault injection, SWD/OpenOCD and HIL automation.
