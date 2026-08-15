#ifndef DOWNLOAD_CHECKPOINT_H
#define DOWNLOAD_CHECKPOINT_H

#include <stdint.h>

#define DOWNLOAD_CHECKPOINT_MAGIC            0x37504344UL /* "DCP7" */
#define DOWNLOAD_CHECKPOINT_FORMAT_VERSION   1UL
#define DOWNLOAD_CHECKPOINT_FIRST_GENERATION 1UL

typedef enum
{
    DOWNLOAD_CHECKPOINT_IDLE = 0,
    DOWNLOAD_CHECKPOINT_RECEIVING,
    DOWNLOAD_CHECKPOINT_ARTIFACT_READY
} DownloadCheckpointState_t;

typedef struct
{
    uint32_t magic;
    uint32_t generation;
    uint32_t format_version;
    uint32_t state;
    uint32_t update_id;
    uint32_t target_version;
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t next_offset;
    uint32_t crc32;
} DownloadCheckpointRecord_t;

typedef enum
{
    DOWNLOAD_CHECKPOINT_VALID = 0,
    DOWNLOAD_CHECKPOINT_INVALID_ARGUMENT,
    DOWNLOAD_CHECKPOINT_INVALID_MAGIC,
    DOWNLOAD_CHECKPOINT_INVALID_VERSION,
    DOWNLOAD_CHECKPOINT_INVALID_GENERATION,
    DOWNLOAD_CHECKPOINT_INVALID_STATE,
    DOWNLOAD_CHECKPOINT_INVALID_FIELDS,
    DOWNLOAD_CHECKPOINT_INVALID_CRC
} DownloadCheckpointValidationStatus_t;

typedef enum
{
    DOWNLOAD_CHECKPOINT_SLOT_NONE = 0,
    DOWNLOAD_CHECKPOINT_SLOT_A,
    DOWNLOAD_CHECKPOINT_SLOT_B
} DownloadCheckpointSlot_t;

void DownloadCheckpoint_InitIdle(DownloadCheckpointRecord_t *record);
void DownloadCheckpoint_InitSession(DownloadCheckpointRecord_t *record,
                                    DownloadCheckpointState_t state,
                                    uint32_t update_id,
                                    uint32_t target_version,
                                    uint32_t image_size,
                                    uint32_t image_crc32,
                                    uint32_t next_offset);
uint32_t DownloadCheckpoint_CalculateCrc(
    const DownloadCheckpointRecord_t *record);
void DownloadCheckpoint_Finalize(DownloadCheckpointRecord_t *record);
DownloadCheckpointValidationStatus_t DownloadCheckpoint_Validate(
    const DownloadCheckpointRecord_t *record);
uint8_t DownloadCheckpoint_IsGenerationNewer(uint32_t candidate,
                                             uint32_t reference);
uint32_t DownloadCheckpoint_NextGeneration(uint32_t current_generation);
DownloadCheckpointSlot_t DownloadCheckpoint_SelectNewest(
    const DownloadCheckpointRecord_t *slot_a,
    const DownloadCheckpointRecord_t *slot_b);
DownloadCheckpointSlot_t DownloadCheckpoint_SelectWriteSlot(
    const DownloadCheckpointRecord_t *slot_a,
    const DownloadCheckpointRecord_t *slot_b);

#endif
