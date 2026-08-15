# Phase 10 Report — HTTPS Download

Phase 10 replaces the embedded Phase-9 artifact source with an authenticated
HTTPS stream while leaving the hardware-verified STM32 UART/install/trial path
unchanged.

Implemented pipeline:

```text
Wi-Fi STA
  -> network time
  -> authenticated HTTPS GET
  -> transactional ESP32 stm32_cache
  -> Phase-9 UART OTA protocol
  -> STM32 W25Q Incoming
  -> backup/install
  -> trial/confirm
```

## Network/TLS

Production mode:

- Wi-Fi station waits for DHCP/IP.
- System time is synchronized through the ESP-NETIF SNTP wrapper.
- URL must start with `https://`.
- `esp_http_client` uses `esp_crt_bundle_attach`.
- Certificate/hostname verification is not bypassed.
- Only HTTP 200 with a positive `Content-Length` is accepted.
- Chunked transfer is intentionally deferred beyond Phase 10.

The complete firmware is not buffered in ESP32 RAM. The HTTPS component reads
bounded 1024-byte chunks and writes them sequentially to the cache.

## Transactional cache

The persistent Phase-9 cache header format remains unchanged.

```text
invalidate header
erase artifact range
stream bytes + CRC32
read stored artifact back
compare stored CRC32
publish CRC-protected header
re-open cache and verify again
```

A failed final re-open also erases the header, so a failed or interrupted
download is never exposed as a valid UART artifact.

## Hardware self-test

The Phase-10 hardware runner creates a short-lived private CA and HTTPS server
certificate on the developer PC. The certificate contains the PC LAN IPv4
address as an IP Subject Alternative Name. Only the public CA is embedded into
the temporary ESP32 test build.

The test then performs:

```text
PC HTTPS server
  -> ESP32 TLS download
  -> ESP32 cache commit
  -> UART transfer
  -> STM32 install
  -> trial confirmation
  -> ST-Link metadata + byte comparison
```

The private CA mode uses a test-only current epoch so the LAN test does not
depend on Internet SNTP availability. Production mode still uses SNTP and the
normal certificate bundle.

## Validation completed before packaging

```text
Phase 9 UART protocol host regression: PASS
Phase 9 resume/final-state model:      PASS

Phase 10 HTTPS policy C tests:         PASS
Phase 10 transactional-cache model:    PASS
ESP-IDF-specific C syntax/stub check:  PASS
STM32 bootloader build:                PASS
STM32 application v1 build:            PASS
STM32 candidate v2 build:              PASS
Phase 10 combined image:               PASS
```

Measured Clang/LLD packaging artifacts:

```text
Bootloader:        11240 bytes / 24 KiB
Application v1:    11876 bytes / 38 KiB
Candidate v2:      11888 bytes / 38 KiB
Candidate CRC32:   0x02904AA9
Combined Phase 10: 36452 bytes
```

A real ESP-IDF build could not be executed in the packaging environment because
`idf.py` is not installed there. `phase10_check.py` automatically performs the
real ESP-IDF build when run from an activated ESP-IDF environment.

Physical Phase-10 HTTPS/Wi-Fi hardware validation remains pending on the user's
ESP32 + STM32 setup.

## ESP-IDF 6.x SNTP initializer fix

`ESP_NETIF_SNTP_DEFAULT_CONFIG()` expands to a brace initializer. Phase 10 now
uses it directly in the declaration of `esp_sntp_config_t config` rather than
as a later assignment. `scripts/phase10_check.py` contains a regression check
for this exact build failure.

## ESP-IDF build-directory recovery fix

The project now pins `CONFIG_IDF_TARGET="esp32"` in `sdkconfig.defaults` and no
longer calls `idf.py set-target esp32` as part of every check/build. Espressif
documents that `set-target` performs a full clean and regenerates configuration,
so repeatedly invoking it is unnecessary for a fixed-target project.

`scripts/esp32_build_guard.py` preserves a valid incremental CMake/Ninja build
directory but removes a partial directory that lacks `CMakeCache.txt` or
`build.ninja`. This handles interrupted CMake generation without asking
`idf.py fullclean` to delete a directory it does not recognize.

## Dedicated HTTPS worker-task stack fix

Hardware testing showed `***ERROR*** A stack overflow in task main` immediately
after the HTTPS GET started. The Phase-10 pipeline now runs in a dedicated
FreeRTOS task named `phase10_gateway` with a configurable 16384-byte default
stack. `app_main()` only creates this worker and returns, so ESP-IDF can delete
the small system main task.

After a successful end-to-end run the worker prints
`P10_STACK=PASS high_water_mark=... bytes` so the remaining stack margin can be
measured on the real target.
