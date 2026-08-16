# Benchmarks

Phase 17 keeps one checked-in reference snapshot plus a reproducible generator.

Run:

```bash
make phase17-benchmark
```

Outputs are written to `dist/phase17/`:

- `phase17_benchmark.json` — machine-readable metrics;
- `phase17_benchmark.csv` — spreadsheet-friendly summary;
- `phase17_benchmark.md` — portfolio-friendly table.

The checked-in `phase17_reference.*` files are a reference measurement from the
final Phase-17 source tree. Absolute build times are host-specific and are not
used as pass/fail thresholds. Portable claims are firmware footprint, raw and
signed delta savings, partition utilization, and the Phase-16 9/9 HIL result.

The benchmark creates an ephemeral P-256 private key only inside a temporary
directory. It is deleted after signed-artifact size measurement.
