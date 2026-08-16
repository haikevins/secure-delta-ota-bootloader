# Benchmark and Portfolio Guide

The benchmark is reproducible and separates portable acceptance criteria from host/toolchain-specific observations.

## Run

GNU Arm GCC:

```bash
make benchmark TOOLCHAIN=gcc
```

Clang/LLVM:

```bash
make benchmark TOOLCHAIN=clang
```

## Generated reports

```text
dist/benchmark/benchmark.json
dist/benchmark/benchmark.csv
dist/benchmark/benchmark.md
```

`benchmark.json` is the machine-readable source for automation. The CSV is convenient for spreadsheets, and the Markdown report is intended for human review.

## Acceptance criteria

The benchmark fails if any of these conditions are violated:

| Check | Limit |
|---|---:|
| Bootloader flash | `<= 24 KiB` |
| Application flash | `<= 38 KiB` |
| SRAM | `<= 20 KiB` |
| Raw delta savings | `>= 20%` |
| Signed delta savings | `>= 20%` |
| Signed delta artifact | `<= 128 KiB` |
| Signed full artifact | `<= 128 KiB` |
| Delta reconstruction | exact byte-for-byte target |
| Recorded HIL evidence | `9/9` |

## What is measured

The benchmark builds:

- bootloader;
- application v1;
- application v2.

It then:

1. reads ELF section sizes;
2. generates a v1→v2 JojoDiff-compatible patch;
3. reconstructs the target and compares exact bytes;
4. creates a signed release with an ephemeral P-256 key;
5. compares signed delta against signed full size;
6. checks artifact fit against the Incoming partition;
7. writes timing observations.

## Checked-in reference

`benchmarks/reference.json` records a Clang reference:

```text
bootloader flash        11400 B
application v2 flash    11276 B
raw delta                 970 B
signed delta             1174 B
signed full             11480 B
raw savings              91.40%
signed savings           89.77%
HIL evidence             9/9 PASS
```

## User-verified GCC run

A later GNU Arm GCC run recorded:

```text
bootloader flash         9412 B
application v2 flash     9648 B
raw delta                1242 B
signed delta             1446 B
signed full              9852 B
raw savings              87.13%
signed savings           85.32%
HIL evidence             9/9 PASS
```

Exact byte counts are compiler/version specific. The stable portfolio claims are:

- both images stay within fixed partitions;
- SRAM remains within 20 KiB;
- delta savings exceed policy;
- signed artifacts fit staging storage;
- reconstruction is exact;
- physical HIL evidence is 9/9.

## Timing interpretation

Wall-clock build, patch and signing times are included for context only.

They deliberately have no PASS/FAIL threshold because results depend on:

- CPU;
- storage;
- OS;
- Python version;
- compiler version;
- OpenSSL version;
- filesystem cache state.

Do not compare timing values from two hosts as if they were MCU runtime measurements.

## Portfolio use

For a portfolio screenshot/demo, prefer:

```bash
make benchmark TOOLCHAIN=gcc
cat dist/benchmark/benchmark.md
```

Then pair the generated report with:

- `docs/results-report.md`;
- `docs/hil-results.md`;
- `docs/portfolio-evidence.md`.
