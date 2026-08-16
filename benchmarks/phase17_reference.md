# Phase 17 Benchmark

- Toolchain: `clang`
- Host: `Linux-6.18.35-x86_64-with-glibc2.41`
- Timing values are host wall-clock observations, not MCU cycle counts.

## STM32 footprint

| Image | Flash | Limit | Utilization | RAM | SRAM limit |
|---|---:|---:|---:|---:|---:|
| Bootloader | 11400 B | 24576 B | 46.39% | 2072 B | 20480 B |
| App v1 | 11264 B | 38912 B | 28.95% | 1968 B | 20480 B |
| App v2 | 11276 B | 38912 B | 28.98% | 1968 B | 20480 B |

## OTA artifact efficiency

| Metric | Result |
|---|---:|
| Raw target binary | 11276 B |
| Raw JojoDiff patch | 970 B |
| Raw delta savings | 91.40% |
| Signed full SDOT | 11480 B |
| Signed delta SDOT | 1174 B |
| Signed delta savings | 89.77% |
| Incoming partition used by signed delta | 0.90% |

## Reliability evidence

- Phase 16 deterministic HIL: **9/9 PASS**.
- Final rollback-reset state: confirmed application v1 restored.
- HIL private signing key persisted: **no**.

## Host execution time

| Operation | Wall time |
|---|---:|
| Bootloader build | 1.827 s |
| App v1 build | 1.392 s |
| App v2 build | 1.274 s |
| Delta generate + round-trip | 0.708 s |
| Signed release generation | 7.015 s |

These wall-clock timings are informational and intentionally have no PASS/FAIL threshold.
