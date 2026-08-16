# Portfolio Demo — 5-minute path

This is a **5-minute** explanation/demo plan. The full HIL matrix is longer and
is shown as recorded evidence unless there is enough time to run it live.

## 0:00–1:00 — Architecture

Open `docs/portfolio-one-page.md` and explain:

- ESP32 handles Internet/TLS and caches the artifact.
- STM32 owns the trust decision and internal-flash state machine.
- W25Q holds Incoming, Reconstructed and Backup images.
- MQTT orchestrates; HTTPS carries bytes; UART bridges the two MCUs.

## 1:00–2:00 — Security + delta

Show the SDOT layout and the JojoDiff path. Emphasize that a delta is accepted
only after signed-container verification and exact-base verification.

Run:

```bash
make phase17-check
```

Point out the 24 KiB/38 KiB footprint budgets and signed-delta savings.

## 2:00–3:00 — Release pipeline

Show `tools/phase15_release.py` and `.github/workflows/firmware-release.yml`:

- immutable release ID;
- exact previous published binary;
- full fallback;
- external signing-key custody;
- key fingerprint authorization.

## 3:00–4:00 — Fault evidence

Open `tests/fault/phase16_fault_matrix.json`. Highlight:

- `install-midpage-reset`;
- `mqtt-drop-after-accepted`;
- `tampered-signature`;
- `rollback-reset`.

Explain that fault hooks are checkpoint-based, not random sleeps.

## 4:00–5:00 — Hardware proof

Show the final Phase-16 result:

```text
Phase 16 fault injection/HIL hardware test: PASS (9 deterministic scenarios)
```

If hardware/time is available, the full command is:

```bash
make phase16-hw-test   ESP32_PORT=/dev/ttyUSB0   WIFI_SSID="..."   WIFI_PASSWORD="..."   PHASE16_HOST_IP=<PC_LAN_IP>
```

Close with the result: a bad or interrupted update never becomes the confirmed
active firmware; rollback restores the exact known-good image.
