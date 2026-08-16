# Make Command Reference

This document is the dedicated command reference for the repository. `README.md` also contains the complete public target guide; this file is convenient when only command usage is needed.

## Discover commands

```bash
make help
```

## Toolchain selection

Supported STM32 toolchains:

```text
TOOLCHAIN=gcc
TOOLCHAIN=clang
```

Examples:

```bash
make firmware TOOLCHAIN=gcc
make TOOLCHAIN=clang firmware
```

When omitted, GNU Arm GCC is preferred if available; otherwise Clang/LLVM is selected.

## Validation and quality

### Integrated closure

```bash
make check TOOLCHAIN=gcc
```

Expected final marker:

```text
SECURE_DELTA_OTA_PROJECT_CHECK=PASS
```

### Static-only closure

```bash
python3 scripts/project_check.py --static-only
```

Use while editing documentation/configuration when cross-builds are unnecessary.

### Warning-clean C build

```bash
make warning-check TOOLCHAIN=gcc
```

Expected:

```text
WARNING_CLEAN_BUILD=PASS
```

### Benchmark

```bash
make benchmark TOOLCHAIN=gcc
```

Outputs:

```text
dist/benchmark/benchmark.json
dist/benchmark/benchmark.csv
dist/benchmark/benchmark.md
```

### Test alias

```bash
make test TOOLCHAIN=gcc
```

Equivalent to `make check`.

### Python smoke check

```bash
make tools
```

## STM32 build

### Both images

```bash
make firmware TOOLCHAIN=gcc
```

### Bootloader only

```bash
make bootloader TOOLCHAIN=gcc
```

### Application only

```bash
make application TOOLCHAIN=gcc
```

### Merged binary

```bash
make combined TOOLCHAIN=gcc
```

Outputs:

```text
dist/secure-delta-ota-combined.bin
dist/secure-delta-ota-combined.txt
```

### Toolchain information

```bash
make toolchain-info TOOLCHAIN=gcc
```

## ESP32 gateway

```bash
source ~/esp/esp-idf/export.sh
make gateway
```

`make gateway-build` is the explicit equivalent target.

## Signed release

Delta + full:

```bash
make release \
  TARGET=/path/to/application-v2.bin \
  TARGET_VERSION=2 \
  BASE=/path/to/application-v1.bin \
  BASE_VERSION=1 \
  SIGNING_KEY=/secure/path/release-signing.pem \
  KEY_ID=0x15000001 \
  BASE_URL=https://firmware.example \
  CHANNEL=stable
```

Full only:

```bash
make release \
  TARGET=/path/to/application-v2.bin \
  TARGET_VERSION=2 \
  SIGNING_KEY=/secure/path/release-signing.pem \
  KEY_ID=0x15000001 \
  BASE_URL=https://firmware.example
```

Variables:

| Variable | Required | Default | Notes |
|---|---:|---|---|
| `TARGET` | yes | — | Target application binary |
| `TARGET_VERSION` | yes | — | Target version |
| `BASE` | no | — | Exact previous binary |
| `BASE_VERSION` | with `BASE` | — | Base version |
| `SIGNING_KEY` | yes | — | External P-256 private key |
| `KEY_ID` | yes | — | Non-zero uint32 ID |
| `BASE_URL` | yes | — | HTTPS firmware base URL |
| `CHANNEL` | no | `stable` | `stable`, `beta`, `dev` |
| `RELEASE_ROOT` | no | `dist/releases` | Output root |

## STM32 flashing

The simple flash scripts use these optional environment variables:

```bash
export OPENOCD=/usr/bin/openocd
export OPENOCD_INTERFACE=interface/stlink.cfg
export OPENOCD_TARGET=target/stm32f1x.cfg
```

### Bootloader

```bash
make flash-bootloader TOOLCHAIN=gcc
```

### Application

```bash
make flash-application TOOLCHAIN=gcc
```

### Combined image

```bash
make flash-combined TOOLCHAIN=gcc
```

## Metadata tools

### Dump and decode

```bash
make dump-metadata
```

Output:

```text
dist/metadata-pages.bin
```

### Erase metadata

```bash
make erase-metadata
```

Destructive range:

```text
0x0800F800 - 0x0800FFFF
```

## Hardware-in-the-loop

Activate ESP-IDF:

```bash
source ~/esp/esp-idf/export.sh
```

Select STM32 OpenOCD if the host has more than one OpenOCD installation:

```bash
export STM32_OPENOCD=/usr/bin/openocd
export STM32_OPENOCD_SCRIPTS=/usr/share/openocd/scripts
```

Run:

```bash
make hil-test \
  TOOLCHAIN=gcc \
  ESP32_PORT=/dev/ttyUSB0 \
  WIFI_SSID="your-ssid" \
  WIFI_PASSWORD="your-password" \
  SDOTA_HOST_IP=<PC_LAN_IP>
```

Optional:

```text
PORT                alias for ESP32_PORT
HTTPS_PORT          default 8443
MQTT_PORT           default 8883
SDOTA_HIL_KEY_ID    default 0xC0DE0001
```

Expected final result:

```text
HIL hardware test: PASS (9 deterministic scenarios)
```

## Cleaning

```bash
make clean
```

Removes generated STM32, ESP-IDF, benchmark and distribution outputs.

## Suggested final validation sequence

```bash
make warning-check TOOLCHAIN=gcc
make check TOOLCHAIN=gcc
make benchmark TOOLCHAIN=gcc
```

For an LLVM-only host:

```bash
make warning-check TOOLCHAIN=clang
make check TOOLCHAIN=clang
make benchmark TOOLCHAIN=clang
```
