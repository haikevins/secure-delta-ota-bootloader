#include "download_checkpoint_storage.h"

#include <stddef.h>
#include <stdint.h>

#include "external_flash_storage.h"
#include "memory_map.h"
#include "update_handoff.h"

#define CHECKPOINT_RECORD_OFFSET EXT_METADATA_DOWNLOAD_OFFSET

static ExternalFlashPartition_t PartitionForSlot(DownloadCheckpointSlot_t slot)
{
    return (slot == DOWNLOAD_CHECKPOINT_SLOT_B)
               ? EXTERNAL_FLASH_PARTITION_METADATA_B
               : EXTERNAL_FLASH_PARTITION_METADATA_A;
}

static uint8_t RecordsEqual(const DownloadCheckpointRecord_t *left,
                            const DownloadCheckpointRecord_t *right)
{
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;
    uint32_t i;

    for (i = 0UL; i < sizeof(*left); ++i)
    {
        if (a[i] != b[i])
        {
            return 0U;
        }
    }

    return 1U;
}

static uint8_t ReadSlot(DownloadCheckpointSlot_t slot,
                        DownloadCheckpointRecord_t *record)
{
    return (uint8_t)ExternalFlashStorage_Read(
        PartitionForSlot(slot),
        CHECKPOINT_RECORD_OFFSET,
        (uint8_t *)record,
        sizeof(*record));
}

DownloadCheckpointStorageStatus_t DownloadCheckpointStorage_Load(
    DownloadCheckpointRecord_t *record,
    DownloadCheckpointSlot_t *slot)
{
    DownloadCheckpointRecord_t a;
    DownloadCheckpointRecord_t b;
    DownloadCheckpointSlot_t newest;

    if (record == (DownloadCheckpointRecord_t *)0)
    {
        return DOWNLOAD_CHECKPOINT_STORAGE_INVALID_ARGUMENT;
    }

    if ((ReadSlot(DOWNLOAD_CHECKPOINT_SLOT_A, &a) == 0U) ||
        (ReadSlot(DOWNLOAD_CHECKPOINT_SLOT_B, &b) == 0U))
    {
        return DOWNLOAD_CHECKPOINT_STORAGE_READ_FAILED;
    }

    newest = DownloadCheckpoint_SelectNewest(&a, &b);
    if (newest == DOWNLOAD_CHECKPOINT_SLOT_NONE)
    {
        if (slot != (DownloadCheckpointSlot_t *)0)
        {
            *slot = newest;
        }
        return DOWNLOAD_CHECKPOINT_STORAGE_NOT_FOUND;
    }

    *record = (newest == DOWNLOAD_CHECKPOINT_SLOT_A) ? a : b;
    if (slot != (DownloadCheckpointSlot_t *)0)
    {
        *slot = newest;
    }

    return DOWNLOAD_CHECKPOINT_STORAGE_OK;
}

DownloadCheckpointStorageStatus_t DownloadCheckpointStorage_Commit(
    const DownloadCheckpointRecord_t *requested,
    DownloadCheckpointRecord_t *committed,
    DownloadCheckpointSlot_t *written_slot)
{
    DownloadCheckpointRecord_t a;
    DownloadCheckpointRecord_t b;
    DownloadCheckpointRecord_t candidate;
    DownloadCheckpointRecord_t verify;
    DownloadCheckpointSlot_t current;
    DownloadCheckpointSlot_t target;
    uint32_t current_generation = 0UL;
    ExternalFlashPartition_t partition;
    UpdateHandoffRecord_t preserved_handoff;
    uint8_t preserve_handoff = 0U;
    const uint32_t crc_offset =
        (uint32_t)offsetof(DownloadCheckpointRecord_t, crc32);

    if (requested == (const DownloadCheckpointRecord_t *)0)
    {
        return DOWNLOAD_CHECKPOINT_STORAGE_INVALID_ARGUMENT;
    }

    if ((ReadSlot(DOWNLOAD_CHECKPOINT_SLOT_A, &a) == 0U) ||
        (ReadSlot(DOWNLOAD_CHECKPOINT_SLOT_B, &b) == 0U))
    {
        return DOWNLOAD_CHECKPOINT_STORAGE_READ_FAILED;
    }

    current = DownloadCheckpoint_SelectNewest(&a, &b);
    target = DownloadCheckpoint_SelectWriteSlot(&a, &b);

    if (current == DOWNLOAD_CHECKPOINT_SLOT_A)
    {
        current_generation = a.generation;
    }
    else if (current == DOWNLOAD_CHECKPOINT_SLOT_B)
    {
        current_generation = b.generation;
    }

    candidate = *requested;
    candidate.generation =
        DownloadCheckpoint_NextGeneration(current_generation);
    DownloadCheckpoint_Finalize(&candidate);

    if (DownloadCheckpoint_Validate(&candidate) !=
        DOWNLOAD_CHECKPOINT_VALID)
    {
        return DOWNLOAD_CHECKPOINT_STORAGE_INVALID_ARGUMENT;
    }

    /*
     * The checkpoint and install handoff share Metadata A/B sectors at
     * different offsets. Preserve any valid handoff record in the target
     * sector across the erase so the two redundant record types can coexist.
     */
    partition = PartitionForSlot(target);

    if (ExternalFlashStorage_Read(
            partition,
            EXT_METADATA_HANDOFF_OFFSET,
            (uint8_t *)&preserved_handoff,
            sizeof(preserved_handoff)) &&
        (UpdateHandoff_Validate(&preserved_handoff) == UPDATE_HANDOFF_VALID))
    {
        preserve_handoff = 1U;
    }

    if (!ExternalFlashStorage_ErasePartition(partition))
    {
        return DOWNLOAD_CHECKPOINT_STORAGE_ERASE_FAILED;
    }

    if (!ExternalFlashStorage_Write(
            partition,
            CHECKPOINT_RECORD_OFFSET,
            (const uint8_t *)&candidate,
            crc_offset) ||
        !ExternalFlashStorage_Write(
            partition,
            CHECKPOINT_RECORD_OFFSET + crc_offset,
            (const uint8_t *)&candidate.crc32,
            sizeof(candidate.crc32)))
    {
        return DOWNLOAD_CHECKPOINT_STORAGE_WRITE_FAILED;
    }

    if ((preserve_handoff != 0U) &&
        !ExternalFlashStorage_Write(
            partition,
            EXT_METADATA_HANDOFF_OFFSET,
            (const uint8_t *)&preserved_handoff,
            sizeof(preserved_handoff)))
    {
        return DOWNLOAD_CHECKPOINT_STORAGE_WRITE_FAILED;
    }

    if (!ExternalFlashStorage_Read(
            partition,
            CHECKPOINT_RECORD_OFFSET,
            (uint8_t *)&verify,
            sizeof(verify)) ||
        (DownloadCheckpoint_Validate(&verify) !=
         DOWNLOAD_CHECKPOINT_VALID) ||
        (RecordsEqual(&candidate, &verify) == 0U))
    {
        return DOWNLOAD_CHECKPOINT_STORAGE_VERIFY_FAILED;
    }

    if (committed != (DownloadCheckpointRecord_t *)0)
    {
        *committed = verify;
    }
    if (written_slot != (DownloadCheckpointSlot_t *)0)
    {
        *written_slot = target;
    }

    return DOWNLOAD_CHECKPOINT_STORAGE_OK;
}

DownloadCheckpointStorageStatus_t DownloadCheckpointStorage_Clear(void)
{
    DownloadCheckpointRecord_t idle;

    DownloadCheckpoint_InitIdle(&idle);
    return DownloadCheckpointStorage_Commit(
        &idle,
        (DownloadCheckpointRecord_t *)0,
        (DownloadCheckpointSlot_t *)0);
}
