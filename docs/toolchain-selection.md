# Toolchain Selection

The STM32F103 firmware build supports two ARM toolchain paths. GNU Arm Embedded GCC is preferred; Clang/LLD is the fallback.

## Supported toolchains

### GNU Arm Embedded GCC

The GCC path requires all of the following commands:

```text
arm-none-eabi-gcc
arm-none-eabi-objcopy
arm-none-eabi-size
```

On Ubuntu, the usual packages are:

```bash
sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi
```

### Clang/LLD fallback

The fallback path requires the complete set:

```text
clang
ld.lld
llvm-objcopy
size
```

Having `clang` alone is not sufficient because the firmware build also links with LLD and emits binary/Intel HEX artifacts with `llvm-objcopy`.

## Automatic selection

When `TOOLCHAIN` is not set, the STM32 Make rules select the first complete toolchain in this order:

1. GNU Arm Embedded GCC;
2. Clang/LLD;
3. otherwise fail immediately with a missing-tool error.

This avoids selecting a partial host installation and failing later during link or artifact generation.

Inspect the selected toolchain with:

```bash
make toolchain-info
```

## Explicit selection

Force GCC:

```bash
make check TOOLCHAIN=gcc
```

Force Clang/LLD:

```bash
make check TOOLCHAIN=clang
```

Explicit selection also validates the required companion tools before the integrated build starts.

## CI baseline

Repository CI uses GNU Arm Embedded GCC for the primary firmware quality gate:

```bash
make warning-check TOOLCHAIN=gcc
make check TOOLCHAIN=gcc
make benchmark TOOLCHAIN=gcc
```

The Clang path remains useful as a supported secondary build path and for local portability checks.
