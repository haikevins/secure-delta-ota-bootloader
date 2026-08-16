# Validation Status

## Integrated checks

The project has been validated with:

- host-side protocol/container tests;
- deterministic JojoDiff round-trip reconstruction;
- STM32 bootloader/application cross compilation;
- flash and SRAM partition-budget checks;
- signed release generation and verification;
- release-key fingerprint pinning;
- immutable-release overwrite rejection;
- private-key custody checks;
- HTTPS/MQTTS contract checks;
- packaging checks for empty credentials and an unprovisioned trust anchor;
- reproducible benchmark generation.

## Hardware-in-the-loop

Result: **PASS — 9/9 deterministic scenarios**.

Scenarios:

```text
control-secure-delta
patch-reset
backup-reset
install-midpage-reset
mqtt-drop-after-accepted
https-truncate
tampered-signature
rollback-control
rollback-reset
```

Final board state after the last scenario:

```text
generation=74
state=IDLE
active_version=1
pending_version=0
boot_attempts=0
last_error=0x0008B003
application=v1 verified
```

## Reproduce host/build closure

```bash
make check TOOLCHAIN=gcc
make benchmark TOOLCHAIN=gcc
```

## Reproduce hardware closure

```bash
make hil-test \
  ESP32_PORT=/dev/ttyUSB0 \
  WIFI_SSID="..." \
  WIFI_PASSWORD="..." \
  SDOTA_HOST_IP=<PC_LAN_IP>
```

Hardware execution requires the documented STM32/ESP32/W25Q wiring and an ST-Link.
