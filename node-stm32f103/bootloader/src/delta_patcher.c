#include "delta_patcher.h"

#include <stdint.h>

#include "crc32.h"
#include "delta_patch.h"
#include "download_checkpoint_storage.h"
#include "external_flash_storage.h"
#include "full_image_validation.h"
#include "janpatch_port.h"
#include "memory_map.h"
#include "metadata_storage.h"

#define DELTA_CRC_BUFFER_SIZE 256UL

static uint32_t GetU32Le(const uint8_t *src)
{
    return (uint32_t)src[0] |
           ((uint32_t)src[1] << 8U) |
           ((uint32_t)src[2] << 16U) |
           ((uint32_t)src[3] << 24U);
}

static MetadataStorageStatus_t CommitMetadata(BootMetadata_t *metadata)
{
    BootMetadata_t committed;
    const MetadataStorageStatus_t status =
        MetadataStorage_Commit(
            metadata,
            &committed,
            (BootMetadataSlot_t *)0);

    if (status == METADATA_STORAGE_OK)
    {
        *metadata = committed;
    }

    return status;
}

static void CopyResult(const BootMetadata_t *metadata,
                       BootMetadata_t *result_metadata)
{
    if (result_metadata != (BootMetadata_t *)0)
    {
        *result_metadata = *metadata;
    }
}

static DeltaPatcherStatus_t RejectDelta(
    BootMetadata_t *metadata,
    DeltaPatcherStatus_t reason,
    BootMetadata_t *result_metadata)
{
    metadata->state = (uint32_t)UPDATE_IDLE;
    metadata->pending_version = 0UL;
    metadata->active_update_id = 0UL;
    metadata->received_size = 0UL;
    metadata->expected_size = 0UL;
    metadata->copy_offset = 0UL;
    metadata->boot_attempts = 0UL;
    metadata->last_error =
        DELTA_PATCHER_ERROR_BASE | (uint32_t)reason;

    (void)DownloadCheckpointStorage_Clear();

    if (CommitMetadata(metadata) != METADATA_STORAGE_OK)
    {
        return DELTA_PATCHER_METADATA_COMMIT_FAILED;
    }

    CopyResult(metadata, result_metadata);
    return DELTA_PATCHER_SOURCE_REJECTED;
}

static DeltaPatcherStatus_t ReadHeader(
    uint32_t artifact_size,
    DeltaPatchHeader_t *header)
{
    uint8_t raw[DELTA_PATCH_HEADER_SIZE];
    DeltaPatchHeaderStatus_t status;

    if (!ExternalFlashStorage_Read(
            EXTERNAL_FLASH_PARTITION_INCOMING,
            0UL,
            raw,
            sizeof(raw)))
    {
        return DELTA_PATCHER_HEADER_READ_FAILED;
    }

    status = DeltaPatch_ParseHeader(
        raw,
        artifact_size,
        header);

    if (status != DELTA_PATCH_HEADER_VALID)
    {
        return DELTA_PATCHER_HEADER_INVALID;
    }

    return DELTA_PATCHER_OK;
}

static DeltaPatcherStatus_t CalculateExternalCrc(
    ExternalFlashPartition_t partition,
    uint32_t offset,
    uint32_t length,
    uint32_t *crc_out)
{
    uint8_t buffer[DELTA_CRC_BUFFER_SIZE];
    uint32_t running = CRC32_IEEE_INITIAL_VALUE;
    uint32_t processed = 0UL;

    if (crc_out == (uint32_t *)0)
    {
        return DELTA_PATCHER_TARGET_READ_FAILED;
    }

    while (processed < length)
    {
        uint32_t chunk = length - processed;

        if (chunk > sizeof(buffer))
        {
            chunk = sizeof(buffer);
        }

        if (!ExternalFlashStorage_Read(
                partition,
                offset + processed,
                buffer,
                chunk))
        {
            return (partition == EXTERNAL_FLASH_PARTITION_INCOMING)
                       ? DELTA_PATCHER_PATCH_READ_FAILED
                       : DELTA_PATCHER_TARGET_READ_FAILED;
        }

        running = Crc32_Update(running, buffer, chunk);
        processed += chunk;
    }

    *crc_out = running ^ CRC32_IEEE_FINAL_XOR;
    return DELTA_PATCHER_OK;
}

static uint32_t CalculateInternalCrc(uint32_t length)
{
    uint8_t buffer[DELTA_CRC_BUFFER_SIZE];
    uint32_t running = CRC32_IEEE_INITIAL_VALUE;
    uint32_t offset = 0UL;

    while (offset < length)
    {
        uint32_t chunk = length - offset;
        volatile const uint8_t *source;

        if (chunk > sizeof(buffer))
        {
            chunk = sizeof(buffer);
        }

        source = (volatile const uint8_t *)(
            APPLICATION_START_ADDRESS + offset);

        for (uint32_t i = 0UL; i < chunk; ++i)
        {
            buffer[i] = source[i];
        }

        running = Crc32_Update(running, buffer, chunk);
        offset += chunk;
    }

    return running ^ CRC32_IEEE_FINAL_XOR;
}

static DeltaPatcherStatus_t ValidateHeaderAgainstMetadata(
    const DeltaPatchHeader_t *header,
    const BootMetadata_t *metadata)
{
    uint32_t expected_size;

    if ((header == (const DeltaPatchHeader_t *)0) ||
        (metadata == (const BootMetadata_t *)0))
    {
        return DELTA_PATCHER_METADATA_MISMATCH;
    }

    if ((metadata->active_update_id == 0UL) ||
        (metadata->pending_version != header->target_version) ||
        (metadata->active_version != header->base_version))
    {
        return DELTA_PATCHER_METADATA_MISMATCH;
    }

    if ((metadata->state == (uint32_t)UPDATE_IMAGE_READY) ||
        (metadata->state == (uint32_t)UPDATE_BACKING_UP) ||
        (metadata->state == (uint32_t)UPDATE_INSTALLING) ||
        (metadata->state == (uint32_t)UPDATE_VERIFYING_INSTALL) ||
        (metadata->state == (uint32_t)UPDATE_TRIAL_BOOT) ||
        (metadata->state == (uint32_t)UPDATE_CONFIRMED) ||
        (metadata->state == (uint32_t)UPDATE_ROLLBACK))
    {
        expected_size = header->target_image_size;
    }
    else
    {
        expected_size =
            DELTA_PATCH_HEADER_SIZE + header->patch_size;
    }

    if ((metadata->received_size != metadata->expected_size) ||
        (metadata->expected_size != expected_size))
    {
        return DELTA_PATCHER_METADATA_MISMATCH;
    }

    return DELTA_PATCHER_OK;
}

static DeltaPatcherStatus_t ValidatePatchCrc(
    const DeltaPatchHeader_t *header)
{
    uint32_t crc = 0UL;
    DeltaPatcherStatus_t status =
        CalculateExternalCrc(
            EXTERNAL_FLASH_PARTITION_INCOMING,
            DELTA_PATCH_HEADER_SIZE,
            header->patch_size,
            &crc);

    if (status != DELTA_PATCHER_OK)
    {
        return status;
    }

    return (crc == header->patch_crc32)
               ? DELTA_PATCHER_OK
               : DELTA_PATCHER_PATCH_CRC_MISMATCH;
}

static DeltaPatcherStatus_t ValidateBase(
    const DeltaPatchHeader_t *header,
    const BootMetadata_t *metadata)
{
    if ((metadata->active_version != header->base_version) ||
        (header->base_version == 0UL))
    {
        return DELTA_PATCHER_BASE_VERSION_MISMATCH;
    }

    if (CalculateInternalCrc(header->base_image_size) !=
        header->base_image_crc32)
    {
        return DELTA_PATCHER_BASE_CRC_MISMATCH;
    }

    return DELTA_PATCHER_OK;
}

static DeltaPatcherStatus_t Transition(
    BootMetadata_t *metadata,
    UpdateState_t state)
{
    metadata->state = (uint32_t)state;
    metadata->copy_offset = 0UL;
    metadata->boot_attempts = 0UL;
    metadata->last_error = 0UL;

    return (CommitMetadata(metadata) == METADATA_STORAGE_OK)
               ? DELTA_PATCHER_OK
               : DELTA_PATCHER_METADATA_COMMIT_FAILED;
}

static DeltaPatcherStatus_t EraseReconstructed(
    uint32_t target_size)
{
    uint32_t erase_size =
        (target_size + EXT_FLASH_SECTOR_SIZE - 1UL) &
        ~(EXT_FLASH_SECTOR_SIZE - 1UL);

    if ((erase_size == 0UL) ||
        (erase_size > EXT_RECONSTRUCTED_SIZE))
    {
        return DELTA_PATCHER_TARGET_ERASE_FAILED;
    }

    return ExternalFlashStorage_EraseRange(
               EXTERNAL_FLASH_PARTITION_RECONSTRUCTED,
               0UL,
               erase_size)
               ? DELTA_PATCHER_OK
               : DELTA_PATCHER_TARGET_ERASE_FAILED;
}

static DeltaPatcherStatus_t VerifyTarget(
    const DeltaPatchHeader_t *header)
{
    uint8_t vector[8];
    uint32_t crc = 0UL;
    DeltaPatcherStatus_t status;

    status = CalculateExternalCrc(
        EXTERNAL_FLASH_PARTITION_RECONSTRUCTED,
        0UL,
        header->target_image_size,
        &crc);
    if (status != DELTA_PATCHER_OK)
    {
        return status;
    }

    if (crc != header->target_image_crc32)
    {
        return DELTA_PATCHER_TARGET_CRC_MISMATCH;
    }

    if (!ExternalFlashStorage_Read(
            EXTERNAL_FLASH_PARTITION_RECONSTRUCTED,
            0UL,
            vector,
            sizeof(vector)))
    {
        return DELTA_PATCHER_TARGET_READ_FAILED;
    }

    if (FullImage_ValidateVector(
            header->target_image_size,
            GetU32Le(&vector[0]),
            GetU32Le(&vector[4])) != FULL_IMAGE_VALID)
    {
        return DELTA_PATCHER_TARGET_VECTOR_INVALID;
    }

    return DELTA_PATCHER_OK;
}

uint8_t DeltaPatcher_IsDeltaArtifact(void)
{
    uint8_t magic[4];

    if (!ExternalFlashStorage_Init())
    {
        return 0U;
    }

    if (!ExternalFlashStorage_Read(
            EXTERNAL_FLASH_PARTITION_INCOMING,
            0UL,
            magic,
            sizeof(magic)))
    {
        return 0U;
    }

    return (uint8_t)(
        GetU32Le(magic) == DELTA_PATCH_MAGIC);
}

DeltaPatcherStatus_t DeltaPatcher_Process(
    const BootMetadata_t *metadata,
    BootMetadata_t *result_metadata)
{
    BootMetadata_t working;
    DeltaPatchHeader_t header;
    DeltaPatcherStatus_t status;
    JanpatchPortResult_t patch_result;

    if (metadata == (const BootMetadata_t *)0)
    {
        return DELTA_PATCHER_METADATA_MISMATCH;
    }

    working = *metadata;

    if (!ExternalFlashStorage_Init())
    {
        return DELTA_PATCHER_EXTERNAL_FLASH_INIT_FAILED;
    }

    if (DeltaPatcher_IsDeltaArtifact() == 0U)
    {
        return DELTA_PATCHER_NOT_DELTA;
    }

    /*
     * While validating/patching, metadata still carries the received artifact
     * length. After IMAGE_READY it carries the reconstructed target length.
     */
    status = ReadHeader(
        (working.state <= (uint32_t)UPDATE_PATCHING)
            ? working.expected_size
            : 0UL,
        &header);
    if (status != DELTA_PATCHER_OK)
    {
        return RejectDelta(&working, status, result_metadata);
    }

    status = ValidateHeaderAgainstMetadata(&header, &working);
    if (status != DELTA_PATCHER_OK)
    {
        return RejectDelta(&working, status, result_metadata);
    }

    if (working.state == (uint32_t)UPDATE_ARTIFACT_READY)
    {
        status = Transition(&working, UPDATE_VERIFYING_CONTAINER);
        if (status != DELTA_PATCHER_OK)
        {
            return status;
        }
    }

    status = ValidatePatchCrc(&header);
    if (status != DELTA_PATCHER_OK)
    {
        return RejectDelta(&working, status, result_metadata);
    }

    if (working.state == (uint32_t)UPDATE_VERIFYING_CONTAINER)
    {
        status = Transition(&working, UPDATE_VERIFYING_BASE);
        if (status != DELTA_PATCHER_OK)
        {
            return status;
        }
    }

    status = ValidateBase(&header, &working);
    if (status != DELTA_PATCHER_OK)
    {
        return RejectDelta(&working, status, result_metadata);
    }

    if (working.state == (uint32_t)UPDATE_VERIFYING_BASE)
    {
        status = Transition(&working, UPDATE_PATCHING);
        if (status != DELTA_PATCHER_OK)
        {
            return status;
        }
    }

    if (working.state != (uint32_t)UPDATE_PATCHING)
    {
        CopyResult(&working, result_metadata);
        return DELTA_PATCHER_OK;
    }

    /*
     * PATCHING recovery is restart-from-scratch. The active internal image is
     * untouched, so after any reset the reconstructed partition can be safely
     * re-erased and the deterministic patch replayed.
     */
    status = EraseReconstructed(header.target_image_size);
    if (status != DELTA_PATCHER_OK)
    {
        return RejectDelta(&working, status, result_metadata);
    }

    if (JanpatchPort_Apply(&header, &patch_result) != JANPATCH_PORT_OK)
    {
        return RejectDelta(
            &working,
            DELTA_PATCHER_APPLY_FAILED,
            result_metadata);
    }

    if ((patch_result.patch_position != header.patch_size) ||
        (patch_result.target_position != header.target_image_size))
    {
        return RejectDelta(
            &working,
            DELTA_PATCHER_APPLY_FAILED,
            result_metadata);
    }

    status = VerifyTarget(&header);
    if (status != DELTA_PATCHER_OK)
    {
        return RejectDelta(&working, status, result_metadata);
    }

    /*
     * From IMAGE_READY onward the "complete payload" tracked by persistent
     * metadata is the reconstructed install candidate, not the compact patch.
     * The original patch length remains available in the D13P envelope.
     */
    working.state = (uint32_t)UPDATE_IMAGE_READY;
    working.received_size = header.target_image_size;
    working.expected_size = header.target_image_size;
    working.copy_offset = 0UL;
    working.boot_attempts = 0UL;
    working.last_error = 0UL;

    if (CommitMetadata(&working) != METADATA_STORAGE_OK)
    {
        return DELTA_PATCHER_METADATA_COMMIT_FAILED;
    }

    CopyResult(&working, result_metadata);
    return DELTA_PATCHER_OK;
}
