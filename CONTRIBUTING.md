# Contributing

Thanks for taking the time to improve Secure Delta OTA Bootloader.

This repository is a security-sensitive embedded firmware project. Changes should stay focused, preserve the documented trust boundaries, and remain reproducible through the project validation gates.

## Development environment

Required for the integrated STM32 validation path:

- Python 3
- GNU Arm Embedded toolchain: `arm-none-eabi-gcc`, `arm-none-eabi-objcopy`, and `arm-none-eabi-size`
- or the supported fallback toolchain: `clang`, `ld.lld`, `llvm-objcopy`, and `size`

The ESP32 gateway additionally requires an active ESP-IDF environment with `idf.py` available.

Check the selected STM32 toolchain with:

```bash
make toolchain-info
```

See [`docs/toolchain-selection.md`](docs/toolchain-selection.md) for supported toolchain behavior.

## Change discipline

Keep pull requests narrow and reviewable. Avoid mixing unrelated feature, refactor, documentation, and formatting work in the same change.

Security-sensitive changes should explicitly describe the affected trust boundary, failure mode, and validation evidence. Never commit private signing keys, production credentials, Wi-Fi credentials, broker credentials, or generated secrets.

## Validation

For changes that can affect STM32 firmware, release tooling, validation scripts, or security behavior, run:

```bash
git diff --check
make warning-check TOOLCHAIN=gcc
make check TOOLCHAIN=gcc
make benchmark TOOLCHAIN=gcc
```

The Clang/LLD path is also supported when the complete fallback toolchain is installed:

```bash
make warning-check TOOLCHAIN=clang
make check TOOLCHAIN=clang
make benchmark TOOLCHAIN=clang
```

For ESP32 gateway changes, also run:

```bash
make gateway-build
```

For hardware-facing changes that affect OTA, rollback, recovery, transport, flash layout, or boot decisions, run the relevant HIL scenarios and update the evidence when appropriate:

```bash
make hil-test
```

## Repository manifests

`FILES.txt` lists the repository files covered by the project integrity manifest. `SHA256SUMS.txt` contains SHA-256 hashes for every listed file except the checksum file itself.

After adding, removing, renaming, or modifying tracked files, regenerate both files from the repository root:

```bash
LC_ALL=C git ls-files | sort > FILES.txt
while IFS= read -r file; do
    [ "$file" = "SHA256SUMS.txt" ] && continue
    sha256sum "$file"
done < FILES.txt > SHA256SUMS.txt
```

Then verify:

```bash
sha256sum -c SHA256SUMS.txt
```

## Pull requests

Before opening a pull request:

- keep generated build outputs and secrets out of the repository;
- run the applicable validation gates;
- update documentation when behavior or public commands change;
- update `FILES.txt` and `SHA256SUMS.txt` when tracked content changes;
- explain any validation that could not be run locally.

Use the pull request template as the final checklist.
