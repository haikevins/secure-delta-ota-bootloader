# Reference Benchmark

Checked-in reproducible reference captured with `clang`.

## STM32 footprint

| Image | Flash | Limit | Utilization | RAM |
|---|---:|---:|---:|---:|
| Bootloader | 11400 B | 24576 B | 46.39% | 2072 B |
| App v1 | 11264 B | 38912 B | 28.95% | 1968 B |
| App v2 | 11276 B | 38912 B | 28.98% | 1968 B |

## OTA artifact efficiency

| Metric | Result |
|---|---:|
| Raw target binary | 11276 B |
| Raw JojoDiff patch | 970 B |
| Raw delta savings | 91.40% |
| Signed full SDOT | 11480 B |
| Signed delta SDOT | 1174 B |
| Signed delta savings | 89.77% |

## Reliability evidence

- Deterministic HIL: **9/9 PASS**.
- Final rollback-reset application: v1.
- Rollback diagnostic: `0x0008B003`.
- Private signing key persisted: **no**.

Timing fields in `reference.json` are host observations and are not pass/fail thresholds.
