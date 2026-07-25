# Boot and Update State Machine

Status: **Phase 0 design frozen; Phase 2 active-application validation and jump implemented**

## 1. Update states

```c
typedef enum
{
    UPDATE_IDLE = 0,

    UPDATE_RECEIVING,
    UPDATE_ARTIFACT_READY,

    UPDATE_VERIFYING_CONTAINER,
    UPDATE_VERIFYING_BASE,
    UPDATE_PATCHING,
    UPDATE_IMAGE_READY,

    UPDATE_BACKING_UP,
    UPDATE_INSTALLING,
    UPDATE_VERIFYING_INSTALL,

    UPDATE_TRIAL_BOOT,
    UPDATE_CONFIRMED,

    UPDATE_ROLLBACK,
    UPDATE_FAILED
} UpdateState_t;
```

## 2. High-level transition diagram

```text
IDLE
  |
  | START
  v
RECEIVING ------------------------------+
  | DATA / resume                       |
  | FINISH + artifact CRC valid         | ABORT / unrecoverable error
  v                                     |
ARTIFACT_READY                           |
  | reset / INSTALL                     |
  v                                     |
VERIFYING_CONTAINER                     |
  | valid                               |
  +-----------------------+             |
  |                       |             |
  | delta                 | full        |
  v                       |             |
VERIFYING_BASE            |             |
  | base matches          |             |
  v                       |             |
PATCHING                  |             |
  | target hash valid     |             |
  +-----------+-----------+             |
              v                         |
         IMAGE_READY                    |
              |                         |
              v                         |
         BACKING_UP                     |
              | backup verified         |
              v                         |
         INSTALLING                     |
              |                         |
              v                         |
      VERIFYING_INSTALL                 |
              | installed hash valid    |
              v                         |
         TRIAL_BOOT                     |
          /       \
 confirm           timeout/crash/attempt limit
   |                    |
   v                    v
CONFIRMED            ROLLBACK
   |                    |
   v                    | restore verified backup
 IDLE <-----------------+

Any unrecoverable validation/storage error -> FAILED.
FAILED permits a new START after explicit cleanup.
```

## 3. Boot decision order

On reset, the bootloader performs:

1. initialize clock minimally, watchdog policy, debug and storage;
2. read internal metadata and external redundant metadata;
3. choose newest valid generation;
4. validate state consistency;
5. execute state recovery action;
6. validate active application before jump.

Decision table:

| Persisted state | Bootloader action |
|---|---|
| IDLE | Validate active application; jump or stay in recovery |
| RECEIVING | Do not install; jump active application so transfer can resume |
| ARTIFACT_READY | Validate/install when requested policy says install; otherwise jump active app |
| VERIFYING_CONTAINER | Restart validation from beginning |
| VERIFYING_BASE | Restart base verification |
| PATCHING | Erase reconstructed partition and restart patch safely |
| IMAGE_READY | Continue with backup |
| BACKING_UP | Restart or resume backup using verified progress metadata |
| INSTALLING | Resume installation from last verified internal page |
| VERIFYING_INSTALL | Re-run complete installed-image verification |
| TRIAL_BOOT | Increment attempt policy and boot target, or rollback when limit reached |
| CONFIRMED | Commit new active version, retire backup per policy, transition IDLE |
| ROLLBACK | Resume restore from backup |
| FAILED | Stay recovery-capable; active valid application may be booted only if policy allows |

## 4. Metadata design

```c
typedef struct
{
    uint32_t magic;
    uint32_t generation;
    uint32_t metadata_version;

    uint32_t state;
    uint32_t active_version;
    uint32_t pending_version;

    uint32_t active_update_id;
    uint32_t received_size;
    uint32_t expected_size;

    uint32_t copy_offset;
    uint32_t boot_attempts;
    uint32_t last_error;

    uint32_t crc32;
} BootMetadata_t;
```

External Flash contains Metadata A and B in separate erase sectors. A new record is written to the older/invalid copy, verified, then selected by its larger generation counter.

Internal metadata is deliberately small and boot-critical. Phase 1 will decide whether it contains the complete record or an emergency recovery record pointing to external metadata; either choice must remain compatible with the fixed 1 KiB region.

## 5. Application validity

Before jump, bootloader checks at minimum:

- initial MSP lies inside `0x20000000`–`0x20005000` (top-of-stack inclusive) and is 8-byte aligned;
- reset handler lies inside the application Flash region and has Thumb bit set;
- application size from trusted metadata is nonzero and within bounds;
- installed image hash matches trusted metadata when state requires it.

Jump sequence:

```text
disable interrupts
stop SysTick
reset/disable used peripherals
clear NVIC enable and pending state
set SCB->VTOR = APPLICATION_START_ADDRESS
load MSP from application vector table
branch to application reset handler
```

## 6. Trial boot policy

Initial policy:

- maximum unconfirmed boot attempts: 3;
- backup remains available throughout trial;
- application calls `Boot_ConfirmApplication()` only after essential self-test;
- a watchdog reset before confirmation counts as a failed attempt;
- after the attempt limit, state transitions to ROLLBACK.

Essential self-test should cover enough functionality to decide that the release can operate, but it must not wait indefinitely for optional network availability.

## 7. Power-loss guarantees

### During download

Active internal application is untouched. After reset, application resumes from `next_expected_offset`.

### During patching

Active application and backup remain untouched. Reconstructed partition is erased and patch restarts or safely resumes according to the final janpatch adapter capability.

### During backup

Internal application remains untouched. Installation cannot start until backup verification succeeds.

### During installation

Progress is committed after each verified internal Flash page. Reset resumes installation from the first unverified page. If source reconstructed image becomes invalid, rollback is attempted from verified backup.

### During rollback

Restore progress is also page-based and idempotent.

## 8. Illegal transitions

Examples that must be rejected:

- DATA while not RECEIVING;
- INSTALL while RECEIVING;
- PATCHING before signature and base checks;
- INSTALLING before backup verification;
- CONFIRMED without TRIAL_BOOT;
- clearing backup before confirmation;
- accepting a different update ID during RECEIVING without ABORT or new START cleanup.
