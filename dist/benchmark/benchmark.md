# Secure Delta OTA Benchmark

- Toolchain: `gcc`
- Host: `Linux-7.0.0-28-generic-x86_64-with-glibc2.39`
- Timing values are host wall-clock observations, not MCU cycle counts.

## STM32 footprint

| Image | Flash | Limit | Utilization | RAM | SRAM limit |
|---|---:|---:|---:|---:|---:|
| Bootloader | 9412 B | 24576 B | 38.30% | 2072 B | 20480 B |
| App v1 | 9640 B | 38912 B | 24.77% | 1968 B | 20480 B |
| App v2 | 9648 B | 38912 B | 24.79% | 1968 B | 20480 B |

## OTA artifact efficiency

| Metric | Result |
|---|---:|
| Raw target binary | 9648 B |
| Raw JojoDiff patch | 1242 B |
| Raw delta savings | 87.13% |
| Signed full SDOT | 9852 B |
| Signed delta SDOT | 1446 B |
| Signed delta savings | 85.32% |
| Incoming partition used by signed delta | 1.10% |

## Reliability evidence

- Deterministic hardware-in-the-loop fault matrix: **9/9 PASS**.
- Final rollback-reset state: confirmed application v1 restored.
- HIL private signing key persisted: **no**.

## Host execution time

| Operation | Wall time |
|---|---:|
| Bootloader build | 3.540 s |
| App v1 build | 2.852 s |
| App v2 build | 2.956 s |
| Delta generate + round-trip | 0.800 s |
| Signed release generation | 0.895 s |

These wall-clock timings are informational and intentionally have no PASS/FAIL threshold.
