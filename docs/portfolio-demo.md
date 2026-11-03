# 5-Minute Portfolio Demo

[↑ Documentation Index](README.md) · [← Root](../README.md) · [← One-Page Summary](portfolio-one-page.md) · [Evidence Map →](portfolio-evidence.md)

## Table of contents

- [Architecture](#architecture)
- [Memory and protocol](#memory-and-protocol)
- [Secure release](#secure-release)
- [Delta and recovery](#delta-and-recovery)
- [Hardware evidence](#hardware-evidence)
- [Benchmark and closure](#benchmark-and-closure)
## Architecture

**Suggested timing:** 0:00–0:45

Show `README.md` and explain the split trust model: ESP32 handles network transport; STM32 bootloader remains authoritative for signature verification, version policy, patching, install and rollback.

## Memory and protocol

**Suggested timing:** 0:45–1:30

Open:

```text
docs/memory-map.md
docs/uart-ota-protocol.md
```

Highlight the 24 KiB bootloader budget, 38 KiB application partition, 128 KiB staging partitions and the COBS UART packet with sequence/offset/retry/resume.

## Secure release

**Suggested timing:** 1:30–2:15

Show:

```text
tools/release.py
tools/secure_container.py
node-stm32f103/bootloader/include/trusted_key.h
```

Explain SHA-256 + ECDSA P-256, key ID, anti-downgrade and the intentionally unprovisioned checked-in trust anchor.

## Delta and recovery

**Suggested timing:** 2:15–3:10

Show the JojoDiff generator and STM32 streaming patch adapter. Then open `tests/fault/fault_matrix.json` and highlight deterministic resets during patch, backup, install and rollback.

## Hardware evidence

**Suggested timing:** 3:10–4:10

Open `docs/hil-results.md`. Show the **9/9 PASS** result, including tampered-signature rejection and exact v1 restoration after reset during rollback.

To reproduce on hardware:

```bash
make hil-test \
  ESP32_PORT=/dev/ttyUSB0 \
  WIFI_SSID="..." \
  WIFI_PASSWORD="..." \
  SDOTA_HOST_IP=<PC_LAN_IP>
```

## Benchmark and closure

**Suggested timing:** 4:10–5:00

Run:

```bash
make check TOOLCHAIN=gcc
make benchmark TOOLCHAIN=gcc
```

Show `dist/benchmark/benchmark.md` and the measured flash/delta figures. End with the key claim: a secure delta OTA system that was validated not only on the happy path but under deterministic power-loss, transport and signature-failure scenarios.

## References

- [`README.md`](../README.md)
- [`hil-results.md`](hil-results.md)
- [`benchmark-portfolio.md`](benchmark-portfolio.md)

[↑ Documentation Index](README.md) · [← Root](../README.md) · [← One-Page Summary](portfolio-one-page.md) · [Evidence Map →](portfolio-evidence.md)
