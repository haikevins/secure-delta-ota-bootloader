#include "update_handoff_storage.h"

#include <stddef.h>
#include <stdint.h>

#include "download_checkpoint.h"
#include "external_flash_storage.h"
#include "memory_map.h"

static ExternalFlashPartition_t PartitionForSlot(UpdateHandoffSlot_t slot)
{
    return (slot == UPDATE_HANDOFF_SLOT_B)
               ? EXTERNAL_FLASH_PARTITION_METADATA_B
               : EXTERNAL_FLASH_PARTITION_METADATA_A;
}

static uint8_t RecordsEqual(const UpdateHandoffRecord_t *left,
                            const UpdateHandoffRecord_t *right)
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

static uint8_t ReadSlot(UpdateHandoffSlot_t slot,
                        UpdateHandoffRecord_t *record)
{
    return (uint8_t)ExternalFlashStorage_Read(
        PartitionForSlot(slot),
        EXT_METADATA_HANDOFF_OFFSET,
        (uint8_t *)record,
        sizeof(*record));
}

UpdateHandoffStorageStatus_t UpdateHandoffStorage_Load(
    UpdateHandoffRecord_t *record,
    UpdateHandoffSlot_t *slot)
{
    UpdateHandoffRecord_t a;
    UpdateHandoffRecord_t b;
    UpdateHandoffSlot_t newest;

    if (record == (UpdateHandoffRecord_t *)0)
    {
        return UPDATE_HANDOFF_STORAGE_INVALID_ARGUMENT;
    }

    if ((ReadSlot(UPDATE_HANDOFF_SLOT_A, &a) == 0U) ||
        (ReadSlot(UPDATE_HANDOFF_SLOT_B, &b) == 0U))
    {
        return UPDATE_HANDOFF_STORAGE_READ_FAILED;
    }

    newest = UpdateHandoff_SelectNewest(&a, &b);
    if (newest == UPDATE_HANDOFF_SLOT_NONE)
    {
        if (slot != (UpdateHandoffSlot_t *)0)
        {
            *slot = newest;
        }
        return UPDATE_HANDOFF_STORAGE_NOT_FOUND;
    }

    *record = (newest == UPDATE_HANDOFF_SLOT_A) ? a : b;
    if (slot != (UpdateHandoffSlot_t *)0)
    {
        *slot = newest;
    }

    return UPDATE_HANDOFF_STORAGE_OK;
}

UpdateHandoffStorageStatus_t UpdateHandoffStorage_Commit(
    const UpdateHandoffRecord_t *requested,
    UpdateHandoffRecord_t *committed,
    UpdateHandoffSlot_t *written_slot)
{
    UpdateHandoffRecord_t a;
    UpdateHandoffRecord_t b;
    UpdateHandoffRecord_t candidate;
    UpdateHandoffRecord_t verify;
    UpdateHandoffSlot_t current;
    UpdateHandoffSlot_t target;
    uint32_t current_generation = 0UL;
    ExternalFlashPartition_t partition;
    DownloadCheckpointRecord_t preserved_checkpoint;
    uint8_t preserve_checkpoint = 0U;
    const uint32_t crc_offset =
        (uint32_t)offsetof(UpdateHandoffRecord_t, crc32);

    if (requested == (const UpdateHandoffRecord_t *)0)
    {
        return UPDATE_HANDOFF_STORAGE_INVALID_ARGUMENT;
    }

    if ((ReadSlot(UPDATE_HANDOFF_SLOT_A, &a) == 0U) ||
        (ReadSlot(UPDATE_HANDOFF_SLOT_B, &b) == 0U))
    {
        return UPDATE_HANDOFF_STORAGE_READ_FAILED;
    }

    current = UpdateHandoff_SelectNewest(&a, &b);
    target = UpdateHandoff_SelectWriteSlot(&a, &b);

    if (current == UPDATE_HANDOFF_SLOT_A)
    {
        current_generation = a.generation;
    }
    else if (current == UPDATE_HANDOFF_SLOT_B)
    {
        current_generation = b.generation;
    }

    candidate = *requested;
    candidate.generation = UpdateHandoff_NextGeneration(current_generation);
    UpdateHandoff_Finalize(&candidate);

    if (UpdateHandoff_Validate(&candidate) != UPDATE_HANDOFF_VALID)
    {
        return UPDATE_HANDOFF_STORAGE_INVALID_ARGUMENT;
    }

    partition = PartitionForSlot(target);

    /*
     * power-loss recovery stores the UART download checkpoint at offset 0x100 in the same
     * redundant external-metadata sectors. Preserve a valid checkpoint across
     * the sector erase required by a handoff update.
     */
    if (ExternalFlashStorage_Read(
            partition,
            EXT_METADATA_DOWNLOAD_OFFSET,
            (uint8_t *)&preserved_checkpoint,
            sizeof(preserved_checkpoint)) &&
        (DownloadCheckpoint_Validate(&preserved_checkpoint) ==
         DOWNLOAD_CHECKPOINT_VALID))
    {
        preserve_checkpoint = 1U;
    }

    if (!ExternalFlashStorage_ErasePartition(partition))
    {
        return UPDATE_HANDOFF_STORAGE_ERASE_FAILED;
    }

    /* CRC is programmed last. A reset before this final word leaves the new
     * external record invalid, while the older slot remains valid. */
    if (!ExternalFlashStorage_Write(
            partition,
            EXT_METADATA_HANDOFF_OFFSET,
            (const uint8_t *)&candidate,
            crc_offset) ||
        !ExternalFlashStorage_Write(
            partition,
            EXT_METADATA_HANDOFF_OFFSET + crc_offset,
            (const uint8_t *)&candidate.crc32,
            sizeof(candidate.crc32)))
    {
        return UPDATE_HANDOFF_STORAGE_WRITE_FAILED;
    }

    if ((preserve_checkpoint != 0U) &&
        !ExternalFlashStorage_Write(
            partition,
            EXT_METADATA_DOWNLOAD_OFFSET,
            (const uint8_t *)&preserved_checkpoint,
            sizeof(preserved_checkpoint)))
    {
        return UPDATE_HANDOFF_STORAGE_WRITE_FAILED;
    }

    if (!ExternalFlashStorage_Read(
            partition,
            EXT_METADATA_HANDOFF_OFFSET,
            (uint8_t *)&verify,
            sizeof(verify)) ||
        (UpdateHandoff_Validate(&verify) != UPDATE_HANDOFF_VALID) ||
        (RecordsEqual(&candidate, &verify) == 0U))
    {
        return UPDATE_HANDOFF_STORAGE_VERIFY_FAILED;
    }

    if (committed != (UpdateHandoffRecord_t *)0)
    {
        *committed = verify;
    }
    if (written_slot != (UpdateHandoffSlot_t *)0)
    {
        *written_slot = target;
    }

    return UPDATE_HANDOFF_STORAGE_OK;
}
