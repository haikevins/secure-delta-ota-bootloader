#ifndef DELTA_PATCH_H
#define DELTA_PATCH_H

#include <stdint.h>

#define DELTA_PATCH_MAGIC            0x50333144UL /* "D13P" */
#define DELTA_PATCH_FORMAT_VERSION   1U
#define DELTA_PATCH_HEADER_SIZE      48U

typedef struct
{
    uint32_t base_version;
    uint32_t target_version;
    uint32_t base_image_size;
    uint32_t patch_size;
    uint32_t target_image_size;
    uint32_t target_load_address;
    uint32_t base_image_crc32;
    uint32_t target_image_crc32;
    uint32_t patch_crc32;
} DeltaPatchHeader_t;

typedef enum
{
    DELTA_PATCH_HEADER_VALID = 0,
    DELTA_PATCH_HEADER_INVALID_ARGUMENT,
    DELTA_PATCH_HEADER_INVALID_MAGIC,
    DELTA_PATCH_HEADER_INVALID_FORMAT,
    DELTA_PATCH_HEADER_INVALID_HEADER_SIZE,
    DELTA_PATCH_HEADER_INVALID_VERSION,
    DELTA_PATCH_HEADER_INVALID_BASE_SIZE,
    DELTA_PATCH_HEADER_INVALID_PATCH_SIZE,
    DELTA_PATCH_HEADER_INVALID_TARGET_SIZE,
    DELTA_PATCH_HEADER_INVALID_TARGET_ADDRESS,
    DELTA_PATCH_HEADER_INVALID_TOTAL_SIZE,
    DELTA_PATCH_HEADER_INVALID_CRC
} DeltaPatchHeaderStatus_t;

uint32_t DeltaPatch_HeaderCrc32(const uint8_t raw[DELTA_PATCH_HEADER_SIZE]);

DeltaPatchHeaderStatus_t DeltaPatch_ParseHeader(
    const uint8_t raw[DELTA_PATCH_HEADER_SIZE],
    uint32_t artifact_size,
    DeltaPatchHeader_t *header);

#endif
