# artifact_cache — Phase 9

Phase 9 implements a power-cut-safe cache in ESP32 internal SPI flash using the
custom `stm32_cache` data partition.

Write order:

1. erase/write candidate bytes at partition offset `0x1000`;
2. read back and CRC32 the stored bytes;
3. erase header sector;
4. write CRC-protected cache header last.

The UART gateway consumes the artifact only through an offset-based read
callback. Phase 10 can therefore replace the Phase-9 embedded seed with HTTPS
download without changing the UART protocol component.
