# Secure Firmware Container Specification Version 1

Status: **Frozen for initial scaffold**

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

The exact production signature algorithm is intentionally selected in signed secure container after code-size measurement. Protocol version 1 reserves identifiers for ECDSA-P256 and Ed25519. initial scaffold requires a signed-container interface but does not falsely claim that either implementation already fits the 24 KiB bootloader budget.

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

## 11. Unsupported initial scaffold features

The following flags are not allowed in format version 1 implementation until explicitly added:

- encrypted payload;
- compressed payload;
- multiple target segments;
- bootloader update;
- chained delta patch;
- writable calibration/configuration segments.


## full-image OTA compatibility note

The secure container format above remains the target format, but full-image OTA does
not parse or authenticate it yet. The full-image OTA basic full OTA test transfers the
raw linker-produced `application.bin`; the START payload plus the external
handoff record carries image size, target version and CRC32. Secure container
parsing/signature verification can evolve without changing
the internal application load address.


## delta generation compatibility note

delta generation now generates and host-verifies the delta payload format used by
`FW_IMAGE_DELTA`.

The generated payload is a JojoDiff-compatible stream and the delta generation JSON
metadata records the exact values needed for later secure-container assembly:

```text
base_version
target_version
payload_size
target_image_size
target_load_address
base_image_sha256
target_image_sha256
payload_crc32
```

delta generation does not yet serialize the final signed container header and does not
claim authenticity. Container signing is provided by the SDOT secure-container layer. STM32 patch
application starts in streaming delta reconstruction.


## streaming delta reconstruction functional-envelope note

streaming delta reconstruction introduces a temporary 48-byte `D13P` engineering envelope around
the JojoDiff-compatible payload so the STM32 bootloader can persist exact
base/target sizes, versions and CRC32 values across reset.

`D13P` is not the final secure container and is not signed. It exists only to
exercise the complete embedded delta path together with the signed-container path.
The frozen `FirmwareContainerHeader_t` remains the target authenticated
container format.


## signed secure container implementation note

signed secure container now implements the previously reserved signed-container interface.

The initial scaffold fixed header remains 120 bytes. A signed `SCX1` extension v1 adds
20 bytes, so the implemented canonical `header_size` is 140 bytes.

`SCX1` carries:

```text
key_id
base_image_size
target_image_crc32
```

The implemented signature profile is:

```text
hash_algorithm      = FW_HASH_SHA256
signature_algorithm = FW_SIGNATURE_ECDSA_P256
signature_size      = 64
signature encoding  = big-endian r[32] || s[32]
```

The signature covers the complete 140-byte header plus payload. Unsigned raw
full and streaming delta reconstruction D13P artifacts are disabled by default in signed secure container.

The private signing key remains host-side. The STM32 bootloader contains only a
public P-256 trust anchor and key ID.

Key custody, release authorization and server publication remain release pipeline.
