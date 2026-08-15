#include "delta_patch.h"

#include "crc32.h"
#include "memory_map.h"

#define DELTA_PATCH_CRC_OFFSET 44U

static uint16_t GetU16Le(const uint8_t *src)
{
    return (uint16_t)((uint16_t)src[0] |
                      ((uint16_t)src[1] << 8U));
}

static uint32_t GetU32Le(const uint8_t *src)
{
    return (uint32_t)src[0] |
           ((uint32_t)src[1] << 8U) |
           ((uint32_t)src[2] << 16U) |
           ((uint32_t)src[3] << 24U);
}

uint32_t DeltaPatch_HeaderCrc32(
    const uint8_t raw[DELTA_PATCH_HEADER_SIZE])
{
    if (raw == (const uint8_t *)0)
    {
        return 0UL;
    }

    return Crc32_Calculate(raw, DELTA_PATCH_CRC_OFFSET);
}

DeltaPatchHeaderStatus_t DeltaPatch_ParseHeader(
    const uint8_t raw[DELTA_PATCH_HEADER_SIZE],
    uint32_t artifact_size,
    DeltaPatchHeader_t *header)
{
    uint32_t stored_crc;

    if ((raw == (const uint8_t *)0) ||
        (header == (DeltaPatchHeader_t *)0))
    {
        return DELTA_PATCH_HEADER_INVALID_ARGUMENT;
    }

    if (GetU32Le(&raw[0]) != DELTA_PATCH_MAGIC)
    {
        return DELTA_PATCH_HEADER_INVALID_MAGIC;
    }

    if (GetU16Le(&raw[4]) != DELTA_PATCH_FORMAT_VERSION)
    {
        return DELTA_PATCH_HEADER_INVALID_FORMAT;
    }

    if (GetU16Le(&raw[6]) != DELTA_PATCH_HEADER_SIZE)
    {
        return DELTA_PATCH_HEADER_INVALID_HEADER_SIZE;
    }

    header->base_version = GetU32Le(&raw[8]);
    header->target_version = GetU32Le(&raw[12]);
    header->base_image_size = GetU32Le(&raw[16]);
    header->patch_size = GetU32Le(&raw[20]);
    header->target_image_size = GetU32Le(&raw[24]);
    header->target_load_address = GetU32Le(&raw[28]);
    header->base_image_crc32 = GetU32Le(&raw[32]);
    header->target_image_crc32 = GetU32Le(&raw[36]);
    header->patch_crc32 = GetU32Le(&raw[40]);
    stored_crc = GetU32Le(&raw[44]);

    if ((header->base_version == 0UL) ||
        (header->target_version == 0UL) ||
        (header->target_version <= header->base_version))
    {
        return DELTA_PATCH_HEADER_INVALID_VERSION;
    }

    if ((header->base_image_size < 8UL) ||
        (header->base_image_size > APPLICATION_MAX_SIZE))
    {
        return DELTA_PATCH_HEADER_INVALID_BASE_SIZE;
    }

    if ((header->patch_size == 0UL) ||
        (header->patch_size >
         (EXT_INCOMING_SIZE - DELTA_PATCH_HEADER_SIZE)))
    {
        return DELTA_PATCH_HEADER_INVALID_PATCH_SIZE;
    }

    if ((header->target_image_size < 8UL) ||
        (header->target_image_size > APPLICATION_MAX_SIZE) ||
        (header->target_image_size > EXT_RECONSTRUCTED_SIZE))
    {
        return DELTA_PATCH_HEADER_INVALID_TARGET_SIZE;
    }

    if (header->target_load_address != APPLICATION_START_ADDRESS)
    {
        return DELTA_PATCH_HEADER_INVALID_TARGET_ADDRESS;
    }

    if ((artifact_size != 0UL) &&
        ((DELTA_PATCH_HEADER_SIZE + header->patch_size) != artifact_size))
    {
        return DELTA_PATCH_HEADER_INVALID_TOTAL_SIZE;
    }

    if (stored_crc != DeltaPatch_HeaderCrc32(raw))
    {
        return DELTA_PATCH_HEADER_INVALID_CRC;
    }

    return DELTA_PATCH_HEADER_VALID;
}
