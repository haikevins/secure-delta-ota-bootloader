# Phase 10 — HTTPS Download

Status: **implemented; end-to-end hardware test provided**

Phase 10 inserts a real network download stage before the already verified
ESP32 UART gateway.

```text
HTTPS firmware server
        |
        | TLS-authenticated GET
        v
ESP32 stm32_cache
        |
        | UART OTA protocol v1
        v
STM32 W25Q Incoming
        |
        v
backup -> install -> trial -> confirm
```

MQTT is intentionally not part of this phase. Phase 11 owns remote
notification/control/status.

## Production flow

The ESP32:

1. connects as a Wi-Fi station;
2. obtains an IP address;
3. synchronizes system time with SNTP;
4. performs an HTTPS GET;
5. verifies the TLS server certificate;
6. streams the body directly into `stm32_cache`;
7. reads the stored artifact back and verifies CRC32;
8. commits the cache header last;
9. forwards that cached artifact to STM32 over the Phase-9 UART gateway;
10. waits for the Phase-8 trial/confirmation result.

The firmware body is never buffered as one complete image in ESP32 RAM.

## HTTPS policy

Phase 10 deliberately keeps the server contract narrow:

- scheme must be `https://`;
- server authentication is mandatory;
- HTTP status must be `200`;
- positive `Content-Length` is mandatory;
- chunked transfer encoding is rejected;
- image length must be `<= 38 KiB`;
- incomplete bodies are rejected.

Production TLS verification uses the ESP x509 certificate bundle through
`esp_crt_bundle_attach`. No certificate-common-name bypass or insecure
server-verification option is enabled.

## Transactional ESP32 cache

The existing `stm32_cache` partition is reused. Persistent header format stays
compatible with Phase 9.

Download publication order:

```text
erase/invalidate cache header
erase required data range
stream HTTPS bytes sequentially
compute streaming CRC32
read complete stored image back
verify stored CRC32 == streamed CRC32
write CRC-protected cache header last
re-open and validate cache
```

Therefore a power loss during HTTPS download cannot publish a partial artifact
as valid. On the next Phase-10 run a new download starts and replaces the
unpublished bytes.

## Time and TLS

Public CA certificates have validity periods, so production mode performs SNTP
synchronization before TLS.

The deterministic hardware test is different: it creates a short-lived private
CA and server certificate on the developer PC. To avoid requiring Internet
access just for SNTP, the test runner injects a current build/test epoch and
enables that bootstrap only when the generated private test CA is selected.
Production builds do not use this override.

## Configuration

Activate ESP-IDF, then:

```bash
cd gateway-esp32
idf.py set-target esp32
idf.py menuconfig
```

Under `Secure Delta OTA Phase 10`, configure:

```text
Wi-Fi SSID
Wi-Fi password
HTTPS URL of raw STM32 application binary
Phase-10 update ID
Phase-10 target version
```

Then:

```bash
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

The HTTPS URL must return the raw STM32 `application.bin` with
`Content-Length`.

## End-to-end hardware test

Requirements:

- ESP32 and developer PC are on the same Wi-Fi/LAN;
- ESP32 UART2 is connected to STM32 USART1;
- ST-Link remains connected to STM32;
- `openssl` is installed on the PC;
- the chosen TCP port (default `8443`) is reachable from the ESP32.

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

make phase10-hw-test \
  ESP32_PORT=/dev/ttyUSB0 \
  WIFI_SSID="your-ssid" \
  WIFI_PASSWORD="your-password"
```

If automatic PC LAN address detection picks the wrong interface:

```bash
HTTPS_HOST_IP=192.168.1.50 \
make phase10-hw-test \
  ESP32_PORT=/dev/ttyUSB0 \
  WIFI_SSID="your-ssid" \
  WIFI_PASSWORD="your-password"
```

The runner:

1. generates a temporary CA and HTTPS server certificate with an IP SAN;
2. embeds only the public CA into the ESP32 test build;
3. builds the Phase-10 ESP32 firmware;
4. flashes STM32 baseline v1 and clears internal metadata;
5. starts a local TLS 1.2+ HTTPS server serving candidate v2;
6. flashes ESP32;
7. requires Wi-Fi, time, HTTPS, UART and final-confirmation markers;
8. verifies that the local HTTPS server actually received the firmware GET;
9. dumps STM32 metadata with ST-Link;
10. compares installed STM32 bytes with the exact HTTPS-served candidate.

Expected final state:

```text
state=IDLE
active_version=2
pending_version=0
boot_attempts=0
copy_offset=0
last_error=0
```

## ESP32 worker task and stack

Phase 10 does not execute TLS/HTTPS on ESP-IDF's system `main` task.
`app_main()` creates `phase10_gateway` and returns.

Default worker stack:

```text
16384 bytes
```

The value is configurable with `SDOTA_PHASE10_GATEWAY_TASK_STACK_SIZE`.
A successful hardware run reports:

```text
P10_STACK=PASS high_water_mark=...
```
