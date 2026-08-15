#include "backup_progress.h"

#include "memory_map.h"

BackupProgressStatus_t BackupProgress_Validate(uint32_t copy_offset)
{
    if (copy_offset > APPLICATION_MAX_SIZE)
    {
        return BACKUP_PROGRESS_INVALID_OFFSET;
    }

    /*
     * Backup checkpoints are 4 KiB external-Flash sector boundaries.
     * The final 38 KiB checkpoint is allowed to be sector-unaligned.
     */
    if ((copy_offset != APPLICATION_MAX_SIZE) &&
        ((copy_offset & (EXT_FLASH_SECTOR_SIZE - 1UL)) != 0UL))
    {
        return BACKUP_PROGRESS_INVALID_ALIGNMENT;
    }

    return BACKUP_PROGRESS_VALID;
}

uint32_t BackupProgress_ChunkLength(uint32_t copy_offset)
{
    uint32_t remaining;

    if (BackupProgress_Validate(copy_offset) != BACKUP_PROGRESS_VALID)
    {
        return 0UL;
    }
    if (copy_offset == APPLICATION_MAX_SIZE)
    {
        return 0UL;
    }

    remaining = APPLICATION_MAX_SIZE - copy_offset;
    return (remaining > EXT_FLASH_SECTOR_SIZE)
               ? EXT_FLASH_SECTOR_SIZE
               : remaining;
}

uint32_t BackupProgress_NextCheckpoint(uint32_t copy_offset)
{
    return copy_offset + BackupProgress_ChunkLength(copy_offset);
}
