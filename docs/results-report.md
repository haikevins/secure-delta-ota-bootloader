# Consolidated Results Report

## Summary

This document consolidates the current portfolio evidence into one place.

Overall status:

```text
Integrated source/build/security check     PASS
Warning-clean STM32 build                  PASS (Clang validated)
Reproducible benchmark                     PASS
Signed full/delta release path             PASS
Deterministic physical HIL                 PASS 9/9
Rollback-reset final restoration           PASS
Private signing key persisted              NO
Packaged trust anchor                      intentionally unprovisioned
```

## Hardware result

Physical target:

```text
STM32F103C8T6
ESP32 gateway
W25Q external SPI NOR
ST-Link SWD
USART1 OTA transport
```

Nine deterministic scenarios passed:

| Scenario | Result |
|---|---|
| secure delta control | PASS |
| reset during delta reconstruction | PASS |
| reset during backup | PASS |
| reset during internal-flash installation | PASS |
| MQTT disconnect after accepted update | PASS |
| truncated HTTPS transfer | PASS |
| tampered ECDSA signature | PASS |
| rollback control | PASS |
| reset during rollback | PASS |

Final hardware state:

```text
generation=74
state=IDLE
active_version=1
pending_version=0
boot_attempts=0
last_error=0x0008B003
application v1 exact verification=PASS
```

## Recovery evidence

The HIL runner records fault witnesses rather than relying only on the final state:

```text
patch-reset       generation = control + 1
backup-reset      generation = control + 1
install-reset     generation = control + 1
MQTT disconnect   generation = control
rollback-reset    generation = rollback-control + 1
```

This demonstrates that the intended reset/network fault path actually executed.

## Security-negative evidence

### Tampered signature

Properties of the test:

- artifact transfer/storage path remains otherwise valid;
- signature bytes are modified;
- artifact reaches the STM32 security boundary;
- ECDSA verification rejects;
- v1 remains byte-for-byte unchanged;
- a security diagnostic is persisted.

### Truncated HTTPS

Properties:

- gateway receives an incomplete artifact;
- gateway fails before installation;
- STM32 does not install a partial image;
- active 38 KiB application region remains unchanged.

### Downgrade policy

Host/security validation rejects a target version that is not newer than the active version.

## Benchmark — checked-in Clang reference

| Metric | Value |
|---|---:|
| Bootloader flash | 11,400 B |
| Bootloader budget | 24,576 B |
| Bootloader utilization | 46.39% |
| Bootloader RAM | 2,072 B |
| Application v2 flash | 11,276 B |
| Application budget | 38,912 B |
| Application utilization | 28.98% |
| Application RAM | 1,968 B |
| Raw delta | 970 B |
| Raw delta savings | 91.40% |
| Signed delta | 1,174 B |
| Signed full | 11,480 B |
| Signed delta savings | 89.77% |
| HIL evidence | 9/9 |

Reference source:

```text
benchmarks/reference.json
benchmarks/reference.csv
benchmarks/reference.md
```

## Benchmark — user-verified GNU Arm GCC run

```text
bootloader flash       9412 B
application v2 flash   9648 B
raw delta              1242 B
signed delta           1446 B
signed full            9852 B
raw savings            87.13%
signed savings         85.32%
HIL evidence           9/9 PASS
```

The compiler-specific size difference is expected. Both runs are well inside fixed partition limits.

## Delta efficiency interpretation

Policy threshold:

```text
minimum delta savings = 20%
```

Observed:

```text
Clang reference raw     91.40%
Clang reference signed  89.77%

GCC observed raw         87.13%
GCC observed signed      85.32%
```

The signing/container overhead reduces savings slightly compared with the raw patch, but the signed delta remains substantially smaller than the signed full artifact.

## Footprint headroom

Using the checked-in Clang reference:

```text
Bootloader free flash headroom:
24576 - 11400 = 13176 B

Application v2 free flash headroom:
38912 - 11276 = 27636 B

Bootloader free SRAM headroom:
20480 - 2072 = 18408 B

Application free SRAM headroom:
20480 - 1968 = 18512 B
```

These are reference-toolchain observations, not guarantees for every compiler revision.

## Build-quality result

The current source keeps aggressive warning flags enabled and provides:

```bash
make warning-check TOOLCHAIN=gcc
```

or:

```bash
make warning-check TOOLCHAIN=clang
```

The Clang `-Werror` validation passes on the packaged source. Known unused warnings were fixed with compile-time feature guards rather than blanket warning suppression.

## Release pipeline result

Validated behaviors include:

```text
immutable release directory       PASS
full artifact                      PASS
delta artifact policy              PASS
signed manifest                    PASS
release-key authorization          PASS
HTTPS serving                      PASS
MQTTS command QoS1/PUBACK          PASS
tampered release rejection         PASS
private key in release output      NO
```

## Reproduce current results

Integrated:

```bash
make check TOOLCHAIN=gcc
```

Warning-clean:

```bash
make warning-check TOOLCHAIN=gcc
```

Benchmark:

```bash
make benchmark TOOLCHAIN=gcc
```

Physical HIL:

```bash
make hil-test \
  TOOLCHAIN=gcc \
  ESP32_PORT=/dev/ttyUSB0 \
  WIFI_SSID="your-ssid" \
  WIFI_PASSWORD="your-password" \
  SDOTA_HOST_IP=<PC_LAN_IP>
```

## Evidence sources

- `PROJECT_REPORT.md`
- `VALIDATION.md`
- `docs/hil-results.md`
- `docs/benchmark-portfolio.md`
- `docs/portfolio-evidence.md`
- `benchmarks/reference.json`
- `scripts/project_check.py`
- `scripts/benchmark.py`
- `scripts/hil_test.py`
