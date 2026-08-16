# Phase 16 — Fault Injection and HIL

## Goal

Phase 16 converts the previously manual recovery tests into a deterministic
hardware-in-the-loop fault matrix that exercises the complete secure release
path:

```text
signed release
  -> MQTTS command/status
  -> ESP32
  -> HTTPS artifact
  -> cache
  -> UART
  -> STM32 SDOT authentication
  -> delta reconstruction
  -> backup/install
  -> trial/confirm or rollback
```

The Phase-16 HIL runner always starts each scenario from a freshly programmed
and metadata-erased application v1 baseline. Faults are injected at persistent
checkpoints rather than at arbitrary delays, so a PASS has a reproducible
meaning.

## Fault matrix

The authoritative machine-readable matrix is
`tests/fault/phase16_fault_matrix.json`.

| Scenario | Injected fault | Required invariant |
|---|---|---|
| `control-secure-delta` | none | signed delta reaches confirmed v2 |
| `patch-reset` | STM32 reset immediately after persistent `PATCHING` transition | reconstruction replays and confirms v2 |
| `backup-reset` | STM32 reset after the first verified 4 KiB backup checkpoint | backup resumes before internal-app erase |
| `install-midpage-reset` | STM32 reset at byte 1536 of candidate programming | torn 1 KiB page is erased/replayed and v2 confirms |
| `mqtt-drop-after-accepted` | broker closes TLS after `accepted` | status-channel reconnect does not interrupt the install |
| `https-truncate` | HTTPS closes after 512 bytes while advertising full length | no STM32 install; exact v1 application region remains |
| `tampered-signature` | last ECDSA signature byte is flipped and transport CRC is recomputed | STM32 rejects transport-valid but unauthentic SDOT |
| `rollback-control` | signed candidate intentionally never confirms | three failed trial boots restore exact v1 |
| `rollback-reset` | reset after first rollback page checkpoint | rollback resumes and restores exact v1 |

## Deterministic STM32 reset hooks

Phase-16 reset hooks are compile-time test instrumentation. They are absent
unless the corresponding `PHASE16_FAULT_*` macro is supplied to the bootloader
build.

### PATCHING reset

`PHASE16_FAULT_PATCH_RESET=1`

A one-shot witness metadata commit is made after the persistent `PATCHING`
state is established and before reconstructed external Flash is erased. The
reset proves that secure reconstruction can restart from the signed incoming
artifact without touching the active internal application.

### Backup checkpoint reset

`PHASE16_FAULT_BACKUP_RESET_OFFSET=4096UL`

The reset occurs after a verified 4 KiB backup checkpoint is committed. The
next boot resumes from the committed sector boundary.

### Install mid-page reset

`PHASE16_FAULT_INSTALL_OFFSET=1536UL`

The reset occurs while programming the second 1 KiB internal application page.
The persistent `copy_offset` still points to the preceding verified page.
Recovery must erase and replay the torn page before advancing the checkpoint.

### Rollback checkpoint reset

`PHASE16_FAULT_ROLLBACK_RESET_OFFSET=1024UL`

The reset occurs after the first verified rollback page checkpoint. The
fault-witness metadata preserves the original production rollback diagnostic;
after recovery the final `last_error` must still be the normal Phase-8 trial
limit reason.

## Transport faults

Phase 16 contains two HIL-only transport fixtures.

`tools/phase16_mqtt_broker.py` is a reconnect-capable TLS MQTT broker. It can
close the TLS connection after a selected status while remembering whether the
QoS1 update command was already acknowledged. It must not send a second update
command merely because the status connection reconnects.

`tools/phase16_fault_https_server.py` serves exactly one fixed artifact route.
In `truncate` mode it sends a valid HTTP 200 response with the original
`Content-Length`, writes only the configured prefix, then closes the TLS
connection. This proves that a partial HTTPS body never becomes an installable
STM32 artifact.

The tampered-signature scenario also uses this HIL-only server. Production
Phase-15 release serving intentionally verifies immutable releases and would
refuse a tampered artifact before serving it; Phase 16 must bypass that
server-side protection to exercise the STM32 signature trust boundary itself.

## HIL invariants

The runner verifies the STM32 after every scenario using ST-Link/OpenOCD.

Positive recovery scenarios require:
- final state `IDLE`;
- active version v2;
- zero pending/update progress fields;
- `last_error == 0`;
- target application bytes match the healthy v2 image.

Negative pre-install/security scenarios require:
- final state `IDLE`;
- active version v1;
- the complete 38 KiB application region equals the pre-test snapshot;
- HTTPS truncation leaves `last_error == 0`;
- signature tampering leaves a non-zero security diagnostic.

Rollback scenarios require:
- final state `IDLE`;
- active version v1;
- exact 38 KiB restoration;
- final `last_error == 0x0008B003`.

For reset faults, the test-only witness adds exactly one metadata commit. The
runner therefore compares the final metadata generation against an unfaulted
control run. This prevents a false PASS in which the fault macro compiled but
never actually executed.

## ESP32 reliability policy inherited from Phase 15

The Phase-15 HIL fix remains part of the baseline:
- Wi-Fi modem power saving is disabled with `WIFI_PS_NONE`;
- the runtime MQTT URI is checked in generated config, built binary and runtime
  log;
- STM32 secure final-state wait is 120 seconds;
- Phase-15/16 test MQTT idle windows are long enough for P-256 verification,
  patching, trial boots and rollback.

## Host/static gate

```bash
make phase16-check
```

This validates the matrix and source contracts, Python syntax, security
packaging boundary and builds the bootloader with every Phase-16 fault macro.
All fault variants must remain within the 24 KiB bootloader partition.

## Physical HIL gate

Keep the normal gateway wiring:

```text
ESP32 GPIO17 TX -> STM32 PA10 RX
ESP32 GPIO16 RX <- STM32 PA9 TX
ESP32 GND       -> STM32 GND
ST-Link         -> STM32 SWD
```

Then:

```bash
source ~/esp/esp-idf/export.sh

export STM32_OPENOCD=/usr/bin/openocd
export STM32_OPENOCD_SCRIPTS=/usr/share/openocd/scripts

make phase16-hw-test \
  ESP32_PORT=/dev/ttyUSB0 \
  WIFI_SSID="your-ssid" \
  WIFI_PASSWORD="your-password" \
  PHASE16_HOST_IP=<PC_LAN_IP>
```

The test creates an ephemeral P-256 signing key and local TLS PKI, provisions
only the test public key into transient bootloader builds, creates healthy and
never-confirming signed releases, builds the ESP32 once, then executes every
matrix scenario.

No HIL private key, provisioned trust anchor, Wi-Fi password, test CA build or
fault-injection binary is intentionally retained after the test.

Expected final marker:

```text
Phase 16 fault injection/HIL hardware test: PASS (9 deterministic scenarios)
```

That marker was observed on the physical ESP32 + STM32F103 + W25Q setup.
Phase 16 is therefore COMPLETE + HARDWARE VERIFIED.
