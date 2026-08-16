# Portfolio Claim / Evidence Map

| Claim | Evidence | Reproduce |
|---|---|---|
| STM32 bootloader stays within 24 KiB | `benchmarks/reference.json` and live benchmark | `make benchmark TOOLCHAIN=gcc` |
| Application stays within 38 KiB | benchmark footprint checks | `make benchmark TOOLCHAIN=gcc` |
| Delta reconstruction is byte-for-byte correct | `tools/jojodiff_patch.py`, benchmark round-trip | `make benchmark TOOLCHAIN=gcc` |
| Signed delta is materially smaller than signed full image | benchmark artifact table | `make benchmark TOOLCHAIN=gcc` |
| STM32 enforces ECDSA P-256 security boundary | `secure_container.c`, tampered-signature HIL scenario | `make hil-test ...` |
| Downgrade is rejected | secure container/version policy source and host checks | `make check TOOLCHAIN=gcc` |
| Reset during patch/backup/install is recoverable | deterministic HIL fault matrix | `docs/hil-results.md` |
| Unhealthy candidate rolls back to exact prior application | rollback scenarios | `docs/hil-results.md` |
| Reset during rollback is recoverable | `rollback-reset` scenario | `docs/hil-results.md` |
| MQTT disconnect does not duplicate accepted update | `mqtt-drop-after-accepted` scenario | `make hil-test ...` |
| Truncated HTTPS artifact is not installed | `https-truncate` scenario | `make hil-test ...` |
| Physical end-to-end fault matrix passes 9/9 | final HIL markers | `docs/hil-results.md` |
| Private signing key is not packaged | packaging checker | `make check TOOLCHAIN=gcc` |
| Checked-in trust anchor is intentionally unprovisioned | `trusted_key.h` | inspect source / `make check` |

Final rollback-reset evidence:

```text
STM32_METADATA label=rollback-reset generation=74 state=0
active_version=1 pending_version=0 boot_attempts=0 last_error=0x0008B003
STM32_VERIFY=PASS label=rollback-reset app=v1
SCENARIO=PASS id=rollback-reset generation=74 gateway=EXPECTED_FAIL
```
