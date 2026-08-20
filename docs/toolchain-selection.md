# Toolchain Selection

[↑ Documentation Index](README.md) · [← Root](../README.md) · [← Release Process](release-process.md) · [Make Commands →](make-command-reference.md)

## Table of contents

- [Automatic selection](#automatic-selection)
- [Explicit selection](#explicit-selection)
- [Flash scripts](#flash-scripts)
The STM32 build supports two toolchains:

- `gcc`: GNU Arm Embedded (`arm-none-eabi-gcc`)
- `clang`: Clang/LLD targeting `arm-none-eabi`

## Automatic selection

When `TOOLCHAIN` is unset **or empty**, the common Makefile selects:

1. GNU Arm GCC when `arm-none-eabi-gcc` is available.
2. Otherwise Clang/LLD when the required LLVM tools are available.

This includes shells or scripts that accidentally export an empty variable:

```bash
TOOLCHAIN="" make flash-combined
```

An empty value no longer disables auto-detection.

## Explicit selection

```bash
make TOOLCHAIN=gcc firmware
make TOOLCHAIN=gcc flash-combined
```

or:

```bash
make TOOLCHAIN=clang firmware
```

Any other non-empty value is rejected.

## Flash scripts

The bootloader, application, and combined-image flash scripts now:

- pass `TOOLCHAIN` only when it is non-empty;
- unset an inherited empty value before invoking Make;
- reject unsupported explicit values early.

## References

- [`stm32f103.mk`](../mk/stm32f103.mk)
- [`Makefile`](../node-stm32f103/bootloader/Makefile)
- [`Makefile`](../node-stm32f103/application/Makefile)

[↑ Documentation Index](README.md) · [← Root](../README.md) · [← Release Process](release-process.md) · [Make Commands →](make-command-reference.md)
