# Phase 12 — Delta Patch Generation

Status: **implemented and host-verified**

Phase 12 is intentionally a host/CI phase. It creates a compact binary patch
from one exact STM32 application image to another exact STM32 application
image.

```text
application v1.bin
        |
        | JojoDiff-compatible generator
        v
application-v1-to-v2.jdiff
        |
        | host reconstruction verification
        v
reconstructed v2.bin == application v2.bin
```

STM32 does **not** apply the patch in Phase 12. The embedded streaming adapter
belongs to Phase 13.

## Compatibility target

The project targets the binary patch stream consumed by JANPatch/JojoDiff
patchers.

The Phase-12 generator emits a deterministic subset:

```text
EQL  copy bytes from source
MOD  replace source bytes with patch bytes
INS  insert patch bytes without advancing source
DEL  skip source bytes
```

The generator does not need `BKT` because the matching algorithm emits
monotonic source/target matches. The parser understands `BKT` so generated
streams remain inside the broader JojoDiff operation model.

Operation identifiers:

```text
ESC = 0xA7
MOD = 0xA6
INS = 0xA5
DEL = 0xA4
EQL = 0xA3
BKT = 0xA2
```

Literal `0xA7` inside MOD/INS data is escaped as two consecutive ESC bytes.

## Length encoding

EQL, DEL and BKT use the JojoDiff variable-length representation supported by
JANPatch.

Phase-12 tests cover boundary values:

```text
1
252
253
508
509
65535
65536
0x12345678
```

## Matching algorithm

`tools/jojodiff_patch.py` uses Python's deterministic `SequenceMatcher` with
`autojunk=False`.

For each monotonic matching block:

1. bytes present in both gaps become MOD;
2. extra target bytes become INS;
3. extra source bytes become DEL;
4. the matching region becomes EQL.

This is a host-side heuristic. Phase 12 does not claim that the patch is the
globally smallest possible patch. Selection is based on measured size, not on
an optimality claim.

## Delta selection policy

A generated patch is eligible only when it saves at least the configured
percentage compared with the full target image.

Default:

```text
minimum savings = 20%
```

This implements the architecture rule that delta is used only when it is
meaningfully smaller than the full artifact.

The release tool exits nonzero if the threshold is not met unless
`--allow-inefficient` is explicitly supplied for diagnostics.

## Exact base identity

The generated manifest records:

```text
base_version
base_size
base_sha256
target_version
target_size
target_sha256
patch_size
patch_sha256
patch_crc32
target_crc32
target_load_address
```

This data maps directly to the secure delta container fields planned for the
later container/signature phase.

Phase 13 must reject a delta unless the installed base version and base image
hash match these exact base values.

## Commands

Build deterministic v1 and v2 application binaries:

```bash
make phase12-base
make phase12-target
```

Generate the delta:

```bash
make phase12-delta
```

Run all Phase-12 host checks:

```bash
make phase12-check
```

Expected artifacts:

```text
dist/phase12/application-v1-to-v2.jdiff
dist/phase12/application-v1-to-v2.json
dist/phase12/application-v1-to-v2-reconstructed.bin
```

## Direct CLI

```bash
python3 tools/jojodiff_patch.py generate \
  old.bin new.bin update.jdiff

python3 tools/jojodiff_patch.py apply \
  old.bin update.jdiff reconstructed.bin
```

Release metadata:

```bash
python3 tools/phase12_delta.py \
  --base old.bin \
  --target new.bin \
  --base-version 1 \
  --target-version 2 \
  --output-dir dist/phase12 \
  --min-savings-percent 20
```

## Verification

Phase 12 performs four independent checks:

1. Python property tests across length boundaries, escaped data and
   deterministic mutation cases.
2. Python generation then reconstruction of the real firmware pair.
3. A separately implemented host C parser applies the generated patch and
   must reconstruct the target byte-for-byte.
4. Patch generation is repeated and both patch bytes and manifest must be
   identical.

An optional external JANPatch-compatible CLI can be supplied:

```bash
export JANPATCH_CLI=/path/to/janpatch-cli
make phase12-check
```

This external cross-check is optional because no third-party executable is
vendored into this repository.

## Security boundary

Phase 12 provides integrity metadata and deterministic patch generation only.

It does not provide firmware authenticity. CRC32/SHA-256 values in the Phase-12
manifest are not signatures. Signed containers remain a later phase.

## Phase 13 boundary

Phase 13 will add the STM32 streaming patch adapter:

```text
internal application (base)
+ external Incoming Artifact (patch)
-> streaming JANPatch-compatible reads/seeks
-> external Reconstructed Image
-> verify target
-> existing backup/install/trial flow
```

No STM32 delta patch application is enabled in Phase 12.
