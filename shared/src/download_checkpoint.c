#include "download_checkpoint.h"

#include <stddef.h>
#include <stdint.h>

#include "crc32.h"
#include "memory_map.h"

_Static_assert(sizeof(DownloadCheckpointRecord_t) == 40U,
               "power-loss recovery download checkpoint layout changed");
_Static_assert(offsetof(DownloadCheckpointRecord_t, crc32) == 36U,
               "download checkpoint CRC must be final");

static void ClearRecord(DownloadCheckpointRecord_t *record)
{
    uint8_t *bytes = (uint8_t *)record;
    uint32_t i;

    for (i = 0UL; i < sizeof(*record); ++i)
    {
        bytes[i] = 0U;
    }
}

void DownloadCheckpoint_InitIdle(DownloadCheckpointRecord_t *record)
{
    if (record == (DownloadCheckpointRecord_t *)0)
    {
        return;
    }

    ClearRecord(record);
    record->magic = DOWNLOAD_CHECKPOINT_MAGIC;
    record->format_version = DOWNLOAD_CHECKPOINT_FORMAT_VERSION;
    record->state = (uint32_t)DOWNLOAD_CHECKPOINT_IDLE;
}

void DownloadCheckpoint_InitSession(DownloadCheckpointRecord_t *record,
                                    DownloadCheckpointState_t state,
                                    uint32_t update_id,
                                    uint32_t target_version,
                                    uint32_t image_size,
                                    uint32_t image_crc32,
                                    uint32_t next_offset)
{
    if (record == (DownloadCheckpointRecord_t *)0)
    {
        return;
    }

    ClearRecord(record);
    record->magic = DOWNLOAD_CHECKPOINT_MAGIC;
    record->format_version = DOWNLOAD_CHECKPOINT_FORMAT_VERSION;
    record->state = (uint32_t)state;
    record->update_id = update_id;
    record->target_version = target_version;
    record->image_size = image_size;
    record->image_crc32 = image_crc32;
    record->next_offset = next_offset;
}

uint32_t DownloadCheckpoint_CalculateCrc(
    const DownloadCheckpointRecord_t *record)
{
    if (record == (const DownloadCheckpointRecord_t *)0)
    {
        return 0UL;
    }

    return Crc32_Calculate(record,
                           (uint32_t)offsetof(DownloadCheckpointRecord_t,
                                              crc32));
}

void DownloadCheckpoint_Finalize(DownloadCheckpointRecord_t *record)
{
    if (record == (DownloadCheckpointRecord_t *)0)
    {
        return;
    }

    record->magic = DOWNLOAD_CHECKPOINT_MAGIC;
    record->format_version = DOWNLOAD_CHECKPOINT_FORMAT_VERSION;
    record->crc32 = DownloadCheckpoint_CalculateCrc(record);
}

static uint8_t SessionFieldsValid(const DownloadCheckpointRecord_t *record)
{
    if ((record->update_id == 0UL) ||
        (record->image_size == 0UL) ||
        (record->image_size > EXT_INCOMING_SIZE) ||
        (record->next_offset > record->image_size))
    {
        return 0U;
    }

    if (record->state == (uint32_t)DOWNLOAD_CHECKPOINT_RECEIVING)
    {
        return (uint8_t)(
            (record->next_offset & (EXT_FLASH_SECTOR_SIZE - 1UL)) == 0UL);
    }

    if (record->state == (uint32_t)DOWNLOAD_CHECKPOINT_ARTIFACT_READY)
    {
        return (uint8_t)(record->next_offset == record->image_size);
    }

    return 0U;
}

DownloadCheckpointValidationStatus_t DownloadCheckpoint_Validate(
    const DownloadCheckpointRecord_t *record)
{
    if (record == (const DownloadCheckpointRecord_t *)0)
    {
        return DOWNLOAD_CHECKPOINT_INVALID_ARGUMENT;
    }
    if (record->magic != DOWNLOAD_CHECKPOINT_MAGIC)
    {
        return DOWNLOAD_CHECKPOINT_INVALID_MAGIC;
    }
    if (record->format_version != DOWNLOAD_CHECKPOINT_FORMAT_VERSION)
    {
        return DOWNLOAD_CHECKPOINT_INVALID_VERSION;
    }
    if (record->generation == 0UL)
    {
        return DOWNLOAD_CHECKPOINT_INVALID_GENERATION;
    }
    if (record->state > (uint32_t)DOWNLOAD_CHECKPOINT_ARTIFACT_READY)
    {
        return DOWNLOAD_CHECKPOINT_INVALID_STATE;
    }

    if (record->state == (uint32_t)DOWNLOAD_CHECKPOINT_IDLE)
    {
        if ((record->update_id != 0UL) ||
            (record->target_version != 0UL) ||
            (record->image_size != 0UL) ||
            (record->image_crc32 != 0UL) ||
            (record->next_offset != 0UL))
        {
            return DOWNLOAD_CHECKPOINT_INVALID_FIELDS;
        }
    }
    else if (SessionFieldsValid(record) == 0U)
    {
        return DOWNLOAD_CHECKPOINT_INVALID_FIELDS;
    }

    if (record->crc32 != DownloadCheckpoint_CalculateCrc(record))
    {
        return DOWNLOAD_CHECKPOINT_INVALID_CRC;
    }

    return DOWNLOAD_CHECKPOINT_VALID;
}

uint8_t DownloadCheckpoint_IsGenerationNewer(uint32_t candidate,
                                             uint32_t reference)
{
    const uint32_t difference = candidate - reference;
    return (uint8_t)((difference != 0UL) &&
                     (difference < 0x80000000UL));
}

uint32_t DownloadCheckpoint_NextGeneration(uint32_t current_generation)
{
    uint32_t next = current_generation + 1UL;

    if (next == 0UL)
    {
        next = DOWNLOAD_CHECKPOINT_FIRST_GENERATION;
    }

    return next;
}

DownloadCheckpointSlot_t DownloadCheckpoint_SelectNewest(
    const DownloadCheckpointRecord_t *slot_a,
    const DownloadCheckpointRecord_t *slot_b)
{
    const uint8_t valid_a =
        (uint8_t)(DownloadCheckpoint_Validate(slot_a) ==
                  DOWNLOAD_CHECKPOINT_VALID);
    const uint8_t valid_b =
        (uint8_t)(DownloadCheckpoint_Validate(slot_b) ==
                  DOWNLOAD_CHECKPOINT_VALID);

    if ((valid_a == 0U) && (valid_b == 0U))
    {
        return DOWNLOAD_CHECKPOINT_SLOT_NONE;
    }
    if (valid_a == 0U)
    {
        return DOWNLOAD_CHECKPOINT_SLOT_B;
    }
    if (valid_b == 0U)
    {
        return DOWNLOAD_CHECKPOINT_SLOT_A;
    }

    return (DownloadCheckpoint_IsGenerationNewer(slot_b->generation,
                                                  slot_a->generation) != 0U)
               ? DOWNLOAD_CHECKPOINT_SLOT_B
               : DOWNLOAD_CHECKPOINT_SLOT_A;
}

DownloadCheckpointSlot_t DownloadCheckpoint_SelectWriteSlot(
    const DownloadCheckpointRecord_t *slot_a,
    const DownloadCheckpointRecord_t *slot_b)
{
    return (DownloadCheckpoint_SelectNewest(slot_a, slot_b) ==
            DOWNLOAD_CHECKPOINT_SLOT_A)
               ? DOWNLOAD_CHECKPOINT_SLOT_B
               : DOWNLOAD_CHECKPOINT_SLOT_A;
}
