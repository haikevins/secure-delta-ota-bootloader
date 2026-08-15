# Phase 4 — External SPI Flash Checklist

## Repository/build

- [x] Common W25Q32/W25Q64-compatible SPI1 driver shared by bootloader and application.
- [x] JEDEC ID read and validation.
- [x] Read Data, Page Program and 4 KiB Sector Erase.
- [x] Bounded transfer/program/erase waits.
- [x] 256-byte page-boundary splitting.
- [x] Verify-after-write.
- [x] Reject writes that require NOR `0 -> 1` transitions.
- [x] Overflow-safe range validation.
- [x] 4 KiB alignment enforcement for erase.
- [x] Partition-relative storage abstraction.
- [x] Final 4 KiB sector reserved for hardware self-test.
- [x] Host geometry/bounds unit test.
- [x] Dedicated physical hardware test script.

## Local check

```bash
make phase4-check
```

Expected:

```text
Phase 4 external-Flash geometry tests: PASS
Secure Delta OTA Phase 4 external SPI Flash check: PASS
```

## Hardware check

Wire PA5/PA6/PA7/PB0 to W25Q32/W25Q64 CLK/DO/DI/CS, use 3.3 V and common GND, then:

```bash
make phase4-hw-test
```

Pass criteria:

- [ ] JEDEC ID = `0xEF4016` (W25Q32) or `0xEF4017` (W25Q64).
- [ ] Test sector erase and full blank-check succeed.
- [ ] Cross-page write succeeds.
- [ ] Readback and explicit verify succeed.
- [ ] Out-of-partition request is rejected.
- [ ] Final erase and blank-check succeed.
- [ ] `P4_STATUS=0x50415353`.

The repository-only check cannot prove physical wiring or real Flash
program/erase behavior; those are confirmed by the hardware test.


### OpenOCD connection profile

`phase4-hw-test` uses the fixed profile:

```text
source [find interface/stlink.cfg]
transport select hla_swd
source [find target/stm32f1x.cfg]
reset_config none
adapter speed 1000
```
