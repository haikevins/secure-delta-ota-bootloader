# Phase 7 — Power-loss Recovery

Status: **implemented; hardware fault-injection test provided**

## Scope

Phase 7 makes the Phase-6 full-image OTA path recoverable across resets in two
places where progress can be torn:

1. while the running application is receiving the image over UART; and
2. while the bootloader is replacing the internal application image.

It does not add rollback or trial boot. Those remain Phase 8.

## Recovery model

```text
PC -> UART DATA -> W25Q Incoming Artifact
          |
          +-- every complete 4 KiB:
              commit external Download Checkpoint A/B

power loss
   |
   v
application boots
   |
load newest valid Download Checkpoint
   |
RESUME from last complete 4 KiB boundary
   |
re-erase first uncheckpointed W25Q sector
   |
retransmit tail
   v
ARTIFACT_READY -> INSTALL -> reset
                          |
                          v
                    bootloader
                          |
                  validate source
                          |
              INSTALLING copy_offset=N
                          |
              erase/program/verify 1 KiB
                          |
              commit internal Metadata A/B
                          |
                    copy_offset=N+page
```

The core rule is: **persistent progress is advanced only after the data it
describes has been verified**.

## Persistent UART download checkpoint

A 40-byte CRC-protected `DownloadCheckpointRecord_t` is stored redundantly in
the two external metadata sectors at offset `0x100`.

```c
typedef struct
{
    uint32_t magic;
    uint32_t generation;
    uint32_t format_version;
    uint32_t state;
    uint32_t update_id;
    uint32_t target_version;
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t next_offset;
    uint32_t crc32;
} DownloadCheckpointRecord_t;
```

States are `IDLE`, `RECEIVING`, and `ARTIFACT_READY`.

The receiver commits a `RECEIVING` checkpoint only at a complete 4 KiB
Incoming Artifact sector boundary. It intentionally does not erase/rewrite a
metadata sector for every 256-byte UART packet.

Example:

```text
runtime received = 4608 bytes
persistent checkpoint = 4096 bytes
power lost
reboot restores = 4096 bytes
PC retransmits from = 4096 bytes
```

The first sector at the restored offset is erased before retransmission, so
partially programmed bytes beyond the checkpoint cannot contaminate the new
stream.

`FINISH` first verifies CRC32 over the complete W25Q artifact and then commits
`ARTIFACT_READY`. `ABORT` writes a newer `IDLE` tombstone.

The PC sender detects a matching persistent `RECEIVING` session and sends
`RESUME`; it derives the UART sequence from the returned offset.

## Coexistence with the Phase-6 install handoff

Each external Metadata A/B sector contains two independent records:

```text
sector + 0x000 : Phase-6/7 install handoff, 36 bytes
sector + 0x100 : Phase-7 download checkpoint, 40 bytes
```

Because W25Q erase granularity is 4 KiB, updating either record can erase the
whole sector. The storage layer therefore preserves and restores the other
valid record while updating its own A/B slot. The other sector remains the
fallback if power is lost during that operation.

## Internal installation checkpoint

The bootloader uses `BootMetadata_t.copy_offset` as the persistent install
checkpoint.

Intermediate checkpoints must be aligned to the STM32F103 internal Flash erase
page size, 1 KiB. The final checkpoint may equal the exact image size when the
last page is shorter than 1 KiB.

For each application page the bootloader performs:

```text
1. read source page from W25Q into SRAM
2. erase the current 1 KiB internal Flash page
3. program halfwords
4. compare every programmed byte with the SRAM source page
5. set copy_offset = next page boundary / exact final image size
6. commit internal Metadata A/B
```

A reset before step 6 leaves the previous valid checkpoint selected. On the
next boot the same first unverified page is erased and programmed again.

This also covers a reset after page verification but before/during the metadata
commit: repeating a verified page is safe.

## Final verification and idempotence

When `copy_offset == image_size`, the bootloader commits
`UPDATE_VERIFYING_INSTALL` and verifies:

- complete internal image CRC32;
- initial MSP range/alignment;
- Reset_Handler Thumb bit and image range;
- normal application-vector validation.

If verification succeeds, the persistent download checkpoint is cleared
before internal metadata is published as `UPDATE_IDLE`. If power is lost while
finalizing, `VERIFYING_INSTALL` remains selected and finalization is retried.

If installed-image verification fails while the external source is still
valid, installation restarts safely from page zero.

## Fault-injection hardware test

`make phase7-hw-test PORT=/dev/ttyUSB0` builds a test-only bootloader and
performs two deterministic reset scenarios:

1. receive 4608 bytes, reset the board, prove that the persistent UART progress
   restores to 4096 bytes, then resume the transfer;
2. during bootloader installation, force a one-shot reset at internal copy
   offset 1536 bytes, inside the second 1 KiB page.

The test then verifies application version 2, checks the final internal
metadata generation/state, and restores the normal Phase-7 bootloader without
overwriting application v2.

The OpenOCD profile is:

```text
source [find interface/stlink.cfg]
transport select hla_swd
source [find target/stm32f1x.cfg]
reset_config none
adapter speed 1000
```

## Guarantees and limits

Phase 7 improves reset/power-loss convergence for the existing full-image OTA
path. It still has these limits:

- CRC32 detects accidental corruption; it is not authenticity.
- There is no active-image backup or rollback yet.
- There is no trial-boot confirmation yet.
- A programming failure that is permanent, rather than a transient power loss,
  can leave the device in bootloader recovery.
- Delta patch recovery is not implemented yet.

Backup, trial boot and rollback are Phase 8 concerns; signed containers arrive
later in the roadmap.
