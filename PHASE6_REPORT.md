# Phase 6 Report — Basic Full OTA

Phase 6 implements the first complete update cycle from a PC to a newly booted
STM32 application.

## Data path

```text
application-v1.bin running internally
       |
PC sends application-v2.bin over UART
       v
W25Q Incoming Artifact
       |
INSTALL
       v
external handoff A/B + internal metadata A/B
       |
reset
       v
bootloader validates source CRC/vector
       |
erase/program internal application partition
       |
verify installed CRC/vector
       v
metadata IDLE / active_version=2
       |
jump application v2
```

## Safety boundary

The bootloader validates the full external source before the first internal
application erase. Bootloader and internal metadata pages are outside the erase
range.

Phase 6 is still a basic updater: CRC32 is not authenticity, there is no backup
or rollback, and `copy_offset` is not yet committed page-by-page.

## Validation commands

```bash
make phase6-check
make flash-combined
python3 -m pip install -r tools/requirements-phase5.txt
make phase6-hw-test PORT=/dev/ttyUSB0
```

Hardware validation is intentionally left pending until run on the real board.


## Measured Clang/LLD build sizes

```text
bootloader.bin      6316 bytes / 24 KiB
application-v1.bin 10660 bytes / 38 KiB
application-v2.bin 10672 bytes / 38 KiB
combined Phase 6   35236 bytes
```

Repository validation:

```text
Phase 0: PASS
Phase 1: PASS
Phase 2: PASS
Phase 3: PASS
Phase 4: PASS
Phase 5: PASS
Phase 6 host/build check: PASS
```
