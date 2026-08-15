# Phase 8 — Trial Boot and Rollback

Status: **implemented; hardware validation command provided**

Phase 8 changes the basic full-image lifecycle from immediate activation to a
two-phase commit:

```text
ARTIFACT_READY
    |
    v
BACKING_UP  -- verified W25Q backup of complete 38 KiB app region
    |
    v
INSTALLING  -- 1 KiB internal-Flash checkpoints
    |
    v
VERIFYING_INSTALL
    |
    v
TRIAL_BOOT
   |  \
   |   \ 3 unconfirmed attempts / invalid trial
   |    v
   |  ROLLBACK -- 1 KiB restore checkpoints
   |    |
confirm |
   v    v
CONFIRMED
    |
    v
IDLE
```

## Backup layout

The existing 128 KiB `Backup Image` partition is subdivided internally:

```text
0x042000-0x042FFF  backup header sector
0x043000-...       exact 38 KiB application-region copy
```

The header is CRC32-protected and stores the previous `active_version`, fixed
application-region size, image CRC32 and load address. Image data begins on a
separate 4 KiB sector so a torn header rewrite never destroys backup bytes.

Backup progress uses `BootMetadata.copy_offset` at 4 KiB boundaries. A reset
inside a W25Q sector causes that sector to be erased and copied again.

## Trial semantics

After candidate install verification:

- `active_version` remains the previous confirmed version;
- `pending_version` remains the candidate version;
- state becomes `UPDATE_TRIAL_BOOT`;
- `boot_attempts` starts at zero.

Before every trial jump, the bootloader increments and commits `boot_attempts`.
Maximum attempts remain `3`.

The bootloader then starts STM32 IWDG immediately before the candidate jump.
A healthy application reaches the normal main loop, with SysTick, GPIO and the
UART OTA agent initialized, then commits `UPDATE_CONFIRMED` after a short health
window and resets through the bootloader.

The bootloader finalizes `CONFIRMED` by promoting `pending_version` to
`active_version` and returning metadata to `IDLE`.

`CMD_CONFIRM (0x22)` is also implemented for explicit application/PC-driven
confirmation while a trial is running.

## Rollback

If the candidate:

- reaches the 3-attempt limit without confirmation, or
- has invalid vectors at a trial boot,

the bootloader enters `UPDATE_ROLLBACK`.

The complete 38 KiB application region is restored from W25Q using the same
power-loss-safe rule as Phase 7:

```text
read backup page to SRAM
erase internal 1 KiB page
program page
byte-verify
commit copy_offset
```

After all 38 pages are restored, the bootloader verifies the full backup CRC and
application vector, restores the previous `active_version`, clears pending
fields and enters `IDLE`.

A successful rollback leaves a diagnostic in `last_error`. Trial-limit rollback
with three attempts is `0x0008B003`.

## Hardware test

```bash
make phase8-check
make phase8-hw-test PORT=/dev/ttyUSB0
```

The hardware test is deterministic:

1. flash baseline application v1 and clear internal metadata;
2. OTA healthy v2;
3. prove v2 passes trial and becomes confirmed/IDLE;
4. OTA special v3 built with `PHASE8_DISABLE_TRIAL_CONFIRM=1`;
5. observe v3 in `TRIAL_BOOT`;
6. allow IWDG to reset it three times;
7. prove rollback returns to v2;
8. verify final metadata diagnostic `0x0008B003`;
9. snapshot the complete confirmed-v2 38 KiB application region and compare the complete restored 38 KiB region byte-for-byte after rollback.
