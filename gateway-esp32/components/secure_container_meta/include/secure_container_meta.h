#ifndef SECURE_CONTAINER_META_H
#define SECURE_CONTAINER_META_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SECURE_CONTAINER_META_MAGIC              0x544F4453UL
#define SECURE_CONTAINER_META_FORMAT_VERSION     1U
#define SECURE_CONTAINER_META_HEADER_SIZE        140U
#define SECURE_CONTAINER_META_SIGNATURE_SIZE     64U
#define SECURE_CONTAINER_META_EXT_MAGIC          0x31584353UL
#define SECURE_CONTAINER_META_PRODUCT_ID         0x00001001UL
#define SECURE_CONTAINER_META_HW_REVISION        1UL

#define SECURE_CONTAINER_META_IMAGE_FULL         1U
#define SECURE_CONTAINER_META_IMAGE_DELTA        2U

typedef struct
{
    uint8_t image_type;
    uint32_t base_version;
    uint32_t target_version;
    uint32_t payload_size;
    uint32_t target_size;
    uint32_t key_id;
} SecureContainerMeta_t;

bool SecureContainerMeta_HasMagic(const uint8_t *data, size_t length);

bool SecureContainerMeta_Parse(const uint8_t *header,
                               size_t header_length,
                               uint32_t container_size,
                               SecureContainerMeta_t *meta);

#endif
