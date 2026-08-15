# uart_ota — Phase 9

ESP-IDF implementation of the frozen STM32 UART OTA protocol v1:

- UART 115200 8-N-1;
- COBS + `0x00`;
- IEEE CRC32;
- 256-byte DATA;
- ACK/NACK with 1500 ms timeout and five retries;
- HELLO/QUERY/START/DATA/FINISH/ABORT/RESUME/INSTALL;
- persistent STM32 resume from 4 KiB checkpoints;
- waits through bootloader backup/install/trial and detects final confirmed
  target or rollback.

The wire codec and transfer-plan selection are portable C and are host-unit
tested without ESP-IDF.
