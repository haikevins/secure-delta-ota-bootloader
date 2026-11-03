# Secure Firmware Container Specification Version 1

[↑ Documentation Index](README.md) · [← Root](../README.md) · [← UART OTA Protocol](uart-ota-protocol.md) · [Boot State Machine →](boot-state-machine.md)

> **Status:** **Signed SDOT v1 implemented and enforced by the STM32 bootloader**

## Table of contents

- [1. Goals](#1-goals)
- [2. Binary layout](#2-binary-layout)
- [3. Header fields](#3-header-fields)
- [4. Fixed constants](#4-fixed-constants)
- [5. Version encoding](#5-version-encoding)
- [6. Full-image container rules](#6-full-image-container-rules)
- [7. Delta container rules](#7-delta-container-rules)
- [8. Signed bytes](#8-signed-bytes)
- [9. Validation order](#9-validation-order)
- [10. Product identity](#10-product-identity)
- [11. Unsupported v1 features](#11-unsupported-v1-features)
- [12. Implemented signed SDOT v1 + SCX1 profile](#12-implemented-signed-sdot-v1--scx1-profile)
- [13. Historical engineering formats](#13-historical-engineering-formats)
- [14. Trust-anchor provisioning](#14-trust-anchor-provisioning)

## 1. Goals

The same outer container format carries either:

- a complete target firmware image; or
- a delta patch that transforms one exact base firmware into one exact target firmware.

The bootloader validates product compatibility, version policy, hashes and signature before installation.

## 2. Binary layout

```text
+-------------------------------+
| Fixed header                  |
+-------------------------------+
| Optional header extensions    |
+-------------------------------+
| Payload: full image or delta  |
+-------------------------------+
| Digital signature            |
+-------------------------------+
```

## 3. Header fields

All integers are little-endian.

```c
typedef enum
{
    FW_IMAGE_FULL  = 1U,
    FW_IMAGE_DELTA = 2U
} FirmwareImageType_t;

typedef struct
{
    uint32_t magic;                 /* 'SDOT' = 0x544F4453 */
    uint16_t format_version;        /* 1 */
    uint16_t header_size;

    uint32_t product_id;
    uint32_t hardware_revision;

    uint32_t image_type;
    uint32_t flags;

    uint32_t base_version;
    uint32_t target_version;

    uint32_t payload_size;
    uint32_t target_image_size;
    uint32_t target_load_address;

    uint8_t  base_image_sha256[32];
    uint8_t  target_image_sha256[32];

    uint32_t payload_crc32;

    uint16_t hash_algorithm;
    uint16_t signature_algorithm;
    uint16_t signature_size;
    uint16_t reserved;
} FirmwareContainerHeader_t;
```

## 4. Fixed constants

```c
#define FW_CONTAINER_MAGIC            0x544F4453UL
#define FW_CONTAINER_FORMAT_VERSION   1U
#define FW_HASH_SHA256                1U
#define FW_SIGNATURE_NONE             0U
#define FW_SIGNATURE_ECDSA_P256       1U
#define FW_SIGNATURE_ED25519          2U
```

The **implemented signed SDOT profile uses ECDSA P-256** (`1`) and SHA-256
(`1`), with a 64-byte raw signature; `FW_SIGNATURE_ED25519` is a reserved enum
value, **not** evidence of an implemented Ed25519 verifier. The canonical v1
header comprises 120 fixed bytes plus a mandatory 20-byte `SCX1` extension.
See [`firmware_container.h`](../shared/include/firmware_container.h) and
`FirmwareContainer_Parse()` for actual accepted fields.

## 5. Version encoding

Firmware versions use a monotonic unsigned 32-bit build version for bootloader policy. Human-readable semantic versions are stored in the server manifest and may map to this integer.

Example:

```text
1.2.3 -> build version 10203 or CI-assigned monotonic release number
```

The only bootloader requirement is monotonic ordering and uniqueness for released firmware.

## 6. Full-image container rules

- `image_type = FW_IMAGE_FULL`.
- `base_version = 0`.
- `base_image_sha256` is all zero.
- Payload contains exactly `target_image_size` application bytes.
- `payload_size == target_image_size` unless a future compression flag is defined.

## 7. Delta container rules

- `image_type = FW_IMAGE_DELTA`.
- `base_version` identifies the exact allowed source release.
- `base_image_sha256` identifies the exact source application bytes.
- Payload contains JojoDiff-compatible patch bytes.
- Patch output must produce exactly `target_image_size` bytes.
- The target image hash must equal `target_image_sha256` before install.

## 8. Signed bytes

The signature covers:

```text
header bytes from magic through reserved
+ optional header extension bytes
+ payload bytes
```

The signature field itself is not included. Header fields must be serialized canonically; compiler memory layout is not used directly.

## 9. Validation order

The bootloader validates in this order:

1. magic and supported format version;
2. header and total-length arithmetic without overflow;
3. product ID and hardware revision;
4. image type and flags;
5. target address and target size;
6. anti-rollback version policy;
7. payload CRC32;
8. container digital signature;
9. delta base version and base SHA-256, when applicable;
10. target SHA-256 after extraction/reconstruction.

No internal application erase may begin before all applicable checks pass and backup is verified.

## 10. Product identity

Initial fixed values are placeholders until board/product registration in platform bring-up:

```c
#define PRODUCT_ID_STM32F103_NODE  0x00001001UL
#define HARDWARE_REVISION_1        0x00000001UL
```

They are explicitly present in the signed header rather than inferred only from signing keys.

## 11. Unsupported v1 features

The following flags are not allowed in format version 1 implementation until explicitly added:

- encrypted payload;
- compressed payload;
- multiple target segments;
- bootloader update;
- chained delta patch;
- writable calibration/configuration segments.


## 12. Implemented signed SDOT v1 + SCX1 profile

The canonical signed artifact used by the secure build is:

```text
fixed SDOT header (120 bytes)
SCX1 extension v1 (20 bytes)
full-image or JojoDiff-compatible delta payload
ECDSA P-256 signature (64 bytes; big-endian r[32] || s[32])
```

`header_size = 140` and the extension carries `key_id`, `base_image_size`, and
`target_image_crc32`. The signature authenticates the exact 140-byte canonical
header **and** payload (the signature bytes themselves are excluded).
`FW_SIGNATURE_ECDSA_P256` and `FW_HASH_SHA256` identify the supported signature
profile. Signed full and delta artifacts are produced by the host release
pipeline (`tools/secure_container.py`, `tools/release.py`) and verified by
`SecureContainer_LoadVerifiedInfo()` before destructive installation.

For both image types, the bootloader uses `UPDATE_PATCHING` as the persisted
**target-image preparation** state. Full image: copy validated payload into
Reconstructed; delta: verify the active base and apply the patch to
Reconstructed. Both paths verify the reconstructed target before backup/install.
Unsigned raw full and streaming-delta artifacts are **disabled by default**
(`SECURE_CONTAINER_ALLOW_UNSIGNED_LEGACY=0`).

## 13. Historical engineering formats

Earlier project stages used an unsigned raw full-image path and an engineering
`D13P` envelope around JojoDiff-compatible delta bytes for streaming/recovery
tests. Those are **not** the authenticated release format. The optional legacy
branch is compile-time gated; it must not be presented as the default secure OTA
workflow. The signed SDOT v1 + SCX1 container supersedes those scaffolding
formats for normal updates.

## 14. Trust-anchor provisioning

The checked-in [`trusted_key.h`](../node-stm32f103/bootloader/include/trusted_key.h)
sets `TRUSTED_KEY_PROVISIONED` to `0`, with an unprovisioned public-key slot.
`SecureContainer_LoadVerifiedInfo()` explicitly rejects secure installation
when the trust anchor is not provisioned. Reproducible/HIL tooling can inject
temporary test keys; this is **not** production key provisioning. Before
deployment, install an authorized P-256 public key and key ID in the
bootloader, protect the signing private key on the release host, and use the
signed release workflow. Do not claim that a stock unprovisioned clone can
install production SDOT artifacts without this step.

## References

- [`firmware_container.h`](../shared/include/firmware_container.h)
- [`secure_container.c`](../node-stm32f103/bootloader/src/secure_container.c)
- [`secure_container.py`](../tools/secure_container.py)

[↑ Documentation Index](README.md) · [← Root](../README.md) · [← UART OTA Protocol](uart-ota-protocol.md) · [Boot State Machine →](boot-state-machine.md)
