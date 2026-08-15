#ifndef EXTERNAL_FLASH_STORAGE_H
#define EXTERNAL_FLASH_STORAGE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    EXTERNAL_FLASH_PARTITION_METADATA_A = 0,
    EXTERNAL_FLASH_PARTITION_METADATA_B,
    EXTERNAL_FLASH_PARTITION_INCOMING,
    EXTERNAL_FLASH_PARTITION_RECONSTRUCTED,
    EXTERNAL_FLASH_PARTITION_BACKUP,
    EXTERNAL_FLASH_PARTITION_LOG,
    EXTERNAL_FLASH_PARTITION_SELF_TEST,
    EXTERNAL_FLASH_PARTITION_COUNT
} ExternalFlashPartition_t;

typedef struct
{
    uint32_t address;
    uint32_t size;
} ExternalFlashPartitionInfo_t;

bool ExternalFlashStorage_Init(void);
bool ExternalFlashStorage_GetPartition(ExternalFlashPartition_t partition,
                                       ExternalFlashPartitionInfo_t *info);
bool ExternalFlashStorage_Read(ExternalFlashPartition_t partition,
                               uint32_t offset, uint8_t *data, uint32_t length);
bool ExternalFlashStorage_Write(ExternalFlashPartition_t partition,
                                uint32_t offset, const uint8_t *data, uint32_t length);
bool ExternalFlashStorage_Verify(ExternalFlashPartition_t partition,
                                 uint32_t offset, const uint8_t *data, uint32_t length);
bool ExternalFlashStorage_ErasePartition(ExternalFlashPartition_t partition);
bool ExternalFlashStorage_EraseRange(ExternalFlashPartition_t partition,
                                     uint32_t offset,
                                     uint32_t length);
bool ExternalFlashStorage_IsErased(ExternalFlashPartition_t partition,
                                   uint32_t offset, uint32_t length);

#endif
