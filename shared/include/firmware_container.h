#ifndef FIRMWARE_CONTAINER_H
#define FIRMWARE_CONTAINER_H

#include "project_types.h"

#define FW_CONTAINER_MAGIC           0x544F4453UL
#define FW_CONTAINER_FORMAT_VERSION  1U
#define PRODUCT_ID_STM32F103_NODE     0x00001001UL
#define HARDWARE_REVISION_1           0x00000001UL

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
    uint8_t base_image_sha256[32];
    uint8_t target_image_sha256[32];
    uint32_t payload_crc32;
    uint16_t hash_algorithm;
    uint16_t signature_algorithm;
    uint16_t signature_size;
    uint16_t reserved;
} FirmwareContainerHeader_t;

#endif
