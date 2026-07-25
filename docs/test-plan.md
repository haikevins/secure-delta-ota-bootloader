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
