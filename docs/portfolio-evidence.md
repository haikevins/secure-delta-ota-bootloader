# Portfolio Claim / Evidence Map

[↑ Documentation Index](README.md) · [← Root](../README.md) · [← Demo Guide](portfolio-demo.md)

## Table of contents

- [Strongest hardware evidence](#strongest-hardware-evidence)
- [Benchmark evidence](#benchmark-evidence)
- [Recommended portfolio presentation](#recommended-portfolio-presentation)
This map separates implementation claims from hardware-verified claims and shows how to reproduce each one.

| Claim | Evidence | Reproduce |
|---|---|---|
| Bootloader stays within 24 KiB | `benchmarks/reference.json` + live ELF size check | `make benchmark TOOLCHAIN=gcc` |
| Application stays within 38 KiB | benchmark footprint checks | `make benchmark TOOLCHAIN=gcc` |
| SRAM remains within 20 KiB | benchmark ELF section analysis | `make benchmark TOOLCHAIN=gcc` |
| STM32 source can be built warning-clean | `-Werror` quality target | `make warning-check TOOLCHAIN=gcc` |
| Delta reconstruction is byte-for-byte correct | JojoDiff host regression + benchmark round-trip | `make benchmark TOOLCHAIN=gcc` |
| Signed delta materially reduces release size | signed delta/full benchmark table | `make benchmark TOOLCHAIN=gcc` |
| Signed artifacts fit 128 KiB Incoming partition | benchmark policy gate | `make benchmark TOOLCHAIN=gcc` |
| STM32 enforces ECDSA P-256 firmware authenticity | secure-container source + tampered-signature HIL | `make hil-test ...` |
| Downgrade is rejected | version policy + host negative checks | `make check TOOLCHAIN=gcc` |
| Delta is rejected on the wrong base | base version/hash checks | `make check TOOLCHAIN=gcc` |
| Reset during patch is recoverable | `patch-reset` physical scenario | `docs/hil-results.md` |
| Reset during backup is recoverable | `backup-reset` physical scenario | `docs/hil-results.md` |
| Reset during internal-flash install is recoverable | `install-midpage-reset` physical scenario | `docs/hil-results.md` |
| MQTT disconnect does not duplicate the accepted update | `mqtt-drop-after-accepted` physical scenario | `make hil-test ...` |
| Truncated HTTPS artifact is never installed | `https-truncate` physical scenario | `make hil-test ...` |
| Tampered signature preserves the old application | `tampered-signature` physical scenario | `docs/hil-results.md` |
| Unhealthy candidate rolls back to exact prior app | `rollback-control` physical scenario | `docs/hil-results.md` |
| Reset during rollback is recoverable | `rollback-reset` physical scenario | `docs/hil-results.md` |
| Physical end-to-end fault matrix passes | final HIL markers | `docs/hil-results.md` |
| Fault hooks actually executed | metadata-generation witnesses | `docs/hil-results.md` |
| Private signing key is not packaged | packaging checker | `make check TOOLCHAIN=gcc` |
| Checked-in trust anchor is intentionally unprovisioned | `trusted_key.h` | `make check TOOLCHAIN=gcc` |
| Release directories are immutable | release host tests | `make check TOOLCHAIN=gcc` |
| Release command supports full fallback | release tool + benchmark/release tests | `make release ...` |
| CI enforces integrated closure | `.github/workflows/build.yml` | inspect CI / run local commands |

## Strongest hardware evidence

Final rollback-reset state:

```text
generation=74
state=IDLE
active_version=1
pending_version=0
boot_attempts=0
last_error=0x0008B003
application=v1 verified
```

Fault witness relationships:

```text
patch-reset       control + 1 generation
backup-reset      control + 1 generation
install-reset     control + 1 generation
MQTT disconnect   same generation as control
rollback-reset    rollback-control + 1 generation
```

## Benchmark evidence

Checked-in Clang reference:

```text
bootloader flash 11400 B
app v2 flash     11276 B
signed savings   89.77%
```

User-verified GCC run:

```text
bootloader flash 9412 B
app v2 flash     9648 B
signed savings   85.32%
```

Exact compiler-specific sizes are observations. Partition compliance and savings thresholds are automated claims.

## Recommended portfolio presentation

For a short demonstration:

```bash
make help
make warning-check TOOLCHAIN=gcc
make check TOOLCHAIN=gcc
make benchmark TOOLCHAIN=gcc
cat dist/benchmark/benchmark.md
```

Then show:

- `docs/hil-results.md`;
- `docs/results-report.md`;
- `PROJECT_REPORT.md`;
- the hardware wiring/board.

## References

- [`README.md`](../README.md)
- [`VALIDATION.md`](../VALIDATION.md)
- [`hil-results.md`](hil-results.md)

[↑ Documentation Index](README.md) · [← Root](../README.md) · [← Demo Guide](portfolio-demo.md)
