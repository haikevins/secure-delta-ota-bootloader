# Secure Delta OTA Bootloader

Portfolio-grade secure OTA update system for an STM32F103C8T6 node with an ESP32 network gateway and external W25Q SPI NOR flash.

The repository includes the bootloader, application-side OTA receiver, ESP32 gateway, signed release tooling, deterministic fault-injection HIL runner, reproducible benchmark tooling, CI contracts, and portfolio documentation.

## What the system demonstrates

- **Secure boot/update boundary:** SHA-256 + ECDSA P-256 signed SDOT containers.
- **Delta OTA:** JojoDiff-compatible host generation and streaming reconstruction on STM32.
- **Resumable transport:** COBS-framed UART protocol with sequence, offset, ACK/NACK, retry and resume.
- **External staging:** W25Q32/W25Q64 support with a fixed logical 4 MiB OTA layout.
- **Power-loss recovery:** persistent checkpoints for receive, patch, backup, install and rollback paths.
- **Trial boot and rollback:** candidate firmware must confirm health; otherwise the validated backup is restored.
- **Network gateway:** ESP32 uses MQTTS for orchestration/status and HTTPS for firmware bytes.
- **Release pipeline:** immutable signed releases, exact previous-binary deltas, full-image fallback and signed manifests.
- **Deterministic HIL:** nine hardware fault scenarios covering reset, transport failure, signature tamper and rollback recovery.
- **Reproducible benchmark:** flash/RAM footprint, raw/signed delta efficiency and build-time observations.

## Hardware

- STM32F103C8T6 Blue Pill
- ESP32 gateway
- W25Q64 external SPI NOR on STM32
- ST-Link/V2 for SWD programming/debug
- UART between ESP32 and STM32

UART wiring:

```text
ESP32 GPIO17 TX -> STM32 PA10 RX
ESP32 GPIO16 RX <- STM32 PA9 TX
ESP32 GND       -> STM32 GND
```

External flash wiring:

```text
STM32 PA5  -> W25Q SCK
STM32 PA6  <- W25Q MISO
STM32 PA7  -> W25Q MOSI
STM32 PB0  -> W25Q CS
```

## Memory layout

Internal flash:

```text
0x08000000 - 0x08005FFF   Bootloader        24 KiB
0x08006000 - 0x0800F7FF   Application       38 KiB
0x0800F800 - 0x0800FBFF   Metadata A         1 KiB
0x0800FC00 - 0x0800FFFF   Metadata B         1 KiB
```

External logical OTA layout:

```text
0x000000 - 0x001FFF   Metadata A/B
0x002000 - 0x021FFF   Incoming artifact      128 KiB
0x022000 - 0x041FFF   Reconstructed image    128 KiB
0x042000 - 0x061FFF   Validated backup       128 KiB
0x062000 - 0x071FFF   Logs                    64 KiB
remaining space       Reserved
```

## End-to-end update flow

```text
CI / release tooling
        |
        | signed manifest + SDOT artifact
        v
HTTPS release server <---- MQTTS command/status ----> ESP32 gateway
                                                    |
                                                    | COBS UART
                                                    v
                                               STM32 application
                                                    |
                                                    | stages artifact
                                                    v
                                               external SPI NOR
                                                    |
                                                    | reset / install
                                                    v
                                               STM32 bootloader
                                                    |
                          verify signature/base -> patch/full image
                                                    |
                               backup -> install -> verify -> trial
                                                    |
                                    confirm or automatic rollback
```

Firmware bytes remain on HTTPS. MQTT is used only for orchestration, progress and terminal status.

## Security model

The SDOT container binds:

- image type;
- product and hardware revision;
- base and target versions;
- payload size;
- SHA-256 of payload/base/target;
- signing key ID;
- ECDSA P-256 signature over the signed header plus payload.

Unsigned legacy artifacts are disabled in the secure build. Anti-downgrade checks reject target versions that are not newer than the active version.

The repository intentionally ships with an **unprovisioned trust anchor**:

```c
#define TRUSTED_KEY_PROVISIONED 0U
#define TRUSTED_KEY_ID          0UL
```

Use `tools/keytool.py` during a controlled build to generate a provisioned public-key header. Private signing keys are never embedded in STM32 firmware and are not included in this repository.

## Build and verification

### Prerequisites

For STM32 builds, use either GNU Arm Embedded GCC or Clang/LLD:

```bash
# GNU Arm Embedded
arm-none-eabi-gcc --version
arm-none-eabi-objcopy --version

# Or Clang
clang --version
llvm-objcopy --version
```

If `TOOLCHAIN` is omitted, the STM32 build automatically prefers
`arm-none-eabi-gcc`; otherwise it falls back to Clang when available.

For ESP32 builds, activate ESP-IDF first:

```bash
source ~/esp/esp-idf/export.sh
```

For STM32 programming and metadata operations, install OpenOCD and connect the
ST-Link. The flash helpers use these optional variables:

```bash
export OPENOCD=/usr/bin/openocd
export OPENOCD_INTERFACE=interface/stlink.cfg
export OPENOCD_TARGET=target/stm32f1x.cfg
```

The HIL runner has separate OpenOCD discovery variables:

```bash
export STM32_OPENOCD=/usr/bin/openocd
export STM32_OPENOCD_SCRIPTS=/usr/share/openocd/scripts
```

### Make command reference

All commands below are run from the repository root.

| Command | Purpose | Main output / effect |
|---|---|---|
| `make` | Run the default target. Equivalent to `make all`, which runs the integrated project check. | Validation result on stdout |
| `make all` | Alias for the integrated project check. | Same as `make check` |
| `make check [TOOLCHAIN=gcc\|clang]` | Run host checks, source/security contracts, build validation, fault-build matrix, benchmark validation, and portfolio checks. | Final `SECURE_DELTA_OTA_PROJECT_CHECK=PASS` marker |
| `make test [TOOLCHAIN=gcc\|clang]` | Alias for `make check`. | Same as `make check` |
| `make benchmark [TOOLCHAIN=gcc\|clang]` | Rebuild benchmark firmware, generate delta/signed artifacts, measure footprint and delta efficiency. | `dist/benchmark/benchmark.json`, `.csv`, `.md` |
| `make firmware [TOOLCHAIN=gcc\|clang]` | Build both STM32 bootloader and application. | Component ELF/BIN/HEX/MAP/size files |
| `make bootloader [TOOLCHAIN=gcc\|clang]` | Build only the STM32 bootloader. | `node-stm32f103/bootloader/out/` |
| `make application [TOOLCHAIN=gcc\|clang]` | Build only the STM32 application. | `node-stm32f103/application/out/` |
| `make combined [TOOLCHAIN=gcc\|clang]` | Build bootloader + application, then merge them into one programming image. | `dist/secure-delta-ota-combined.bin` |
| `make gateway` | Build the ESP32 gateway. Alias for `make gateway-build`. Requires active ESP-IDF environment. | `gateway-esp32/build/` |
| `make gateway-build` | Run the ESP32 build guard and `idf.py build`. | `gateway-esp32/build/` |
| `make release ...` | Generate an immutable signed firmware release with full-image fallback and optional delta. | `dist/releases/` by default |
| `make flash-bootloader [TOOLCHAIN=gcc\|clang]` | Build and program only the bootloader with OpenOCD/ST-Link. | Programs bootloader at its linker address |
| `make flash-application [TOOLCHAIN=gcc\|clang]` | Build and program only the application with OpenOCD/ST-Link. | Programs application at its linker address |
| `make flash-combined [TOOLCHAIN=gcc\|clang]` | Build the merged image and program it from `0x08000000`. | Programs bootloader + application |
| `make dump-metadata` | Read the two internal-flash metadata pages through OpenOCD and decode them. | `dist/metadata-pages.bin` + decoded metadata on stdout |
| `make erase-metadata` | Erase internal metadata pages `0x0800F800..0x0800FFFF`. | Destructive metadata erase on connected STM32 |
| `make hil-test ...` | Execute the deterministic nine-scenario hardware-in-the-loop fault suite. | Scenario markers and final HIL PASS/FAIL |
| `make toolchain-info [TOOLCHAIN=gcc\|clang]` | Show the selected STM32 compiler, target, linker script and output ELF. | Toolchain information on stdout |
| `make tools` | Python syntax-compile the host tooling under `tools/`, `server/`, and `scripts/`. | Python compile check |
| `make clean` | Remove STM32 outputs, ESP-IDF build/config outputs, benchmark temporary files and `dist/`. | Clean working tree outputs |

### Common STM32 build commands

Check which compiler will be used:

```bash
make toolchain-info
make toolchain-info TOOLCHAIN=gcc
make toolchain-info TOOLCHAIN=clang
```

Run the complete verification gate:

```bash
make check TOOLCHAIN=gcc
```

or:

```bash
make check TOOLCHAIN=clang
```

A successful integrated check ends with:

```text
SECURE_DELTA_OTA_PROJECT_CHECK=PASS
```

Build both STM32 images:

```bash
make firmware TOOLCHAIN=gcc
```

Build components separately:

```bash
make bootloader TOOLCHAIN=gcc
make application TOOLCHAIN=gcc
```

The component build directories contain:

```text
out/
├── <target>.elf
├── <target>.bin
├── <target>.hex
├── <target>.map
└── <target>.size.txt
```

Create a single image containing both the bootloader and application:

```bash
make combined TOOLCHAIN=gcc
```

Output:

```text
dist/secure-delta-ota-combined.bin
```

### ESP32 gateway build

Activate ESP-IDF:

```bash
source ~/esp/esp-idf/export.sh
```

Then run either alias:

```bash
make gateway
```

or:

```bash
make gateway-build
```

The command refuses to run when `idf.py` is unavailable and executes the
gateway build guard before invoking `idf.py build`.

### STM32 programming with ST-Link/OpenOCD

Optional OpenOCD overrides:

```bash
export OPENOCD=/usr/bin/openocd
export OPENOCD_INTERFACE=interface/stlink.cfg
export OPENOCD_TARGET=target/stm32f1x.cfg
```

Program only the bootloader:

```bash
make flash-bootloader TOOLCHAIN=gcc
```

Program only the application:

```bash
make flash-application TOOLCHAIN=gcc
```

Program the merged bootloader + application image:

```bash
make flash-combined TOOLCHAIN=gcc
```

Each flash helper rebuilds the required image first and requests OpenOCD
verification before reset.

### Metadata maintenance

Dump and decode both redundant metadata pages:

```bash
make dump-metadata
```

The default raw dump is:

```text
dist/metadata-pages.bin
```

Erase both metadata pages:

```bash
make erase-metadata
```

`erase-metadata` is destructive. It erases the internal-flash range
`0x0800F800..0x0800FFFF`; use it only when intentionally resetting persistent
boot/update metadata.

### HIL fault-injection test

Activate ESP-IDF and configure STM32 OpenOCD discovery:

```bash
source ~/esp/esp-idf/export.sh

export STM32_OPENOCD=/usr/bin/openocd
export STM32_OPENOCD_SCRIPTS=/usr/share/openocd/scripts
```

Minimum practical invocation:

```bash
make hil-test \
  TOOLCHAIN=gcc \
  ESP32_PORT=/dev/ttyUSB0 \
  WIFI_SSID="your-ssid" \
  WIFI_PASSWORD="your-password" \
  SDOTA_HOST_IP=<PC_LAN_IP>
```

Supported HIL variables:

| Variable | Required | Default / behavior |
|---|---|---|
| `TOOLCHAIN` | No | Auto-detects `gcc`, then `clang` |
| `ESP32_PORT` | Yes | ESP32 serial device, for example `/dev/ttyUSB0` |
| `PORT` | No | Backward-compatible alias used only when `ESP32_PORT` is empty |
| `WIFI_SSID` | Yes | Wi-Fi SSID used by the ESP32 |
| `WIFI_PASSWORD` | Depends on network | Empty string is allowed by the wrapper |
| `SDOTA_HOST_IP` | Recommended | PC LAN IPv4; runner attempts automatic detection if omitted |
| `HTTPS_PORT` | No | `8443` |
| `MQTT_PORT` | No | `8883` |
| `SDOTA_HIL_KEY_ID` | No | `0xC0DE0001`; must be a non-zero 32-bit value |

The test requires the ESP32 UART link, STM32 ST-Link connection, external
W25Q flash, active ESP-IDF environment, OpenSSL, and network reachability
between the ESP32 and the host PC.

### Signed release generation

Full-image release:

```bash
make release \
  TARGET=path/to/application-v2.bin \
  TARGET_VERSION=2 \
  SIGNING_KEY=/secure/path/release-key.pem \
  KEY_ID=0x15000001 \
  BASE_URL=https://firmware.example
```

Delta-capable release from an exact previous application binary:

```bash
make release \
  TARGET=path/to/application-v2.bin \
  TARGET_VERSION=2 \
  BASE=path/to/application-v1.bin \
  BASE_VERSION=1 \
  SIGNING_KEY=/secure/path/release-key.pem \
  KEY_ID=0x15000001 \
  BASE_URL=https://firmware.example \
  CHANNEL=stable \
  RELEASE_ROOT=dist/releases
```

Variables accepted by the top-level `make release` target:

| Variable | Required | Meaning |
|---|---|---|
| `TARGET` | Yes | Target application `.bin` |
| `TARGET_VERSION` | Yes | Numeric target firmware version |
| `BASE` | No | Exact previous application `.bin`; enables delta generation |
| `BASE_VERSION` | Required with `BASE` | Numeric version of the base image |
| `SIGNING_KEY` | Yes | External ECDSA P-256 private signing-key path |
| `KEY_ID` | Yes | Public signing-key identifier, decimal or `0x...` |
| `BASE_URL` | Yes | HTTPS base URL embedded in the release manifest |
| `CHANNEL` | No | `stable` by default; release tool also supports `beta` and `dev` |
| `RELEASE_ROOT` | No | `dist/releases` by default |

The private signing key must remain outside the repository. The release tooling
validates the application image, signs SDOT artifacts, verifies the generated
signature, enforces the artifact-size limit, and includes a delta only when the
configured savings policy is satisfied.

### Benchmark command

Run the reproducible benchmark:

```bash
make benchmark TOOLCHAIN=gcc
```

or:

```bash
make benchmark TOOLCHAIN=clang
```

Outputs:

```text
dist/benchmark/benchmark.json
dist/benchmark/benchmark.csv
dist/benchmark/benchmark.md
```

The benchmark uses temporary build directories and cleans those temporary
component outputs after collection.

### Host-tool syntax check

Compile-check the Python tooling without running the full firmware gate:

```bash
make tools
```

This covers:

```text
tools/
server/
scripts/
```

### Cleaning generated outputs

Remove generated STM32, ESP32, benchmark and distribution outputs:

```bash
make clean
```

This removes the normal component `build/out` directories, HIL/benchmark
build variants, ESP-IDF `build` and generated `sdkconfig` files,
`.benchmark-tmp`, `build-host`, and `dist`.

### Recommended command sequences

For normal development:

```bash
make clean
make check TOOLCHAIN=gcc
make firmware TOOLCHAIN=gcc
```

For a board programming session:

```bash
make flash-combined TOOLCHAIN=gcc
make dump-metadata
```

For gateway development:

```bash
source ~/esp/esp-idf/export.sh
make gateway
```

For release qualification:

```bash
make clean
make check TOOLCHAIN=gcc
make benchmark TOOLCHAIN=gcc
```

For full hardware validation:

```bash
source ~/esp/esp-idf/export.sh
export STM32_OPENOCD=/usr/bin/openocd
export STM32_OPENOCD_SCRIPTS=/usr/share/openocd/scripts

make hil-test \
  TOOLCHAIN=gcc \
  ESP32_PORT=/dev/ttyUSB0 \
  WIFI_SSID="your-ssid" \
  WIFI_PASSWORD="your-password" \
  SDOTA_HOST_IP=<PC_LAN_IP>
```

## Benchmark

Run:

```bash
make benchmark TOOLCHAIN=gcc
```

Outputs are written to:

```text
dist/benchmark/benchmark.json
dist/benchmark/benchmark.csv
dist/benchmark/benchmark.md
```

The last verified GCC hardware-project benchmark recorded:

```text
Bootloader flash        9412 B
Application v2 flash    9648 B
Raw delta               1242 B
Signed delta             1446 B
Signed full              9852 B
Raw delta savings        87.13%
Signed delta savings     85.32%
HIL evidence             9/9 PASS
```

Exact footprint and timing values can vary by compiler/toolchain version. Partition limits and delta-efficiency policies are checked automatically.

## Deterministic hardware-in-the-loop test

The HIL runner executes nine scenarios:

1. secure delta control update;
2. reset during patch reconstruction;
3. reset during backup;
4. reset during internal-flash install;
5. MQTT disconnect after command acceptance;
6. truncated HTTPS transfer;
7. tampered ECDSA signature;
8. automatic rollback control;
9. reset during rollback.

Run on connected hardware:

```bash
source ~/esp/esp-idf/export.sh

export STM32_OPENOCD=/usr/bin/openocd
export STM32_OPENOCD_SCRIPTS=/usr/share/openocd/scripts

make hil-test \
  ESP32_PORT=/dev/ttyUSB0 \
  WIFI_SSID="your-ssid" \
  WIFI_PASSWORD="your-password" \
  SDOTA_HOST_IP=<PC_LAN_IP>
```

The verified hardware result is documented in `docs/hil-results.md`.

## Release generation

Example:

```bash
make release \
  TARGET=path/to/application-v2.bin \
  TARGET_VERSION=2 \
  BASE=path/to/application-v1.bin \
  BASE_VERSION=1 \
  SIGNING_KEY=/secure/path/release-key.pem \
  KEY_ID=0x15000001 \
  BASE_URL=https://firmware.example
```

The release tool chooses a delta only when it satisfies the configured savings policy and always emits a signed full-image fallback.

## Repository map

```text
node-stm32f103/bootloader/   Secure bootloader, patch/install/rollback logic
node-stm32f103/application/  Application OTA receiver and confirmation logic
node-stm32f103/common/       Shared STM32 flash/storage drivers
gateway-esp32/               ESP32 MQTTS + HTTPS + UART gateway
shared/                      Cross-component container/metadata primitives
server/                      Release server, manifest and MQTT publisher
tools/                       Signing, delta, release and inspection tools
scripts/                     Build/check/benchmark/HIL automation
tests/                       Host/unit/fault test assets
benchmarks/                  Checked-in reference benchmark
docs/                        Architecture, protocol, HIL and portfolio docs
```

## Portfolio evidence

Start with:

- `docs/portfolio-one-page.md`
- `docs/portfolio-demo.md`
- `docs/portfolio-evidence.md`
- `docs/hil-results.md`
- `benchmarks/reference.md`
- `PROJECT_REPORT.md`
- `VALIDATION.md`

## Verified status

The integrated project has passed:

- secure release and signed-container host validation;
- STM32 flash/RAM budget checks;
- deterministic delta round-trip checks;
- packaging checks for unprovisioned trust anchor and absent private credentials;
- full deterministic HIL fault matrix: **9/9 PASS**;
- final rollback-reset state restoring confirmed application v1.

The project is therefore presented as a completed secure delta OTA reference implementation and portfolio artifact.
