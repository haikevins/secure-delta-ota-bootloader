#include "external_flash_storage.h"

#include "ext_flash_layout.h"
#include "memory_map.h"
#include "spi_flash.h"

static const ExternalFlashPartitionInfo_t g_partitions[EXTERNAL_FLASH_PARTITION_COUNT] =
{
    { EXT_METADATA_A_ADDRESS,    EXT_FLASH_SECTOR_SIZE },
    { EXT_METADATA_B_ADDRESS,    EXT_FLASH_SECTOR_SIZE },
    { EXT_INCOMING_ADDRESS,      EXT_INCOMING_SIZE },
    { EXT_RECONSTRUCTED_ADDRESS, EXT_RECONSTRUCTED_SIZE },
    { EXT_BACKUP_ADDRESS,        EXT_BACKUP_SIZE },
    { EXT_LOG_ADDRESS,           EXT_LOG_SIZE },
    { EXT_SELF_TEST_ADDRESS,     EXT_SELF_TEST_SIZE }
};

static bool ResolveRange(ExternalFlashPartition_t partition,
                         uint32_t offset,
                         uint32_t length,
                         uint32_t *absolute_address)
{
    ExternalFlashPartitionInfo_t info;
    if ((absolute_address == (uint32_t *)0) ||
        !ExternalFlashStorage_GetPartition(partition, &info))
    {
        return false;
    }
    if (!ExtFlash_IsRangeValid(info.size, offset, length)) { return false; }
    *absolute_address = info.address + offset;
    return true;
}

bool ExternalFlashStorage_Init(void) { return SpiFlash_Init(); }

bool ExternalFlashStorage_GetPartition(ExternalFlashPartition_t partition,
                                       ExternalFlashPartitionInfo_t *info)
{
    if ((info == (ExternalFlashPartitionInfo_t *)0) ||
        ((uint32_t)partition >= (uint32_t)EXTERNAL_FLASH_PARTITION_COUNT))
    {
        return false;
    }
    *info = g_partitions[(uint32_t)partition];
    return true;
}

bool ExternalFlashStorage_Read(ExternalFlashPartition_t partition,
                               uint32_t offset, uint8_t *data, uint32_t length)
{
    uint32_t address;
    return ResolveRange(partition, offset, length, &address) &&
           SpiFlash_Read(address, data, length);
}

bool ExternalFlashStorage_Write(ExternalFlashPartition_t partition,
                                uint32_t offset, const uint8_t *data, uint32_t length)
{
    uint32_t address;
    return ResolveRange(partition, offset, length, &address) &&
           SpiFlash_Write(address, data, length);
}

bool ExternalFlashStorage_Verify(ExternalFlashPartition_t partition,
                                 uint32_t offset, const uint8_t *data, uint32_t length)
{
    uint32_t address;
    return ResolveRange(partition, offset, length, &address) &&
           SpiFlash_Verify(address, data, length);
}

bool ExternalFlashStorage_ErasePartition(ExternalFlashPartition_t partition)
{
    ExternalFlashPartitionInfo_t info;
    uint32_t offset;

    if (!ExternalFlashStorage_GetPartition(partition, &info)) { return false; }
    if (!ExtFlash_IsSectorAligned(info.address) ||
        ((info.size & (EXT_FLASH_SECTOR_SIZE - 1UL)) != 0UL))
    {
        return false;
    }

    for (offset = 0UL; offset < info.size; offset += EXT_FLASH_SECTOR_SIZE)
    {
        if (!SpiFlash_EraseSector(info.address + offset)) { return false; }
    }
    return true;
}

bool ExternalFlashStorage_IsErased(ExternalFlashPartition_t partition,
                                   uint32_t offset, uint32_t length)
{
    uint32_t address;
    return ResolveRange(partition, offset, length, &address) &&
           SpiFlash_IsErased(address, length);
}
