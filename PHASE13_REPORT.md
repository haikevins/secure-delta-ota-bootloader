# Phase 13 Report — Delta Patch on STM32

## Result

Phase 13 implements the STM32 delta path from a JojoDiff-compatible artifact
through reconstruction, installation and trial confirmation.

The implementation keeps the active internal application unchanged until a
reconstructed target has been completely validated in external SPI NOR.

## Data path

```text
PC UART / future ESP32
       |
       v
Incoming Artifact
[D13P + JojoDiff patch]
       |
       v
VERIFYING_CONTAINER
       |
       v
VERIFYING_BASE
       |
       v
PATCHING
 internal app -> embedded patch engine -> Reconstructed Image
       |
       v
CRC/vector verification
       |
       v
IMAGE_READY
       |
       v
existing backup -> install -> trial -> confirm
```

## Embedded patcher

The STM32 implementation is an independently written JojoDiff/JANPatch-
compatible stream engine. It uses no heap and a 128-byte buffer.

The exact embedded `JanpatchPort_Apply()` implementation is also compiled as a
host executable with mock external Flash primitives. It must reconstruct the
real Phase-13 v2 application byte-for-byte.

## Functional envelope

The 48-byte D13P header persists the exact information needed across reset:

```text
base/target versions
base/patch/target sizes
target address
base/patch/target CRC32
header CRC32
```

This envelope is a Phase-13 engineering format only. It does not replace the
frozen signed secure-container design.

## Base validation

The node performs a cheap base-version rejection while the application still
runs, then the bootloader independently checks active metadata and CRC32 of the
exact base image before patching.

A wrong base never triggers internal application erase.

## Recovery

Download recovery remains the Phase-7 4 KiB checkpoint policy.

PATCHING recovery uses deterministic restart-from-zero. The reconstructed
partition is erased again and the patch is replayed. Because the active
internal application is still intact, this is safe and idempotent.

After IMAGE_READY, Phase-8 backup and Phase-7 1 KiB internal install
checkpointing resume responsibility for recovery.

## Full OTA compatibility

The historical image-installer function name is retained, but its source is
now selected dynamically:

```text
full  -> Incoming Artifact
delta -> Reconstructed Image
```

The full-image handoff record and full OTA behavior remain present.

## Verification

`make phase13-check` validates the actual STM32 builds, generated artifact,
embedded patch engine and target reconstruction.

`make phase13-hw-test PORT=...` performs the real-board proof and independent
ST-Link byte comparison.

Physical Phase-13 hardware validation remains pending until the board runner
returns PASS.

## Security statement

CRC32 here is integrity/base binding, not authenticity. Firmware signature
verification is intentionally not claimed by Phase 13.

## Measured packaged reference build

The packaged Clang reference build produced:

```text
bootloader          = 14156 bytes / 24576
base application v1 = 12620 bytes
target app v2       = 12632 bytes
JojoDiff patch      = 1045 bytes
D13P artifact       = 1093 bytes
D13P artifact CRC32 = 0xDC787F80
delta savings       = 91.35%
```

The exact embedded `JanpatchPort_Apply()` implementation reconstructed the
target byte-for-byte in the host flash-mock harness.

## Initial hardware UART synchronization fix

The first physical Phase-13 run stopped before any delta bytes were sent:
the runner's initial baseline probe timed out after 12 seconds. Immediately
afterward, interactive `hello` and `query` on the same `/dev/ttyUSB0` returned
application v1, IDLE state and capabilities `0x00000017`.

This isolated the failure to the HIL runner's initial serial synchronization,
not to wiring, the v1 baseline image, or the delta patch engine.

The runner now uses a dedicated baseline synchronizer that:

- queries with `CMD_QUERY`, matching the successful interactive command;
- uses the normal CLI response window (`1.5 s`, five retries);
- closes/reopens the serial port between unsuccessful attempts;
- allows 25 seconds overall;
- emits `P13_UART_SYNC` / `P13_UART_SYNC_RETRY` diagnostics;
- retains the existing fast polling helper for the already-open UART during
  the OTA reboot/trial sequence.

The OpenOCD post-metadata-erase run window was also increased from 3.5 to
4.5 seconds before the first serial open.
