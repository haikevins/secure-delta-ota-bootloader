#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "secure_container_meta.h"
#include "uart_ota_protocol.h"

static void PutU16Le(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8U);
}

static void PutU32Le(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8U);
    dst[2] = (uint8_t)(value >> 16U);
    dst[3] = (uint8_t)(value >> 24U);
}

static uint32_t GetU32Le(const uint8_t *src)
{
    return (uint32_t)src[0] |
           ((uint32_t)src[1] << 8U) |
           ((uint32_t)src[2] << 16U) |
           ((uint32_t)src[3] << 24U);
}

static int CheckFull(void)
{
    uint8_t header[SECURE_CONTAINER_META_HEADER_SIZE];
    uint8_t start[UART_OTA_START_PAYLOAD_SIZE];
    SecureContainerMeta_t meta;
    const uint32_t payload_size = 9000UL;
    const uint32_t total =
        SECURE_CONTAINER_META_HEADER_SIZE +
        payload_size +
        SECURE_CONTAINER_META_SIGNATURE_SIZE;

    memset(header, 0, sizeof(header));
    PutU32Le(&header[0], SECURE_CONTAINER_META_MAGIC);
    PutU16Le(&header[4], SECURE_CONTAINER_META_FORMAT_VERSION);
    PutU16Le(&header[6], SECURE_CONTAINER_META_HEADER_SIZE);
    PutU32Le(&header[8], SECURE_CONTAINER_META_PRODUCT_ID);
    PutU32Le(&header[12], SECURE_CONTAINER_META_HW_REVISION);
    PutU32Le(&header[16], SECURE_CONTAINER_META_IMAGE_FULL);
    PutU32Le(&header[24], 0UL);
    PutU32Le(&header[28], 3UL);
    PutU32Le(&header[32], payload_size);
    PutU32Le(&header[36], payload_size);
    PutU32Le(&header[40], 0x08006000UL);
    PutU16Le(&header[112], 1U);
    PutU16Le(&header[114], 1U);
    PutU16Le(&header[116], SECURE_CONTAINER_META_SIGNATURE_SIZE);
    PutU32Le(&header[120], SECURE_CONTAINER_META_EXT_MAGIC);
    PutU16Le(&header[124], 1U);
    PutU16Le(&header[126], 20U);
    PutU32Le(&header[128], 0x15000001UL);
    PutU32Le(&header[132], 0UL);

    if (!SecureContainerMeta_Parse(header, sizeof(header), total, &meta))
    {
        return 1;
    }

    if ((meta.image_type != SECURE_CONTAINER_META_IMAGE_FULL) ||
        (meta.base_version != 0UL) ||
        (meta.target_version != 3UL) ||
        (meta.key_id != 0x15000001UL))
    {
        return 2;
    }

    UartOta_BuildStartPayload(
        start,
        meta.image_type,
        meta.base_version,
        meta.target_version,
        total,
        0x12345678UL,
        SECURE_CONTAINER_META_HEADER_SIZE);

    if ((start[0] != SECURE_CONTAINER_META_IMAGE_FULL) ||
        (GetU32Le(&start[4]) != 0UL) ||
        (GetU32Le(&start[8]) != 3UL) ||
        (GetU32Le(&start[12]) != total) ||
        (GetU32Le(&start[16]) != 0x12345678UL) ||
        (GetU32Le(&start[20]) != SECURE_CONTAINER_META_HEADER_SIZE))
    {
        return 3;
    }

    return 0;
}

static int CheckDelta(void)
{
    uint8_t header[SECURE_CONTAINER_META_HEADER_SIZE];
    uint8_t start[UART_OTA_START_PAYLOAD_SIZE];
    SecureContainerMeta_t meta;
    const uint32_t payload_size = 1100UL;
    const uint32_t total =
        SECURE_CONTAINER_META_HEADER_SIZE +
        payload_size +
        SECURE_CONTAINER_META_SIGNATURE_SIZE;

    memset(header, 0, sizeof(header));
    PutU32Le(&header[0], SECURE_CONTAINER_META_MAGIC);
    PutU16Le(&header[4], SECURE_CONTAINER_META_FORMAT_VERSION);
    PutU16Le(&header[6], SECURE_CONTAINER_META_HEADER_SIZE);
    PutU32Le(&header[8], SECURE_CONTAINER_META_PRODUCT_ID);
    PutU32Le(&header[12], SECURE_CONTAINER_META_HW_REVISION);
    PutU32Le(&header[16], SECURE_CONTAINER_META_IMAGE_DELTA);
    PutU32Le(&header[24], 2UL);
    PutU32Le(&header[28], 3UL);
    PutU32Le(&header[32], payload_size);
    PutU32Le(&header[36], 9500UL);
    PutU32Le(&header[40], 0x08006000UL);
    PutU16Le(&header[112], 1U);
    PutU16Le(&header[114], 1U);
    PutU16Le(&header[116], SECURE_CONTAINER_META_SIGNATURE_SIZE);
    PutU32Le(&header[120], SECURE_CONTAINER_META_EXT_MAGIC);
    PutU16Le(&header[124], 1U);
    PutU16Le(&header[126], 20U);
    PutU32Le(&header[128], 0x15000001UL);
    PutU32Le(&header[132], 9400UL);

    if (!SecureContainerMeta_Parse(header, sizeof(header), total, &meta))
    {
        return 4;
    }

    if ((meta.image_type != SECURE_CONTAINER_META_IMAGE_DELTA) ||
        (meta.base_version != 2UL) ||
        (meta.target_version != 3UL))
    {
        return 5;
    }

    UartOta_BuildStartPayload(
        start,
        meta.image_type,
        meta.base_version,
        meta.target_version,
        total,
        0xAABBCCDDUL,
        SECURE_CONTAINER_META_HEADER_SIZE);

    if ((start[0] != SECURE_CONTAINER_META_IMAGE_DELTA) ||
        (GetU32Le(&start[4]) != 2UL) ||
        (GetU32Le(&start[8]) != 3UL) ||
        (GetU32Le(&start[20]) != SECURE_CONTAINER_META_HEADER_SIZE))
    {
        return 6;
    }

    /* Total-length mismatch must be rejected before UART transfer. */
    if (SecureContainerMeta_Parse(header, sizeof(header), total + 1UL, &meta))
    {
        return 7;
    }

    return 0;
}

int main(void)
{
    int status = CheckFull();
    if (status != 0)
    {
        fprintf(stderr, "Phase15 full contract failed: %d\n", status);
        return status;
    }

    status = CheckDelta();
    if (status != 0)
    {
        fprintf(stderr, "Phase15 delta contract failed: %d\n", status);
        return status;
    }

    puts("Phase 15 ESP32 secure-container UART contract: PASS");
    return 0;
}
