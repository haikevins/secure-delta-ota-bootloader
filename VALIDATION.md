# Validation Report

## 1. Validation summary

Integrated project status: **PASS**.

Recorded physical HIL result: **9/9 deterministic scenarios PASS**.

The validation strategy intentionally separates four evidence classes:

1. static/source contracts;
2. host/tooling tests;
3. cross-compiled firmware and artifact benchmarks;
4. physical hardware-in-the-loop fault injection.

A claim is only treated as hardware-verified when the physical HIL evidence supports it.

## 2. Main reproduction commands

Fast static validation:

```bash
python3 scripts/project_check.py --static-only
```

Warning-clean STM32 build:

```bash
make warning-check TOOLCHAIN=gcc
```

Integrated project closure:

```bash
make check TOOLCHAIN=gcc
```

Fresh benchmark:

```bash
make benchmark TOOLCHAIN=gcc
```

Physical HIL:

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

## 3. Static and packaging contracts

`python3 scripts/project_check.py --static-only` validates:

| Check | Acceptance |
|---|---|
| Product presentation | First-party project contains no obsolete development-stage naming |
| Generated artifact handling | Build/cache/output directories are not treated as first-party source |
| HIL evidence record | 9 passed out of 9 total scenarios |
| Reference benchmark | Schema/limits/results are present and internally consistent |
| Trust anchor | Packaged tree remains unprovisioned |
| Runtime credentials | Packaged runtime configuration contains no Wi-Fi secret |
| Private signing key | No private-key PEM material in first-party package |
| Source contracts | Security/recovery/fault hooks and expected files remain present |
| Automation contracts | CI validates project, warning-clean build and benchmark |
| Python syntax | Main tools/scripts compile |
| Delta model | Deterministic JojoDiff host regression passes |

Expected final marker:

```text
SECURE_DELTA_OTA_PROJECT_CHECK=PASS
```

## 4. Compiler-warning validation

The normal STM32 build enables:

```text
-Wall
-Wextra
-Wpedantic
-Wshadow
-Wundef
-Wdouble-promotion
```

`make warning-check` adds `-Werror` and rebuilds both bootloader and application in isolated temporary directories.

Validated in the packaged environment with Clang:

```text
WARNING_CLEAN_BUILD=PASS
```

The source fixes also guard secure-disabled legacy-only helpers at compile time, addressing the previously observed GCC `-Wunused-variable` / `-Wunused-function` warnings without disabling those warning classes globally. The GCC command to reproduce on a GNU Arm host is:

```bash
make warning-check TOOLCHAIN=gcc
```

## 5. STM32 build and memory validation

Fixed limits:

```text
Bootloader flash: 24576 B
Application flash: 38912 B
SRAM: 20480 B
```

The live benchmark reads ELF section sizes and rejects any build exceeding those limits.

The benchmark builds:

- bootloader;
- application v1;
- application v2.

It measures:

- `.text + .data` flash footprint;
- `.data + .bss` RAM footprint;
- binary size;
- flash/RAM utilization percentage.

## 6. Delta validation

The benchmark creates a fresh v1 → v2 delta and verifies reconstruction byte-for-byte.

Acceptance criteria:

```text
reconstructed target == exact target binary
raw delta savings >= 20%
signed delta savings >= 20%
signed full artifact <= 128 KiB
signed delta artifact <= 128 KiB
```

The signed-release generation uses an ephemeral EC P-256 key for benchmark purposes. The benchmark key is not persisted.

## 7. Checked-in benchmark reference

Environment recorded in `benchmarks/reference.json`:

```text
toolchain: clang
platform: Linux x86_64
Python: 3.13.5
```

Reference footprint:

| Image | Flash | Flash budget | RAM | SRAM budget |
|---|---:|---:|---:|---:|
| Bootloader | 11,400 B | 24,576 B | 2,072 B | 20,480 B |
| Application v1 | 11,264 B | 38,912 B | 1,968 B | 20,480 B |
| Application v2 | 11,276 B | 38,912 B | 1,968 B | 20,480 B |

Reference artifact efficiency:

| Artifact | Size |
|---|---:|
| Base v1 | 11,264 B |
| Target v2 | 11,276 B |
| Raw delta | 970 B |
| Signed delta | 1,174 B |
| Signed full | 11,480 B |

```text
raw delta savings    = 91.40%
signed delta savings = 89.77%
```

Reference wall-clock timings are recorded for context only and are not acceptance thresholds.

## 8. User-verified GCC benchmark

A local GNU Arm GCC benchmark run recorded:

```text
BENCHMARK=PASS
boot_flash=9412
app_v2_flash=9648
raw_delta=1242
signed_delta=1446
signed_full=9852
raw_delta_savings=87.13%
signed_delta_savings=85.32%
HIL_EVIDENCE=PASS 9/9
```

These values are compiler-specific observations, not hard-coded acceptance expectations.

## 9. Deterministic fault-build matrix

The integrated checker cross-builds bootloader variants for deterministic reset hooks:

```text
control
patch reset
backup reset
install reset
rollback reset
```

It also builds the external-flash sanitizer application used to establish a deterministic test baseline.

The build gate enforces the 24 KiB bootloader limit for every fault variant.

## 10. Physical HIL matrix

Physical result: **PASS — 9/9**.

| Scenario | Injected condition | Required invariant | Result |
|---|---|---|---|
| `control-secure-delta` | none | signed v1→v2 delta confirms v2 | PASS |
| `patch-reset` | reset during reconstruction | replay safely; active image not corrupted | PASS |
| `backup-reset` | reset after backup checkpoint boundary | resume backup and complete update | PASS |
| `install-midpage-reset` | reset during internal-flash installation | torn page is erased/replayed safely | PASS |
| `mqtt-drop-after-accepted` | broker disconnect after accepted command | reconnect without duplicate accepted update | PASS |
| `https-truncate` | artifact transfer truncated | no STM32 install; active image unchanged | PASS |
| `tampered-signature` | valid transport CRC, modified ECDSA signature | bootloader rejects; v1 preserved | PASS |
| `rollback-control` | signed unhealthy candidate | three failed trials restore exact v1 | PASS |
| `rollback-reset` | reset during backup restore | resume rollback and restore exact v1 | PASS |

## 11. Fault witnesses

The HIL suite does not accept a scenario merely because the final firmware happens to be correct. It records witnesses proving the intended fault path executed.

Recorded witnesses include:

```text
patch-reset generation = control generation + 1
backup-reset generation = control generation + 1
install-midpage-reset generation = control generation + 1
mqtt-drop-after-accepted generation = control generation
rollback-reset generation = rollback-control generation + 1
```

This distinguishes genuine fault recovery from an injection hook that never fired.

## 12. Final physical board state

After the final rollback-reset scenario:

```text
generation=74
state=0 (IDLE)
active_version=1
pending_version=0
boot_attempts=0
last_error=0x0008B003
application v1 byte-for-byte verification=PASS
```

The rollback diagnostic remains persisted intentionally after successful recovery.

No HIL signing private key was persisted.

## 13. Security validation

### Signed container

Validated controls:

- SHA-256 payload hash;
- SHA-256 base image hash for delta;
- SHA-256 target image hash;
- ECDSA P-256 signature;
- raw 64-byte `r || s` signature representation;
- key-ID trust-anchor selection;
- product/hardware compatibility;
- anti-downgrade target version;
- unsigned secure-path rejection.

### Negative security evidence

The tampered-signature HIL case intentionally keeps transport/storage integrity valid enough for the artifact to reach STM32, then modifies the signature. The STM32 bootloader rejects at the firmware-authentication boundary and the original application remains exact.

### Key custody

Validated repository/package properties:

- no signing private key included;
- trusted-key header unprovisioned by default;
- runtime Wi-Fi credential empty;
- HIL key generated under a temporary directory;
- HIL runner restores the original trust/runtime files even on exit.

## 14. Release-pipeline validation

Host validation covers:

- signed release creation;
- full + delta selection;
- minimum delta-savings policy;
- immutable release overwrite rejection;
- authorized release-key fingerprint policy;
- HTTPS release serving;
- MQTTS command serialization/publication;
- QoS 1 / PUBACK behavior;
- tampered release rejection.

The release tool always keeps a signed full-image fallback even when a delta is included.

## 15. Network fault validation

### MQTT disconnect

The gateway may lose the MQTTS connection after a command has been accepted. The HIL invariant requires:

- reconnect;
- no duplicate accepted STM32 update;
- OTA continues;
- metadata generation matches the control update.

### Truncated HTTPS

The gateway receives only a prefix of the artifact. Required invariant:

- download fails;
- STM32 does not receive an INSTALL request for the incomplete artifact;
- complete internal application region remains unchanged.

## 16. Recovery checkpoint validation

Checkpoint granularity:

```text
download/backup external NOR: 4 KiB sector-oriented recovery
internal install/rollback:    1 KiB STM32 flash page-oriented recovery
```

The installation path is idempotent: after a reset, the first potentially torn internal page is erased and replayed from a trusted external source.

## 17. Metadata validation

Boot-critical metadata uses two independent internal 1 KiB erase pages.

Selection is based on:

- valid magic/version;
- valid CRC32;
- monotonic generation;
- state consistency.

Commit behavior writes/verifies the replacement copy before it becomes authoritative.

`make dump-metadata` provides a hardware-readable diagnostic path.

## 18. CI validation

`.github/workflows/build.yml` runs:

```bash
make warning-check TOOLCHAIN=gcc
make check TOOLCHAIN=gcc
make benchmark TOOLCHAIN=gcc
```

`.github/workflows/test.yml` runs Python syntax and static project closure.

The protected firmware-release workflow runs the integrated check before release creation and materializes the production private key only in the runner temporary directory.

## 19. What is not claimed

The validation does not claim resistance to:

- invasive physical attacks;
- a compromised signing server/environment;
- denial-of-service against the network;
- firmware confidentiality;
- production RDP/debug lock configuration;
- secure bootloader self-update;
- hardware secure-element key storage.

These remain explicit deployment/security hardening topics.

## 20. Evidence index

| Evidence | File / command |
|---|---|
| Integrated closure | `make check TOOLCHAIN=gcc` |
| Warning-clean build | `make warning-check TOOLCHAIN=gcc` |
| Fresh benchmark | `make benchmark TOOLCHAIN=gcc` |
| Physical HIL | `make hil-test ...` |
| HIL result record | `docs/hil-results.md` |
| Consolidated results | `docs/results-report.md` |
| Reference benchmark | `benchmarks/reference.json` |
| Security design | `docs/firmware-container.md`, `docs/threat-model.md` |
| UART protocol | `docs/uart-ota-protocol.md` |
| Memory layout | `docs/memory-map.md` |
| Claim mapping | `docs/portfolio-evidence.md` |
| Full engineering report | `PROJECT_REPORT.md` |
