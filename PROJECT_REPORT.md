# Project Report — Secure Delta OTA Bootloader

## 1. Executive summary

This project implements a secure and recoverable over-the-air firmware update system for an STM32F103C8T6 node without placing a network/TLS stack on the constrained MCU.

An ESP32 gateway owns Wi-Fi, HTTPS and MQTTS connectivity. The STM32 application owns artifact reception and external-flash staging. The STM32 bootloader remains the final trust boundary for firmware authenticity, compatibility, reconstruction, installation, trial boot and rollback.

The final system supports:

- signed full-image OTA;
- signed delta OTA from an exact previous binary;
- SHA-256 image binding;
- ECDSA P-256 authenticity;
- anti-downgrade policy;
- resumable UART transport;
- external NOR staging;
- reset-safe patch, backup, install and rollback;
- trial boot with confirmation;
- immutable release creation;
- deterministic physical fault injection;
- reproducible footprint and delta-efficiency benchmarking.

The recorded hardware validation completed **9/9 deterministic scenarios**.

## 2. Design goals and constraints

### 2.1 Target constraints

STM32F103C8T6 resources are deliberately treated as fixed:

```text
Internal flash   64 KiB
SRAM             20 KiB
Bootloader       24 KiB maximum
Application      38 KiB maximum
Metadata A/B      1 KiB each
```

The OTA design therefore avoids:

- a full TLS stack on STM32;
- large in-RAM firmware buffers;
- patching directly over the active application;
- dynamic allocation as a core recovery mechanism;
- storing private signing material on either embedded device.

### 2.2 Reliability goals

The update system must remain recoverable across:

- reset while receiving an artifact;
- reset during delta reconstruction;
- reset while backing up the active application;
- reset in the middle of internal-flash installation;
- candidate firmware failing to confirm;
- reset while restoring the backup;
- transport interruption;
- corrupted or maliciously modified firmware artifacts.

### 2.3 Security goals

The firmware trust decision must be made by the STM32 bootloader, not by MQTT, HTTPS, ESP32 or UART transport.

The secure path provides:

- release authenticity with ECDSA P-256;
- payload/base/target integrity with SHA-256;
- product and hardware policy binding;
- key-ID selection;
- target-version anti-downgrade policy;
- rejection of unsigned secure-path artifacts;
- private-key separation from deployed firmware.

## 3. System architecture

```text
Developer / CI
    |
    | build application binaries
    | generate full/delta artifacts
    | sign release
    v
Release server
    | HTTPS: manifest + artifact bytes
    | MQTTS: update command / status
    v
ESP32 gateway
    | TLS endpoints
    | cache complete artifact
    | COBS UART transfer
    v
STM32 application
    | packet/order/CRC validation
    | external NOR staging
    | persistent receive checkpoint
    v
STM32 bootloader
    | signed-container verification
    | base verification
    | delta reconstruction or full source
    | backup
    | install
    | verification
    | trial boot
    | confirm / rollback
```

### 3.1 Responsibility split

**CI/release tooling**

- builds target firmware;
- uses the exact prior release binary as the delta base;
- creates JojoDiff-compatible delta data;
- creates signed SDOT artifacts;
- enforces minimum delta savings;
- creates immutable release directories and signed manifests.

**Release server**

- serves immutable files over HTTPS;
- publishes update commands over MQTTS;
- verifies release-directory consistency before publication.

**ESP32 gateway**

- validates TLS peers;
- receives update orchestration over MQTTS;
- downloads and caches complete artifacts over HTTPS;
- transfers artifacts over UART with ACK/NACK/retry/resume;
- publishes progress and terminal status;
- does not own firmware signing authority.

**STM32 application**

- receives protocol frames;
- validates packet CRC, sequence, offset and update identity;
- writes only to the Incoming partition;
- persists resumable receive progress;
- requests bootloader installation after a complete artifact is ready;
- confirms a healthy trial application.

**STM32 bootloader**

- validates application vectors before jumping;
- authenticates the SDOT container;
- enforces version/product/base constraints;
- reconstructs delta output into external NOR;
- verifies reconstructed target image;
- backs up the current application before destructive install;
- installs page-by-page with recovery checkpoints;
- verifies the installed image;
- performs trial boot and rollback.

## 4. Hardware and memory architecture

### 4.1 STM32 / ESP32 UART

```text
ESP32 GPIO17 TX -> STM32 PA10 RX
ESP32 GPIO16 RX <- STM32 PA9 TX
ESP32 GND       -> STM32 GND
```

STM32 uses USART1 at 115200 8-N-1 for protocol version 1.

### 4.2 External W25Q SPI NOR

```text
PA5  SPI1_SCK  -> W25Q CLK
PA6  SPI1_MISO <- W25Q DO
PA7  SPI1_MOSI -> W25Q DI
PB0  GPIO      -> W25Q /CS
```

Supported JEDEC devices include W25Q32 (`EF 40 16`) and W25Q64 (`EF 40 17`). The logical OTA map stays 4 MiB.

### 4.3 Internal flash map

| Region | Address | Size | Purpose |
|---|---|---:|---|
| Bootloader | `0x08000000-0x08005FFF` | 24 KiB | Trust/recovery/install |
| Application | `0x08006000-0x0800F7FF` | 38 KiB | Active firmware |
| Metadata A | `0x0800F800-0x0800FBFF` | 1 KiB | Redundant boot record |
| Metadata B | `0x0800FC00-0x0800FFFF` | 1 KiB | Redundant boot record |

### 4.4 External logical flash map

| Region | Address | Size | Purpose |
|---|---|---:|---|
| External metadata A | `0x000000-0x000FFF` | 4 KiB | Handoff/checkpoint A |
| External metadata B | `0x001000-0x001FFF` | 4 KiB | Handoff/checkpoint B |
| Incoming | `0x002000-0x021FFF` | 128 KiB | Received SDOT artifact |
| Reconstructed | `0x022000-0x041FFF` | 128 KiB | Delta target output |
| Backup | `0x042000-0x061FFF` | 128 KiB | Validated previous app |
| Logs | `0x062000-0x071FFF` | 64 KiB | Bounded diagnostics |
| Reserved | remaining logical map | — | Future use |

The 128 KiB staging sizes intentionally exceed the 38 KiB internal application partition so the external storage layer can hold containers and recovery metadata without forcing exact internal-flash sizing.

## 5. UART OTA protocol

Protocol version 1 uses COBS framing with a `0x00` delimiter.

Raw packet layout:

```text
Offset  Size  Field
0       2     magic = 0xA55A
2       1     protocol_version = 1
3       1     command
4       4     update_id
8       4     offset
12      2     sequence
14      2     payload_length (0..256)
16      N     payload
16+N    4     packet_crc32
```

Key commands:

| Code | Command | Purpose |
|---:|---|---|
| `0x01` | HELLO | Capability discovery |
| `0x02` | QUERY | State/version/resume query |
| `0x10` | START | Begin transfer |
| `0x11` | DATA | Transfer 1..256 bytes |
| `0x12` | FINISH | Complete artifact |
| `0x13` | ABORT | Safely abort |
| `0x14` | RESUME | Continue matching update |
| `0x20` | INSTALL | Request bootloader install |
| `0x21` | STATUS | Detailed status |
| `0x22` | CONFIRM | Trial confirmation interface |
| `0x70` | ACK | Accepted request |
| `0x71` | NACK | Rejected request |

The protocol separates transport integrity from firmware authenticity. Packet CRC32 prevents accidental corruption from being silently accepted, while the signed-container check decides whether firmware is trusted.

## 6. Persistent update state machine

The persistent states are:

```text
IDLE
RECEIVING
ARTIFACT_READY
VERIFYING_CONTAINER
VERIFYING_BASE
PATCHING
IMAGE_READY
BACKING_UP
INSTALLING
VERIFYING_INSTALL
TRIAL_BOOT
CONFIRMED
ROLLBACK
FAILED
```

Recovery behavior is state-specific:

- `RECEIVING`: active firmware remains bootable; gateway can resume from a persisted checkpoint.
- `VERIFYING_CONTAINER`: verification restarts safely.
- `VERIFYING_BASE`: base verification restarts.
- `PATCHING`: reconstructed output is safely replayed.
- `BACKING_UP`: backup resumes from verified 4 KiB sector progress.
- `INSTALLING`: internal flash resumes from verified 1 KiB page progress.
- `VERIFYING_INSTALL`: whole-image verification repeats.
- `TRIAL_BOOT`: boot attempt accounting continues.
- `ROLLBACK`: restore resumes from the backup checkpoint.
- `FAILED`: failure is preserved while a safe recovery/new update remains possible.

Internal metadata uses redundant erase pages, CRC32 and a monotonic generation counter. The new copy is verified before becoming authoritative.

## 7. Delta OTA design

Delta creation uses an exact prior firmware binary. The host creates a JojoDiff-compatible patch; the STM32 bootloader uses a project-owned streaming adapter for reconstruction.

The critical safety rule is:

```text
delta accepted only if current base version and base image hash match
```

The bootloader never reconstructs directly over active internal flash. Delta output goes to the external Reconstructed partition first and is verified before installation.

The release tool includes a delta only when its savings meet the configured policy. Otherwise the signed full-image path remains available.

## 8. Signed SDOT container

The implemented secure container uses:

```text
fixed header               120 B
SCX1 extension              20 B
canonical signed header    140 B
payload                      N B
ECDSA P-256 signature       64 B raw r||s
```

The signature covers:

```text
complete signed header + payload
```

Security-relevant fields include:

- product ID;
- hardware revision;
- artifact type;
- base version;
- target version;
- payload length;
- base/target image properties;
- SHA-256 digests;
- key ID;
- hash/signature algorithm identifiers.

The private signing key is host-side only. The STM32 contains only the trusted public key and key ID.

## 9. Release and network pipeline

A normal release contains:

```text
application-vN.bin
application-vN.full.sdot
application-vBASE-to-vN.delta.sdot   # when policy allows
manifest.json
manifest.json.sig
signing-public.pem
checksums.txt
release-notes.md
```

Release properties:

- existing release directories are immutable;
- release verification re-checks files and hashes;
- exact previous release binary is used for delta generation;
- MQTTS carries orchestration/status only;
- HTTPS carries artifact bytes;
- command/status use QoS 1 where delivery acknowledgement matters;
- gateway terminal failure status waits for PUBACK in the negative path;
- Wi-Fi power save is disabled in the validated HIL profile to improve network test determinism.

## 10. Trial boot and rollback

Installation does not immediately promote `active_version`.

After a candidate is installed:

1. bootloader records `TRIAL_BOOT`;
2. boot attempt count is persisted;
3. watchdog protection is enabled;
4. application boots;
5. application runs essential self-test;
6. application confirms only if healthy.

Maximum unconfirmed attempts: **3**.

If confirmation does not occur, the bootloader transitions to rollback and restores the exact validated backup.

The backup is retained until successful confirmation.

## 11. Deterministic hardware validation

The physical test suite contains nine scenarios:

| Scenario | Main invariant | Result |
|---|---|---|
| `control-secure-delta` | Signed delta installs and confirms v2 | PASS |
| `patch-reset` | Reconstruction reset recovers | PASS |
| `backup-reset` | Backup checkpoint recovers | PASS |
| `install-midpage-reset` | Torn internal-flash install replays safely | PASS |
| `mqtt-drop-after-accepted` | Reconnect does not duplicate accepted update | PASS |
| `https-truncate` | Incomplete artifact is not installed | PASS |
| `tampered-signature` | STM32 rejects artifact and preserves v1 | PASS |
| `rollback-control` | Unhealthy candidate restores exact v1 | PASS |
| `rollback-reset` | Reset during rollback resumes exact restore | PASS |

Recorded final state:

```text
generation=74
state=IDLE
active_version=1
pending_version=0
boot_attempts=0
last_error=0x0008B003
application=v1 verified
```

`0x0008B003` is intentionally preserved as the rollback diagnostic even though recovery has completed and the system is back in `IDLE`.

Fault witnesses also proved that reset hooks actually executed by observing expected metadata-generation differences from control runs.

## 12. Benchmark results

### 12.1 Checked-in Clang reference

| Metric | Value |
|---|---:|
| Bootloader flash | `11400 B / 24576 B` (`46.39%`) |
| Bootloader RAM | `2072 B / 20480 B` (`10.12%`) |
| App v2 flash | `11276 B / 38912 B` (`28.98%`) |
| App v2 RAM | `1968 B / 20480 B` (`9.61%`) |
| Raw delta | `970 B` |
| Raw savings | `91.40%` |
| Signed delta | `1174 B` |
| Signed full | `11480 B` |
| Signed savings | `89.77%` |
| HIL evidence | `9/9 PASS` |

### 12.2 User-verified GCC run

| Metric | Value |
|---|---:|
| Bootloader flash | `9412 B` |
| App v2 flash | `9648 B` |
| Raw delta | `1242 B` |
| Signed delta | `1446 B` |
| Signed full | `9852 B` |
| Raw savings | `87.13%` |
| Signed savings | `85.32%` |
| HIL evidence | `9/9 PASS` |

Compiler output differs between GCC and Clang. The portable claims are the hard partition limits, minimum savings policy and HIL scenario count; exact byte counts remain toolchain-specific measurements.

## 13. Build quality

STM32 code is compiled with:

```text
-Wall
-Wextra
-Wpedantic
-Wshadow
-Wundef
-Wdouble-promotion
```

The repository also provides:

```bash
make warning-check TOOLCHAIN=gcc
```

which adds `-Werror` for a clean rebuild so unused or newly introduced warnings cannot be ignored during final quality review.

The previously identified unused secure-disabled helpers are compile-time guarded rather than hidden with global warning suppression.

## 14. Packaging and key custody

The public repository/package intentionally contains:

```c
TRUSTED_KEY_PROVISIONED = 0
TRUSTED_KEY_ID = 0
```

and empty runtime Wi-Fi credentials.

Packaging validation searches first-party source for:

- private-key PEM material;
- leaked test Wi-Fi credentials;
- provisioned trust-anchor state;
- stale generated output incorrectly treated as source.

Generated build directories are excluded from first-party presentation scans because they can legitimately contain compiler/CMake/ESP-IDF strings copied from external tools.

## 15. Reproduction commands

Full validation:

```bash
make warning-check TOOLCHAIN=gcc
make check TOOLCHAIN=gcc
make benchmark TOOLCHAIN=gcc
```

STM32 build:

```bash
make firmware TOOLCHAIN=gcc
make combined TOOLCHAIN=gcc
```

Gateway build:

```bash
source ~/esp/esp-idf/export.sh
make gateway
```

Hardware test:

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

The complete target/variable reference is in `README.md` and `docs/make-command-reference.md`.

## 16. Known boundaries

The project does not claim to solve:

- physical invasive attacks;
- irreversible production readout-protection policy;
- firmware confidentiality/encryption;
- secure bootloader self-update;
- compromise of the signing environment;
- hardware secure-element custody;
- network denial of service.

These are explicit scope boundaries rather than silent omissions.

## 17. Portfolio value

The repository demonstrates a complete cross-domain embedded system:

- linker/memory-map design;
- STM32 bootloader architecture;
- external NOR constraints;
- persistent recovery metadata;
- custom binary protocol design;
- delta reconstruction;
- public-key firmware authentication;
- trial/rollback reliability;
- ESP-IDF networking;
- TLS and MQTT orchestration;
- immutable release engineering;
- deterministic hardware fault injection;
- reproducible benchmarking;
- CI and evidence-driven validation.

The strongest portfolio claim is not a single feature; it is the demonstrated end-to-end interaction between security, constrained embedded storage, network transport and recovery under deterministic physical faults.
