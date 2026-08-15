#include "boot_metadata.h"

#include <stddef.h>
#include <stdint.h>

#include "crc32.h"
#include "memory_map.h"

_Static_assert(sizeof(BootMetadata_t) == 52U,
               "BootMetadata_t layout changed; update persistent format");
_Static_assert(offsetof(BootMetadata_t, crc32) == 48U,
               "BootMetadata_t CRC must remain the final field");

static uint8_t BootMetadata_StateIsValid(uint32_t state)
{
    return (uint8_t)(state <= (uint32_t)UPDATE_FAILED);
}

static uint8_t BootMetadata_UpdatePayloadIsComplete(
    const BootMetadata_t *metadata)
{
    return (uint8_t)((metadata->active_update_id != 0UL) &&
                     (metadata->expected_size != 0UL) &&
                     (metadata->received_size == metadata->expected_size));
}

static uint8_t BootMetadata_StateFieldsAreConsistent(
    const BootMetadata_t *metadata)
{
    switch ((UpdateState_t)metadata->state)
    {
        case UPDATE_IDLE:
            return (uint8_t)((metadata->pending_version == 0UL) &&
                             (metadata->active_update_id == 0UL) &&
                             (metadata->received_size == 0UL) &&
                             (metadata->expected_size == 0UL) &&
                             (metadata->copy_offset == 0UL) &&
                             (metadata->boot_attempts == 0UL));

        case UPDATE_RECEIVING:
            return (uint8_t)((metadata->active_update_id != 0UL) &&
                             (metadata->expected_size != 0UL) &&
                             (metadata->pending_version != 0UL));

        case UPDATE_ARTIFACT_READY:
        case UPDATE_VERIFYING_CONTAINER:
        case UPDATE_VERIFYING_BASE:
        case UPDATE_PATCHING:
        case UPDATE_IMAGE_READY:
        case UPDATE_BACKING_UP:
        case UPDATE_INSTALLING:
        case UPDATE_VERIFYING_INSTALL:
            return BootMetadata_UpdatePayloadIsComplete(metadata);

        case UPDATE_TRIAL_BOOT:
            return (uint8_t)((metadata->pending_version != 0UL) &&
                             (metadata->boot_attempts <=
                              BOOT_METADATA_MAX_BOOT_ATTEMPTS));

        case UPDATE_CONFIRMED:
            return (uint8_t)(metadata->pending_version != 0UL);

        case UPDATE_ROLLBACK:
            return (uint8_t)(metadata->active_update_id != 0UL);

        case UPDATE_FAILED:
            return 1U;

        default:
            return 0U;
    }
}

void BootMetadata_Init(BootMetadata_t *metadata, uint32_t active_version)
{
    uint8_t *bytes;
    uint32_t index;

    if (metadata == (BootMetadata_t *)0)
    {
        return;
    }

    bytes = (uint8_t *)metadata;
    for (index = 0UL; index < sizeof(*metadata); ++index)
    {
        bytes[index] = 0U;
    }

    metadata->magic = BOOT_METADATA_MAGIC;
    metadata->metadata_version = BOOT_METADATA_FORMAT_VERSION;
    metadata->state = (uint32_t)UPDATE_IDLE;
    metadata->active_version = active_version;
}

uint32_t BootMetadata_CalculateCrc(const BootMetadata_t *metadata)
{
    if (metadata == (const BootMetadata_t *)0)
    {
        return 0UL;
    }

    return Crc32_Calculate(metadata,
                           (uint32_t)offsetof(BootMetadata_t, crc32));
}

void BootMetadata_Finalize(BootMetadata_t *metadata)
{
    if (metadata == (BootMetadata_t *)0)
    {
        return;
    }

    metadata->magic = BOOT_METADATA_MAGIC;
    metadata->metadata_version = BOOT_METADATA_FORMAT_VERSION;
    metadata->crc32 = BootMetadata_CalculateCrc(metadata);
}

BootMetadataValidationStatus_t BootMetadata_Validate(
    const BootMetadata_t *metadata)
{
    if (metadata == (const BootMetadata_t *)0)
    {
        return BOOT_METADATA_INVALID_ARGUMENT;
    }

    if (metadata->magic != BOOT_METADATA_MAGIC)
    {
        return BOOT_METADATA_INVALID_MAGIC;
    }

    if (metadata->metadata_version != BOOT_METADATA_FORMAT_VERSION)
    {
        return BOOT_METADATA_INVALID_VERSION;
    }

    if (metadata->generation == 0UL)
    {
        return BOOT_METADATA_INVALID_GENERATION;
    }

    if (BootMetadata_StateIsValid(metadata->state) == 0U)
    {
        return BOOT_METADATA_INVALID_STATE;
    }

    if ((metadata->received_size > metadata->expected_size) ||
        (metadata->expected_size > EXT_INCOMING_SIZE) ||
        (metadata->copy_offset > APPLICATION_MAX_SIZE))
    {
        return BOOT_METADATA_INVALID_PROGRESS;
    }

    if (BootMetadata_StateFieldsAreConsistent(metadata) == 0U)
    {
        return BOOT_METADATA_INVALID_STATE_FIELDS;
    }

    if (metadata->crc32 != BootMetadata_CalculateCrc(metadata))
    {
        return BOOT_METADATA_INVALID_CRC;
    }

    return BOOT_METADATA_VALID;
}

uint8_t BootMetadata_IsGenerationNewer(uint32_t candidate,
                                       uint32_t reference)
{
    const uint32_t difference = candidate - reference;
    return (uint8_t)((difference != 0UL) &&
                     (difference < 0x80000000UL));
}

uint32_t BootMetadata_NextGeneration(uint32_t current_generation)
{
    uint32_t next = current_generation + 1UL;

    if (next == 0UL)
    {
        next = BOOT_METADATA_FIRST_GENERATION;
    }

    return next;
}

BootMetadataSlot_t BootMetadata_SelectNewestSlot(
    const BootMetadata_t *slot_a,
    const BootMetadata_t *slot_b)
{
    const uint8_t valid_a = (uint8_t)(BootMetadata_Validate(slot_a) ==
                                      BOOT_METADATA_VALID);
    const uint8_t valid_b = (uint8_t)(BootMetadata_Validate(slot_b) ==
                                      BOOT_METADATA_VALID);

    if ((valid_a == 0U) && (valid_b == 0U))
    {
        return BOOT_METADATA_SLOT_NONE;
    }

    if (valid_a == 0U)
    {
        return BOOT_METADATA_SLOT_B;
    }

    if (valid_b == 0U)
    {
        return BOOT_METADATA_SLOT_A;
    }

    if (BootMetadata_IsGenerationNewer(slot_b->generation,
                                       slot_a->generation) != 0U)
    {
        return BOOT_METADATA_SLOT_B;
    }

    return BOOT_METADATA_SLOT_A;
}

BootMetadataSlot_t BootMetadata_SelectWriteSlot(
    const BootMetadata_t *slot_a,
    const BootMetadata_t *slot_b)
{
    const BootMetadataSlot_t newest = BootMetadata_SelectNewestSlot(slot_a,
                                                                    slot_b);

    if (newest == BOOT_METADATA_SLOT_A)
    {
        return BOOT_METADATA_SLOT_B;
    }

    return BOOT_METADATA_SLOT_A;
}
