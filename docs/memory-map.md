# Memory Map Specification

Status: **Base map frozen; Phase 8 defines a sublayout inside Backup Image**

All addresses are inclusive unless stated otherwise.

## 1. STM32F103C8T6 internal Flash

Nominal size: 64 KiB, `0x08000000`–`0x0800FFFF`.

| Region | Start | End | Size | Purpose |
|---|---:|---:|---:|---|
| Bootloader | `0x08000000` | `0x08005FFF` | 24 KiB | Boot decision, validation, future patch/install/rollback |
| Application | `0x08006000` | `0x0800F7FF` | 38 KiB | Active product application |
| Metadata A | `0x0800F800` | `0x0800FBFF` | 1 KiB | Redundant boot record A |
| Metadata B | `0x0800FC00` | `0x0800FFFF` | 1 KiB | Redundant boot record B |

Total: 24 + 38 + 1 + 1 = 64 KiB.

Phase 0 reserved one 1 KiB metadata placeholder. Phase 3 deliberately consumes
one additional page and reduces the application by 1 KiB so each redundant copy
has an independent erase page. This is required for a power-loss-safe commit.

### Application linker region

```ld
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08006000, LENGTH = 38K
    RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 20K
}
```

### Internal Flash safety rules

- Application image size must be `<= 38 KiB`.
- Application erase/write must stop before `0x0800F800`.
- Metadata writes may target only `0x0800F800` or `0x0800FC00`.
- A commit erases only the older/invalid page and verifies the new copy before
  it can become selected.
- Bootloader remains hard-limited to 24 KiB.

## 2. SRAM

Nominal SRAM: 20 KiB, `0x20000000`–`0x20004FFF`.

Phase 3 adds only small stack-local metadata records. No persistent SRAM is
shared across the bootloader/application handoff.

## 3. W25Q32 external SPI NOR Flash

Nominal size: 4 MiB, `0x000000`–`0x3FFFFF`.

| Region | Start | End | Size | Purpose |
|---|---:|---:|---:|---|
| External Metadata A | `0x000000` | `0x000FFF` | 4 KiB | Install handoff A + Phase-7 download checkpoint A |
| External Metadata B | `0x001000` | `0x001FFF` | 4 KiB | Install handoff B + Phase-7 download checkpoint B |
| Incoming Artifact | `0x002000` | `0x021FFF` | 128 KiB | Full/delta secure container |
| Reconstructed Image | `0x022000` | `0x041FFF` | 128 KiB | Reconstructed target image |
| Backup Image | `0x042000` | `0x061FFF` | 128 KiB | Verified active-image backup |
| Update Logs | `0x062000` | `0x071FFF` | 64 KiB | Bounded diagnostics |
| Reserved | `0x072000` | `0x3FEFFF` | Remaining | Future use |
| Phase 4 Self-test | `0x3FF000` | `0x3FFFFF` | 4 KiB | Destructive driver validation |

The Phase 4 driver and storage abstraction enforce these bounds. External metadata does not replace the two internal boot-critical records introduced in Phase 3.

Phase 6/7 uses the first 36 bytes of each 4 KiB external metadata sector for
the A/B install handoff record. Phase 7 additionally stores a 40-byte download
checkpoint at sector offset `0x100`. Because either record update requires a
whole-sector erase, the storage layer preserves the other valid record while
rewriting its target A/B sector.


### Phase 8 Backup Image sublayout

The 128 KiB Backup Image partition keeps its frozen outer boundaries. Phase 8
uses it as:

| Subregion | Relative offset | Absolute start | Purpose |
|---|---:|---:|---|
| Backup header sector | `0x0000-0x0FFF` | `0x042000` | CRC-protected previous-version record |
| Backup application bytes | `0x1000-0xA7FF` | `0x043000` | Exact 38 KiB internal application-region copy |
| Unused backup capacity | remainder | after `0x04C7FF` | Reserved |

The image begins on a separate erase sector so a torn header commit can be
retried without erasing backup data.
