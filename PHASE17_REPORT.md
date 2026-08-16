# Phase 17 Report — Benchmark and Portfolio

## Status

```text
COMPLETE + BENCHMARK/PORTFOLIO VERIFIED
PHASE-16 HARDWARE EVIDENCE: 9/9 PASS
```

## Closure

Phase 17 is the final project phase. It does not change the OTA state machine;
it converts the hardware-verified system into a reproducible engineering
portfolio.

Added:

- reproducible STM32 flash/RAM footprint measurement;
- reproducible raw JojoDiff and signed SDOT delta/full comparison;
- generated JSON/CSV/Markdown benchmark outputs;
- checked-in reference benchmark;
- one-page architecture/project summary;
- five-minute demo script;
- claim-to-evidence/reproduction map;
- final README/CI roadmap closure;
- Phase-17 security/package regression gate.

## Benchmark policy

Portable PASS/FAIL claims:

- bootloader flash <= 24 KiB;
- application flash <= 38 KiB;
- SRAM <= 20 KiB;
- raw and signed delta savings >= 20%;
- SDOT artifacts fit 128 KiB Incoming;
- Phase-16 hardware HIL = 9/9.

Host wall-clock timing is recorded for context but never used as a threshold.

## Hardware evidence

Phase 16 was closed on physical STM32F103 + ESP32 + W25Q hardware before this
portfolio phase. Final rollback-reset evidence:

```text
generation=74
state=IDLE
active_version=1
pending_version=0
boot_attempts=0
last_error=0x0008B003
application v1 byte-for-byte verification: PASS
```

Final HIL marker:

```text
Phase 16 fault injection/HIL hardware test: PASS (9 deterministic scenarios)
```

## Commands

```bash
make phase17-check
make phase17-benchmark
```

The generated benchmark lives under `dist/phase17/`. The stable checked-in
snapshot lives under `benchmarks/`.

## Reference measurement

Reference toolchain: `clang` targeting `arm-none-eabi`.

| Metric | Result |
|---|---:|
| Bootloader flash | 11400 B / 24576 B (46.39%) |
| Bootloader RAM | 2072 B / 20480 B (10.12%) |
| Application v2 flash | 11276 B / 38912 B (28.98%) |
| Application v2 RAM | 1968 B / 20480 B (9.61%) |
| Raw target | 11276 B |
| Raw JojoDiff patch | 970 B |
| Raw delta savings | 91.40% |
| Signed full SDOT | 11480 B |
| Signed delta SDOT | 1174 B |
| Signed delta savings | 89.77% |
| Incoming usage (signed delta) | 0.90% |
| Phase-16 deterministic HIL | 9/9 PASS |

Reference host wall-clock timings are stored in the JSON/Markdown snapshot but
are not treated as portable performance claims.
