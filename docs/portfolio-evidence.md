# Portfolio Claim / Evidence Map

| Claim | Evidence | Reproduce |
|---|---|---|
| Bootloader fits 24 KiB | Phase 17 linker/build benchmark | `make phase17-benchmark` |
| Application fits 38 KiB | Phase 17 linker/build benchmark | `make phase17-benchmark` |
| Delta reconstructs target byte-for-byte | Phase 12 + Phase 17 round-trip | `make phase12-check`; `make phase17-benchmark` |
| Signed delta is smaller than signed full | Phase 17 signed release benchmark | `make phase17-benchmark` |
| STM32 rejects unauthentic SDOT | Phase 14 security checks + Phase 16 `tampered-signature` HIL | `make phase14-check`; `make phase16-hw-test` |
| Reset during patch/backup/install recovers | Phase 16 fault witnesses | `make phase16-hw-test` |
| MQTT disconnect does not duplicate install | Phase 16 MQTT isolation scenario | `make phase16-hw-test` |
| Truncated HTTPS never installs | Phase 16 `https-truncate` | `make phase16-hw-test` |
| Unhealthy candidate rolls back | Phase 8 + Phase 16 rollback scenarios | `make phase16-hw-test` |
| Reset during rollback resumes safely | Phase 16 `rollback-reset` | `make phase16-hw-test` |
| Physical end-to-end fault matrix passes 9/9 | Phase 16 final hardware marker | `PHASE16_REPORT.md` |
| Package contains no HIL private key | Phase 16/17 packaging boundary | `make phase17-check` |
| Release assets are immutable and signed | Phase 15 release pipeline | `make phase15-check` |

## Phase 16 hardware closure

Observed final markers:

```text
P16_STM32_METADATA label=rollback-reset generation=74 state=0
active_version=1 pending_version=0 boot_attempts=0 last_error=0x0008B003
P16_STM32_VERIFY=PASS label=rollback-reset app=v1
P16_SCENARIO=PASS id=rollback-reset generation=74 gateway=EXPECTED_FAIL
Phase 16 fault injection/HIL hardware test: PASS (9 deterministic scenarios)
```

## Phase 17 closure

Phase 17 adds no new bootloader runtime feature. Its PASS means the final
portfolio claims are measurable, reproducible, documented and CI-gated while
preserving the Phase 16 hardware-verified implementation.
