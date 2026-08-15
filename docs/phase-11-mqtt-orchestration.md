# Phase 11 — MQTT Orchestration

Status: **implemented; hardware test provided**

Phase 11 adds MQTT control/status around the already hardware-verified Phase-10
HTTPS OTA pipeline.

```text
MQTT broker
    |
    | mqtts:// command / status / progress
    v
ESP32 gateway
    |
    | HTTPS firmware download
    v
ESP32 stm32_cache
    |
    | UART OTA protocol v1
    v
STM32 W25Q
    |
    v
backup -> install -> trial -> confirm
```

MQTT carries control metadata only. Firmware bytes are still downloaded over
HTTPS.

## Topics

Default topic namespace:

```text
sdota/bluepill-001/command
sdota/bluepill-001/status
sdota/bluepill-001/progress
```

The base and device ID are configurable.

## Update command

Command payload is a bounded JSON object:

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

Rules:

- `schema` must be 1.
- `cmd` must be `update`.
- `update_id` must be nonzero.
- `target_version` must be nonzero.
- `size` must be between 8 bytes and the STM32 38 KiB application maximum.
- `url` must use `https://`.
- downloaded size must match the MQTT command.
- downloaded CRC32 must match the MQTT command.

CRC32 is an integrity check only. Firmware authenticity is still deferred to
the signed-container phase.

## MQTT event handling

The command subscription uses QoS 1. Status messages are queued at QoS 1.
Progress messages use QoS 0.

The MQTT event callback does not perform HTTPS download, Flash operations or
STM32 UART OTA. It only:

1. observes connect/subscription state;
2. reassembles `MQTT_EVENT_DATA` fragments;
3. parses and validates the JSON command;
4. pushes one command into a FreeRTOS queue.

The Phase-11 gateway worker performs the long-running OTA operation.

Command payload is capped at 767 bytes. Fragmented MQTT data is reassembled
using `total_data_len` and `current_data_offset`.

## Status flow

A normal update publishes the following state sequence:

```text
online
accepted
downloaded
installing
confirmed
```

Progress topic reports:

```text
stage=https
stage=uart
```

with current/total/percent values.

Example final status:

```json
{
  "schema": 1,
  "state": "confirmed",
  "update_id": 2953510913,
  "target_version": 2,
  "detail": "trial_confirmed"
}
```

## TLS

Production requires:

```text
mqtts://...
https://...
```

Both MQTT broker verification and HTTPS server verification are mandatory.
Production uses the ESP x509 certificate bundle after network-time
synchronization.

The hardware test creates one temporary private CA and one IP-SAN server
certificate on the PC. The same public test CA is embedded into the temporary
ESP32 test build for both MQTT TLS and HTTPS TLS.

## ESP-IDF 6.x managed dependencies

ESP-IDF 6.x no longer ships ESP-MQTT and cJSON as built-in core components.
The project declares:

```yaml
dependencies:
  espressif/mqtt: "1.0.0"
  espressif/cjson: "^1.7.19"
```

in `gateway-esp32/main/idf_component.yml`.

The first ESP-IDF build may download these managed components.

## Production configuration

Activate ESP-IDF:

```bash
source ~/esp/esp-idf/export.sh
cd gateway-esp32
idf.py menuconfig
```

Configure under `Secure Delta OTA Phase 11`:

```text
Wi-Fi SSID
Wi-Fi password
MQTT broker URI
MQTT client ID
MQTT topic base
MQTT device ID
optional MQTT username/password
```

Then build:

```bash
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## Hardware test

Requirements:

- PC, ESP32 and Wi-Fi AP must permit peer-to-peer LAN traffic.
- TCP `8443` must be reachable from ESP32 for HTTPS.
- TCP `8883` must be reachable from ESP32 for MQTT TLS.
- ESP32 UART2 is connected to STM32 USART1.
- ST-Link is connected to STM32.
- `openssl` is installed.
- ESP-IDF environment is active.

Wiring:

```text
ESP32 GPIO17 TX -> STM32 PA10 RX
ESP32 GPIO16 RX <- STM32 PA9 TX
ESP32 GND       -> STM32 GND
```

Run:

```bash
source ~/esp/esp-idf/export.sh

export STM32_OPENOCD=/usr/bin/openocd
export STM32_OPENOCD_SCRIPTS=/usr/share/openocd/scripts

make phase11-hw-test \
  ESP32_PORT=/dev/ttyUSB0 \
  WIFI_SSID="your-ssid" \
  WIFI_PASSWORD="your-password" \
  PHASE11_HOST_IP=192.168.1.8
```

If a host firewall is active, allow the two test ports from the local LAN
before running the test.

The hardware runner:

1. generates a temporary CA and TLS server certificate with PC IPv4 SAN;
2. creates a Phase-11 JSON command containing candidate size/CRC/HTTPS URL;
3. temporarily embeds the public test CA and test runtime config;
4. builds the ESP32 application with ESP-MQTT managed component;
5. programs deterministic STM32 application v1 baseline;
6. starts a local TLS MQTT broker on port 8883;
7. starts the local HTTPS artifact server on port 8443;
8. flashes ESP32;
9. waits for MQTT command reception and the complete HTTPS/UART/trial flow;
10. verifies MQTT status/progress transcript;
11. independently dumps STM32 metadata and application bytes with ST-Link;
12. verifies the installed candidate byte-for-byte.

Expected ESP32 markers include:

```text
P11_WIFI=PASS
P11_TIME=PASS
P11_MQTT=PASS
P11_COMMAND=PASS
P11_HTTPS=PASS
P11_FINAL=PASS
P11_STACK=PASS ...
P11_PIPELINE=PASS mqtt_https_cache_uart_trial_confirm=complete
P11_GATEWAY_HW_TEST=PASS
```

Expected PC-side final markers:

```text
Local HTTPS server firmware GET: PASS
MQTT orchestration transcript verification: PASS
STM32 MQTT-orchestrated candidate byte-for-byte verification: PASS
Phase 11 MQTT orchestration hardware test: PASS
```

### Delivery behavior

Progress is QoS 0 telemetry and is sent immediately with
`esp_mqtt_client_publish()`. Final `confirmed` status is QoS 1 and Phase 11
waits for the matching `MQTT_EVENT_PUBLISHED` broker acknowledgement before a
single-shot hardware test disconnects.

This prevents a successful STM32 install from being reported as a hardware
test failure merely because the MQTT outbox had not drained yet.
