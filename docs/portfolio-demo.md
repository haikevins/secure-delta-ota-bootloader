# 5-Minute Portfolio Demo

## 0:00–0:45 — Architecture

Show `README.md` and explain the split trust model: ESP32 handles network transport; STM32 bootloader remains authoritative for signature verification, version policy, patching, install and rollback.

## 0:45–1:30 — Memory and protocol

Open:

```text
docs/memory-map.md
docs/uart-ota-protocol.md
```

Highlight the 24 KiB bootloader budget, 38 KiB application partition, 128 KiB staging partitions and the COBS UART packet with sequence/offset/retry/resume.

## 1:30–2:15 — Secure release

Show:

```text
tools/release.py
tools/secure_container.py
node-stm32f103/bootloader/include/trusted_key.h
```

Explain SHA-256 + ECDSA P-256, key ID, anti-downgrade and the intentionally unprovisioned checked-in trust anchor.

## 2:15–3:10 — Delta and recovery

Show the JojoDiff generator and STM32 streaming patch adapter. Then open `tests/fault/fault_matrix.json` and highlight deterministic resets during patch, backup, install and rollback.

## 3:10–4:10 — Hardware evidence

Open `docs/hil-results.md`. Show the **9/9 PASS** result, including tampered-signature rejection and exact v1 restoration after reset during rollback.

To reproduce on hardware:

```bash
make hil-test \
  ESP32_PORT=/dev/ttyUSB0 \
  WIFI_SSID="..." \
  WIFI_PASSWORD="..." \
  SDOTA_HOST_IP=<PC_LAN_IP>
```

## 4:10–5:00 — Benchmark and closure

Run:

```bash
make check TOOLCHAIN=gcc
make benchmark TOOLCHAIN=gcc
```

Show `dist/benchmark/benchmark.md` and the measured flash/delta figures. End with the key claim: a secure delta OTA system that was validated not only on the happy path but under deterministic power-loss, transport and signature-failure scenarios.
