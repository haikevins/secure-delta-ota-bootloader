#include "artifact_cache.h"

#include <stddef.h>
#include <string.h>

#include "esp_partition.h"

#include "uart_ota_protocol.h"

#define ARTIFACT_CACHE_ERASE_SIZE 4096UL

_Static_assert(sizeof(ArtifactCacheHeader_t) == 36U,
               "artifact cache header layout changed");
_Static_assert(offsetof(ArtifactCacheHeader_t, header_crc32) == 32U,
               "cache CRC must be final word");

static const esp_partition_t *FindPartition(void)
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                    ESP_PARTITION_SUBTYPE_ANY,
                                    ARTIFACT_CACHE_PARTITION_LABEL);
}

static uint32_t HeaderCrc(const ArtifactCacheHeader_t *header)
{
    return UartOta_Crc32(
        header,
        offsetof(ArtifactCacheHeader_t, header_crc32));
}

static uint32_t CrcUpdate(uint32_t running,
                          const uint8_t *data,
                          size_t length)
{
    for (size_t i = 0U; i < length; ++i)
    {
        running ^= data[i];
        for (uint32_t bit = 0UL; bit < 8UL; ++bit)
        {
            const uint32_t mask =
                (uint32_t)(0UL - (running & 1UL));
            running = (running >> 1U) ^
                      (0xEDB88320UL & mask);
        }
    }
    return running;
}

static bool HeaderValid(const esp_partition_t *partition,
                        const ArtifactCacheHeader_t *header)
{
    if ((partition == NULL) || (header == NULL))
    {
        return false;
    }

    if ((header->magic != ARTIFACT_CACHE_MAGIC) ||
        (header->format_version != ARTIFACT_CACHE_FORMAT_VERSION) ||
        (header->header_size != sizeof(*header)) ||
        (header->update_id == 0UL) ||
        (header->target_version == 0UL) ||
        (header->image_size == 0UL) ||
        (header->data_offset != ARTIFACT_CACHE_DATA_OFFSET) ||
        (header->header_crc32 != HeaderCrc(header)))
    {
        return false;
    }

    return header->image_size <=
           (partition->size - ARTIFACT_CACHE_DATA_OFFSET);
}

static esp_err_t CalculateStoredImageCrc(const esp_partition_t *partition,
                                         uint32_t image_size,
                                         uint32_t *crc_out)
{
    uint8_t buffer[256];
    uint32_t offset = 0UL;
    uint32_t running = 0xFFFFFFFFUL;

    if ((partition == NULL) || (crc_out == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    while (offset < image_size)
    {
        size_t chunk = image_size - offset;
        if (chunk > sizeof(buffer))
        {
            chunk = sizeof(buffer);
        }

        const esp_err_t status = esp_partition_read(
            partition,
            ARTIFACT_CACHE_DATA_OFFSET + offset,
            buffer,
            chunk);
        if (status != ESP_OK)
        {
            return status;
        }

        running = CrcUpdate(running, buffer, chunk);
        offset += (uint32_t)chunk;
    }

    *crc_out = running ^ 0xFFFFFFFFUL;
    return ESP_OK;
}

esp_err_t ArtifactCache_Open(ArtifactCache_t *cache)
{
    const esp_partition_t *partition;
    ArtifactCacheHeader_t header;
    uint32_t stored_crc = 0UL;
    esp_err_t status;

    if (cache == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    partition = FindPartition();
    if (partition == NULL)
    {
        return ESP_ERR_NOT_FOUND;
    }

    status = esp_partition_read(partition,
                                0UL,
                                &header,
                                sizeof(header));
    if (status != ESP_OK)
    {
        return status;
    }

    if (!HeaderValid(partition, &header))
    {
        return ESP_ERR_INVALID_CRC;
    }

    status = CalculateStoredImageCrc(partition,
                                     header.image_size,
                                     &stored_crc);
    if (status != ESP_OK)
    {
        return status;
    }

    if (stored_crc != header.image_crc32)
    {
        return ESP_ERR_INVALID_CRC;
    }

    cache->partition = partition;
    cache->header = header;
    return ESP_OK;
}

esp_err_t ArtifactCache_BeginWrite(ArtifactCacheWriter_t *writer,
                                   uint32_t expected_size,
                                   uint32_t update_id,
                                   uint32_t target_version)
{
    const esp_partition_t *partition;
    size_t erase_size;
    esp_err_t status;

    if ((writer == NULL) || (expected_size == 0UL) ||
        (update_id == 0UL) || (target_version == 0UL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    partition = FindPartition();
    if (partition == NULL)
    {
        return ESP_ERR_NOT_FOUND;
    }

    if (expected_size >
        (partition->size - ARTIFACT_CACHE_DATA_OFFSET))
    {
        return ESP_ERR_INVALID_SIZE;
    }

    memset(writer, 0, sizeof(*writer));
    writer->partition = partition;
    writer->update_id = update_id;
    writer->target_version = target_version;
    writer->expected_size = expected_size;
    writer->running_crc32 = 0xFFFFFFFFUL;

    /*
     * Invalidate the old published header before touching data. A power cut
     * after this point can leave bytes in the data area, but ArtifactCache_Open
     * cannot accept them until a new verified header is committed.
     */
    status = esp_partition_erase_range(partition,
                                       0UL,
                                       ARTIFACT_CACHE_ERASE_SIZE);
    if (status != ESP_OK)
    {
        return status;
    }

    erase_size =
        ((size_t)expected_size + ARTIFACT_CACHE_ERASE_SIZE - 1U) &
        ~((size_t)ARTIFACT_CACHE_ERASE_SIZE - 1U);

    status = esp_partition_erase_range(partition,
                                       ARTIFACT_CACHE_DATA_OFFSET,
                                       erase_size);
    if (status != ESP_OK)
    {
        return status;
    }

    writer->active = true;
    return ESP_OK;
}

esp_err_t ArtifactCache_Write(ArtifactCacheWriter_t *writer,
                              const uint8_t *data,
                              size_t length)
{
    const esp_partition_t *partition;
    esp_err_t status;

    if ((writer == NULL) ||
        ((data == NULL) && (length != 0U)))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!writer->active)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (length >
        ((size_t)writer->expected_size - writer->written_size))
    {
        return ESP_ERR_INVALID_SIZE;
    }

    if (length == 0U)
    {
        return ESP_OK;
    }

    partition = (const esp_partition_t *)writer->partition;
    if (partition == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    status = esp_partition_write(
        partition,
        ARTIFACT_CACHE_DATA_OFFSET + writer->written_size,
        data,
        length);
    if (status != ESP_OK)
    {
        return status;
    }

    writer->running_crc32 =
        CrcUpdate(writer->running_crc32, data, length);
    writer->written_size += (uint32_t)length;
    return ESP_OK;
}

esp_err_t ArtifactCache_Commit(ArtifactCacheWriter_t *writer,
                               ArtifactCache_t *cache)
{
    const esp_partition_t *partition;
    ArtifactCacheHeader_t header;
    uint32_t stored_crc = 0UL;
    uint32_t streamed_crc;
    esp_err_t status;

    if ((writer == NULL) || (cache == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!writer->active)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (writer->written_size != writer->expected_size)
    {
        return ESP_ERR_INVALID_SIZE;
    }

    partition = (const esp_partition_t *)writer->partition;
    if (partition == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    streamed_crc = writer->running_crc32 ^ 0xFFFFFFFFUL;

    /*
     * Read the complete stored artifact back before publishing its header.
     * This catches an interrupted/failed flash write without retaining the
     * whole HTTPS payload in RAM.
     */
    status = CalculateStoredImageCrc(partition,
                                     writer->expected_size,
                                     &stored_crc);
    if (status != ESP_OK)
    {
        return status;
    }
    if (stored_crc != streamed_crc)
    {
        return ESP_ERR_INVALID_CRC;
    }

    memset(&header, 0, sizeof(header));
    header.magic = ARTIFACT_CACHE_MAGIC;
    header.format_version = ARTIFACT_CACHE_FORMAT_VERSION;
    header.header_size = sizeof(header);
    header.update_id = writer->update_id;
    header.target_version = writer->target_version;
    header.image_size = writer->expected_size;
    header.image_crc32 = stored_crc;
    header.data_offset = ARTIFACT_CACHE_DATA_OFFSET;
    header.header_crc32 = HeaderCrc(&header);

    status = esp_partition_write(partition,
                                 0UL,
                                 &header,
                                 sizeof(header));
    if (status != ESP_OK)
    {
        return status;
    }

    status = ArtifactCache_Open(cache);
    if (status != ESP_OK)
    {
        /*
         * A header that cannot pass the final open/readback validation must
         * never remain published.
         */
        (void)esp_partition_erase_range(
            partition, 0UL, ARTIFACT_CACHE_ERASE_SIZE);
        writer->active = false;
        return status;
    }

    writer->active = false;
    return ESP_OK;
}

esp_err_t ArtifactCache_Abort(ArtifactCacheWriter_t *writer)
{
    const esp_partition_t *partition;
    esp_err_t status = ESP_OK;

    if (writer == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    partition = (const esp_partition_t *)writer->partition;

    if ((partition != NULL) && writer->active)
    {
        status = esp_partition_erase_range(
            partition, 0UL, ARTIFACT_CACHE_ERASE_SIZE);
    }

    writer->active = false;
    return status;
}

esp_err_t ArtifactCache_Seed(ArtifactCache_t *cache,
                             const uint8_t *image,
                             size_t image_size,
                             uint32_t update_id,
                             uint32_t target_version)
{
    ArtifactCacheWriter_t writer;
    esp_err_t status;

    if ((cache == NULL) || (image == NULL) ||
        (image_size == 0U) ||
        (image_size > UINT32_MAX))
    {
        return ESP_ERR_INVALID_ARG;
    }

    status = ArtifactCache_BeginWrite(&writer,
                                      (uint32_t)image_size,
                                      update_id,
                                      target_version);
    if (status != ESP_OK)
    {
        return status;
    }

    status = ArtifactCache_Write(&writer, image, image_size);
    if (status != ESP_OK)
    {
        (void)ArtifactCache_Abort(&writer);
        return status;
    }

    status = ArtifactCache_Commit(&writer, cache);
    if (status != ESP_OK)
    {
        (void)ArtifactCache_Abort(&writer);
    }
    return status;
}

esp_err_t ArtifactCache_Read(void *context,
                             uint32_t offset,
                             uint8_t *buffer,
                             size_t length)
{
    ArtifactCache_t *cache = (ArtifactCache_t *)context;
    const esp_partition_t *partition;

    if ((cache == NULL) || (buffer == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if ((offset > cache->header.image_size) ||
        (length > (cache->header.image_size - offset)))
    {
        return ESP_ERR_INVALID_SIZE;
    }

    partition = (const esp_partition_t *)cache->partition;
    if (partition == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    return esp_partition_read(partition,
                              cache->header.data_offset + offset,
                              buffer,
                              length);
}

esp_err_t ArtifactCache_AsUartArtifact(ArtifactCache_t *cache,
                                      UartOtaArtifact_t *artifact)
{
    if ((cache == NULL) || (artifact == NULL) ||
        (cache->partition == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    artifact->update_id = cache->header.update_id;
    artifact->target_version = cache->header.target_version;
    artifact->image_size = cache->header.image_size;
    artifact->image_crc32 = cache->header.image_crc32;
    artifact->read = ArtifactCache_Read;
    artifact->read_context = cache;
    return ESP_OK;
}
