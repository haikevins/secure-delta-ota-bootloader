#ifndef DOWNLOAD_CHECKPOINT_STORAGE_H
#define DOWNLOAD_CHECKPOINT_STORAGE_H

#include "download_checkpoint.h"

typedef enum
{
    DOWNLOAD_CHECKPOINT_STORAGE_OK = 0,
    DOWNLOAD_CHECKPOINT_STORAGE_INVALID_ARGUMENT,
    DOWNLOAD_CHECKPOINT_STORAGE_NOT_FOUND,
    DOWNLOAD_CHECKPOINT_STORAGE_READ_FAILED,
    DOWNLOAD_CHECKPOINT_STORAGE_ERASE_FAILED,
    DOWNLOAD_CHECKPOINT_STORAGE_WRITE_FAILED,
    DOWNLOAD_CHECKPOINT_STORAGE_VERIFY_FAILED
} DownloadCheckpointStorageStatus_t;

DownloadCheckpointStorageStatus_t DownloadCheckpointStorage_Load(
    DownloadCheckpointRecord_t *record,
    DownloadCheckpointSlot_t *slot);
DownloadCheckpointStorageStatus_t DownloadCheckpointStorage_Commit(
    const DownloadCheckpointRecord_t *requested,
    DownloadCheckpointRecord_t *committed,
    DownloadCheckpointSlot_t *written_slot);
DownloadCheckpointStorageStatus_t DownloadCheckpointStorage_Clear(void);

#endif
