#ifndef BACKUP_IMAGE_STORAGE_H
#define BACKUP_IMAGE_STORAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "backup_image.h"

typedef enum
{
    BACKUP_IMAGE_STORAGE_OK = 0,
    BACKUP_IMAGE_STORAGE_INVALID_ARGUMENT,
    BACKUP_IMAGE_STORAGE_NOT_FOUND,
    BACKUP_IMAGE_STORAGE_READ_FAILED,
    BACKUP_IMAGE_STORAGE_ERASE_FAILED,
    BACKUP_IMAGE_STORAGE_WRITE_FAILED,
    BACKUP_IMAGE_STORAGE_VERIFY_FAILED
} BackupImageStorageStatus_t;

BackupImageStorageStatus_t BackupImageStorage_LoadHeader(
    BackupImageRecord_t *record);
BackupImageStorageStatus_t BackupImageStorage_CommitHeader(
    const BackupImageRecord_t *requested,
    BackupImageRecord_t *committed);
BackupImageStorageStatus_t BackupImageStorage_InvalidateHeader(void);

bool BackupImageStorage_Read(uint32_t offset,
                             uint8_t *data,
                             uint32_t length);
bool BackupImageStorage_Write(uint32_t offset,
                              const uint8_t *data,
                              uint32_t length);
bool BackupImageStorage_Verify(uint32_t offset,
                               const uint8_t *data,
                               uint32_t length);
bool BackupImageStorage_EraseSector(uint32_t image_offset);

#endif
