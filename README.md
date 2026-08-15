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
- Phase 10 — HTTPS download: implemented; hardware test provided.

## Phase 10 pipeline

```text
HTTPS server
    |
Wi-Fi + TLS
    |
ESP32 stm32_cache
    |
UART2
    |
STM32 USART1
    |
W25Q -> backup -> install -> trial -> confirm
```

Production mode authenticates the HTTPS server and synchronizes time before
TLS. The downloaded body is streamed directly into the ESP32 cache, verified,
then handed to the already verified UART/STM32 pipeline.

## STM32 build

```bash
make phase10-check
make combined
make flash-combined
```

Current STM32 combined image:

```text
dist/secure-delta-ota-phase10.bin
```

## ESP32 production build

```bash
source ~/esp/esp-idf/export.sh
cd gateway-esp32
idf.py set-target esp32
idf.py menuconfig
idf.py build
```

Configure Wi-Fi and the HTTPS artifact URL under
`Secure Delta OTA Phase 10`.

## Phase 10 end-to-end hardware test

```bash
source ~/esp/esp-idf/export.sh

export STM32_OPENOCD=/usr/bin/openocd
export STM32_OPENOCD_SCRIPTS=/usr/share/openocd/scripts

make phase10-hw-test \
  ESP32_PORT=/dev/ttyUSB0 \
  WIFI_SSID="your-ssid" \
  WIFI_PASSWORD="your-password"
```

Default UART wiring:

```text
ESP32 GPIO17 TX -> STM32 PA10 RX
ESP32 GPIO16 RX <- STM32 PA9 TX
ESP32 GND       -> STM32 GND
```

See:

- `docs/phase-10-https-download.md`
- `docs/phase-10-checklist.md`
- `PHASE10_REPORT.md`
