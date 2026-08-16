# Benchmark and Portfolio Guide

The benchmark is intentionally reproducible and separates portable claims from host-specific timing observations.

Run:

```bash
make benchmark TOOLCHAIN=gcc
```

It checks:

- bootloader flash <= 24 KiB;
- application flash <= 38 KiB;
- RAM <= 20 KiB;
- raw delta savings >= 20%;
- signed delta savings >= 20%;
- signed artifacts fit the 128 KiB Incoming partition;
- delta reconstruction is byte-for-byte correct;
- recorded deterministic HIL evidence remains 9/9.

Generated output:

```text
dist/benchmark/benchmark.json
dist/benchmark/benchmark.csv
dist/benchmark/benchmark.md
```

Build and signing wall-clock times are recorded for context but are not used as pass/fail thresholds because they depend on the host and toolchain.
