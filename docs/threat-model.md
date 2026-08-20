# Threat Model

[↑ Documentation Index](README.md) · [← Root](../README.md) · [← External SPI Flash](external-spi-flash.md) · [Release Process →](release-process.md)

> **Status:** implemented security/recovery baseline with physical negative testing

## Table of contents

- [Assets](#assets)
- [Trust boundaries](#trust-boundaries)
- [Addressed threats](#addressed-threats)
- [Out of scope](#out-of-scope)
## Assets

- bootloader code and embedded public key;
- active application integrity and authenticity;
- update metadata and rollback availability;
- firmware signing private key on the build system;
- update availability and version policy.

## Trust boundaries

- Firmware signing environment is trusted and isolated.
- HTTPS server identity is checked by ESP32.
- ESP32 and UART transport are not the final firmware trust boundary.
- STM32 bootloader is the final authority for authenticity, product compatibility and installation.

## Addressed threats

| Threat | Primary control |
|---|---|
| Random UART corruption | Packet CRC32, sequence and offset |
| Interrupted download | Resume metadata and complete artifact CRC |
| Maliciously modified artifact | Bootloader signature verification |
| Wrong product firmware | Signed product ID and hardware revision |
| Delta applied to wrong base | Base version plus base SHA-256 |
| Downgrade | Monotonic target-version policy |
| Power loss during install | External backup, idempotent page progress |
| New firmware boot failure | Trial boot and rollback |
| Metadata corruption | Redundant copies, CRC32, generation counter |

## Out of scope

- physical invasive attacks;
- debug-port lock policy and irreversible RDP deployment;
- confidentiality of firmware payload;
- secure bootloader self-update;
- protection against a compromised signing environment;
- hardware secure element key storage;
- network denial of service.

## References

- [`architecture.md`](architecture.md)
- [`firmware-container.md`](firmware-container.md)
- [`secure_container.c`](../node-stm32f103/bootloader/src/secure_container.c)

[↑ Documentation Index](README.md) · [← Root](../README.md) · [← External SPI Flash](external-spi-flash.md) · [Release Process →](release-process.md)
