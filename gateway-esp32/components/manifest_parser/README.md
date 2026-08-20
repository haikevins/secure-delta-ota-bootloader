# `manifest_parser` — Manifest Parser (Reserved)

> **Status:** Reserved scaffold; not part of the active ESP32 runtime component graph.

[← Gateway](../../README.md)

## Current project role

The ESP32 does not parse the release manifest in the current architecture. The server verifies the signed manifest and publishes a bounded artifact command; the gateway validates that command and the STM32 bootloader verifies the SDOT itself.

Keeping this distinction explicit prevents the documentation from presenting future component names as implemented functionality.

## Active implementation references

- [Gateway README](../../README.md)
- [`../../main/CMakeLists.txt`](../../main/CMakeLists.txt)
- [`../../main/gateway_manager.c`](../../main/gateway_manager.c)
