# Phase 14 Report — Secure Container

## Result

Phase 14 implements authenticated full and delta firmware containers for the
STM32F103 bootloader.

The selected profile is SHA-256 + ECDSA P-256. The host stores the private key;
the STM32 bootloader contains only a public trust anchor.

## Signed layout

```text
SDOT fixed header      120 bytes
SCX1 extension          20 bytes
payload                  N bytes
ECDSA signature          64 bytes
```

Signed bytes:

```text
header[0:140] + payload
```

The ECDSA signature is stored as raw big-endian `r||s`.

## Boot path

Both full and delta artifacts are staged in Incoming Artifact. The bootloader
authenticates them before any active application erase.

Full payloads are copied to Reconstructed Image after authentication.

Delta payloads validate the exact current base image and then use the embedded
JojoDiff-compatible streaming patcher to create Reconstructed Image.

The reconstructed image must match the signed SHA-256 and signed CRC32 before
the existing backup/install/trial lifecycle starts.

## Recovery

The signed envelope is re-verified on recovery. PATCHING is replay-safe because
the active internal application is unchanged until IMAGE_READY and verified
backup.

The shared installer re-authenticates SDOT metadata before trusting a
reconstructed candidate during later install recovery.

## Compatibility

Unsigned raw full and Phase-13 D13P artifacts are disabled by default:

```text
PHASE14_ALLOW_UNSIGNED_LEGACY=0
```

This is intentional. Phase 14 must not silently downgrade back to an
unauthenticated update path.

## Key provisioning

The repository packages an unprovisioned public-key placeholder. Test/check
scripts generate temporary P-256 keys, provision only the public point into the
bootloader build, and restore the source header afterward.

No private key is intended to be stored in the repository or ZIP.

## Validation

`make phase14-check` validates:

- SHA-256 known vector;
- ECDSA valid/tampered vectors against OpenSSL-generated signatures;
- canonical SDOT/SCX1 parsing;
- exact bootloader secure-envelope verification;
- key-ID and signature tamper rejection;
- signed delta reconstruction using exact embedded JanpatchPort code;
- provisioned bootloader 24 KiB budget;
- private-key non-persistence.

`make phase14-hw-test PORT=...` then validates the physical STM32 path:
signed delta, unsigned rejection, bad-signature rejection, signed full update,
downgrade rejection and ST-Link byte comparison.

Physical Phase-14 HIL: PASS on STM32F103 hardware.

## Production boundary

The project crypto verifier is compact and interoperability-tested, but is not
claimed to be independently audited/certified. Phase 15 must address signing
key custody, release authorization and artifact publication before treating the
pipeline as a production release system.

## Measured host/build/security result

The final Phase-14 checker completed successfully with a temporary P-256 test
key and restored the repository trust anchor afterward.

```text
Provisioned bootloader = 19064 / 24576 bytes
Application v1/v2/v3  = 11264 / 11276 / 11276 bytes
Delta payload          = 970 bytes
Signed delta SDOT      = 1174 bytes
Delta savings          = 89.59% versus full v2 image
Signed full v3 SDOT    = 11480 bytes
```

Exact MCU-side SHA-256/ECDSA verification, canonical container parsing,
secure-envelope verification and exact embedded Janpatch reconstruction all
passed on the host. The temporary signing private key was kept outside the
repository tree and the trust-anchor source was restored to the unprovisioned
placeholder after validation.

Physical Phase-14 HIL: PASS. Signed delta, unsigned rejection, tampered-signature rejection, signed full update, byte-for-byte verification and downgrade rejection all passed.

## HIL empty `PHASE14_KEY_ID` fix

The initial physical Phase-14 command:

```bash
make phase14-hw-test PORT=/dev/ttyUSB0
```

failed before any STM32 operation because the Makefile exports
`PHASE14_KEY_ID="$(PHASE14_KEY_ID)"`. When the Make variable is unset this
creates an environment variable whose value is the empty string.

Python's `os.environ.get(name, default)` returns that empty string because the
variable exists, so the previous code attempted `int("", 0)`.

The runner now treats an unset **or empty/whitespace-only** `PHASE14_KEY_ID` as
the default test key ID. Explicit invalid values produce a readable error.


## HIL absolute merge-output path fix

The Phase-14 HIL runner creates its combined baseline in `/tmp`. The previous
`merge_images.py` tried to print that path using `Path.relative_to(project_root)`,
which raises `ValueError` for valid outputs outside the repository.

The merge tool now prints a repository-relative path when possible and falls
back to the resolved absolute path otherwise. The same behavior is used for
the sidecar manifest.


## Final physical HIL result

Phase 14 was physically verified before Phase 15 began.

```text
P14_BASELINE=PASS app=v1 caps=0x0000001F
P14_SECURE_DELTA=PASS target=v2
P14_UNSIGNED_REJECT=PASS status=SIGNATURE_ERROR
P14_SIGNATURE_REJECT=PASS tampered_signature_preserved_v2
P14_SECURE_FULL=PASS target=v3
P14_DOWNGRADE_REJECT=PASS status=VERSION_REJECTED
Phase 14 secure container hardware test: PASS
```

Final STM32 metadata was `IDLE`, active version `v3`, no pending update, no trial
attempts and `last_error=0`.
