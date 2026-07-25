# Verification and Test Plan

Status: Phase 0 baseline

## Required test categories

### Build and memory

- bootloader binary remains within 24 KiB;
- application remains within 39 KiB;
- no linker section overlaps metadata;
- external partitions are sector aligned and non-overlapping.

### UART protocol

- valid COBS frame;
- malformed COBS frame;
- packet CRC mismatch;
- wrong protocol version;
- zero and maximum payload lengths;
- duplicate packet;
- conflicting duplicate packet;
- wrong sequence and wrong offset;
- timeout and retry exhaustion;
- gateway and STM32 reset followed by resume.

### Storage

- page-boundary SPI writes;
- sector erase verification;
- out-of-range rejection;
- 1 MiB repeated write/read stress target;
- metadata A corruption and metadata B recovery.

### Full update

- valid V1 to V2 update;
- oversized image rejection;
- target address rejection;
- payload corruption rejection;
- power loss at every internal page.

### Delta update

- exact reconstruction equals host new.bin byte-for-byte;
- base version mismatch;
- base hash mismatch;
- truncated patch;
- corrupted patch;
- target hash mismatch;
- fallback to full image.

### Security

- one-byte payload modification;
- modified signed header;
- signature by unknown key;
- wrong product ID;
- wrong hardware revision;
- downgrade target version.

### Rollback

- trial application HardFault;
- watchdog reset before confirmation;
- no confirmation for three attempts;
- power loss during rollback;
- invalid backup prevents destructive installation start.

## Final reliability target

At least 100 automated update cycles with fault injection and no unrecoverable brick before the project is declared complete.

## Phase 2 hardware acceptance

- flash the combined image at `0x08000000`;
- observe five bootloader flashes, a pause, then the application heartbeat;
- confirm `SCB->VTOR == 0x08006000` after handoff in a debugger;
- place a breakpoint in the application `SysTick_Handler`;
- reset the board at least 100 times and confirm repeatable handoff;
- erase/corrupt the application vector table and confirm the bootloader stays in
  its coded error pattern rather than branching;
- set a reset vector without the Thumb bit and confirm five-pulse rejection;
- set a reset vector outside the application partition and confirm six-pulse
  rejection.
