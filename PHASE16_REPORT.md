# Phase 16 Report — Fault Injection and HIL

## Result at packaging

Phase 16 is implemented with a deterministic nine-scenario hardware fault
matrix and a host/static regression gate.

Status:

```text
COMPLETE + HARDWARE VERIFIED
PHYSICAL 9-SCENARIO HIL: PASS
```

The final physical run completed all nine deterministic scenarios on the
ESP32 + STM32F103 + external SPI NOR hardware.

## What Phase 16 adds

Phase 16 moves recovery/security validation from isolated/manual tests into the
same production-shaped path validated in Phase 15:

```text
release/signing -> MQTTS -> ESP32 -> HTTPS -> UART -> STM32 secure bootloader
```

Faults cover four classes:

1. persistent-state reset recovery (`PATCHING`, backup, install, rollback);
2. server/network interruption (MQTT disconnect, HTTPS truncated body);
3. authentication failure (transport-valid ECDSA signature tamper);
4. trial/rollback recovery (unhealthy signed candidate, with and without a
   rollback checkpoint reset).

## Persistent fault witnesses

A deterministic reset test can falsely pass if its hook never runs. Phase 16
therefore makes each STM32 reset hook perform exactly one extra metadata commit
before resetting. The final generation is compared with a matching control
scenario.

The reset marker is test-only instrumentation. Normal update transitions clear
it, and the rollback hook preserves/restores the original production rollback
diagnostic.

## Fail-safe validation

For HTTPS truncation, signature tamper and rollback, the HIL runner dumps the
entire 38 KiB application region and compares it with the pre-test baseline,
not merely the linked binary length. This catches accidental writes beyond the
active image as well as wrong-image restoration.

## Security boundary

The signature-tamper fixture deliberately serves a modified SDOT from an
HIL-only fixed-route TLS server. This is necessary because the Phase-15
production release server verifies immutable release content before serving
it.

Transport CRC is recomputed for the modified bytes. Therefore rejection at
the STM32 demonstrates the ECDSA/SHA-256 secure-container trust boundary, not
a UART/HTTPS transport CRC failure.

## Gateway negative-state behavior

After an INSTALL ACK, an application query returning the old active version in
`IDLE` now terminates the gateway wait as a rejection/rollback result even if
`TRIAL_BOOT` was never observed. This prevents a signature rejection from
consuming the full 120-second secure-operation timeout.

The existing 120-second positive recovery window remains unchanged.

## HIL cleanup/security

The physical runner:
- generates an ephemeral P-256 private signing key;
- writes only the public point into transient bootloader builds;
- restores the unprovisioned trust header in `finally`;
- restores the default credential-free ESP32 runtime config and test CA;
- deletes ESP32 build/sdkconfig outputs and Phase-16 fault build trees.

The ZIP contains no provisioned private signing key.

## Validation

Required host/static command:

```bash
make phase16-check
```

Required physical command:

```bash
make phase16-hw-test \
  ESP32_PORT=/dev/ttyUSB0 \
  WIFI_SSID="your-ssid" \
  WIFI_PASSWORD="your-password" \
  PHASE16_HOST_IP=<PC_LAN_IP>
```

Final HIL success marker:

```text
Phase 16 fault injection/HIL hardware test: PASS (9 deterministic scenarios)
```

## Host-check / protected-workflow regression fix

The first packaged Phase-16 checker exposed two backward-compatibility
regressions before physical HIL:

1. `secure_container.c` included `stm32f10x.h` unconditionally after adding
   the PATCHING reset hook. Phase-14 intentionally compiles that exact secure
   verifier with host GCC, so the host build failed before reaching Phase 16.
   The STM32 header is now included only when
   `PHASE16_FAULT_PATCH_RESET` is defined.

2. The protected GitHub release workflow replaced the Phase-15 validation
   command with the Phase-16 Make target. Phase-15 has a static contract that
   deliberately requires `make phase15-check TOOLCHAIN=gcc`. The workflow now
   preserves that protected-release check and runs
   `python3 scripts/phase16_check.py` as a subsequent fault-injection
   validation step.

Regression validation:
- exact Phase-14 host secure-verifier compile command: PASS;
- direct Phase-15 server/release checker: PASS;
- direct Phase-16 fault checker: PASS;
- Phase-16 STM32 fault-build matrix: PASS.

## ESP-IDF log-format compile regression fix

The first physical Phase-16 HIL build reached the ESP32 compiler and exposed
a macro-level C compile error in `uart_ota_client.c`.

The Phase-16 final-state diagnostic had used a conditional expression as the
`format` argument to `ESP_LOGW()`:

```c
ESP_LOGW(TAG, saw_trial ? "..." : "...", value);
```

ESP-IDF logging macros wrap the format token with `LOG_FORMAT(...)`, which
requires a preprocessing string literal at that position. The conditional
expression therefore fails during macro expansion with:

```text
error: expected ')' before 'saw_trial'
```

The diagnostic is now expressed as two ordinary branches, each with a literal
format string. Phase-16 static validation also rejects the broken pattern so
this regression is caught before physical HIL.

## Deterministic external-flash baseline sanitization

Physical HIL reached the first STM32 UART START and returned:

```text
NACK status=0x06 state=0 next=0 detail=0x00071005
```

The detail decodes to `DownloadCheckpointStorage WRITE_FAILED`. The Phase-16
runner was restoring internal flash and the internal metadata pages between
scenarios, but it intentionally left the external W25Q contents untouched.
That is insufficient for a deterministic fault matrix because external
Metadata A/B contain both the persistent download checkpoint and install
handoff from earlier OTA runs.

Phase 16 now builds a dedicated one-shot test application with
`PHASE16_HIL_SANITIZE_EXTERNAL=1`. Before every scenario it:

1. boots with the exact scenario bootloader;
2. initializes W25Q storage;
3. erases only external Metadata A and Metadata B (0x000000 and 0x001000);
4. waits in the sanitizer application;
5. reflashes the normal v1 baseline and erases internal metadata.

Incoming, Reconstructed, Backup and Log partitions are not erased by this
sanitizer. Production application behavior is unchanged because the sanitizer
code is compile-time gated and never enabled outside Phase-16 HIL.

Expected host marker before each scenario:

```text
P16_EXTFLASH_SANITIZE=PASS scenario=<id> metadata_a_b=erased
```

## Incoming-partition NOR baseline fix

The first HIL run after Metadata A/B sanitization progressed further but
failed in `control-secure-delta` at `next=1280`:

```text
NACK status=0x06 state=1 next=1280 detail=0x00000009
```

`detail=9` is `SPI_FLASH_STATUS_NEEDS_ERASE`. This is a NOR-flash 0→1
programming rejection. The newly generated SDOT is mostly deterministic, so
its first bytes can match the previous artifact and program successfully over
old contents; its fresh ECDSA signature differs near the end, exposing the
dirty Incoming sector only on the final UART chunk.

The Phase-16 sanitizer now establishes a stronger deterministic baseline:

- erase external Metadata A;
- erase external Metadata B;
- erase all 128 KiB of Incoming;
- verify Metadata A/B are erased;
- verify the whole Incoming partition is erased.

The HIL runner gives the W25Q up to 18 seconds before reflashing the normal
v1 baseline. Reconstructed, Backup and Logs are not sanitized here because
their owning bootloader workflows perform their own erase/checkpoint logic;
those behaviors are part of the fault matrix under test.

Expected marker:

```text
P16_EXTFLASH_SANITIZE=PASS scenario=<id> metadata_a_b=erased incoming=erased_verified
```

## Tampered-signature terminal-status PUBACK fix

The HIL matrix reached scenario 7 (`tampered-signature`) with HTTPS serving
the 1446-byte corrupted SDOT correctly, but the broker transcript ended with
a clean MQTT DISCONNECT before it observed:

```text
P16_BROKER_FINAL=PASS state=failed
P16_BROKER_RESULT=PASS final=failed
```

The failure was an orchestration race, not a security-verification bypass.
`GatewayManager::PublishFailure()` used the asynchronous
`MqttOrchestrator_PublishStatus()` path. In single-shot HIL the gateway can
finish a fast STM32 signature rejection and stop the MQTT client before the
queued QoS-1 failure state is transmitted and PUBACKed.

The fix keeps the HIL oracle strict and changes the gateway behavior instead:
all terminal failure states now use
`MqttOrchestrator_PublishStatusAndWait(..., 5000)` before teardown. The
original OTA error remains the function result even if failure telemetry
cannot be acknowledged.

This makes negative terminal state delivery symmetric with the existing
`confirmed` terminal state and preserves the Phase-15 QoS-1 status contract.
Phase-16 static validation rejects any regression back to asynchronous
`PublishFailure()` behavior.

## Physical HIL closure — 9/9 PASS

The final Phase-16 hardware run completed the full deterministic matrix.

Observed final rollback-reset evidence:

```text
P16_STM32_METADATA label=rollback-reset generation=74 state=0 active_version=1 pending_version=0 boot_attempts=0 last_error=0x0008B003
P16_STM32_VERIFY=PASS label=rollback-reset app=v1
P16_SCENARIO=PASS id=rollback-reset generation=74 gateway=EXPECTED_FAIL
P16_FAULT_WITNESS=PASS id=patch-reset generation=34 control=33
P16_FAULT_WITNESS=PASS id=backup-reset generation=34 control=33
P16_FAULT_WITNESS=PASS id=install-midpage-reset generation=34 control=33
P16_MQTT_ISOLATION=PASS generation=33
P16_ROLLBACK_FAULT_WITNESS=PASS generation=74 control=73
Phase 16 fault injection/HIL hardware test: PASS (9 deterministic scenarios)
Final board state: rollback-reset scenario restores confirmed application v1; no HIL signing private key persisted.
```

This closes Phase 16 as **COMPLETE + HARDWARE VERIFIED**.
