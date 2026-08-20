# Checked-In Benchmark Reference

> **Source of truth:** [`reference.json`](reference.json). This Markdown view summarizes the same checked-in measurement; generate a fresh local result with `make benchmark` when reporting the current checkout.

[← Benchmark Index](README.md) · [Root README](../README.md)

## STM32 footprint

| Image | Flash | Flash budget | RAM | SRAM budget |
|---|---:|---:|---:|---:|
| Bootloader | `11,400 B` (`46.39%`) | `24,576 B` | `2,072 B` (`10.12%`) | `20,480 B` |
| Application v1 | `11,264 B` (`28.95%`) | `38,912 B` | `1,968 B` (`9.61%`) | `20,480 B` |
| Application v2 | `11,276 B` (`28.98%`) | `38,912 B` | `1,968 B` (`9.61%`) | `20,480 B` |

## Delta artifact efficiency

| Metric | Value |
|---|---:|
| Base image | `11,264 B` |
| Target image | `11,276 B` |
| Raw JojoDiff delta | `970 B` |
| Raw delta savings | `91.40%` |
| Signed delta SDOT | `1,174 B` |
| Signed full SDOT | `11,480 B` |
| Signed delta savings | `89.77%` |
| Incoming partition budget | `131,072 B` |

The release policy threshold is `20%`; this reference clears it comfortably.

## Hardware evidence

- deterministic HIL: **9/9 PASS**;
- final application version after rollback scenario set: `v1`;
- rollback diagnostic: `0x0008B003`;
- persisted private signing key: `false`.

## Environment

- toolchain: `clang`;
- Python: `3.13.5`;
- platform: `Linux-6.18.35-x86_64-with-glibc2.41`.

Wall-clock timing in `reference.json` is informational and environment-specific.

## References

- [Benchmark Guide](../docs/benchmark-portfolio.md)
- [HIL Results](../docs/hil-results.md)
- [Results Report](../docs/results-report.md)
