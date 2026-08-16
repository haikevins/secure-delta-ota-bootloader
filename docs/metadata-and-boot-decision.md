# Persistent Metadata and Boot Decision

## 1. Purpose

persistent metadata adds the first persistent boot state. It does **not** install OTA images.
Its responsibilities are:

- persist a CRC-protected `BootMetadata_t` record;
- retain two independently erasable internal-Flash copies;
- select the newest valid generation;
- write only the older/invalid copy;
- verify a committed copy before selecting it;
- map each update state to an explicit boot action;
- boot the active application only when both metadata policy and vector checks allow it.

## 2. Internal Flash revision

The initial scaffold single-page placeholder is replaced by two 1 KiB pages:

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
It maps persisted states to actions such as resume download, restart patch,
resume install, boot trial or rollback. persistent metadata executes only safe active-image
jumps (`IDLE`, `RECEIVING`, non-expired `TRIAL_BOOT`). Actions owned by later
states remain in bootloader recovery with a nine-pulse PC13 pattern.

## 7. LED diagnostics

- 1–6 pulses: invalid application vector reason from boot metadata;
- 8 pulses: metadata storage/load fatal error;
- 9 pulses: a valid metadata state requires a recovery action;
- five fast pulses followed by application heartbeat: active image booted.
