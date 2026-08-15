#include "firmware_container.h"

#include <stdint.h>

#include "memory_map.h"

uint16_t FirmwareContainer_GetU16Le(const uint8_t *source)
{
    if (source == (const uint8_t *)0)
    {
        return 0U;
    }

    return (uint16_t)(
        (uint16_t)source[0] |
        ((uint16_t)source[1] << 8U));
}

uint32_t FirmwareContainer_GetU32Le(const uint8_t *source)
{
    if (source == (const uint8_t *)0)
    {
        return 0UL;
    }

    return (uint32_t)source[0] |
           ((uint32_t)source[1] << 8U) |
           ((uint32_t)source[2] << 16U) |
           ((uint32_t)source[3] << 24U);
}

uint8_t FirmwareContainer_HashIsZero(
    const uint8_t hash[FW_SHA256_SIZE])
{
    uint8_t combined = 0U;

    if (hash == (const uint8_t *)0)
    {
        return 0U;
    }

    for (uint32_t i = 0UL; i < FW_SHA256_SIZE; ++i)
    {
        combined |= hash[i];
    }

    return (uint8_t)(combined == 0U);
}

static void CopyBytes(uint8_t *destination,
                      const uint8_t *source,
                      uint32_t length)
{
    for (uint32_t i = 0UL; i < length; ++i)
    {
        destination[i] = source[i];
    }
}

static FirmwareContainerStatus_t ParseFixed(
    const uint8_t raw[FW_CONTAINER_HEADER_SIZE],
    FirmwareContainerHeader_t *header)
{
    header->magic = FirmwareContainer_GetU32Le(&raw[0]);
    header->format_version = FirmwareContainer_GetU16Le(&raw[4]);
    header->header_size = FirmwareContainer_GetU16Le(&raw[6]);
    header->product_id = FirmwareContainer_GetU32Le(&raw[8]);
    header->hardware_revision = FirmwareContainer_GetU32Le(&raw[12]);
    header->image_type = FirmwareContainer_GetU32Le(&raw[16]);
    header->flags = FirmwareContainer_GetU32Le(&raw[20]);
    header->base_version = FirmwareContainer_GetU32Le(&raw[24]);
    header->target_version = FirmwareContainer_GetU32Le(&raw[28]);
    header->payload_size = FirmwareContainer_GetU32Le(&raw[32]);
    header->target_image_size = FirmwareContainer_GetU32Le(&raw[36]);
    header->target_load_address = FirmwareContainer_GetU32Le(&raw[40]);

    CopyBytes(
        header->base_image_sha256,
        &raw[44],
        FW_SHA256_SIZE);
    CopyBytes(
        header->target_image_sha256,
        &raw[76],
        FW_SHA256_SIZE);

    header->payload_crc32 = FirmwareContainer_GetU32Le(&raw[108]);
    header->hash_algorithm = FirmwareContainer_GetU16Le(&raw[112]);
    header->signature_algorithm = FirmwareContainer_GetU16Le(&raw[114]);
    header->signature_size = FirmwareContainer_GetU16Le(&raw[116]);
    header->reserved = FirmwareContainer_GetU16Le(&raw[118]);

    if (header->magic != FW_CONTAINER_MAGIC)
    {
        return FW_CONTAINER_INVALID_MAGIC;
    }
    if (header->format_version != FW_CONTAINER_FORMAT_VERSION)
    {
        return FW_CONTAINER_INVALID_FORMAT_VERSION;
    }
    if (header->header_size != FW_CONTAINER_HEADER_SIZE)
    {
        return FW_CONTAINER_INVALID_HEADER_SIZE;
    }
    if (header->product_id != PRODUCT_ID_STM32F103_NODE)
    {
        return FW_CONTAINER_INVALID_PRODUCT;
    }
    if (header->hardware_revision != HARDWARE_REVISION_1)
    {
        return FW_CONTAINER_INVALID_HARDWARE;
    }
    if ((header->image_type != (uint32_t)FW_IMAGE_FULL) &&
        (header->image_type != (uint32_t)FW_IMAGE_DELTA))
    {
        return FW_CONTAINER_INVALID_IMAGE_TYPE;
    }
    if (header->flags != FW_CONTAINER_FLAGS_NONE)
    {
        return FW_CONTAINER_INVALID_FLAGS;
    }
    if (header->target_version == 0UL)
    {
        return FW_CONTAINER_INVALID_VERSION;
    }
    if ((header->payload_size == 0UL) ||
        (header->payload_size >
         (EXT_INCOMING_SIZE -
          FW_CONTAINER_HEADER_SIZE -
          FW_ECDSA_P256_RAW_SIGNATURE_SIZE)))
    {
        return FW_CONTAINER_INVALID_PAYLOAD_SIZE;
    }
    if ((header->target_image_size < 8UL) ||
        (header->target_image_size > APPLICATION_MAX_SIZE) ||
        (header->target_image_size > EXT_RECONSTRUCTED_SIZE))
    {
        return FW_CONTAINER_INVALID_TARGET_SIZE;
    }
    if (header->target_load_address != APPLICATION_START_ADDRESS)
    {
        return FW_CONTAINER_INVALID_TARGET_ADDRESS;
    }
    if (header->hash_algorithm != FW_HASH_SHA256)
    {
        return FW_CONTAINER_INVALID_HASH_ALGORITHM;
    }
    if (header->signature_algorithm != FW_SIGNATURE_ECDSA_P256)
    {
        return FW_CONTAINER_INVALID_SIGNATURE_ALGORITHM;
    }
    if (header->signature_size != FW_ECDSA_P256_RAW_SIGNATURE_SIZE)
    {
        return FW_CONTAINER_INVALID_SIGNATURE_SIZE;
    }
    if (header->reserved != 0U)
    {
        return FW_CONTAINER_INVALID_RESERVED;
    }

    return FW_CONTAINER_VALID;
}

static FirmwareContainerStatus_t ParseExtension(
    const uint8_t raw[FW_CONTAINER_HEADER_SIZE],
    FirmwareContainerExtensionV1_t *extension)
{
    const uint32_t offset = FW_CONTAINER_FIXED_HEADER_SIZE;

    extension->extension_magic =
        FirmwareContainer_GetU32Le(&raw[offset + 0UL]);
    extension->extension_version =
        FirmwareContainer_GetU16Le(&raw[offset + 4UL]);
    extension->extension_size =
        FirmwareContainer_GetU16Le(&raw[offset + 6UL]);
    extension->key_id =
        FirmwareContainer_GetU32Le(&raw[offset + 8UL]);
    extension->base_image_size =
        FirmwareContainer_GetU32Le(&raw[offset + 12UL]);

    extension->target_image_crc32 =
        FirmwareContainer_GetU32Le(&raw[offset + 16UL]);

    if ((extension->extension_magic != FW_CONTAINER_EXTENSION_MAGIC) ||
        (extension->extension_version != FW_CONTAINER_EXTENSION_VERSION) ||
        (extension->extension_size != FW_CONTAINER_EXTENSION_SIZE) ||
        (extension->key_id == 0UL))
    {
        return FW_CONTAINER_INVALID_EXTENSION;
    }

    return FW_CONTAINER_VALID;
}

FirmwareContainerStatus_t FirmwareContainer_Parse(
    const uint8_t raw_header[FW_CONTAINER_HEADER_SIZE],
    uint32_t artifact_size,
    FirmwareContainerInfo_t *info)
{
    FirmwareContainerStatus_t status;
    uint32_t expected_total;

    if ((raw_header == (const uint8_t *)0) ||
        (info == (FirmwareContainerInfo_t *)0))
    {
        return FW_CONTAINER_INVALID_ARGUMENT;
    }

    status = ParseFixed(raw_header, &info->header);
    if (status != FW_CONTAINER_VALID)
    {
        return status;
    }

    status = ParseExtension(raw_header, &info->extension);
    if (status != FW_CONTAINER_VALID)
    {
        return status;
    }

    if (info->header.image_type == (uint32_t)FW_IMAGE_FULL)
    {
        if ((info->header.base_version != 0UL) ||
            (info->extension.base_image_size != 0UL) ||
            (FirmwareContainer_HashIsZero(
                 info->header.base_image_sha256) == 0U) ||
            (info->header.payload_size != info->header.target_image_size))
        {
            return FW_CONTAINER_INVALID_BASE_SIZE;
        }
    }
    else
    {
        if ((info->header.base_version == 0UL) ||
            (info->extension.base_image_size < 8UL) ||
            (info->extension.base_image_size > APPLICATION_MAX_SIZE) ||
            (FirmwareContainer_HashIsZero(
                 info->header.base_image_sha256) != 0U))
        {
            return FW_CONTAINER_INVALID_BASE_SIZE;
        }
    }

    if (info->header.payload_size >
        (0xFFFFFFFFUL -
         (uint32_t)info->header.header_size -
         (uint32_t)info->header.signature_size))
    {
        return FW_CONTAINER_INVALID_TOTAL_SIZE;
    }

    expected_total =
        (uint32_t)info->header.header_size +
        info->header.payload_size +
        (uint32_t)info->header.signature_size;

    if ((artifact_size != 0UL) &&
        (artifact_size != expected_total))
    {
        return FW_CONTAINER_INVALID_TOTAL_SIZE;
    }

    info->payload_offset = info->header.header_size;
    info->signature_offset =
        info->payload_offset + info->header.payload_size;
    info->total_size = expected_total;

    return FW_CONTAINER_VALID;
}
