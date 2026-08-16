#ifndef FIRMWARE_CONTAINER_H
#define FIRMWARE_CONTAINER_H

#include <stdint.h>

#define FW_CONTAINER_MAGIC                    0x544F4453UL /* "SDOT" */
#define FW_CONTAINER_FORMAT_VERSION           1U
#define FW_CONTAINER_FIXED_HEADER_SIZE        120U
#define FW_CONTAINER_EXTENSION_MAGIC          0x31584353UL /* "SCX1" */
#define FW_CONTAINER_EXTENSION_VERSION        1U
#define FW_CONTAINER_EXTENSION_SIZE           20U
#define FW_CONTAINER_HEADER_SIZE              \
    (FW_CONTAINER_FIXED_HEADER_SIZE + FW_CONTAINER_EXTENSION_SIZE)

#define PRODUCT_ID_STM32F103_NODE             0x00001001UL
#define HARDWARE_REVISION_1                   0x00000001UL

#define FW_CONTAINER_FLAGS_NONE               0x00000000UL

#define FW_HASH_SHA256                        1U
#define FW_SIGNATURE_NONE                     0U
#define FW_SIGNATURE_ECDSA_P256               1U
#define FW_SIGNATURE_ED25519                  2U

#define FW_ECDSA_P256_RAW_SIGNATURE_SIZE      64U
#define FW_SHA256_SIZE                        32U

#ifndef SECURE_CONTAINER_ALLOW_UNSIGNED_LEGACY
#define SECURE_CONTAINER_ALLOW_UNSIGNED_LEGACY         0U
#endif

typedef enum
{
    FW_IMAGE_FULL = 1U,
    FW_IMAGE_DELTA = 2U
} FirmwareImageType_t;

typedef struct
{
    uint32_t magic;
    uint16_t format_version;
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
    uint8_t base_image_sha256[FW_SHA256_SIZE];
    uint8_t target_image_sha256[FW_SHA256_SIZE];
    uint32_t payload_crc32;
    uint16_t hash_algorithm;
    uint16_t signature_algorithm;
    uint16_t signature_size;
    uint16_t reserved;
} FirmwareContainerHeader_t;

typedef struct
{
    uint32_t extension_magic;
    uint16_t extension_version;
    uint16_t extension_size;
    uint32_t key_id;
    uint32_t base_image_size;
    uint32_t target_image_crc32;
} FirmwareContainerExtensionV1_t;

typedef struct
{
    FirmwareContainerHeader_t header;
    FirmwareContainerExtensionV1_t extension;
    uint32_t payload_offset;
    uint32_t signature_offset;
    uint32_t total_size;
} FirmwareContainerInfo_t;

typedef enum
{
    FW_CONTAINER_VALID = 0,
    FW_CONTAINER_INVALID_ARGUMENT,
    FW_CONTAINER_INVALID_MAGIC,
    FW_CONTAINER_INVALID_FORMAT_VERSION,
    FW_CONTAINER_INVALID_HEADER_SIZE,
    FW_CONTAINER_INVALID_PRODUCT,
    FW_CONTAINER_INVALID_HARDWARE,
    FW_CONTAINER_INVALID_IMAGE_TYPE,
    FW_CONTAINER_INVALID_FLAGS,
    FW_CONTAINER_INVALID_VERSION,
    FW_CONTAINER_INVALID_PAYLOAD_SIZE,
    FW_CONTAINER_INVALID_TARGET_SIZE,
    FW_CONTAINER_INVALID_TARGET_ADDRESS,
    FW_CONTAINER_INVALID_HASH_ALGORITHM,
    FW_CONTAINER_INVALID_SIGNATURE_ALGORITHM,
    FW_CONTAINER_INVALID_SIGNATURE_SIZE,
    FW_CONTAINER_INVALID_RESERVED,
    FW_CONTAINER_INVALID_EXTENSION,
    FW_CONTAINER_INVALID_BASE_SIZE,
    FW_CONTAINER_INVALID_TOTAL_SIZE
} FirmwareContainerStatus_t;

uint16_t FirmwareContainer_GetU16Le(const uint8_t *source);
uint32_t FirmwareContainer_GetU32Le(const uint8_t *source);

FirmwareContainerStatus_t FirmwareContainer_Parse(
    const uint8_t raw_header[FW_CONTAINER_HEADER_SIZE],
    uint32_t artifact_size,
    FirmwareContainerInfo_t *info);

uint8_t FirmwareContainer_HashIsZero(
    const uint8_t hash[FW_SHA256_SIZE]);

#endif
