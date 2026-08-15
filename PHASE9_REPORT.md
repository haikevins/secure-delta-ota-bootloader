# Phase 9 Report — ESP32 UART Gateway

Phase 9 implements the ESP32 as the transport gateway for the already
hardware-verified STM32 OTA stack.

The gateway uses an ESP-IDF `stm32_cache` partition as an artifact source and
runs the frozen UART protocol v1 over UART2. The protocol layer is independent
of the cache implementation, so HTTPS can populate the same cache in Phase 10.

Implemented behavior:

```text
QUERY
  -> IDLE: START
  -> same RECEIVING: RESUME
  -> foreign transfer: ABORT + START
  -> ARTIFACT_READY: INSTALL
  -> install/trial in progress: WAIT

START
  -> DATA 256 B + ACK/retry
  -> FINISH + idempotent retry
  -> INSTALL
  -> wait through reset/bootloader
  -> target TRIAL_BOOT / confirmation
  -> target IDLE + requested version
```

ESP32 cache publication is data-first/header-last and validates both header and
artifact CRC32 before use.

Hardware test uses STM32 v1 as baseline and embeds a healthy STM32 v2 candidate
inside the ESP32 firmware. Final verification is done independently with
ST-Link by checking metadata and comparing installed internal Flash bytes to the
candidate.

Security boundary is unchanged from Phase 8: CRC32 provides integrity only.
HTTPS transport begins in Phase 10 and signed authenticity remains Phase 14.

## ESP-IDF public dependency fix

`uart_ota.h` includes `driver/uart.h`, so `esp_driver_uart` is a public
component dependency and is declared in `REQUIRES`. Likewise,
`artifact_cache.h` includes `uart_ota.h`, so `uart_ota` is public from the
artifact-cache component. This is required by ESP-IDF's component dependency
propagation rules and is checked by `scripts/phase9_check.py`.

## Phase-9 OpenOCD isolation fix

The ESP-IDF environment can put Espressif's `openocd-esp32` ahead of the
system OpenOCD. The Phase-9 hardware runner now uses a dedicated
`STM32_OPENOCD`/`STM32_OPENOCD_SCRIPTS` pair for ST-Link and strips
ESP-IDF-specific OpenOCD environment variables before launching it.

## Relocatable build-tree fix

Release ZIPs no longer contain STM32 `build*` directories with GCC/Clang `.d`
dependency files. Those files contain absolute source paths and are not
relocatable between the packaging environment and a user's checkout.

`phase9-hw-test` also performs a clean of the normal STM32 bootloader and
application build directories before rebuilding, so a stale local `.d` file
cannot block the hardware test before compilation starts.
