# Phase 11 Report — MQTT Orchestration

Phase 11 wraps the hardware-verified HTTPS/UART OTA pipeline with MQTT
command, progress and final-state reporting.

## Pipeline

```text
MQTTS command
  -> validate command metadata
  -> HTTPS GET
  -> transactional ESP32 cache
  -> UART OTA
  -> STM32 backup/install
  -> trial/confirm
  -> MQTTS confirmed status
```

MQTT never transports the firmware body.

## Command contract

The command is JSON schema v1 and contains:

```text
cmd=update
update_id
target_version
size
crc32
https url
```

The ESP32 rejects wrong schema, zero IDs/versions, an oversized image or a
non-HTTPS URL. After HTTPS download it compares actual image size and CRC32
against the MQTT command before exposing the cache to UART.

CRC32 remains integrity-only.

## ESP-MQTT architecture

The MQTT event handler is intentionally short. It reassembles fragmented
`MQTT_EVENT_DATA`, parses the JSON and queues a command. It does not call the
HTTPS downloader or UART OTA state machine.

Long operations remain on the dedicated Phase-11 gateway worker task. The
existing 16 KiB worker stack and stack high-water telemetry are retained.

## MQTT topics

Default:

```text
sdota/bluepill-001/command
sdota/bluepill-001/status
sdota/bluepill-001/progress
```

Normal status lifecycle:

```text
online -> accepted -> downloaded -> installing -> confirmed
```

## Security boundary

Production requires TLS for both MQTT and HTTPS and validates both servers.
This phase does not claim firmware authenticity; signed container verification
remains a later phase.

## ESP-IDF 6.x compatibility

ESP-MQTT moved to the ESP Component Registry in ESP-IDF 6.x. cJSON is also a
managed component. The project pins the MQTT component to `1.0.0` and declares
a compatible cJSON dependency.

The project keeps the Phase-10 fixes for:

- SNTP brace-initializer compatibility;
- separate STM32 OpenOCD from Espressif OpenOCD;
- relocatable STM32 dependency files;
- partial ESP-IDF build-directory recovery;
- dedicated HTTPS/gateway task stack.

## Hardware verification design

The Phase-11 hardware runner requires no external MQTT broker package. It
starts a minimal TLS MQTT 3.1.1 broker implemented in Python for the narrow HIL
test contract.

The broker:

- accepts the ESP32 TLS connection;
- CONNACKs;
- accepts the command subscription;
- observes the retained `online` status;
- sends one QoS-1 update command;
- observes the command PUBACK;
- records status and progress publications;
- requires a final `confirmed` status.

The HTTPS server serves the exact candidate named by the MQTT command.
ST-Link then verifies final STM32 metadata and installed candidate bytes.

Physical Phase-11 hardware validation remains pending until the board test
returns PASS.

## ESP-IDF Kconfig bool compatibility fix

Hardware-side ESP-IDF compilation exposed that a disabled Kconfig `bool`
(`SDOTA_PHASE11_SINGLE_SHOT`, default `n`) is not emitted as a usable
`CONFIG_*` C macro. Phase 11 no longer evaluates such options directly in C
initializers.

`gateway_config.c` maps bool options through `#if defined(CONFIG_...)` guards,
so both `y` and `n` configurations compile. The hardware-test override assigns
single-shot mode after the base configuration is constructed.

`scripts/phase11_check.py` now rejects direct C-expression use of these bool
options and the package was compile-tested with `SINGLE_SHOT` both absent
(`n`) and defined (`y`).

## MQTT publish/drain hardware fix

The first end-to-end Phase-11 run proved the actual OTA path but exposed two
MQTT-reporting bugs:

1. progress used `esp_mqtt_client_enqueue()` with QoS 0 and `store=false`, so
   the progress samples were not retained for transmission;
2. the final QoS-1 `confirmed` state was only enqueued, then the single-shot
   test stopped the MQTT client before the broker necessarily received/PUBACKed
   that message.

Progress now uses immediate `esp_mqtt_client_publish(..., qos=0)`. Final
`confirmed` status uses QoS 1 and waits for the matching
`MQTT_EVENT_PUBLISHED` message ID before the gateway reports PASS or stops the
client. The same broker-ACK rule is used for the `already_current` final path.
