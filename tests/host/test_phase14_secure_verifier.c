#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "external_flash_storage.h"
#include "firmware_container.h"
#include "secure_container.h"
#include "phase14_trusted_key.h"

static uint8_t *g_incoming;
static size_t g_incoming_size;

static uint8_t *ReadFile(const char *path, size_t *size_out)
{
    FILE *stream = fopen(path, "rb");
    uint8_t *data;
    long size;

    if (stream == NULL) return NULL;
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

    if ((size != 0L) &&
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

bool ExternalFlashStorage_Init(void) { return true; }

bool ExternalFlashStorage_Read(ExternalFlashPartition_t partition,
                               uint32_t offset,
                               uint8_t *data,
                               uint32_t length)
{
    if ((partition != EXTERNAL_FLASH_PARTITION_INCOMING) ||
        (g_incoming == NULL) ||
        ((size_t)offset > g_incoming_size) ||
        ((size_t)length > (g_incoming_size - (size_t)offset)))
    {
        return false;
    }

    memcpy(data, g_incoming + offset, length);
    return true;
}

int main(int argc, char **argv)
{
    FirmwareContainerInfo_t info;
    SecureContainerStatus_t status;
    uint8_t *original;
    size_t size;
    uint8_t saved;

    if (argc != 2)
    {
        fprintf(stderr, "usage: %s container.sdot\n", argv[0]);
        return 2;
    }

    original = ReadFile(argv[1], &size);
    if (original == NULL)
    {
        return 3;
    }

    g_incoming = original;
    g_incoming_size = size;

    status = SecureContainer_LoadVerifiedInfo(
        (uint32_t)size,
        &info);
    if (status != SECURE_CONTAINER_OK)
    {
        fprintf(stderr, "valid secure container rejected: %d\n", (int)status);
        free(original);
        return 4;
    }

    saved = original[size - 1U];
    original[size - 1U] ^= 0x01U;

    status = SecureContainer_LoadVerifiedInfo(
        (uint32_t)size,
        &info);
    original[size - 1U] = saved;

    if (status != SECURE_CONTAINER_SIGNATURE_INVALID)
    {
        fprintf(stderr, "signature tamper status=%d\n", (int)status);
        free(original);
        return 5;
    }

    saved = original[FW_CONTAINER_FIXED_HEADER_SIZE + 8U];
    original[FW_CONTAINER_FIXED_HEADER_SIZE + 8U] ^= 0x01U;

    status = SecureContainer_LoadVerifiedInfo(
        (uint32_t)size,
        &info);
    original[FW_CONTAINER_FIXED_HEADER_SIZE + 8U] = saved;

    if (status != SECURE_CONTAINER_KEY_ID_MISMATCH)
    {
        fprintf(stderr, "key-id tamper status=%d\n", (int)status);
        free(original);
        return 6;
    }

    printf(
        "Phase 14 exact bootloader secure-envelope verification: PASS "
        "target=v%lu key=0x%08lX\n",
        (unsigned long)info.header.target_version,
        (unsigned long)PHASE14_TRUSTED_KEY_ID);

    free(original);
    return 0;
}
