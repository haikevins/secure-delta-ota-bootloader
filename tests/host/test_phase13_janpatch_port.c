#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "delta_patch.h"
#include "external_flash_storage.h"
#include "janpatch_port.h"

const uint8_t *g_janpatch_host_source;

static uint8_t *g_incoming;
static size_t g_incoming_size;
static uint8_t *g_reconstructed;
static size_t g_reconstructed_size;

static uint8_t *ReadFile(const char *path, size_t *size_out)
{
    FILE *stream;
    long size;
    uint8_t *data;

    stream = fopen(path, "rb");
    if (stream == NULL)
    {
        return NULL;
    }

    if ((fseek(stream, 0L, SEEK_END) != 0) ||
        ((size = ftell(stream)) < 0) ||
        (fseek(stream, 0L, SEEK_SET) != 0))
    {
        fclose(stream);
        return NULL;
    }

    data = (uint8_t *)malloc((size_t)size);
    if ((data == NULL) && (size != 0L))
    {
        fclose(stream);
        return NULL;
    }

    if (((size_t)size != 0U) &&
        (fread(data, 1U, (size_t)size, stream) != (size_t)size))
    {
        free(data);
        fclose(stream);
        return NULL;
    }

    fclose(stream);
    *size_out = (size_t)size;
    return data;
}

bool ExternalFlashStorage_Read(ExternalFlashPartition_t partition,
                               uint32_t offset,
                               uint8_t *data,
                               uint32_t length)
{
    const uint8_t *source = NULL;
    size_t size = 0U;

    if (partition == EXTERNAL_FLASH_PARTITION_INCOMING)
    {
        source = g_incoming;
        size = g_incoming_size;
    }
    else if (partition == EXTERNAL_FLASH_PARTITION_RECONSTRUCTED)
    {
        source = g_reconstructed;
        size = g_reconstructed_size;
    }
    else
    {
        return false;
    }

    if ((source == NULL) ||
        ((size_t)offset > size) ||
        ((size_t)length > (size - (size_t)offset)))
    {
        return false;
    }

    memcpy(data, source + offset, length);
    return true;
}

bool ExternalFlashStorage_Write(ExternalFlashPartition_t partition,
                                uint32_t offset,
                                const uint8_t *data,
                                uint32_t length)
{
    if ((partition != EXTERNAL_FLASH_PARTITION_RECONSTRUCTED) ||
        (g_reconstructed == NULL) ||
        ((size_t)offset > g_reconstructed_size) ||
        ((size_t)length >
         (g_reconstructed_size - (size_t)offset)))
    {
        return false;
    }

    memcpy(g_reconstructed + offset, data, length);
    return true;
}

/* Unused storage API stubs required only by the shared header contract. */
bool ExternalFlashStorage_Init(void) { return true; }
bool ExternalFlashStorage_GetPartition(
    ExternalFlashPartition_t partition,
    ExternalFlashPartitionInfo_t *info)
{
    (void)partition;
    (void)info;
    return false;
}
bool ExternalFlashStorage_Verify(ExternalFlashPartition_t partition,
                                 uint32_t offset,
                                 const uint8_t *data,
                                 uint32_t length)
{
    (void)partition;
    (void)offset;
    (void)data;
    (void)length;
    return false;
}
bool ExternalFlashStorage_ErasePartition(
    ExternalFlashPartition_t partition)
{
    (void)partition;
    return false;
}
bool ExternalFlashStorage_EraseRange(
    ExternalFlashPartition_t partition,
    uint32_t offset,
    uint32_t length)
{
    (void)partition;
    (void)offset;
    (void)length;
    return false;
}
bool ExternalFlashStorage_IsErased(
    ExternalFlashPartition_t partition,
    uint32_t offset,
    uint32_t length)
{
    (void)partition;
    (void)offset;
    (void)length;
    return false;
}

int main(int argc, char **argv)
{
    uint8_t *base = NULL;
    uint8_t *artifact = NULL;
    uint8_t *target = NULL;
    size_t base_size = 0U;
    size_t artifact_size = 0U;
    size_t target_size = 0U;
    DeltaPatchHeader_t header;
    JanpatchPortResult_t result;
    int exit_code = 1;

    if (argc != 4)
    {
        fprintf(stderr, "usage: %s base.bin delta.d13 target.bin\n", argv[0]);
        return 2;
    }

    base = ReadFile(argv[1], &base_size);
    artifact = ReadFile(argv[2], &artifact_size);
    target = ReadFile(argv[3], &target_size);

    if ((base == NULL) || (artifact == NULL) || (target == NULL))
    {
        fprintf(stderr, "failed to load test files\n");
        goto cleanup;
    }

    if ((artifact_size < DELTA_PATCH_HEADER_SIZE) ||
        (DeltaPatch_ParseHeader(
             artifact,
             (uint32_t)artifact_size,
             &header) != DELTA_PATCH_HEADER_VALID))
    {
        fprintf(stderr, "D13P header validation failed\n");
        goto cleanup;
    }

    if ((base_size != header.base_image_size) ||
        (target_size != header.target_image_size))
    {
        fprintf(stderr, "header/image size mismatch\n");
        goto cleanup;
    }

    g_janpatch_host_source = base;
    g_incoming = artifact;
    g_incoming_size = artifact_size;
    g_reconstructed_size = target_size;
    g_reconstructed = (uint8_t *)malloc(target_size);

    if (g_reconstructed == NULL)
    {
        fprintf(stderr, "allocation failed\n");
        goto cleanup;
    }
    memset(g_reconstructed, 0xFF, target_size);

    if (JanpatchPort_Apply(&header, &result) != JANPATCH_PORT_OK)
    {
        fprintf(stderr, "embedded janpatch port returned failure\n");
        goto cleanup;
    }

    if ((result.patch_position != header.patch_size) ||
        (result.target_position != header.target_image_size) ||
        (memcmp(g_reconstructed, target, target_size) != 0))
    {
        fprintf(stderr, "embedded reconstructed target mismatch\n");
        goto cleanup;
    }

    printf(
        "Phase 13 embedded JanpatchPort host reconstruction: PASS "
        "ops=%lu eql=%lu mod=%lu ins=%lu del=%lu bkt=%lu\n",
        (unsigned long)result.operation_count,
        (unsigned long)result.equal_bytes,
        (unsigned long)result.modified_bytes,
        (unsigned long)result.inserted_bytes,
        (unsigned long)result.deleted_bytes,
        (unsigned long)result.backward_bytes);

    exit_code = 0;

cleanup:
    free(g_reconstructed);
    free(target);
    free(artifact);
    free(base);
    return exit_code;
}
