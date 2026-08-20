# `mqtt_orchestrator`

> **Scope:** MQTTS command ingestion plus status/progress publication for one serialized STM32 update at a time; firmware bytes remain on HTTPS.

[← HTTPS Download](../https_download/README.md) · [Gateway](../../README.md) · [UART OTA →](../uart_ota/README.md)

## Topics

```text
<base>/<device_id>/command
<base>/<device_id>/status
<base>/<device_id>/progress
```

Default topic root/device is `sdota/bluepill-001`.

## Command contract

Schema version `1` contains a non-zero `update_id`, positive target version, expected artifact size, artifact CRC32, and HTTPS URL. Maximum URL length is `255` bytes; maximum reconstructed MQTT data payload is `767` bytes.

```json
{
  "schema": 1,
  "cmd": "update",
  "update_id": 2953510913,
  "target_version": 2,
  "size": 1174,
  "crc32": 2321302940,
  "url": "https://firmware.example/releases/fw-v2/application-v1-to-v2.delta.sdot"
}
```

## Concurrency rule

The ESP-MQTT callback performs only bounded orchestration work:

```text
MQTT_EVENT_DATA
   -> fragment reassembly
   -> schema validation
   -> enqueue one command
   -> return
```

HTTPS download and UART OTA run in the dedicated gateway worker, not inside the MQTT event callback. The command queue length is `1`, so the current implementation deliberately serializes updates.

Status is QoS 1. Terminal status may wait for PUBACK before teardown. Progress is published separately and does not replace the persistent STM32 state machine.

## Implementation references

- [`include/mqtt_orchestrator.h`](include/mqtt_orchestrator.h)
- [`include/mqtt_orchestration_contract.h`](include/mqtt_orchestration_contract.h)
- [`mqtt_orchestrator.c`](mqtt_orchestrator.c)
