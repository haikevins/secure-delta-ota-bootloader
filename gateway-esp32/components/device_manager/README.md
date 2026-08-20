# `device_manager` — Device Manager

> **Status:** Reserved scaffold; not part of the active ESP32 runtime component graph.

[← Gateway](../../README.md)

## Current project role

No `.c`/public header is currently wired into the gateway build. Device identity is supplied through Kconfig/runtime configuration and MQTT topic construction in the active implementation.

Keeping this distinction explicit prevents the documentation from presenting future component names as implemented functionality.

## Active implementation references

- [Gateway README](../../README.md)
- [`../../main/CMakeLists.txt`](../../main/CMakeLists.txt)
- [`../../main/gateway_manager.c`](../../main/gateway_manager.c)
