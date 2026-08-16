#include "update_handoff.h"

#include <stddef.h>
#include <stdint.h>

#include "crc32.h"
#include "memory_map.h"

_Static_assert(sizeof(UpdateHandoffRecord_t) == 36U,
               "full-image OTA handoff persistent layout changed");
_Static_assert(offsetof(UpdateHandoffRecord_t, crc32) == 32U,
               "handoff CRC must be the final field");

static void ClearRecord(UpdateHandoffRecord_t *record)
{
    uint8_t *bytes = (uint8_t *)record;
    uint32_t i;
    for (i = 0UL; i < sizeof(*record); ++i) { bytes[i] = 0U; }
}

void UpdateHandoff_Init(UpdateHandoffRecord_t *record,
                        uint32_t update_id,
                        uint32_t target_version,
                        uint32_t image_size,
                        uint32_t image_crc32)
{
    if (record == (UpdateHandoffRecord_t *)0) { return; }

    ClearRecord(record);
    record->magic = UPDATE_HANDOFF_MAGIC;
    record->format_version = UPDATE_HANDOFF_FORMAT_VERSION;
    record->update_id = update_id;
    record->target_version = target_version;
    record->image_size = image_size;
    record->image_crc32 = image_crc32;
    record->target_load_address = APPLICATION_START_ADDRESS;
}

uint32_t UpdateHandoff_CalculateCrc(const UpdateHandoffRecord_t *record)
{
    if (record == (const UpdateHandoffRecord_t *)0) { return 0UL; }
    return Crc32_Calculate(record,
                           (uint32_t)offsetof(UpdateHandoffRecord_t, crc32));
}

void UpdateHandoff_Finalize(UpdateHandoffRecord_t *record)
{
    if (record == (UpdateHandoffRecord_t *)0) { return; }
    record->magic = UPDATE_HANDOFF_MAGIC;
    record->format_version = UPDATE_HANDOFF_FORMAT_VERSION;
    record->crc32 = UpdateHandoff_CalculateCrc(record);
}

UpdateHandoffValidationStatus_t UpdateHandoff_Validate(
    const UpdateHandoffRecord_t *record)
{
    if (record == (const UpdateHandoffRecord_t *)0)
    {
        return UPDATE_HANDOFF_INVALID_ARGUMENT;
    }
    if (record->magic != UPDATE_HANDOFF_MAGIC)
    {
        return UPDATE_HANDOFF_INVALID_MAGIC;
    }
    if (record->format_version != UPDATE_HANDOFF_FORMAT_VERSION)
    {
        return UPDATE_HANDOFF_INVALID_VERSION;
    }
    if (record->generation == 0UL)
    {
        return UPDATE_HANDOFF_INVALID_GENERATION;
    }
    if (record->update_id == 0UL)
    {
        return UPDATE_HANDOFF_INVALID_UPDATE_ID;
    }
    if (record->target_version == 0UL)
    {
        return UPDATE_HANDOFF_INVALID_TARGET_VERSION;
    }
    if ((record->image_size < 8UL) ||
        (record->image_size > APPLICATION_MAX_SIZE))
    {
        return UPDATE_HANDOFF_INVALID_IMAGE_SIZE;
    }
    if (record->target_load_address != APPLICATION_START_ADDRESS)
    {
        return UPDATE_HANDOFF_INVALID_LOAD_ADDRESS;
    }
    if (record->crc32 != UpdateHandoff_CalculateCrc(record))
    {
        return UPDATE_HANDOFF_INVALID_CRC;
    }
    return UPDATE_HANDOFF_VALID;
}

uint8_t UpdateHandoff_IsGenerationNewer(uint32_t candidate,
                                        uint32_t reference)
{
    const uint32_t difference = candidate - reference;
    return (uint8_t)((difference != 0UL) && (difference < 0x80000000UL));
}

uint32_t UpdateHandoff_NextGeneration(uint32_t current_generation)
{
    uint32_t next = current_generation + 1UL;
    if (next == 0UL) { next = UPDATE_HANDOFF_FIRST_GENERATION; }
    return next;
}

UpdateHandoffSlot_t UpdateHandoff_SelectNewest(
    const UpdateHandoffRecord_t *slot_a,
    const UpdateHandoffRecord_t *slot_b)
{
    const uint8_t valid_a =
        (uint8_t)(UpdateHandoff_Validate(slot_a) == UPDATE_HANDOFF_VALID);
    const uint8_t valid_b =
        (uint8_t)(UpdateHandoff_Validate(slot_b) == UPDATE_HANDOFF_VALID);

    if ((valid_a == 0U) && (valid_b == 0U)) { return UPDATE_HANDOFF_SLOT_NONE; }
    if (valid_a == 0U) { return UPDATE_HANDOFF_SLOT_B; }
    if (valid_b == 0U) { return UPDATE_HANDOFF_SLOT_A; }

    return (UpdateHandoff_IsGenerationNewer(slot_b->generation,
                                            slot_a->generation) != 0U)
               ? UPDATE_HANDOFF_SLOT_B
               : UPDATE_HANDOFF_SLOT_A;
}

UpdateHandoffSlot_t UpdateHandoff_SelectWriteSlot(
    const UpdateHandoffRecord_t *slot_a,
    const UpdateHandoffRecord_t *slot_b)
{
    return (UpdateHandoff_SelectNewest(slot_a, slot_b) ==
            UPDATE_HANDOFF_SLOT_A)
               ? UPDATE_HANDOFF_SLOT_B
               : UPDATE_HANDOFF_SLOT_A;
}
