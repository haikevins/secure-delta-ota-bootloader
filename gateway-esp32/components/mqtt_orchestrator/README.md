# mqtt_orchestrator — Phase 11

Phase 11 uses MQTT only for orchestration/status. Firmware bytes remain on the
Phase-10 HTTPS path.

Topics:

```text
<base>/<device_id>/command
<base>/<device_id>/status
<base>/<device_id>/progress
```

Default:

```text
sdota/bluepill-001/command
sdota/bluepill-001/status
sdota/bluepill-001/progress
```

Command payload:

```json
{
  "schema": 1,
  "cmd": "update",
  "update_id": 2953510913,
  "target_version": 2,
  "size": 10184,
  "crc32": 2321302940,
  "url": "https://192.168.1.8:8443/phase11_candidate.bin"
}
```

The command topic is subscribed at QoS 1. Status is queued at QoS 1; progress
uses QoS 0. The MQTT event callback only reassembles/parses/enqueues commands.
HTTPS download and STM32 UART OTA are performed by the Phase-11 gateway worker,
not by the MQTT task.

`MQTT_EVENT_DATA` fragmentation is handled with `total_data_len` and
`current_data_offset`, bounded to 767 bytes.

Production requires `mqtts://` and broker certificate verification.
