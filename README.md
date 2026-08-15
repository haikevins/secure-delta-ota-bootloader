# Secure Delta OTA Bootloader

Reference project for **STM32F103C8T6 + ESP32 + external SPI NOR Flash**.

STM32 uses **Standard Peripheral Library + CMSIS**, not HAL. ESP32 uses
**ESP-IDF**.

## Status

- Phase 0 — Specification: complete.
- Phase 1 — Build foundation: complete.
- Phase 2 — Application handoff: complete + hardware verified.
- Phase 3 — Metadata/boot decision: complete + hardware verified.
- Phase 4 — External SPI Flash: complete + hardware verified.
- Phase 5 — UART protocol with PC Python: complete + hardware verified.
- Phase 6 — Basic Full OTA: complete + hardware verified.
- Phase 7 — Power-loss recovery: complete + hardware fault-injection verified.
- Phase 8 — Trial boot and rollback: complete + hardware verified.
- Phase 9 — ESP32 UART Gateway: complete + hardware verified.
- Phase 10 — HTTPS download: complete + hardware verified.
- Phase 11 — MQTT orchestration: complete + hardware verified.
- Phase 12 — Delta patch generation: complete + host verified.
- Phase 13 — STM32 delta patching: complete + hardware verified.
- Phase 14 — Secure container/signature: complete + hardware verified.
- Phase 15 — Server and release pipeline: implemented + host/server/security validation; physical end-to-end HIL pending.

## Phase 11 pipeline

```text
MQTTS broker
   |
   | update metadata / status / progress
   v
ESP32
   |
   | HTTPS firmware
   v
stm32_cache
   |
   | UART OTA
   v
STM32
   |
backup -> install -> trial -> confirm
```

MQTT does not carry firmware bytes. The MQTT command names the HTTPS artifact
and supplies expected size/CRC32. ESP32 verifies those fields after download
before passing the artifact to STM32.

## Host/build validation

```bash
make phase11-check
```

## ESP32 build

ESP-IDF 6.x downloads the managed MQTT and cJSON components declared in
`gateway-esp32/main/idf_component.yml`.

```bash
source ~/esp/esp-idf/export.sh
make phase11-gateway-build
```

## Phase 11 hardware test

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

The PC starts both a local TLS MQTT broker (`8883`) and TLS HTTPS firmware
server (`8443`). If a host firewall is active, permit those ports from the
local LAN.

Default UART wiring:

```text
ESP32 GPIO17 TX -> STM32 PA10 RX
ESP32 GPIO16 RX <- STM32 PA9 TX
ESP32 GND       -> STM32 GND
```

See:

- `docs/phase-11-mqtt-orchestration.md`
- `docs/phase-11-checklist.md`
- `PHASE11_REPORT.md`


## Phase 12 delta generation

Phase 12 generates a JojoDiff-compatible delta from an exact application v1
binary to application v2, reconstructs v2 on the host and validates the result
byte-for-byte.

```bash
make phase12-check
```

Individual targets:

```bash
make phase12-base
make phase12-target
make phase12-delta
```

Artifacts:

```text
dist/phase12/application-v1-to-v2.jdiff
dist/phase12/application-v1-to-v2.json
dist/phase12/application-v1-to-v2-reconstructed.bin
```

Phase 12 is host-only. STM32 streaming delta application begins in Phase 13.

See:

- `docs/phase-12-delta-generation.md`
- `docs/phase-12-checklist.md`
- `PHASE12_REPORT.md`


## Phase 13 STM32 delta patch

Phase 13 applies the Phase-12 JojoDiff-compatible patch on the STM32
bootloader without overwriting the active application during reconstruction.

```text
internal app v1 + W25Q Incoming D13P/.jdiff
        -> W25Q Reconstructed v2
        -> verify
        -> backup/install/trial/confirm
```

Host/build validation:

```bash
make phase13-check
```

Direct STM32 hardware test:

```bash
export STM32_OPENOCD=/usr/bin/openocd
export STM32_OPENOCD_SCRIPTS=/usr/share/openocd/scripts

make phase13-hw-test PORT=/dev/ttyUSB0
```

For the direct PC-UART hardware test, disconnect the ESP32 UART wires from
STM32 PA9/PA10.

See:

- `docs/phase-13-stm32-delta.md`
- `docs/phase-13-checklist.md`
- `PHASE13_REPORT.md`


## Phase 14 secure container

Phase 14 requires signed `SDOT` containers by default:

```text
SHA-256
ECDSA P-256
140-byte signed header (SDOT + SCX1)
64-byte raw r||s signature
```

The STM32 bootloader contains only a trusted public key. Private signing keys
stay outside the repository.

Host/build/security validation:

```bash
make phase14-check
```

Physical STM32 validation:

```bash
export STM32_OPENOCD=/usr/bin/openocd
export STM32_OPENOCD_SCRIPTS=/usr/share/openocd/scripts
make phase14-hw-test PORT=/dev/ttyUSB0
```

See:

- `docs/phase-14-secure-container.md`
- `docs/phase-14-checklist.md`
- `PHASE14_REPORT.md`


## Phase 15 server and release pipeline

Phase 15 creates immutable signed releases, verifies them before publication,
pins the authorized release-key fingerprint on server/publisher operations,
serves artifacts over HTTPS, publishes Phase-11-compatible commands over MQTTS
QoS1, and upgrades the ESP32 gateway to extract Phase-14 SDOT metadata for the
UART START packet.

Host/server validation:

```bash
make phase15-check
```

Create a local release:

```bash
make phase15-release \
  TARGET=/path/to/application-v3.bin \
  TARGET_VERSION=3 \
  BASE=/path/to/application-v2.bin \
  BASE_VERSION=2 \
  SIGNING_KEY=/secure/location/firmware-signing.pem \
  KEY_ID=0x14000001 \
  BASE_URL=https://firmware.example
```

Physical end-to-end HIL:

```bash
source ~/esp/esp-idf/export.sh
export STM32_OPENOCD=/usr/bin/openocd
export STM32_OPENOCD_SCRIPTS=/usr/share/openocd/scripts

make phase15-hw-test \
  ESP32_PORT=/dev/ttyUSB0 \
  WIFI_SSID="your-ssid" \
  WIFI_PASSWORD="your-password" \
  PHASE15_HOST_IP=<PC_LAN_IP>
```

Wiring is the normal ESP32 gateway wiring:

```text
ESP32 GPIO17 TX -> STM32 PA10 RX
ESP32 GPIO16 RX <- STM32 PA9 TX
ESP32 GND       -> STM32 GND
ST-Link         -> STM32 SWD
```

See:

- `docs/phase-15-server-release-pipeline.md`
- `docs/phase-15-checklist.md`
- `PHASE15_REPORT.md`
