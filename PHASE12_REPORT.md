# Phase 12 Report — Delta Patch Generation

## Result

Phase 12 adds deterministic host-side JojoDiff-compatible patch generation and
verification. It does not modify the STM32 bootloader patch path yet.

## Real firmware pair

The Phase-12 reference pair is:

```text
base   = application v1
target = application v2
```

Both are built from the same STM32 application project with only the monotonic
application version changed.

The checker validates MSP/reset vectors and the 38 KiB application boundary
before generating a patch.

## Generator

`tools/jojodiff_patch.py` emits:

```text
EQL
MOD
INS
DEL
```

with JojoDiff ESC and variable-length encoding. Literal ESC bytes in payload
data are escaped.

A monotonic diff is sufficient for this firmware use case, so the generator
does not emit BKT. The parser implements BKT for compatibility testing.

## Release tool

`tools/phase12_delta.py` creates:

```text
.jdiff patch
.json delta metadata
reconstructed .bin
```

The reconstructed binary must be byte-identical to the target before any
artifact is accepted.

The metadata records exact base/target SHA-256 values so a later embedded
patcher can bind the patch to the correct installed firmware.

## Selection

Default delta eligibility requires at least 20% savings versus the full target
binary. This is a release policy, not a property of the patch format.

## Verification strategy

The package verifies:

- operation/length encoding boundaries;
- ESC escaping;
- deterministic random replace/insert/delete cases;
- real firmware v1 -> v2 reconstruction;
- independent host C reconstruction;
- repeat generation determinism;
- manifest/hash/CRC consistency.

An external JANPatch-compatible CLI may additionally be supplied using
`JANPATCH_CLI`.

## Architectural boundary

Phase 12 does not alter:

- Phase-11 MQTT orchestration;
- HTTPS download;
- UART transport;
- STM32 install/trial/rollback logic.

Phase 13 consumes the `.jdiff` format and provides the embedded streaming
adapter backed by internal Flash and external SPI NOR.

## Measured reference result

For the packaged Clang-built reference pair:

```text
base v1 size      = 11876 bytes
target v2 size    = 11888 bytes
delta patch size  = 1021 bytes
delta savings     = 91.41%
patch CRC32       = 0x84CB514A
patch SHA-256     = 34d63a199aae161eca14fd1bdce42ea2736d390d0bdcc13511ac18f37d9b331c
```

The packaged reconstructed image is byte-for-byte identical to the target.
