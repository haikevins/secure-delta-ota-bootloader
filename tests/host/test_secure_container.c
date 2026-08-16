#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "firmware_container.h"

static uint8_t *ReadFile(const char *path, size_t *size_out)
{
    FILE *stream = fopen(path, "rb");
    uint8_t *data;
    long size;

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

int main(int argc, char **argv)
{
    uint8_t *data;
    size_t size;
    FirmwareContainerInfo_t info;
    FirmwareContainerStatus_t status;

    if (argc != 2)
    {
        fprintf(stderr, "usage: %s container.sdot\n", argv[0]);
        return 2;
    }

    data = ReadFile(argv[1], &size);
    if ((data == NULL) || (size < FW_CONTAINER_HEADER_SIZE))
    {
        fprintf(stderr, "failed to load container\n");
        free(data);
        return 3;
    }

    status = FirmwareContainer_Parse(
        data,
        (uint32_t)size,
        &info);
    if (status != FW_CONTAINER_VALID)
    {
        fprintf(stderr, "container parse failed: %d\n", (int)status);
        free(data);
        return 4;
    }

    if ((info.total_size != size) ||
        (info.payload_offset != FW_CONTAINER_HEADER_SIZE) ||
        (info.header.signature_algorithm != FW_SIGNATURE_ECDSA_P256) ||
        (info.header.signature_size != FW_ECDSA_P256_RAW_SIGNATURE_SIZE) ||
        (info.extension.key_id == 0UL))
    {
        fprintf(stderr, "parsed container fields inconsistent\n");
        free(data);
        return 5;
    }

    printf(
        "signed secure container canonical container parser: PASS "
        "type=%lu base=v%lu target=v%lu payload=%lu total=%lu key=0x%08lX\n",
        (unsigned long)info.header.image_type,
        (unsigned long)info.header.base_version,
        (unsigned long)info.header.target_version,
        (unsigned long)info.header.payload_size,
        (unsigned long)info.total_size,
        (unsigned long)info.extension.key_id);

    free(data);
    return 0;
}
