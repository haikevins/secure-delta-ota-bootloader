# Persistent Metadata and Boot Decision

[↑ Documentation Index](README.md) · [← Root](../README.md) · [← Boot State Machine](boot-state-machine.md) · [Boot Jump →](boot-jump.md)

## Table of contents

- [1. Purpose](#1-purpose)
- [2. Internal Flash revision](#2-internal-flash-revision)
- [3. Record commit protocol](#3-record-commit-protocol)
- [4. Generation ordering](#4-generation-ordering)
- [5. First boot](#5-first-boot)
- [6. Boot decision ownership](#6-boot-decision-ownership)
- [7. LED diagnostics](#7-led-diagnostics)
## 1. Purpose

This document describes the **persistent metadata storage and pure boot-decision
components**. These components do not themselves install OTA images; the integrated
bootloader does perform signed validation, reconstruction, installation, trial
boot, rollback, and recovery through `BootManager`, `SecureContainer`, and
`ImageInstaller`. Their responsibilities here are:

- persist a CRC-protected `BootMetadata_t` record;
- retain two independently erasable internal-Flash copies;
- select the newest valid generation;
- write only the older/invalid copy;
- verify a committed copy before selecting it;
- map each update state to an explicit boot action;
- boot the active application only when both metadata policy and vector checks allow it.

## 2. Internal Flash revision

The implemented layout uses two separately erasable 1 KiB pages:

```text
0x08000000  Bootloader       24 KiB
0x08006000  Application      38 KiB
0x0800F800  Metadata A        1 KiB
0x0800FC00  Metadata B        1 KiB
0x08010000  End
```

This deliberate 1 KiB application-budget reduction is required because an
STM32F103 medium-density page erase affects a complete 1 KiB page. Two separate
pages let the bootloader erase/program the inactive copy without destroying the
currently valid copy.

## 3. Record commit protocol

```text
read A and B
     |
validate magic/version/state/progress/CRC
     |
select newest valid generation
     |
select the other page as write target
     |
erase target page
     |
program record (CRC is the final field)
     |
read back + validate + byte-compare
     |
new generation becomes selected on next load
```

Power loss during erase/program leaves the previous selected page unchanged.
A partial target record has an invalid CRC and is ignored.

### A/B commit state

Metadata loading first selects the newest valid generation or defaults when neither slot validates.

```text
Load metadata
├── Slot A valid and newer -> select Slot A
├── Slot B valid and newer -> select Slot B
└── neither slot valid     -> use defaults
```

Commits always program the non-selected slot and make it authoritative only after readback verification.

**When Slot A is selected**

```text
Slot A selected
    -> program Slot B with generation + 1
    -> read back and verify CRC + bytes
    -> Slot B becomes authoritative
```

**When Slot B is selected, or no valid slot exists**

```text
Slot B selected / defaults
    -> program Slot A with next generation
    -> read back and verify CRC + bytes
    -> Slot A becomes authoritative
```

## 4. Generation ordering

Generation zero is invalid. Normal updates increment the selected generation.
Wrap changes `0xFFFFFFFF` to `1`. Newness uses signed modular subtraction, so
`1` is treated as newer than `0xFFFFFFFF` while ordinary adjacent generations
retain their expected ordering.

## 5. First boot

A combined persistent metadata image leaves both metadata pages erased. On first reset:

1. neither copy validates;
2. bootloader creates `UPDATE_IDLE` defaults for `APPLICATION_VERSION`;
3. defaults are committed to slot A as generation 1;
4. the active application vectors are validated;
5. bootloader performs the boot metadata handoff.

If metadata programming fails but the active application vectors are valid,
persistent metadata uses a finalized RAM default for that boot. This prevents a metadata
hardware fault from bricking an otherwise valid pre-OTA product image.

## 6. Boot decision ownership

`BootDecision_Evaluate()` is host-testable and has no peripheral dependency.
It maps persisted states to actions; `BootManager_Run()` dispatches those actions
into the owning implementation and reevaluates metadata after each completed
transition. In particular:

| Persisted state | Decision action |
|---|---|
| `UPDATE_IDLE` | `BOOT_ACTION_JUMP_ACTIVE` (subject to valid vectors) |
| `UPDATE_RECEIVING` | `BOOT_ACTION_RESUME_DOWNLOAD` (jump active app if valid) |
| `UPDATE_ARTIFACT_READY` | `BOOT_ACTION_PROCESS_ARTIFACT` |
| `UPDATE_VERIFYING_CONTAINER`, `UPDATE_VERIFYING_BASE` | `BOOT_ACTION_RESTART_VALIDATION` |
| `UPDATE_PATCHING` | `BOOT_ACTION_RESTART_PATCH` |
| `UPDATE_IMAGE_READY`, `UPDATE_BACKING_UP` | `BOOT_ACTION_CONTINUE_BACKUP` |
| `UPDATE_INSTALLING` | `BOOT_ACTION_RESUME_INSTALL` |
| `UPDATE_VERIFYING_INSTALL` | `BOOT_ACTION_VERIFY_INSTALL` |
| `UPDATE_TRIAL_BOOT` | `BOOT_ACTION_BOOT_TRIAL` or `BOOT_ACTION_RESUME_ROLLBACK` at attempt limit / invalid vectors |
| `UPDATE_CONFIRMED` | `BOOT_ACTION_FINALIZE_CONFIRMATION` |
| `UPDATE_ROLLBACK` | `BOOT_ACTION_RESUME_ROLLBACK` |
| `UPDATE_FAILED` | `BOOT_ACTION_STAY_RECOVERY` |

A persisted `UPDATE_FAILED` does **not** jump to the Application in the current
boot-decision implementation. The bootloader's recovery LED loop is not an
application-side UART `START` handler; do not present it as remote recovery.

## 7. LED diagnostics

- 1–6 pulses: invalid application vector reason from boot metadata;
- 8 pulses: metadata storage/load fatal error;
- 9 pulses: `BootManager` cannot complete/dispatch the state and stays in a
  fatal recovery LED loop (not every non-idle state causes this pattern);
- five fast pulses followed by application heartbeat: active image booted.

## References

- [`boot_metadata.h`](../shared/include/boot_metadata.h)
- [`metadata_storage.c`](../node-stm32f103/bootloader/storage/metadata_storage.c)
- [`boot_decision.c`](../node-stm32f103/bootloader/src/boot_decision.c)

[↑ Documentation Index](README.md) · [← Root](../README.md) · [← Boot State Machine](boot-state-machine.md) · [Boot Jump →](boot-jump.md)
