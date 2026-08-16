# Project Report — Secure Delta OTA Bootloader

## Objective

Build a resilient OTA system for a constrained STM32F103 node without giving the MCU a direct network stack. The ESP32 handles network transport while the STM32 owns the update trust boundary, persistent state machine, image reconstruction, installation and rollback.

## Delivered architecture

The implementation separates responsibilities deliberately:

- **CI/release tooling:** builds immutable signed releases and delta/full artifacts.
- **Release server:** serves manifest/artifacts over HTTPS and publishes update commands over MQTTS.
- **ESP32 gateway:** validates command metadata, downloads and caches an SDOT artifact, then forwards it over a resumable UART protocol.
- **STM32 application:** receives the artifact into external NOR and hands installation to the bootloader.
- **STM32 bootloader:** verifies the signed container, validates base/target constraints, reconstructs a delta when needed, backs up the active image, installs, verifies and trial-boots.
- **Persistent metadata:** dual-copy internal metadata plus external checkpoints provide reset recovery.

## Security properties

- ECDSA P-256 release signatures.
- SHA-256 payload/base/target binding.
- Key-ID based trust-anchor selection.
- Anti-downgrade target-version checks.
- Unsigned secure-path artifacts rejected.
- Private signing key kept outside firmware and outside the packaged repository.
- HTTPS server authentication and MQTTS server verification on the gateway.

## Reliability properties

Persistent checkpoints cover reception, delta reconstruction, backup, internal-flash installation and rollback. The hardware test matrix deliberately injects resets and transport failures at deterministic boundaries. The final test verifies exact rollback restoration after a reset during rollback.

## Verified hardware result

Nine deterministic scenarios passed on the STM32F103 + ESP32 + W25Q setup. The final scenario ended with:

```text
active_version=1
pending_version=0
boot_attempts=0
last_error=0x0008B003
application verification=PASS
```

The diagnostic is intentionally preserved after successful recovery.

## Benchmark summary

Latest verified GCC run:

```text
bootloader flash       9412 B / 24576 B
application v2 flash   9648 B / 38912 B
raw delta              1242 B
signed delta           1446 B
signed full            9852 B
raw savings            87.13%
signed savings         85.32%
```

## Portfolio value

This repository demonstrates embedded bootloader design, persistent recovery, NOR-flash constraints, custom UART framing, secure release engineering, public-key verification, binary delta reconstruction, ESP-IDF networking, TLS/MQTT integration, fault injection and hardware-in-the-loop validation in one end-to-end system.
