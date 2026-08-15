# Phase 4 — External SPI Flash

Status: **implemented; hardware validation script provided**

## Target and wiring

The production layout targets W25Q32 (4 MiB, JEDEC `EF 40 16`). Phase 4 also accepts W25Q64 (8 MiB, JEDEC `EF 40 17`) as compatible development hardware; the OTA layout still uses only the first 4 MiB.

| Signal | STM32F103 | W25Q32/W25Q64 |
|---|---|---|
| SCK | PA5 / SPI1_SCK | CLK |
| MISO | PA6 / SPI1_MISO | DO |
| MOSI | PA7 / SPI1_MOSI | DI |
| CS | PB0 | /CS |
| Supply | 3.3 V | VCC |
| Ground | GND | GND |

For a bare chip, keep `/WP` and `/HOLD` high. SPI is mode 0, 8-bit, MSB
first, software NSS, prescaler /4.

## Commands used

- Write Enable `0x06`
- Read Status Register-1 `0x05`
- Read Data `0x03`
- Page Program `0x02`
- 4 KiB Sector Erase `0x20`
- Read JEDEC ID `0x9F`

The common driver lives in `node-stm32f103/common/drivers/spi_flash.c` and is
compiled by both bootloader and application.

## Safety behavior

`SpiFlash_Write()` validates the full address range, checks that no `0 -> 1`
NOR transition is requested, splits writes at 256-byte page boundaries, waits
for BUSY to clear, and verifies programmed bytes by reading them back.

`SpiFlash_EraseSector()` requires a 4 KiB-aligned address and never rounds an
address down silently.

The partition wrapper `ExternalFlashStorage_*` accepts a fixed partition plus
a relative offset, preventing later OTA code from using arbitrary addresses.

## Partition map

```text
0x000000  External Metadata A      4 KiB
0x001000  External Metadata B      4 KiB
0x002000  Incoming Artifact      128 KiB
0x022000  Reconstructed Image    128 KiB
0x042000  Backup Image           128 KiB
0x062000  Update Logs             64 KiB
0x072000  Reserved
...
0x3FF000  Phase 4 Self-test        4 KiB
0x400000  End
```

The final sector is exclusively reserved for destructive Phase 4 hardware
validation.

## Repository validation

```bash
make phase4-check
```

## Hardware validation

```bash
make phase4-hw-test
```

The dedicated image checks JEDEC ID, erases the final sector, blank-checks it,
writes 40 bytes starting at offset `0xF0` (crossing a 256-byte page boundary),
reads and verifies the data, tests partition bounds, then erases and
blank-checks the sector again.

Expected final output:

```text
P4_STATUS=0x50415353
P4_JEDEC=0x00EF4016  # or 0x00EF4017
P4_DRIVER_STATUS=0x00000000
Phase 4 hardware SPI Flash test: PASS
```

Restore the normal firmware afterwards with `make flash-combined`.


## Primary references

- Winbond W25Q32JV and W25Q64JV data sheet/product documentation.
- ST RM0008 STM32F10x reference manual.
- STSW-STM32054 STM32F10x Standard Peripheral Library.
