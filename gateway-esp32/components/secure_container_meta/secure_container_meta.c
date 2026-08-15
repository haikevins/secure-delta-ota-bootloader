#include "secure_container_meta.h"

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

bool SecureContainerMeta_HasMagic(const uint8_t *data, size_t length)
{
    return (data != NULL) &&
           (length >= 4U) &&
           (GetU32Le(data) == SECURE_CONTAINER_META_MAGIC);
}

bool SecureContainerMeta_Parse(const uint8_t *header,
                               size_t header_length,
                               uint32_t container_size,
                               SecureContainerMeta_t *meta)
{
    uint8_t image_type;
    uint32_t base_version;
    uint32_t target_version;
    uint32_t payload_size;
    uint16_t signature_size;
    uint64_t expected_total;

    if ((header == NULL) || (meta == NULL) ||
        (header_length < SECURE_CONTAINER_META_HEADER_SIZE))
    {
        return false;
    }

    if (GetU32Le(&header[0]) != SECURE_CONTAINER_META_MAGIC ||
        GetU16Le(&header[4]) != SECURE_CONTAINER_META_FORMAT_VERSION ||
        GetU16Le(&header[6]) != SECURE_CONTAINER_META_HEADER_SIZE ||
        GetU32Le(&header[8]) != SECURE_CONTAINER_META_PRODUCT_ID ||
        GetU32Le(&header[12]) != SECURE_CONTAINER_META_HW_REVISION ||
        GetU32Le(&header[120]) != SECURE_CONTAINER_META_EXT_MAGIC ||
        GetU16Le(&header[124]) != 1U ||
        GetU16Le(&header[126]) != 20U ||
        GetU16Le(&header[112]) != 1U ||
        GetU16Le(&header[114]) != 1U)
    {
        return false;
    }

    image_type = (uint8_t)GetU32Le(&header[16]);
    base_version = GetU32Le(&header[24]);
    target_version = GetU32Le(&header[28]);
    payload_size = GetU32Le(&header[32]);
    signature_size = GetU16Le(&header[116]);

    if ((image_type != SECURE_CONTAINER_META_IMAGE_FULL) &&
        (image_type != SECURE_CONTAINER_META_IMAGE_DELTA))
    {
        return false;
    }
    if (target_version == 0UL)
    {
        return false;
    }
    if (image_type == SECURE_CONTAINER_META_IMAGE_FULL)
    {
        if ((base_version != 0UL) || (GetU32Le(&header[132]) != 0UL))
        {
            return false;
        }
    }
    else
    {
        if ((base_version == 0UL) ||
            (target_version <= base_version) ||
            (GetU32Le(&header[132]) < 8UL))
        {
            return false;
        }
    }

    if (GetU32Le(&header[40]) != 0x08006000UL ||
        signature_size != SECURE_CONTAINER_META_SIGNATURE_SIZE ||
        GetU32Le(&header[128]) == 0UL)
    {
        return false;
    }

    expected_total =
        (uint64_t)SECURE_CONTAINER_META_HEADER_SIZE +
        (uint64_t)payload_size +
        (uint64_t)signature_size;
    if (expected_total != (uint64_t)container_size)
    {
        return false;
    }

    meta->image_type = image_type;
    meta->base_version = base_version;
    meta->target_version = target_version;
    meta->payload_size = payload_size;
    meta->target_size = GetU32Le(&header[36]);
    meta->key_id = GetU32Le(&header[128]);
    return true;
}
