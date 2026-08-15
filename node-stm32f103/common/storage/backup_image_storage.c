#include "backup_image_storage.h"

#include <stddef.h>
#include <stdint.h>

#include "external_flash_storage.h"
#include "memory_map.h"

static uint8_t RecordsEqual(const BackupImageRecord_t *left,
                            const BackupImageRecord_t *right)
{
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;
    uint32_t i;

    for (i = 0UL; i < sizeof(*left); ++i)
    {
        if (a[i] != b[i])
        {
            return 0U;
        }
    }
    return 1U;
}

BackupImageStorageStatus_t BackupImageStorage_LoadHeader(
    BackupImageRecord_t *record)
{
    if (record == (BackupImageRecord_t *)0)
    {
        return BACKUP_IMAGE_STORAGE_INVALID_ARGUMENT;
    }

    if (!ExternalFlashStorage_Read(EXTERNAL_FLASH_PARTITION_BACKUP,
                                   0UL,
                                   (uint8_t *)record,
                                   sizeof(*record)))
    {
        return BACKUP_IMAGE_STORAGE_READ_FAILED;
    }

    if (BackupImage_Validate(record) != BACKUP_IMAGE_VALID)
    {
        return BACKUP_IMAGE_STORAGE_NOT_FOUND;
    }

    return BACKUP_IMAGE_STORAGE_OK;
}

BackupImageStorageStatus_t BackupImageStorage_InvalidateHeader(void)
{
    if (!ExternalFlashStorage_EraseRange(EXTERNAL_FLASH_PARTITION_BACKUP,
                                         0UL,
                                         EXT_FLASH_SECTOR_SIZE))
    {
        return BACKUP_IMAGE_STORAGE_ERASE_FAILED;
    }

    return BACKUP_IMAGE_STORAGE_OK;
}

BackupImageStorageStatus_t BackupImageStorage_CommitHeader(
    const BackupImageRecord_t *requested,
    BackupImageRecord_t *committed)
{
    BackupImageRecord_t candidate;
    BackupImageRecord_t verify;
    const uint32_t crc_offset =
        (uint32_t)offsetof(BackupImageRecord_t, crc32);

    if (requested == (const BackupImageRecord_t *)0)
    {
        return BACKUP_IMAGE_STORAGE_INVALID_ARGUMENT;
    }

    candidate = *requested;
    BackupImage_Finalize(&candidate);
    if (BackupImage_Validate(&candidate) != BACKUP_IMAGE_VALID)
    {
        return BACKUP_IMAGE_STORAGE_INVALID_ARGUMENT;
    }

    /*
     * Header lives in its own 4 KiB sector. Backup image data starts at
     * BACKUP_IMAGE_DATA_OFFSET, so a torn header may be safely erased/retried
     * without touching the completed backup bytes.
     */
    if (BackupImageStorage_InvalidateHeader() != BACKUP_IMAGE_STORAGE_OK)
    {
        return BACKUP_IMAGE_STORAGE_ERASE_FAILED;
    }

    if (!ExternalFlashStorage_Write(EXTERNAL_FLASH_PARTITION_BACKUP,
                                    0UL,
                                    (const uint8_t *)&candidate,
                                    crc_offset) ||
        !ExternalFlashStorage_Write(EXTERNAL_FLASH_PARTITION_BACKUP,
                                    crc_offset,
                                    (const uint8_t *)&candidate.crc32,
                                    sizeof(candidate.crc32)))
    {
        return BACKUP_IMAGE_STORAGE_WRITE_FAILED;
    }

    if (!ExternalFlashStorage_Read(EXTERNAL_FLASH_PARTITION_BACKUP,
                                   0UL,
                                   (uint8_t *)&verify,
                                   sizeof(verify)) ||
        (BackupImage_Validate(&verify) != BACKUP_IMAGE_VALID) ||
        (RecordsEqual(&candidate, &verify) == 0U))
    {
        return BACKUP_IMAGE_STORAGE_VERIFY_FAILED;
    }

    if (committed != (BackupImageRecord_t *)0)
    {
        *committed = verify;
    }

    return BACKUP_IMAGE_STORAGE_OK;
}

static bool ImageRangeValid(uint32_t offset, uint32_t length)
{
    if (offset > APPLICATION_MAX_SIZE)
    {
        return false;
    }

    return length <= (APPLICATION_MAX_SIZE - offset);
}

bool BackupImageStorage_Read(uint32_t offset,
                             uint8_t *data,
                             uint32_t length)
{
    if (!ImageRangeValid(offset, length))
    {
        return false;
    }

    return ExternalFlashStorage_Read(EXTERNAL_FLASH_PARTITION_BACKUP,
                                     BACKUP_IMAGE_DATA_OFFSET + offset,
                                     data,
                                     length);
}

bool BackupImageStorage_Write(uint32_t offset,
                              const uint8_t *data,
                              uint32_t length)
{
    if (!ImageRangeValid(offset, length))
    {
        return false;
    }

    return ExternalFlashStorage_Write(EXTERNAL_FLASH_PARTITION_BACKUP,
                                      BACKUP_IMAGE_DATA_OFFSET + offset,
                                      data,
                                      length);
}

bool BackupImageStorage_Verify(uint32_t offset,
                               const uint8_t *data,
                               uint32_t length)
{
    if (!ImageRangeValid(offset, length))
    {
        return false;
    }

    return ExternalFlashStorage_Verify(EXTERNAL_FLASH_PARTITION_BACKUP,
                                       BACKUP_IMAGE_DATA_OFFSET + offset,
                                       data,
                                       length);
}

bool BackupImageStorage_EraseSector(uint32_t image_offset)
{
    const uint32_t partition_offset = BACKUP_IMAGE_DATA_OFFSET + image_offset;

    if ((image_offset >= APPLICATION_MAX_SIZE) ||
        ((image_offset & (EXT_FLASH_SECTOR_SIZE - 1UL)) != 0UL))
    {
        return false;
    }

    return ExternalFlashStorage_EraseRange(EXTERNAL_FLASH_PARTITION_BACKUP,
                                           partition_offset,
                                           EXT_FLASH_SECTOR_SIZE);
}
