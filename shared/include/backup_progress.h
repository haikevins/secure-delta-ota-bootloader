#ifndef BACKUP_PROGRESS_H
#define BACKUP_PROGRESS_H

#include <stdint.h>

typedef enum
{
    BACKUP_PROGRESS_VALID = 0,
    BACKUP_PROGRESS_INVALID_OFFSET,
    BACKUP_PROGRESS_INVALID_ALIGNMENT
} BackupProgressStatus_t;

BackupProgressStatus_t BackupProgress_Validate(uint32_t copy_offset);
uint32_t BackupProgress_ChunkLength(uint32_t copy_offset);
uint32_t BackupProgress_NextCheckpoint(uint32_t copy_offset);

#endif
