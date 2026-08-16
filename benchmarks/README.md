# Benchmarks

Run the live benchmark with:

```bash
make benchmark TOOLCHAIN=gcc
```

or:

```bash
make benchmark TOOLCHAIN=clang
```

Generated files:

- `dist/benchmark/benchmark.json` — machine-readable metrics;
- `dist/benchmark/benchmark.csv` — spreadsheet-friendly summary;
- `dist/benchmark/benchmark.md` — human-readable portfolio table.

The checked-in `reference.*` files are a reproducible reference measurement. Compiler and host timing can vary, so the automated pass/fail policy is based on partition budgets, delta savings, artifact capacity and recorded HIL closure rather than exact wall-clock time.
