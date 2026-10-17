## Summary

Describe the problem and the smallest change that solves it.

## Scope

- [ ] Firmware / bootloader
- [ ] ESP32 gateway
- [ ] Host tooling / release tooling
- [ ] Tests / validation
- [ ] Documentation / repository hygiene

## Validation

List the commands and HIL scenarios that were run, or explain why a gate was not applicable.

```text
make warning-check TOOLCHAIN=...
make check TOOLCHAIN=...
make benchmark TOOLCHAIN=...
```

## Security and release checklist

- [ ] No private keys, credentials, generated secrets, or build artifacts are included.
- [ ] Trust-boundary or security behavior changes are documented.
- [ ] Public commands and documentation match the implemented behavior.
- [ ] `FILES.txt` and `SHA256SUMS.txt` are updated when tracked content changes.
- [ ] Relevant CI and validation gates pass.
