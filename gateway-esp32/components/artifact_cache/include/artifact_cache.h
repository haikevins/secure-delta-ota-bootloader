#ifndef ARTIFACT_CACHE_H
#define ARTIFACT_CACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "uart_ota.h"

#define ARTIFACT_CACHE_PARTITION_LABEL  "stm32_cache"
#define ARTIFACT_CACHE_DATA_OFFSET      0x1000UL
#define ARTIFACT_CACHE_MAGIC            0x39414347UL /* "GCA9", retained */
#define ARTIFACT_CACHE_FORMAT_VERSION   1UL

typedef struct
{
    uint32_t magic;
    uint32_t format_version;
    uint32_t header_size;
    uint32_t update_id;
    uint32_t target_version;
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t data_offset;
    uint32_t header_crc32;
} ArtifactCacheHeader_t;

typedef struct
{
    const void *partition;
    ArtifactCacheHeader_t header;
} ArtifactCache_t;

typedef struct
{
    const void *partition;
    uint32_t update_id;
    uint32_t target_version;
    uint32_t expected_size;
    uint32_t written_size;
    uint32_t running_crc32;
    bool active;
} ArtifactCacheWriter_t;

esp_err_t ArtifactCache_Open(ArtifactCache_t *cache);

esp_err_t ArtifactCache_BeginWrite(ArtifactCacheWriter_t *writer,
                                   uint32_t expected_size,
                                   uint32_t update_id,
                                   uint32_t target_version);

esp_err_t ArtifactCache_Write(ArtifactCacheWriter_t *writer,
                              const uint8_t *data,
                              size_t length);

esp_err_t ArtifactCache_Commit(ArtifactCacheWriter_t *writer,
                               ArtifactCache_t *cache);

esp_err_t ArtifactCache_Abort(ArtifactCacheWriter_t *writer);

esp_err_t ArtifactCache_Seed(ArtifactCache_t *cache,
                             const uint8_t *image,
                             size_t image_size,
                             uint32_t update_id,
                             uint32_t target_version);

esp_err_t ArtifactCache_AsUartArtifact(ArtifactCache_t *cache,
                                      UartOtaArtifact_t *artifact);

esp_err_t ArtifactCache_Read(void *context,
                             uint32_t offset,
                             uint8_t *buffer,
                             size_t length);

#endif
