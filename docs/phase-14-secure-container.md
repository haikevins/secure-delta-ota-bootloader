# Phase 14 — Secure Container

Status: **implemented and host/build verified; physical HIL test provided**

Phase 14 turns the Phase-13 functional OTA pipeline into an authenticated
firmware-update path.

The trust boundary remains the STM32 bootloader. The application and ESP32 may
transport/cache an artifact, but they cannot make it trusted.

## Cryptographic profile

Phase 14 selects:

```text
Hash:       SHA-256
Signature:  ECDSA P-256
Wire sig:   r[32] || s[32], big-endian
Public key: X[32] || Y[32], big-endian
```

The frozen Phase-0 algorithm ID is used:

```text
FW_HASH_SHA256          = 1
FW_SIGNATURE_ECDSA_P256 = 1
```

The private signing key is host/release-side only. STM32 stores only one
compiled trusted public key plus its `key_id`.

The implementation is intentionally not an encryption layer. Firmware
confidentiality is out of scope; authenticity and integrity are the Phase-14
goals.

## Canonical SDOT v1 layout

Phase 14 completes the optional-header-extension mechanism reserved in Phase 0.

```text
offset
0       fixed SDOT header                       120 bytes
120     SCX1 extension v1                        20 bytes
140     payload                                  N bytes
140+N   raw ECDSA-P256 signature                 64 bytes
```

Total:

```text
container_size = 140 + payload_size + 64
```

The signature covers exactly:

```text
fixed header
+ SCX1 extension
+ payload
```

The 64-byte signature itself is not included in the signed message.

All integer fields are canonical little-endian. ECDSA scalar/coordinate bytes
are big-endian.

## Fixed header

The Phase-0 fixed header remains 120 bytes and contains:

```text
magic = "SDOT"
format_version = 1
header_size = 140
product_id
hardware_revision
image_type
flags
base_version
target_version
payload_size
target_image_size
target_load_address
base_image_sha256[32]
target_image_sha256[32]
payload_crc32
hash_algorithm
signature_algorithm
signature_size = 64
reserved
```

## SCX1 extension

The signed 20-byte extension is:

```c
typedef struct
{
    uint32_t extension_magic;      /* "SCX1" */
    uint16_t extension_version;    /* 1 */
    uint16_t extension_size;       /* 20 */
    uint32_t key_id;
    uint32_t base_image_size;
    uint32_t target_image_crc32;
} FirmwareContainerExtensionV1_t;
```

`target_image_crc32` is retained because the existing page-checkpointed
installer already uses CRC32 for fast reconstructed/install verification. It is
inside the signed header, so an attacker cannot replace it without invalidating
the signature.

SHA-256 remains the cryptographic image identity.

## Full image rules

For `FW_IMAGE_FULL`:

```text
base_version = 0
base_image_size = 0
base_image_sha256 = all zero
payload_size = target_image_size
payload = exact application.bin
```

The bootloader authenticates header + payload and checks the payload SHA-256
against `target_image_sha256` before the active application is touched.

## Delta rules

For `FW_IMAGE_DELTA`:

```text
base_version = exact installed source version
base_image_size = exact source application length
base_image_sha256 = exact source application SHA-256
payload = JojoDiff-compatible patch
target_image_size = exact reconstructed image length
target_image_sha256 = exact reconstructed image SHA-256
```

Before patching, the bootloader checks:

1. signed container structure;
2. key ID;
3. payload CRC32;
4. ECDSA signature;
5. monotonic target version;
6. exact base version;
7. exact base SHA-256.

Only then does it enter `PATCHING`.

## Bootloader verification flow

```text
Incoming Artifact
       |
       v
parse SDOT + SCX1
       |
       v
key_id == compiled trust anchor
       |
       v
stream header+payload SHA-256
stream payload SHA-256 / CRC32
       |
       v
ECDSA-P256 verify
       |
       +---- full ----> validate target hash
       |
       +---- delta ---> validate installed base SHA-256
                         |
                         v
                 streaming JojoDiff patch
                         |
                         v
                 Reconstructed Image
                         |
                         v
                target SHA-256 + signed CRC
                         |
                         v
                    IMAGE_READY
                         |
                         v
              existing backup/install/trial
```

The active internal application is not erased until the reconstructed candidate
has been authenticated/verified and the existing backup phase succeeds.

## Recovery behavior

Resets in:

```text
VERIFYING_CONTAINER
VERIFYING_BASE
PATCHING
```

restart authentication/validation.

A reset in `PATCHING` re-erases the external Reconstructed Image and replays
the deterministic patch/copy. The active internal application is still intact.

After `IMAGE_READY`, the existing Phase-8 backup/install/trial/rollback state
machine owns recovery.

The installer re-authenticates the SDOT envelope when recovering from later
install states before trusting target size/CRC metadata stored in external
Flash.

## Unsigned legacy policy

Default:

```c
#define PHASE14_ALLOW_UNSIGNED_LEGACY 0U
```

Therefore raw full images and the Phase-13 D13P envelope are rejected by the
Phase-14 OTA application/bootloader path.

The compatibility macro exists only for controlled regression work. It should
not be enabled in a secure release.

## Trust-anchor provisioning

The repository intentionally contains an unprovisioned placeholder:

```text
node-stm32f103/bootloader/include/phase14_trusted_key.h
```

Provision a public key:

```bash
python3 tools/phase14_keytool.py \
  /outside/repo/signing-private.pem \
  --key-id 0x14000001 \
  --output node-stm32f103/bootloader/include/phase14_trusted_key.h
```

The generated header contains only:

```text
key_id
64-byte public X||Y point
```

It never contains the private key.

`phase14-check` and `phase14-hw-test` use temporary keys by default and restore
the placeholder source header afterward.

For a persistent user-managed HIL signing key:

```bash
export PHASE14_PRIVATE_KEY=/secure/path/signing-private.pem
export PHASE14_KEY_ID=0x14000001
make phase14-hw-test PORT=/dev/ttyUSB0
```

The supplied private-key file is read from its external location and is not
copied into the repository.

## Host container creation

Signed delta:

```bash
python3 tools/phase14_secure_container.py build \
  --type delta \
  --payload update.jdiff \
  --base application-v1.bin \
  --target application-v2.bin \
  --base-version 1 \
  --target-version 2 \
  --key /secure/path/signing-private.pem \
  --key-id 0x14000001 \
  --output application-v1-to-v2.sdot
```

Signed full:

```bash
python3 tools/phase14_secure_container.py build \
  --type full \
  --payload application-v3.bin \
  --target application-v3.bin \
  --target-version 3 \
  --key /secure/path/signing-private.pem \
  --key-id 0x14000001 \
  --output application-v3.sdot
```

Inspect without the private key:

```bash
python3 tools/phase14_secure_container.py inspect application-v3.sdot
```

## Direct PC-UART secure OTA

```bash
python3 tools/uart_ota_sender.py \
  --port /dev/ttyUSB0 \
  secure-ota application-v1-to-v2.sdot
```

The STM32 advertises:

```text
FULL       0x01
DELTA      0x02
RESUME     0x04
SIGNATURE  0x08
ROLLBACK   0x10
----------------
combined   0x1F
```

## Validation

Host/build:

```bash
make phase14-check
```

Physical board:

```bash
export STM32_OPENOCD=/usr/bin/openocd
export STM32_OPENOCD_SCRIPTS=/usr/share/openocd/scripts

make phase14-hw-test PORT=/dev/ttyUSB0
```

The HIL runner checks:

```text
signed delta v1 -> v2 succeeds
unsigned v3 START is rejected
tampered signed v3 is downloaded but rejected by bootloader signature check
v2 bytes remain unchanged after rejection
valid signed full v3 succeeds
signed downgrade v3 -> v2 is rejected
final metadata is IDLE, active_version=3, last_error=0
```

## Security limitations / Phase 15 boundary

Phase 14 establishes functional firmware authenticity, but it is not a
production key-management system.

Current boundaries:

- one compiled trusted public key at a time;
- `key_id` enables explicit identity but multi-key rotation is not implemented;
- no key revocation list;
- no HSM/CI secret integration;
- no release approval policy;
- no encrypted firmware;
- the compact P-256 verifier is project code and has host interoperability
  tests, but is not claimed to be independently audited or certified.

Phase 15 owns release publication, manifest generation, signing-key custody and
server-side lifecycle.
