# Memory Map Specification

Status: **Phase 3 revision frozen**

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
| External Metadata A/B | `0x000000` | `0x001FFF` | 8 KiB | Future mirrored/extended metadata |
| Incoming Artifact | `0x002000` | `0x021FFF` | 128 KiB | Full/delta secure container |
| Reconstructed Image | `0x022000` | `0x041FFF` | 128 KiB | Reconstructed target image |
| Backup Image | `0x042000` | `0x061FFF` | 128 KiB | Verified active-image backup |
| Update Logs | `0x062000` | `0x071FFF` | 64 KiB | Bounded diagnostics |
| Reserved | `0x072000` | `0x3FFFFF` | Remaining | Future use |

The W25Q32 driver begins in Phase 4. External metadata does not replace the two
internal boot-critical records introduced in Phase 3.
