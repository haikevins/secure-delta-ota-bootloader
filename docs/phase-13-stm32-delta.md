# Phase 13 — Delta Patch on STM32

Status: **implemented; host/build verified; physical board test provided**

Phase 13 moves the Phase-12 JojoDiff-compatible patch onto the STM32F103
bootloader.

```text
internal Flash application v1
            +
W25Q Incoming: D13P header + .jdiff
            |
            v
bootloader streaming patch engine
            |
            v
W25Q Reconstructed Image: application v2
            |
        CRC/vector verify
            |
            v
existing Phase-8 backup/install/trial/confirm
```

No HAL is used. The STM32 code remains SPL + CMSIS.

## D13P functional delta envelope

Phase 13 needs persistent information that was not carried by the raw
Phase-12 `.jdiff` stream. A small functional envelope is therefore placed
before the patch in `Incoming Artifact`.

It is deliberately **not** the final secure signed container. That remains a
later phase.

The 48-byte little-endian header contains:

```text
magic               "D13P" / 0x50333144
format_version      1
header_size         48
base_version
target_version
base_image_size
patch_size
target_image_size
target_load_address 0x08006000
base_image_crc32
target_image_crc32
patch_crc32
header_crc32
```

Artifact layout:

```text
0x0000  48-byte D13P header
0x0030  JojoDiff-compatible patch payload
```

CRC32 is used for Phase-13 integrity/base binding only. It is not firmware
authenticity.

## Streaming patch engine

`node-stm32f103/bootloader/patch/janpatch_port.c` implements the stream
operations needed by the Phase-12 generator:

```text
EQL
MOD
INS
DEL
BKT
```

The generator currently emits monotonic EQL/MOD/INS/DEL streams, so BKT is
normally zero. The embedded parser still implements BKT compatibility.

Memory usage is bounded:

```text
patch I/O / target write buffer = 128 bytes
no malloc/free
```

Streams are mapped as:

```text
source -> internal application Flash at 0x08006000
patch  -> external Incoming Artifact + 48
target -> external Reconstructed Image
```

The target is never reconstructed directly over the active internal
application.

## Persistent state flow

Phase 13 uses the states reserved since Phase 3:

```text
ARTIFACT_READY
    |
    v
VERIFYING_CONTAINER
    |
    v
VERIFYING_BASE
    |
    v
PATCHING
    |
    v
IMAGE_READY
    |
    v
BACKING_UP
    |
    v
INSTALLING
    |
    v
VERIFYING_INSTALL
    |
    v
TRIAL_BOOT -> CONFIRMED -> IDLE
```

The application stores the D13P artifact in Incoming exactly like a normal OTA
artifact and persists `ARTIFACT_READY`. The bootloader then owns all validation,
patching and installation work.

## Base binding

A delta is accepted only when:

```text
D13P base_version == active metadata version
D13P base_version == running application version at START
CRC32(internal application[0:base_image_size]) == D13P base_image_crc32
```

The application checks the base before reset and the bootloader checks it
again before PATCHING.

If base validation fails before internal application erase, the candidate is
rejected and metadata returns to IDLE with a Phase-13 diagnostic.

## Patch validation

Before reconstruction the bootloader checks:

```text
D13P header format/header CRC
metadata/update/version binding
patch payload CRC32
base version
base application CRC32
target size/address bounds
```

After reconstruction it checks:

```text
reconstructed target CRC32
target vector table
exact target size
```

Only after these checks does metadata enter `IMAGE_READY`.

## Power-loss behavior

`PATCHING` is recoverable without modifying the active application.

A reboot while `UPDATE_PATCHING` causes:

```text
erase required Reconstructed Image sectors
replay the deterministic patch from offset zero
verify target again
```

This is a restart-from-scratch patch policy rather than a patch-output
checkpoint. For the STM32F103 firmware sizes in this project it keeps
persistent logic simple while preserving the active application.

Once `IMAGE_READY` is reached, the existing Phase-8/Phase-7 backup and
page-checkpointed installation lifecycle is reused.

## Reusing the existing installer

The Phase-8 image installer now has a candidate-source descriptor.

Full OTA:

```text
candidate partition = Incoming Artifact
metadata state       = ARTIFACT_READY
handoff               = existing OTA6/Phase8 record
```

Delta OTA:

```text
candidate partition = Reconstructed Image
metadata state       = IMAGE_READY
descriptor           = persistent D13P header in Incoming
```

Both paths then use the same:

```text
backup full 38 KiB
install with 1 KiB internal-Flash checkpoints
verify installed CRC/vector
trial boot
application confirmation
rollback on failure
```

## UART protocol integration

The protocol version remains v1.

The STM32 application now advertises:

```text
OTA_CAP_DELTA_IMAGE
```

A delta START uses the already frozen 24-byte START payload:

```text
artifact_type          = FW_IMAGE_DELTA (2)
base_version            = exact installed base
target_version          = target
artifact_size           = D13P header + patch
artifact_crc32          = CRC32 of whole D13P artifact
container_header_size   = 48
```

The PC sender adds:

```bash
python3 tools/uart_ota_sender.py \
  --port /dev/ttyUSB0 \
  delta-ota dist/phase13/application-v1-to-v2.d13 \
  --update-id 0xD0130001
```

Resume remains the Phase-7 download checkpoint behavior. On a resumed transfer,
INSTALL detects the D13P magic from Incoming, so artifact type does not need a
new persistent checkpoint format.

## Host/build validation

Run:

```bash
make phase13-check
```

This verifies:

```text
STM32 bootloader <= 24 KiB
v1/v2 application <= 38 KiB
Phase-12 patch property tests
D13P header/artifact generation
exact embedded JanpatchPort compiled on host
embedded JanpatchPort byte-for-byte reconstruction
Python patch reconstruction
artifact/metadata hashes and CRCs
Phase-13 baseline image creation
```

## Hardware test

Phase 13 deliberately uses a PC USB-UART directly so the test isolates the
STM32 patch path from the already verified ESP32 network/orchestration phases.

Wiring:

```text
USB-UART TX -> STM32 PA10 RX
USB-UART RX <- STM32 PA9 TX
USB-UART GND -> STM32 GND
ST-Link      -> STM32 SWD
```

Disconnect the ESP32 UART wires from PA9/PA10 during this test so two devices
do not drive the same UART pins.

Run:

```bash
export STM32_OPENOCD=/usr/bin/openocd
export STM32_OPENOCD_SCRIPTS=/usr/share/openocd/scripts

make phase13-hw-test PORT=/dev/ttyUSB0
```

The runner:

1. programs bootloader + exact v1 baseline;
2. erases internal metadata and confirms v1/IDLE;
3. requires `OTA_CAP_DELTA_IMAGE`;
4. transfers only the D13P delta artifact;
5. requests INSTALL;
6. waits for v2 to reach confirmed IDLE;
7. dumps internal metadata with ST-Link;
8. dumps installed application and compares target byte-for-byte;
9. attempts the same v1->v2 delta again while running v2 and requires
   `OTA_STATUS_BASE_MISMATCH`.

Expected final markers:

```text
P13_BASELINE=PASS
Phase 13 Delta OTA PASS ...
P13_STM32_METADATA ... state=0 active_version=2 ...
STM32 delta-installed target byte-for-byte verification: PASS
Wrong-base delta rejection after v2 confirmation: PASS
Phase 13 STM32 delta patch hardware test: PASS
```

## Security boundary

This phase does **not** authenticate firmware.

TLS from earlier phases authenticates the network server, while the D13P CRCs
only detect corruption and bind a patch to an exact base byte sequence.

The next security/container phase can replace the functional D13P envelope
with the frozen secure container while retaining the streaming patch engine.
