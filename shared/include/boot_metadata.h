#ifndef BOOT_METADATA_H
#define BOOT_METADATA_H

#include "project_types.h"

#define BOOT_METADATA_MAGIC              0x424D4554UL /* "BMET" */
#define BOOT_METADATA_FORMAT_VERSION     1UL
#define BOOT_METADATA_FIRST_GENERATION   1UL
#define BOOT_METADATA_MAX_BOOT_ATTEMPTS  3UL

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

typedef enum
{
    BOOT_METADATA_VALID = 0,
    BOOT_METADATA_INVALID_ARGUMENT,
    BOOT_METADATA_INVALID_MAGIC,
    BOOT_METADATA_INVALID_VERSION,
    BOOT_METADATA_INVALID_GENERATION,
    BOOT_METADATA_INVALID_STATE,
    BOOT_METADATA_INVALID_PROGRESS,
    BOOT_METADATA_INVALID_STATE_FIELDS,
    BOOT_METADATA_INVALID_CRC
} BootMetadataValidationStatus_t;

typedef enum
{
    BOOT_METADATA_SLOT_NONE = 0,
    BOOT_METADATA_SLOT_A,
    BOOT_METADATA_SLOT_B
} BootMetadataSlot_t;

void BootMetadata_Init(BootMetadata_t *metadata, uint32_t active_version);
uint32_t BootMetadata_CalculateCrc(const BootMetadata_t *metadata);
void BootMetadata_Finalize(BootMetadata_t *metadata);
BootMetadataValidationStatus_t BootMetadata_Validate(
    const BootMetadata_t *metadata);
uint8_t BootMetadata_IsGenerationNewer(uint32_t candidate,
                                       uint32_t reference);
uint32_t BootMetadata_NextGeneration(uint32_t current_generation);
BootMetadataSlot_t BootMetadata_SelectNewestSlot(
    const BootMetadata_t *slot_a,
    const BootMetadata_t *slot_b);
BootMetadataSlot_t BootMetadata_SelectWriteSlot(
    const BootMetadata_t *slot_a,
    const BootMetadata_t *slot_b);

#endif
