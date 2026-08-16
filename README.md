# Secure Delta OTA Bootloader

Portfolio-grade secure OTA update system for an STM32F103C8T6 node with an ESP32 network gateway and external W25Q SPI NOR flash.

The repository includes the STM32 bootloader, application-side OTA receiver, ESP32 gateway, signed release tooling, deterministic fault-injection hardware test runner, reproducible benchmark tooling, CI validation, and evidence-oriented portfolio documentation.

## Project status

The integrated implementation is complete and has been validated as one end-to-end system.

Verified evidence currently recorded in the repository:

- deterministic hardware fault matrix: **9/9 PASS**;
- secure signed full and delta artifact path: PASS;
- rollback after an unhealthy candidate: PASS;
- reset during rollback recovery: PASS;
- packaging boundary with no private signing key: PASS;
- reproducible STM32 flash/RAM and delta-efficiency benchmark: PASS;
- warning-clean STM32 Clang build with `-Werror`: PASS;
- user-verified GCC benchmark: bootloader `9412 B`, application v2 `9648 B`, signed delta savings `85.32%`.

The checked-in benchmark reference is compiler-specific and is not treated as an immutable binary-size promise. Run `make benchmark` on the current host for a fresh measurement.

## What the system demonstrates

- **Secure firmware trust boundary:** SHA-256 + ECDSA P-256 signed SDOT containers.
- **Delta OTA:** JojoDiff-compatible host generation and streaming reconstruction on STM32.
- **Resumable transport:** COBS-framed UART protocol with sequence, offset, ACK/NACK, retry and resume.
- **External staging:** W25Q32/W25Q64 support with a fixed logical 4 MiB OTA layout.
- **Power-loss recovery:** persistent checkpoints for receive, patch, backup, install and rollback paths.
- **Trial boot and rollback:** candidate firmware must confirm health; otherwise the validated backup is restored.
- **Network gateway:** ESP32 uses MQTTS for orchestration/status and HTTPS for firmware bytes.
- **Release engineering:** immutable signed releases, exact previous-binary deltas, full-image fallback and signed manifests.
- **Deterministic HIL:** nine hardware scenarios covering resets, transport failure, signature tamper and rollback recovery.
- **Reproducible benchmark:** flash/RAM footprint, raw/signed delta efficiency, artifact fit and timing observations.
- **Portfolio evidence:** claims are mapped to source, tests, benchmark output and hardware results.

## Hardware

- STM32F103C8T6 Blue Pill
- ESP32 gateway
- W25Q64 external SPI NOR on STM32 (W25Q32 also supported)
- ST-Link/V2 or compatible ST-Link for SWD
- UART connection between ESP32 and STM32

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
3.3 V      -> W25Q VCC
GND        -> W25Q GND
```

For a bare W25Q device, `/WP` and `/HOLD` must be kept high.

## Memory layout

Internal flash:

```text
0x08000000 - 0x08005FFF   Bootloader        24 KiB
0x08006000 - 0x0800F7FF   Application       38 KiB
0x0800F800 - 0x0800FBFF   Metadata A         1 KiB
0x0800FC00 - 0x0800FFFF   Metadata B         1 KiB
```

SRAM:

```text
0x20000000 - 0x20004FFF   SRAM              20 KiB
```

External logical OTA layout:

```text
0x000000 - 0x000FFF   External metadata A      4 KiB
0x001000 - 0x001FFF   External metadata B      4 KiB
0x002000 - 0x021FFF   Incoming artifact      128 KiB
0x022000 - 0x041FFF   Reconstructed image    128 KiB
0x042000 - 0x061FFF   Validated backup       128 KiB
0x062000 - 0x071FFF   Update logs             64 KiB
remaining space       Reserved
```

The W25Q64 development device is larger than the logical map; the OTA design deliberately uses only the first 4 MiB so it remains compatible with W25Q32.

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

Firmware bytes remain on HTTPS. MQTT is used for orchestration, progress and terminal status. The STM32 bootloader remains the final firmware-authentication and installation authority.

## Security model

The signed SDOT container binds:

- container/image type;
- product and hardware revision;
- base and target versions;
- payload and target sizes;
- SHA-256 of payload, base image and target image;
- signing key ID;
- ECDSA P-256 signature over the signed header plus payload.

CRC32 is used for transport/storage corruption detection; it is not treated as an authenticity mechanism.

Unsigned legacy artifacts are disabled in the secure build. Anti-downgrade checks reject a target version that is not newer than the active version.

The repository intentionally ships with an **unprovisioned trust anchor**:

```c
#define TRUSTED_KEY_PROVISIONED 0U
#define TRUSTED_KEY_ID          0UL
```

Use `tools/keytool.py` in a controlled provisioning/build process to generate a public-key header. Private signing keys must remain outside the repository, ESP32 and STM32.

## Prerequisites

### General host tools

Recommended Linux host packages:

```text
make
python3
openssl
git
```

### STM32 build toolchain

Use one of:

```text
GNU Arm Embedded:
  arm-none-eabi-gcc
  arm-none-eabi-objcopy
  arm-none-eabi-size

Clang/LLVM:
  clang
  lld
  llvm-objcopy
  size
```

If `TOOLCHAIN` is omitted, the common STM32 build selects GNU Arm GCC when available, otherwise Clang/LLVM.

### ESP32 gateway

ESP-IDF must be installed and its environment activated before gateway or HIL commands:

```bash
source ~/esp/esp-idf/export.sh
```

### STM32 hardware access

OpenOCD plus ST-Link are required for flash, metadata and hardware tests.

For normal flash scripts, the defaults are:

```text
OPENOCD=openocd
OPENOCD_INTERFACE=interface/stlink.cfg
OPENOCD_TARGET=target/stm32f1x.cfg
```

For the HIL runner, STM32-capable OpenOCD can be selected explicitly:

```bash
export STM32_OPENOCD=/usr/bin/openocd
export STM32_OPENOCD_SCRIPTS=/usr/share/openocd/scripts
```

This distinction is useful on hosts that also contain an ESP32-specific OpenOCD installation.

---

# Make command reference

Run the built-in command summary at any time:

```bash
make help
```

The sections below document every public root-level target in the project.

## Quick command matrix

| Command | Purpose | Hardware required | Main output |
|---|---|---:|---|
| `make` / `make all` | Run integrated project closure | No | PASS/FAIL markers |
| `make help` | Print command summary | No | Terminal help |
| `make check` | Full source/security/build/benchmark validation | No | Integrated PASS/FAIL |
| `make warning-check` | Rebuild STM32 code with warnings as errors | No | `WARNING_CLEAN_BUILD=PASS` |
| `make benchmark` | Generate reproducible benchmark reports | No | `dist/benchmark/*` |
| `make test` | Alias of `make check` | No | Same as `make check` |
| `make tools` | Python compile smoke check | No | Exit status |
| `make firmware` | Build bootloader + application | No | STM32 `out/*` |
| `make bootloader` | Build bootloader only | No | `bootloader.elf/.bin/.hex` |
| `make application` | Build application only | No | `application.elf/.bin/.hex` |
| `make combined` | Merge bootloader + application image | No | `dist/secure-delta-ota-combined.bin` |
| `make toolchain-info` | Show selected STM32 toolchain | No | Toolchain paths/settings |
| `make gateway` | Build ESP32 gateway | No board required for compile | `gateway-esp32/build/` |
| `make gateway-build` | Explicit alias for gateway build | No board required for compile | Same as `make gateway` |
| `make release` | Create immutable signed release | No | `dist/releases/...` |
| `make flash-bootloader` | Build + flash bootloader | STM32 + ST-Link | STM32 programmed |
| `make flash-application` | Build + flash application | STM32 + ST-Link | STM32 programmed |
| `make flash-combined` | Build + flash merged image | STM32 + ST-Link | STM32 programmed |
| `make dump-metadata` | Dump + decode metadata pages | STM32 + ST-Link | `dist/metadata-pages.bin` |
| `make erase-metadata` | Erase internal metadata A/B | STM32 + ST-Link | Metadata reset |
| `make hil-test` | Run deterministic 9-scenario HIL | STM32 + ESP32 + W25Q + ST-Link | Scenario PASS/FAIL |
| `make clean` | Remove generated build/report outputs | No | Clean working tree outputs |

The `TOOLCHAIN` assignment can appear before or after the target:

```bash
make firmware TOOLCHAIN=gcc
make TOOLCHAIN=gcc firmware
```

Supported values are `gcc` and `clang`.

## `make` / `make all`

Default entry point. It is intentionally equivalent to the integrated closure:

```bash
make
```

Equivalent to:

```bash
make check
```

Use this when you want the repository's default quality gate.

## `make help`

Print the root target summary:

```bash
make help
```

This command does not build firmware and does not require a toolchain.

## `make check`

Run the complete host/build/security/portfolio validation:

```bash
make check TOOLCHAIN=gcc
```

or:

```bash
make check TOOLCHAIN=clang
```

The integrated checker validates:

- clean product presentation and first-party source contracts;
- deterministic hardware evidence record;
- checked-in reference benchmark schema and limits;
- trust-anchor and credential packaging boundary;
- secure container and recovery source contracts;
- CI/release automation contracts;
- Python syntax and host regression tests;
- STM32 deterministic fault-build variants;
- external-flash sanitizer build;
- live benchmark generation and artifact policy.

Expected terminal marker:

```text
SECURE_DELTA_OTA_PROJECT_CHECK=PASS
```

For a quick static-only check without STM32 builds/live benchmark:

```bash
python3 scripts/project_check.py --static-only
```

That direct Python command is useful while editing documentation or CI files.

## `make warning-check`

Force a clean STM32 rebuild with compiler warnings promoted to errors:

```bash
make warning-check TOOLCHAIN=gcc
```

or:

```bash
make warning-check TOOLCHAIN=clang
```

This target uses temporary `build-warning` / `out-warning` directories and removes them after success.

Expected marker:

```text
WARNING_CLEAN_BUILD=PASS
```

Use this before publishing a portfolio snapshot or after changing C code. It is also executed by the build CI workflow.

## `make benchmark`

Run a fresh benchmark using the current toolchain and host:

```bash
make benchmark TOOLCHAIN=gcc
```

or:

```bash
make benchmark TOOLCHAIN=clang
```

Generated files:

```text
dist/benchmark/benchmark.json
dist/benchmark/benchmark.csv
dist/benchmark/benchmark.md
```

The benchmark checks:

- bootloader flash `<= 24 KiB`;
- application flash `<= 38 KiB`;
- SRAM `<= 20 KiB`;
- raw delta savings `>= 20%`;
- signed delta savings `>= 20%`;
- signed full/delta artifacts fit the 128 KiB Incoming partition;
- delta reconstruction is byte-for-byte correct;
- recorded deterministic HIL evidence remains `9/9`.

Wall-clock build/signing times are reported but are not pass/fail thresholds because they depend on the host.

Expected markers:

```text
BENCHMARK=PASS
DELTA_SAVINGS=PASS
HIL_EVIDENCE=PASS 9/9
```

## `make test`

Alias of the integrated check:

```bash
make test TOOLCHAIN=gcc
```

Equivalent to:

```bash
make check TOOLCHAIN=gcc
```

## `make tools`

Compile Python files under `tools/`, `server/` and `scripts/` to catch syntax/import-bytecode issues:

```bash
make tools
```

This is a lightweight smoke check. Use `make check` for the complete validation.

## `make firmware`

Build both STM32 components:

```bash
make firmware TOOLCHAIN=gcc
```

Outputs:

```text
node-stm32f103/bootloader/out/bootloader.elf
node-stm32f103/bootloader/out/bootloader.bin
node-stm32f103/bootloader/out/bootloader.hex
node-stm32f103/bootloader/out/bootloader.map
node-stm32f103/bootloader/out/bootloader.size.txt

node-stm32f103/application/out/application.elf
node-stm32f103/application/out/application.bin
node-stm32f103/application/out/application.hex
node-stm32f103/application/out/application.map
node-stm32f103/application/out/application.size.txt
```

This target only builds; it does not flash hardware.

## `make bootloader`

Build the STM32 bootloader only:

```bash
make bootloader TOOLCHAIN=gcc
```

The bootloader linker region is fixed at `0x08000000` with a 24 KiB flash budget.

## `make application`

Build the STM32 application only:

```bash
make application TOOLCHAIN=gcc
```

The application linker region begins at `0x08006000` with a 38 KiB flash budget.

The default application version comes from the source configuration. Benchmark/release tooling can build explicit application versions through `PROJECT_CFLAGS`.

## `make combined`

Build bootloader + application and merge them into one binary:

```bash
make combined TOOLCHAIN=gcc
```

Outputs:

```text
dist/secure-delta-ota-combined.bin
dist/secure-delta-ota-combined.txt
```

The combined image starts at `0x08000000`. Internal metadata pages are deliberately left erased for first-boot initialization.

The packaged repository uses an unprovisioned trust anchor, so the merged image is a development/portfolio image until a trusted public key is provisioned.

## `make toolchain-info`

Show the selected STM32 compiler and bootloader linker/output paths:

```bash
make toolchain-info
```

or explicitly:

```bash
make toolchain-info TOOLCHAIN=gcc
make toolchain-info TOOLCHAIN=clang
```

Typical output identifies the target, toolchain, compiler, linker script and ELF path.

## `make gateway` / `make gateway-build`

Activate ESP-IDF first:

```bash
source ~/esp/esp-idf/export.sh
make gateway
```

`make gateway` delegates to `make gateway-build`.

The target:

1. verifies that `idf.py` is available;
2. runs `scripts/esp32_build_guard.py`;
3. runs `idf.py build` inside `gateway-esp32/`.

Generated ESP-IDF output is under:

```text
gateway-esp32/build/
```

The build directory is intentionally ignored by repository presentation/security scans because it contains generated CMake/ESP-IDF metadata.

## `make release`

Create an immutable signed firmware release.

Full + delta example:

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

Full-only example:

```bash
make release \
  TARGET=/path/to/application-v2.bin \
  TARGET_VERSION=2 \
  SIGNING_KEY=/secure/path/release-signing.pem \
  KEY_ID=0x15000001 \
  BASE_URL=https://firmware.example
```

Make variables:

| Variable | Required | Meaning |
|---|---:|---|
| `TARGET` | yes | Target application `.bin` |
| `TARGET_VERSION` | yes | Target monotonic firmware version |
| `SIGNING_KEY` | yes | External EC P-256 private key path |
| `KEY_ID` | yes | Non-zero signing key identifier |
| `BASE_URL` | yes | HTTPS URL used in release metadata |
| `BASE` | optional | Exact previous application binary for delta generation |
| `BASE_VERSION` | required with `BASE` | Version of the exact base binary |
| `CHANNEL` | optional | `stable`, `beta` or `dev`; default `stable` |
| `RELEASE_ROOT` | optional | Output root; default `dist/releases` |

Security/release rules:

- the private key must not be inside the repository;
- key permissions must satisfy the release tool policy;
- an existing immutable release directory is not overwritten;
- a delta is included only when the configured savings policy passes;
- a signed full-image fallback is always emitted.

Typical release directory:

```text
dist/releases/fw-vN/
├── application-vN.bin
├── application-vN.full.sdot
├── application-vBASE-to-vN.delta.sdot
├── manifest.json
├── manifest.json.sig
├── signing-public.pem
├── checksums.txt
└── release-notes.md
```

## `make flash-bootloader`

Build and program only the bootloader:

```bash
make flash-bootloader TOOLCHAIN=gcc
```

Requirements:

- STM32 connected through ST-Link;
- OpenOCD installed;
- correct interface/target configuration.

Optional environment overrides:

```bash
export OPENOCD=/usr/bin/openocd
export OPENOCD_INTERFACE=interface/stlink.cfg
export OPENOCD_TARGET=target/stm32f1x.cfg
```

The script programs `node-stm32f103/bootloader/out/bootloader.hex`, verifies it and resets the MCU.

## `make flash-application`

Build and program only the application:

```bash
make flash-application TOOLCHAIN=gcc
```

The application HEX is linked for `0x08006000`, so OpenOCD programs it at its linked address.

## `make flash-combined`

Build and flash the merged bootloader + application image:

```bash
make flash-combined TOOLCHAIN=gcc
```

The binary is programmed at:

```text
0x08000000
```

Use this for a clean development board baseline. The combined image does not provision a production trust key by itself.

## `make dump-metadata`

Read the two internal metadata pages through OpenOCD and decode them:

```bash
make dump-metadata
```

Output:

```text
dist/metadata-pages.bin
```

The script then runs:

```text
tools/inspect_metadata.py
```

This command is useful for debugging active/pending versions, update state, generation, boot attempts and diagnostic fields.

## `make erase-metadata`

Erase internal metadata A/B:

```bash
make erase-metadata
```

Affected internal flash range:

```text
0x0800F800 - 0x0800FFFF
```

**This command is destructive.** It resets boot/update metadata. It does not erase the bootloader or application regions, but it changes the persisted recovery state. Do not run it in the middle of a recovery experiment unless that reset is intentional.

## `make hil-test`

Run the deterministic physical fault-injection suite.

Activate ESP-IDF and select STM32 OpenOCD first:

```bash
source ~/esp/esp-idf/export.sh

export STM32_OPENOCD=/usr/bin/openocd
export STM32_OPENOCD_SCRIPTS=/usr/share/openocd/scripts
```

Then run:

```bash
make hil-test \
  TOOLCHAIN=gcc \
  ESP32_PORT=/dev/ttyUSB0 \
  WIFI_SSID="your-ssid" \
  WIFI_PASSWORD="your-password" \
  SDOTA_HOST_IP=<PC_LAN_IP>
```

Required/commonly used variables:

| Variable | Required | Default / behavior |
|---|---:|---|
| `ESP32_PORT` | yes | ESP32 serial port; `PORT` is accepted as an alias |
| `WIFI_SSID` | yes | Test Wi-Fi SSID |
| `WIFI_PASSWORD` | network-dependent | Password; may be empty only for an open network |
| `SDOTA_HOST_IP` | recommended | Auto-detected if omitted |
| `HTTPS_PORT` | optional | `8443` |
| `MQTT_PORT` | optional | `8883` |
| `SDOTA_HIL_KEY_ID` | optional | `0xC0DE0001` |
| `TOOLCHAIN` | optional | GCC preferred if installed, otherwise Clang |
| `STM32_OPENOCD` | optional | Auto-detected; explicit value recommended on mixed OpenOCD hosts |
| `STM32_OPENOCD_SCRIPTS` | optional | Auto-detected from common locations |

The runner creates its signing key only in a temporary directory, provisions the corresponding public key for the test build, restores the repository files in `finally`, and removes temporary HIL builds/ESP-IDF generated files.

The nine scenarios are:

1. `control-secure-delta`
2. `patch-reset`
3. `backup-reset`
4. `install-midpage-reset`
5. `mqtt-drop-after-accepted`
6. `https-truncate`
7. `tampered-signature`
8. `rollback-control`
9. `rollback-reset`

Expected final marker:

```text
HIL hardware test: PASS (9 deterministic scenarios)
```

Detailed expected invariants and the verified physical result are in `docs/hil-results.md`.

## `make clean`

Remove generated build/report output:

```bash
make clean
```

It removes:

- normal STM32 `build/` and `out/`;
- temporary `build-*` / `out-*` STM32 directories;
- `gateway-esp32/build/`;
- ESP-IDF `sdkconfig` and `sdkconfig.old`;
- benchmark scratch directory;
- `dist/`;
- `build-host/`.

It does not delete source, checked-in benchmark references or documentation.

---

# Recommended workflows

## 1. Fast source/documentation edit loop

```bash
python3 scripts/project_check.py --static-only
make tools
```

## 2. Final host validation before commit

```bash
make warning-check TOOLCHAIN=gcc
make check TOOLCHAIN=gcc
make benchmark TOOLCHAIN=gcc
```

If only LLVM is available:

```bash
make warning-check TOOLCHAIN=clang
make check TOOLCHAIN=clang
make benchmark TOOLCHAIN=clang
```

## 3. Build and flash a clean STM32 baseline

```bash
make flash-combined TOOLCHAIN=gcc
```

Then inspect metadata if needed:

```bash
make dump-metadata
```

## 4. Build the ESP32 gateway

```bash
source ~/esp/esp-idf/export.sh
make gateway
```

## 5. Create a signed release

```bash
make release \
  TARGET=/path/to/application-v2.bin \
  TARGET_VERSION=2 \
  BASE=/path/to/application-v1.bin \
  BASE_VERSION=1 \
  SIGNING_KEY=/secure/path/release-signing.pem \
  KEY_ID=0x15000001 \
  BASE_URL=https://firmware.example
```

## 6. Re-run full physical resilience validation

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

---

# Benchmark results

## Checked-in reproducible reference

`benchmarks/reference.json` records a Clang/LLVM reference measurement:

| Metric | Reference |
|---|---:|
| Bootloader flash | 11,400 B / 24,576 B (`46.39%`) |
| Bootloader RAM | 2,072 B / 20,480 B (`10.12%`) |
| Application v2 flash | 11,276 B / 38,912 B (`28.98%`) |
| Application v2 RAM | 1,968 B / 20,480 B (`9.61%`) |
| Raw delta | 970 B |
| Raw delta savings | `91.40%` |
| Signed delta | 1,174 B |
| Signed full | 11,480 B |
| Signed delta savings | `89.77%` |
| HIL evidence | `9/9 PASS` |

The reference also stores environment metadata and informational wall-clock timings.

## User-verified GCC measurement

A later local GCC benchmark produced:

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

The difference between GCC and Clang is expected because code generation and linker behavior differ. Both measurements remain comfortably inside the fixed partitions and both satisfy the minimum delta-savings policy.

For the current checkout, always prefer a freshly generated:

```bash
make benchmark TOOLCHAIN=gcc
```

over copying an old size number into a new report.

# Hardware validation summary

The deterministic HIL suite has recorded **9/9 PASS** on the STM32F103 + ESP32 + W25Q setup.

Final rollback-reset state:

```text
generation=74
state=IDLE
active_version=1
pending_version=0
boot_attempts=0
last_error=0x0008B003
application=v1 byte-for-byte verified
```

The diagnostic is intentionally preserved after successful recovery.

See:

- `docs/hil-results.md`
- `VALIDATION.md`
- `docs/results-report.md`

# Repository map

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
benchmarks/                  Checked-in reproducible benchmark reference
docs/                        Architecture, protocol, HIL, usage and portfolio docs
```

# Reports and evidence

The project contains both a high-level report and detailed evidence documents:

| File | Purpose |
|---|---|
| `PROJECT_REPORT.md` | Full engineering report: objective, architecture, protocols, security, recovery, release flow and results |
| `VALIDATION.md` | Detailed verification matrix, pass criteria, reproduced commands and known boundaries |
| `docs/results-report.md` | Consolidated benchmark + hardware result report |
| `docs/hil-results.md` | Nine-scenario physical fault matrix and final hardware state |
| `docs/benchmark-portfolio.md` | Benchmark method, thresholds and interpretation |
| `benchmarks/reference.md` | Checked-in reference benchmark |
| `benchmarks/reference.json` | Machine-readable reference benchmark |
| `docs/portfolio-evidence.md` | Claim-to-evidence mapping |
| `docs/portfolio-one-page.md` | One-page portfolio summary |
| `docs/portfolio-demo.md` | Suggested live/demo presentation flow |
| `docs/make-command-reference.md` | Dedicated full Make command reference |

# Portfolio evidence

Recommended reading order:

1. `docs/portfolio-one-page.md`
2. `PROJECT_REPORT.md`
3. `docs/architecture.md`
4. `docs/firmware-container.md`
5. `docs/uart-ota-protocol.md`
6. `docs/hil-results.md`
7. `docs/results-report.md`
8. `VALIDATION.md`
9. `docs/portfolio-evidence.md`

The project is presented as a completed secure delta OTA reference implementation and hardware-validated portfolio artifact.
