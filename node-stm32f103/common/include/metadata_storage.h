#ifndef METADATA_STORAGE_H
#define METADATA_STORAGE_H

#include "boot_metadata.h"

typedef enum
{
    METADATA_STORAGE_OK = 0,
    METADATA_STORAGE_DEFAULTS_USED,
    METADATA_STORAGE_INVALID_ARGUMENT,
    METADATA_STORAGE_ERASE_FAILED,
    METADATA_STORAGE_PROGRAM_FAILED,
    METADATA_STORAGE_VERIFY_FAILED
} MetadataStorageStatus_t;

MetadataStorageStatus_t MetadataStorage_Load(BootMetadata_t *metadata,
                                             BootMetadataSlot_t *active_slot);

MetadataStorageStatus_t MetadataStorage_Commit(
    const BootMetadata_t *requested,
    BootMetadata_t *committed,
    BootMetadataSlot_t *written_slot);

BootMetadataValidationStatus_t MetadataStorage_ReadSlot(
    BootMetadataSlot_t slot,
    BootMetadata_t *metadata);

#endif
