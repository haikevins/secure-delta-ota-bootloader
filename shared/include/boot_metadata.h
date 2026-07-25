#ifndef BOOT_METADATA_H
#define BOOT_METADATA_H

#include "project_types.h"

typedef enum
{
    UPDATE_IDLE = 0,
    UPDATE_RECEIVING,
    UPDATE_ARTIFACT_READY,
    UPDATE_VERIFYING_CONTAINER,
    UPDATE_VERIFYING_BASE,
    UPDATE_PATCHING,
    UPDATE_IMAGE_READY,
    UPDATE_BACKING_UP,
    UPDATE_INSTALLING,
    UPDATE_VERIFYING_INSTALL,
    UPDATE_TRIAL_BOOT,
    UPDATE_CONFIRMED,
    UPDATE_ROLLBACK,
    UPDATE_FAILED
} UpdateState_t;

typedef struct
{
    uint32_t magic;
    uint32_t generation;
    uint32_t metadata_version;
    uint32_t state;
    uint32_t active_version;
    uint32_t pending_version;
    uint32_t active_update_id;
    uint32_t received_size;
    uint32_t expected_size;
    uint32_t copy_offset;
    uint32_t boot_attempts;
    uint32_t last_error;
    uint32_t crc32;
} BootMetadata_t;

#endif
