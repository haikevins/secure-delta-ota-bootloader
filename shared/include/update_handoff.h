#ifndef UPDATE_HANDOFF_H
#define UPDATE_HANDOFF_H

#include <stdint.h>

#define UPDATE_HANDOFF_MAGIC            0x3641544FUL /* "OTA6" */
#define UPDATE_HANDOFF_FORMAT_VERSION   1UL
#define UPDATE_HANDOFF_FIRST_GENERATION 1UL

typedef struct
{
    uint32_t magic;
    uint32_t generation;
    uint32_t format_version;
    uint32_t update_id;
    uint32_t target_version;
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t target_load_address;
    uint32_t crc32;
} UpdateHandoffRecord_t;

typedef enum
{
    UPDATE_HANDOFF_VALID = 0,
    UPDATE_HANDOFF_INVALID_ARGUMENT,
    UPDATE_HANDOFF_INVALID_MAGIC,
    UPDATE_HANDOFF_INVALID_VERSION,
    UPDATE_HANDOFF_INVALID_GENERATION,
    UPDATE_HANDOFF_INVALID_UPDATE_ID,
    UPDATE_HANDOFF_INVALID_TARGET_VERSION,
    UPDATE_HANDOFF_INVALID_IMAGE_SIZE,
    UPDATE_HANDOFF_INVALID_LOAD_ADDRESS,
    UPDATE_HANDOFF_INVALID_CRC
} UpdateHandoffValidationStatus_t;

typedef enum
{
    UPDATE_HANDOFF_SLOT_NONE = 0,
    UPDATE_HANDOFF_SLOT_A,
    UPDATE_HANDOFF_SLOT_B
} UpdateHandoffSlot_t;

void UpdateHandoff_Init(UpdateHandoffRecord_t *record,
                        uint32_t update_id,
                        uint32_t target_version,
                        uint32_t image_size,
                        uint32_t image_crc32);
uint32_t UpdateHandoff_CalculateCrc(const UpdateHandoffRecord_t *record);
void UpdateHandoff_Finalize(UpdateHandoffRecord_t *record);
UpdateHandoffValidationStatus_t UpdateHandoff_Validate(
    const UpdateHandoffRecord_t *record);
uint8_t UpdateHandoff_IsGenerationNewer(uint32_t candidate,
                                        uint32_t reference);
uint32_t UpdateHandoff_NextGeneration(uint32_t current_generation);
UpdateHandoffSlot_t UpdateHandoff_SelectNewest(
    const UpdateHandoffRecord_t *slot_a,
    const UpdateHandoffRecord_t *slot_b);
UpdateHandoffSlot_t UpdateHandoff_SelectWriteSlot(
    const UpdateHandoffRecord_t *slot_a,
    const UpdateHandoffRecord_t *slot_b);

#endif
