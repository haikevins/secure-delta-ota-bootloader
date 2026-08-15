#include "backup_image.h"

#include <stddef.h>
#include <stdint.h>

#include "crc32.h"
#include "memory_map.h"

_Static_assert(sizeof(BackupImageRecord_t) == 28U,
               "BackupImageRecord_t persistent layout changed");
_Static_assert(offsetof(BackupImageRecord_t, crc32) == 24U,
               "backup CRC must be the final word");

void BackupImage_Init(BackupImageRecord_t *record,
                      uint32_t active_version,
                      uint32_t image_crc32)
{
    uint8_t *bytes;
    uint32_t index;

    if (record == (BackupImageRecord_t *)0)
    {
        return;
    }

    bytes = (uint8_t *)record;
    for (index = 0UL; index < sizeof(*record); ++index)
    {
        bytes[index] = 0U;
    }

    record->magic = BACKUP_IMAGE_MAGIC;
    record->format_version = BACKUP_IMAGE_FORMAT_VERSION;
    record->active_version = active_version;
    record->image_size = APPLICATION_MAX_SIZE;
    record->image_crc32 = image_crc32;
    record->load_address = APPLICATION_START_ADDRESS;
}

uint32_t BackupImage_CalculateCrc(const BackupImageRecord_t *record)
{
    if (record == (const BackupImageRecord_t *)0)
    {
        return 0UL;
    }

    return Crc32_Calculate(record,
                           (uint32_t)offsetof(BackupImageRecord_t, crc32));
}

void BackupImage_Finalize(BackupImageRecord_t *record)
{
    if (record == (BackupImageRecord_t *)0)
    {
        return;
    }

    record->magic = BACKUP_IMAGE_MAGIC;
    record->format_version = BACKUP_IMAGE_FORMAT_VERSION;
    record->crc32 = BackupImage_CalculateCrc(record);
}

BackupImageValidationStatus_t BackupImage_Validate(
    const BackupImageRecord_t *record)
{
    if (record == (const BackupImageRecord_t *)0)
    {
        return BACKUP_IMAGE_INVALID_ARGUMENT;
    }
    if (record->magic != BACKUP_IMAGE_MAGIC)
    {
        return BACKUP_IMAGE_INVALID_MAGIC;
    }
    if (record->format_version != BACKUP_IMAGE_FORMAT_VERSION)
    {
        return BACKUP_IMAGE_INVALID_VERSION;
    }
    if (record->active_version == 0UL)
    {
        return BACKUP_IMAGE_INVALID_ACTIVE_VERSION;
    }
    if (record->image_size != APPLICATION_MAX_SIZE)
    {
        return BACKUP_IMAGE_INVALID_SIZE;
    }
    if (record->load_address != APPLICATION_START_ADDRESS)
    {
        return BACKUP_IMAGE_INVALID_LOAD_ADDRESS;
    }
    if (record->crc32 != BackupImage_CalculateCrc(record))
    {
        return BACKUP_IMAGE_INVALID_CRC;
    }
    return BACKUP_IMAGE_VALID;
}
