# Phase 17 — Benchmark and Portfolio

## Goal

Phase 17 closes the engineering project by turning the verified OTA system into
a reproducible portfolio artifact. It adds measurements, claim-to-evidence
traceability, a short demo path, CI coverage, and a concise architecture story.

No new bootloader behavior is introduced in this phase. Phase 16 already
proved the recovery/security behavior on physical hardware with 9/9
deterministic HIL scenarios.

## Benchmark scope

`make phase17-benchmark` rebuilds the final source tree and measures:

- STM32 bootloader flash/RAM footprint against the 24 KiB / 20 KiB limits;
- application v1/v2 flash/RAM footprint against the 38 KiB / 20 KiB limits;
- raw JojoDiff patch size and byte-for-byte round-trip;
- signed SDOT delta vs signed SDOT full artifact size;
- signed-delta occupancy of the 128 KiB Incoming partition;
- host wall-clock time for build/delta/release operations;
- project-owned source file and nonblank-line counts;
- Phase-16 HIL evidence (9/9 scenarios).

Wall-clock time is informational only. The project does not claim MCU cycle
counts, cryptographic benchmark cycles, Internet throughput, or Wi-Fi RF
performance because those were not measured with dedicated instrumentation.

## Reproducibility

```bash
make phase17-check
make phase17-benchmark
```

`phase17-check` depends on the full Phase-16 host/static gate, validates the
checked-in benchmark reference and portfolio documentation, then performs a
fresh live build/artifact benchmark.

## Portfolio claim discipline

The portfolio separates claims into three classes:

1. **Build facts** — linker/map footprint and artifact bytes measured directly.
2. **Functional facts** — prior host/unit/security checks.
3. **Hardware facts** — only results actually observed in physical HIL.

This prevents benchmark prose from turning inferred behavior into measured
behavior.

## Hardware closure inherited from Phase 16

Final physical HIL result:

```text
Phase 16 fault injection/HIL hardware test: PASS (9 deterministic scenarios)
Final board state: rollback-reset scenario restores confirmed application v1;
no HIL signing private key persisted.
```

The final rollback-reset metadata retained diagnostic `0x0008B003` while
returning to `IDLE`, active v1, no pending version, and zero boot attempts.

## Deliverables

- `benchmarks/phase17_reference.json`
- `benchmarks/phase17_reference.csv`
- `benchmarks/phase17_reference.md`
- `docs/portfolio-one-page.md`
- `docs/portfolio-demo.md`
- `docs/portfolio-evidence.md`
- `PHASE17_REPORT.md`
- `PHASE17_VALIDATION.txt`
