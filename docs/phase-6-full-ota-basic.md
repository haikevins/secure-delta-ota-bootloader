# Phase 6 — Basic Full OTA

Status: **complete; end-to-end hardware verified**

## Scope

Phase 6 closes the first complete OTA loop:

```text
PC Python
  -> USART1 / COBS / CRC32
  -> STM32 application
  -> W25Q Incoming Artifact
  -> INSTALL handoff
  -> reset
  -> bootloader
  -> source CRC/vector validation
  -> internal application Flash
  -> installed CRC/vector validation
  -> IDLE
  -> jump new application
```

This phase is intentionally **not** the final secure OTA design. The artifact is
a raw application `.bin`. CRC32 provides accidental-corruption detection only.
Signed containers, SHA-256/authenticity and anti-rollback are later phases.

## Install handoff

`FINISH` remains non-destructive and leaves the runtime receiver in
`UPDATE_ARTIFACT_READY`.

`INSTALL (0x20)` performs the persistent handoff:

1. check target version is non-zero;
2. check image fits the 38 KiB internal application partition;
3. read and validate the incoming vector table;
4. write a CRC-protected Phase-6 handoff record to external Metadata A/B;
5. commit internal Metadata A/B as `UPDATE_ARTIFACT_READY`;
6. transmit ACK completely;
7. `NVIC_SystemReset()`.

The external handoff record is 36 bytes:

```c
magic
generation
format_version
update_id
target_version
image_size
image_crc32
target_load_address
crc32
```

External Metadata A and B are separate 4 KiB sectors. CRC is programmed last
and the older slot remains untouched until the new record verifies.

## Bootloader install

On reset the Phase-3 boot decision maps:

```text
ARTIFACT_READY     -> PROCESS_ARTIFACT
INSTALLING         -> RESUME_INSTALL
VERIFYING_INSTALL  -> VERIFY_INSTALL
```

Phase 6 executes these actions.

Before internal Flash is erased, the bootloader verifies:

- handoff record CRC/fields;
- update ID/version/size match internal metadata;
- incoming vector MSP and Reset_Handler;
- Reset_Handler lies inside the received image, not merely the partition;
- full incoming CRC32 equals the persisted expected CRC32.

Only then does it set `UPDATE_INSTALLING`.

The installer erases only:

```text
0x08006000 - 0x0800F7FF
```

It never erases the bootloader or Metadata A/B pages.

After programming, it commits `UPDATE_VERIFYING_INSTALL`, checks installed
CRC32 and vectors, then commits:

```text
state           = UPDATE_IDLE
active_version  = target_version
pending_version = 0
```

and boots the new application.

## Phase-6 recovery boundary

The original Phase-6 implementation can restart the *whole* installation from
offset zero when metadata says `INSTALLING`, and can re-check/reinstall from
`VERIFYING_INSTALL`.

Phase 7 supersedes this coarse behavior with persistent UART download
checkpoints and page-by-page internal installation checkpoints.

There is also no backup/rollback yet. A source artifact rejected before erase
leaves the active application untouched. A power failure or programming failure
after internal erase can still require recovery; rollback is a later phase.

## Build a test candidate

Normal application version is `1`.

```bash
make phase6-candidate
```

builds the same application with:

```text
APPLICATION_VERSION=2
```

at:

```text
node-stm32f103/application/out-phase6-candidate/application.bin
```

## Hardware test

Start with the normal Phase-6 combined firmware installed. For a clean
repeatable v1 baseline, erase the internal metadata first:

```bash
make erase-metadata
make flash-combined
```

The combined image intentionally does not overwrite Metadata A/B, so the
`erase-metadata` step is useful when repeating the v1 -> v2 demonstration.

Install pyserial if needed:

```bash
python3 -m pip install -r tools/requirements-phase5.txt
```

Then run:

```bash
make phase6-hw-test PORT=/dev/ttyUSB0
```

The test sends the v2 candidate over UART, issues INSTALL, waits through the
bootloader install, and repeatedly sends HELLO until application version 2
appears.

Expected final line:

```text
Full OTA PASS update_id=0x60060001 target_version=0x00000002 ...
```
