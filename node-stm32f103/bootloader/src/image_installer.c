#include "image_installer.h"

#include <stdint.h>

#include "application_jump.h"
#include "backup_image.h"
#include "backup_image_storage.h"
#include "backup_progress.h"
#include "crc32.h"
#include "delta_patch.h"
#include "firmware_container.h"
#include "download_checkpoint_storage.h"
#include "external_flash_storage.h"
#include "full_image_validation.h"
#include "install_progress.h"
#include "memory_map.h"
#include "secure_container.h"
#include "metadata_storage.h"
#include "spi_flash.h"
#include "stm32f10x.h"
#include "stm32f10x_flash.h"
#include "update_handoff.h"
#include "update_handoff_storage.h"

#define INSTALL_CRC_BUFFER_SIZE 256UL
#define PHASE8_ERROR_BASE       0x00080000UL

/*
 * A complete internal Flash page is staged in SRAM before the corresponding
 * application page is erased. The same buffer is also reused while backing up
 * the active image to W25Q.
 */
static uint8_t g_install_page_buffer[INTERNAL_FLASH_PAGE_SIZE];

static uint32_t InstallerError(ImageInstallerStatus_t status)
{
    return PHASE8_ERROR_BASE | (uint32_t)status;
}

static uint32_t GetPrimask(void)
{
    uint32_t primask;
    __asm volatile ("mrs %0, primask" : "=r" (primask));
    return primask;
}

static void RestorePrimask(uint32_t primask)
{
    __asm volatile ("msr primask, %0" : : "r" (primask) : "memory");
}

static uint32_t GetU32Le(const uint8_t *src)
{
    return (uint32_t)src[0] |
           ((uint32_t)src[1] << 8U) |
           ((uint32_t)src[2] << 16U) |
           ((uint32_t)src[3] << 24U);
}

typedef struct
{
    ExternalFlashPartition_t partition;
    uint32_t update_id;
    uint32_t target_version;
    uint32_t image_size;
    uint32_t image_crc32;
    uint8_t is_delta;
} CandidateSource_t;

static uint8_t FullRecordMatchesMetadata(
    const UpdateHandoffRecord_t *record,
    const BootMetadata_t *metadata)
{
    return (uint8_t)(
        (record->update_id == metadata->active_update_id) &&
        (record->target_version == metadata->pending_version) &&
        (record->image_size == metadata->expected_size) &&
        (metadata->received_size == metadata->expected_size));
}

static ImageInstallerStatus_t LoadCandidateSource(
    const BootMetadata_t *metadata,
    CandidateSource_t *source)
{
    uint8_t first_word[4];

    if ((metadata == (const BootMetadata_t *)0) ||
        (source == (CandidateSource_t *)0))
    {
        return IMAGE_INSTALLER_HANDOFF_MISMATCH;
    }

    if (!ExternalFlashStorage_Read(
            EXTERNAL_FLASH_PARTITION_INCOMING,
            0UL,
            first_word,
            sizeof(first_word)))
    {
        return IMAGE_INSTALLER_SOURCE_READ_FAILED;
    }

    if (GetU32Le(first_word) == FW_CONTAINER_MAGIC)
    {
        FirmwareContainerInfo_t info;

        /*
         * Re-authenticate the signed envelope on every recovery boot before
         * trusting target_size/target_crc from external Flash.
         */
        if (SecureContainer_LoadVerifiedInfo(0UL, &info) !=
            SECURE_CONTAINER_OK)
        {
            return IMAGE_INSTALLER_HANDOFF_MISMATCH;
        }

        if ((metadata->state == (uint32_t)UPDATE_ARTIFACT_READY) ||
            (metadata->pending_version != info.header.target_version) ||
            (metadata->active_update_id == 0UL) ||
            (metadata->received_size != info.header.target_image_size) ||
            (metadata->expected_size != info.header.target_image_size))
        {
            return IMAGE_INSTALLER_HANDOFF_MISMATCH;
        }

        if ((info.header.image_type == (uint32_t)FW_IMAGE_DELTA) &&
            (metadata->active_version != info.header.base_version))
        {
            return IMAGE_INSTALLER_HANDOFF_MISMATCH;
        }

        source->partition =
            EXTERNAL_FLASH_PARTITION_RECONSTRUCTED;
        source->update_id = metadata->active_update_id;
        source->target_version = info.header.target_version;
        source->image_size = info.header.target_image_size;
        source->image_crc32 = info.extension.target_image_crc32;
        source->is_delta =
            (uint8_t)(info.header.image_type == (uint32_t)FW_IMAGE_DELTA);
        return IMAGE_INSTALLER_OK;
    }

#if PHASE14_ALLOW_UNSIGNED_LEGACY != 0
    if (GetU32Le(first_word) == DELTA_PATCH_MAGIC)
    {
        uint8_t raw[DELTA_PATCH_HEADER_SIZE];
        DeltaPatchHeader_t header;

        if (!ExternalFlashStorage_Read(
                EXTERNAL_FLASH_PARTITION_INCOMING,
                0UL,
                raw,
                sizeof(raw)))
        {
            return IMAGE_INSTALLER_SOURCE_READ_FAILED;
        }

        if (DeltaPatch_ParseHeader(raw, 0UL, &header) !=
            DELTA_PATCH_HEADER_VALID)
        {
            return IMAGE_INSTALLER_HANDOFF_MISMATCH;
        }

        if ((metadata->state == (uint32_t)UPDATE_ARTIFACT_READY) ||
            (metadata->pending_version != header.target_version) ||
            (metadata->active_version != header.base_version) ||
            (metadata->active_update_id == 0UL) ||
            (metadata->received_size != header.target_image_size) ||
            (metadata->expected_size != header.target_image_size))
        {
            return IMAGE_INSTALLER_HANDOFF_MISMATCH;
        }

        source->partition =
            EXTERNAL_FLASH_PARTITION_RECONSTRUCTED;
        source->update_id = metadata->active_update_id;
        source->target_version = header.target_version;
        source->image_size = header.target_image_size;
        source->image_crc32 = header.target_image_crc32;
        source->is_delta = 1U;
        return IMAGE_INSTALLER_OK;
    }
    else
    {
        UpdateHandoffRecord_t record;
        UpdateHandoffStorageStatus_t handoff_status =
            UpdateHandoffStorage_Load(
                &record,
                (UpdateHandoffSlot_t *)0);

        if (handoff_status != UPDATE_HANDOFF_STORAGE_OK)
        {
            return IMAGE_INSTALLER_HANDOFF_LOAD_FAILED;
        }

        if (FullRecordMatchesMetadata(&record, metadata) == 0U)
        {
            return IMAGE_INSTALLER_HANDOFF_MISMATCH;
        }

        source->partition =
            EXTERNAL_FLASH_PARTITION_INCOMING;
        source->update_id = record.update_id;
        source->target_version = record.target_version;
        source->image_size = record.image_size;
        source->image_crc32 = record.image_crc32;
        source->is_delta = 0U;
        return IMAGE_INSTALLER_OK;
    }
#else
    return IMAGE_INSTALLER_HANDOFF_MISMATCH;
#endif
}

static MetadataStorageStatus_t CommitMetadata(BootMetadata_t *metadata)
{
    BootMetadata_t committed;
    const MetadataStorageStatus_t status =
        MetadataStorage_Commit(metadata, &committed, (BootMetadataSlot_t *)0);

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

static ImageInstallerStatus_t RevertToActiveImage(
    BootMetadata_t *metadata,
    ImageInstallerStatus_t reason,
    BootMetadata_t *result_metadata)
{
    metadata->state = (uint32_t)UPDATE_IDLE;
    metadata->pending_version = 0UL;
    metadata->active_update_id = 0UL;
    metadata->received_size = 0UL;
    metadata->expected_size = 0UL;
    metadata->copy_offset = 0UL;
    metadata->boot_attempts = 0UL;
    metadata->last_error = InstallerError(reason);

    /*
     * The active internal image is still untouched in ARTIFACT_READY and
     * BACKING_UP. Discarding the persistent download checkpoint prevents the
     * application from resurrecting a rejected candidate on the next boot.
     */
    (void)DownloadCheckpointStorage_Clear();

    if (CommitMetadata(metadata) != METADATA_STORAGE_OK)
    {
        return IMAGE_INSTALLER_METADATA_COMMIT_FAILED;
    }

    CopyResult(metadata, result_metadata);
    return IMAGE_INSTALLER_SOURCE_REJECTED;
}

static void BestEffortMarkFailed(BootMetadata_t *metadata,
                                 ImageInstallerStatus_t reason)
{
    metadata->state = (uint32_t)UPDATE_FAILED;
    metadata->last_error = InstallerError(reason);
    (void)CommitMetadata(metadata);
}

static uint32_t CalculateInternalCrc(uint32_t length)
{
    uint8_t buffer[INSTALL_CRC_BUFFER_SIZE];
    uint32_t offset = 0UL;
    uint32_t running = CRC32_IEEE_INITIAL_VALUE;

    while (offset < length)
    {
        uint32_t chunk = length - offset;
        uint32_t i;
        volatile const uint8_t *source;

        if (chunk > sizeof(buffer))
        {
            chunk = sizeof(buffer);
        }

        source = (volatile const uint8_t *)(APPLICATION_START_ADDRESS + offset);
        for (i = 0UL; i < chunk; ++i)
        {
            buffer[i] = source[i];
        }

        running = Crc32_Update(running, buffer, chunk);
        offset += chunk;
    }

    return running ^ CRC32_IEEE_FINAL_XOR;
}

static ImageInstallerStatus_t CalculateExternalCrc(
    ExternalFlashPartition_t partition,
    uint32_t length,
    uint32_t *crc_out)
{
    uint8_t buffer[INSTALL_CRC_BUFFER_SIZE];
    uint32_t offset = 0UL;
    uint32_t running = CRC32_IEEE_INITIAL_VALUE;

    if (crc_out == (uint32_t *)0)
    {
        return IMAGE_INSTALLER_SOURCE_READ_FAILED;
    }

    while (offset < length)
    {
        uint32_t chunk = length - offset;

        if (chunk > sizeof(buffer))
        {
            chunk = sizeof(buffer);
        }

        if (!ExternalFlashStorage_Read(partition,
                                       offset, buffer, chunk))
        {
            return IMAGE_INSTALLER_SOURCE_READ_FAILED;
        }

        running = Crc32_Update(running, buffer, chunk);
        offset += chunk;
    }

    *crc_out = running ^ CRC32_IEEE_FINAL_XOR;
    return IMAGE_INSTALLER_OK;
}

static ImageInstallerStatus_t CalculateBackupCrc(uint32_t *crc_out)
{
    uint8_t buffer[INSTALL_CRC_BUFFER_SIZE];
    uint32_t offset = 0UL;
    uint32_t running = CRC32_IEEE_INITIAL_VALUE;

    if (crc_out == (uint32_t *)0)
    {
        return IMAGE_INSTALLER_BACKUP_VERIFY_FAILED;
    }

    while (offset < APPLICATION_MAX_SIZE)
    {
        uint32_t chunk = APPLICATION_MAX_SIZE - offset;

        if (chunk > sizeof(buffer))
        {
            chunk = sizeof(buffer);
        }

        if (!BackupImageStorage_Read(offset, buffer, chunk))
        {
            return IMAGE_INSTALLER_BACKUP_VERIFY_FAILED;
        }

        running = Crc32_Update(running, buffer, chunk);
        offset += chunk;
    }

    *crc_out = running ^ CRC32_IEEE_FINAL_XOR;
    return IMAGE_INSTALLER_OK;
}

static ImageInstallerStatus_t ValidateExternalSource(
    const CandidateSource_t *source)
{
    uint8_t vector[8];
    uint32_t crc;
    ImageInstallerStatus_t crc_status;

    if ((source == (const CandidateSource_t *)0) ||
        !ExternalFlashStorage_Read(
            source->partition,
            0UL,
            vector,
            sizeof(vector)))
    {
        return IMAGE_INSTALLER_SOURCE_READ_FAILED;
    }

    if (FullImage_ValidateVector(
            source->image_size,
            GetU32Le(&vector[0]),
            GetU32Le(&vector[4])) != FULL_IMAGE_VALID)
    {
        return IMAGE_INSTALLER_SOURCE_VECTOR_INVALID;
    }

    crc_status = CalculateExternalCrc(
        source->partition,
        source->image_size,
        &crc);
    if (crc_status != IMAGE_INSTALLER_OK)
    {
        return crc_status;
    }

    if (crc != source->image_crc32)
    {
        return IMAGE_INSTALLER_SOURCE_CRC_MISMATCH;
    }

    return IMAGE_INSTALLER_OK;
}

static ImageInstallerStatus_t ValidateBackupImage(
    BackupImageRecord_t *record)
{
    uint32_t crc = 0UL;
    ImageInstallerStatus_t status;

    if (BackupImageStorage_LoadHeader(record) != BACKUP_IMAGE_STORAGE_OK)
    {
        return IMAGE_INSTALLER_ROLLBACK_SOURCE_INVALID;
    }

    status = CalculateBackupCrc(&crc);
    if (status != IMAGE_INSTALLER_OK)
    {
        return status;
    }

    if (crc != record->image_crc32)
    {
        return IMAGE_INSTALLER_BACKUP_CRC_MISMATCH;
    }

    return IMAGE_INSTALLER_OK;
}

static ImageInstallerStatus_t LoadCandidatePage(
    const CandidateSource_t *source,
    uint32_t page_offset,
    uint32_t *page_length)
{
    const uint32_t length =
        InstallProgress_PageLength(
            source->image_size,
            page_offset);

    if ((length == 0UL) ||
        (page_length == (uint32_t *)0))
    {
        return IMAGE_INSTALLER_PROGRESS_INVALID;
    }

    if (!ExternalFlashStorage_Read(
            source->partition,
            page_offset,
            g_install_page_buffer,
            length))
    {
        return IMAGE_INSTALLER_SOURCE_READ_FAILED;
    }

    *page_length = length;
    return IMAGE_INSTALLER_OK;
}

static ImageInstallerStatus_t LoadBackupPage(uint32_t page_offset,
                                             uint32_t *page_length)
{
    const uint32_t length =
        InstallProgress_PageLength(APPLICATION_MAX_SIZE, page_offset);

    if ((length == 0UL) || (page_length == (uint32_t *)0))
    {
        return IMAGE_INSTALLER_PROGRESS_INVALID;
    }

    if (!BackupImageStorage_Read(page_offset,
                                 g_install_page_buffer,
                                 length))
    {
        return IMAGE_INSTALLER_ROLLBACK_READ_FAILED;
    }

    *page_length = length;
    return IMAGE_INSTALLER_OK;
}

#if defined(PHASE7_FAULT_INJECT_OFFSET)
static uint8_t ShouldInjectReset(const BootMetadata_t *metadata,
                                 uint32_t page_offset,
                                 uint32_t page_length,
                                 uint32_t bytes_programmed)
{
    const uint32_t inject_offset = (uint32_t)PHASE7_FAULT_INJECT_OFFSET;
    const uint32_t page_end = page_offset + page_length;

    return (uint8_t)(
        (metadata != (const BootMetadata_t *)0) &&
        (metadata->last_error != IMAGE_INSTALLER_PHASE7_FAULT_MARKER) &&
        (inject_offset > page_offset) &&
        (inject_offset < page_end) &&
        ((page_offset + bytes_programmed) >= inject_offset));
}

static ImageInstallerStatus_t InjectOneShotReset(BootMetadata_t *metadata)
{
    /*
     * copy_offset intentionally remains at the previous verified page.
     * The current page is only partially programmed. After reset the normal
     * path re-erases this page before programming it again.
     */
    metadata->last_error = IMAGE_INSTALLER_PHASE7_FAULT_MARKER;

    if (CommitMetadata(metadata) != METADATA_STORAGE_OK)
    {
        return IMAGE_INSTALLER_METADATA_COMMIT_FAILED;
    }

    NVIC_SystemReset();
    for (;;)
    {
        __NOP();
    }
}
#endif

static ImageInstallerStatus_t ProgramBufferedApplicationPage(
    uint32_t page_offset,
    uint32_t page_length,
    BootMetadata_t *metadata,
    uint8_t allow_phase7_fault)
{
    uint32_t i;
    uint32_t primask;
    FLASH_Status flash_status;
    volatile const uint8_t *installed;

    primask = GetPrimask();
    __disable_irq();

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR |
                    FLASH_FLAG_WRPRTERR);

    flash_status = FLASH_ErasePage(APPLICATION_START_ADDRESS + page_offset);
    if (flash_status != FLASH_COMPLETE)
    {
        FLASH_Lock();
        RestorePrimask(primask);
        return IMAGE_INSTALLER_FLASH_ERASE_FAILED;
    }

    for (i = 0UL; i < page_length; i += 2UL)
    {
        const uint8_t high =
            ((i + 1UL) < page_length) ? g_install_page_buffer[i + 1UL] : 0xFFU;
        const uint16_t halfword =
            (uint16_t)((uint16_t)g_install_page_buffer[i] |
                       ((uint16_t)high << 8U));

        flash_status = FLASH_ProgramHalfWord(
            APPLICATION_START_ADDRESS + page_offset + i,
            halfword);

        if (flash_status != FLASH_COMPLETE)
        {
            FLASH_Lock();
            RestorePrimask(primask);
            return IMAGE_INSTALLER_FLASH_PROGRAM_FAILED;
        }

#if defined(PHASE7_FAULT_INJECT_OFFSET)
        if ((allow_phase7_fault != 0U) &&
            (ShouldInjectReset(metadata, page_offset, page_length,
                               i + 2UL) != 0U))
        {
            FLASH_Lock();
            RestorePrimask(primask);
            return InjectOneShotReset(metadata);
        }
#else
        (void)allow_phase7_fault;
        (void)metadata;
#endif
    }

    FLASH_Lock();
    RestorePrimask(primask);

    installed = (volatile const uint8_t *)(APPLICATION_START_ADDRESS +
                                           page_offset);
    for (i = 0UL; i < page_length; ++i)
    {
        if (installed[i] != g_install_page_buffer[i])
        {
            return IMAGE_INSTALLER_PAGE_VERIFY_FAILED;
        }
    }

    return IMAGE_INSTALLER_OK;
}

static ImageInstallerStatus_t ProgramAndVerifyCandidatePage(
    const CandidateSource_t *source,
    uint32_t page_offset,
    BootMetadata_t *metadata)
{
    uint32_t page_length = 0UL;
    ImageInstallerStatus_t status =
        LoadCandidatePage(
            source,
            page_offset,
            &page_length);

    if (status != IMAGE_INSTALLER_OK)
    {
        return status;
    }

    return ProgramBufferedApplicationPage(
        page_offset,
        page_length,
        metadata,
        1U);
}

static ImageInstallerStatus_t ProgramAndVerifyBackupPage(
    uint32_t page_offset,
    BootMetadata_t *metadata)
{
    uint32_t page_length = 0UL;
    ImageInstallerStatus_t status =
        LoadBackupPage(page_offset, &page_length);

    if (status != IMAGE_INSTALLER_OK)
    {
        return status;
    }

    return ProgramBufferedApplicationPage(page_offset,
                                          page_length,
                                          metadata,
                                          0U);
}

static ImageInstallerStatus_t BeginBackup(BootMetadata_t *metadata)
{
    if (BackupImageStorage_InvalidateHeader() != BACKUP_IMAGE_STORAGE_OK)
    {
        return IMAGE_INSTALLER_BACKUP_HEADER_FAILED;
    }

    metadata->state = (uint32_t)UPDATE_BACKING_UP;
    metadata->copy_offset = 0UL;
    metadata->boot_attempts = 0UL;
    metadata->last_error = 0UL;

    if (CommitMetadata(metadata) != METADATA_STORAGE_OK)
    {
        return IMAGE_INSTALLER_METADATA_COMMIT_FAILED;
    }

    return IMAGE_INSTALLER_OK;
}

static ImageInstallerStatus_t CopyActiveImageToBackupChunk(
    uint32_t chunk_offset,
    uint32_t chunk_length)
{
    uint32_t copied = 0UL;

    if (!BackupImageStorage_EraseSector(chunk_offset))
    {
        return IMAGE_INSTALLER_BACKUP_ERASE_FAILED;
    }

    while (copied < chunk_length)
    {
        uint32_t length = chunk_length - copied;
        uint32_t i;
        volatile const uint8_t *source;

        if (length > sizeof(g_install_page_buffer))
        {
            length = sizeof(g_install_page_buffer);
        }

        source = (volatile const uint8_t *)(APPLICATION_START_ADDRESS +
                                            chunk_offset + copied);
        for (i = 0UL; i < length; ++i)
        {
            g_install_page_buffer[i] = source[i];
        }

        if (!BackupImageStorage_Write(chunk_offset + copied,
                                      g_install_page_buffer,
                                      length))
        {
            return IMAGE_INSTALLER_BACKUP_WRITE_FAILED;
        }

        if (!BackupImageStorage_Verify(chunk_offset + copied,
                                       g_install_page_buffer,
                                       length))
        {
            return IMAGE_INSTALLER_BACKUP_VERIFY_FAILED;
        }

        copied += length;
    }

    return IMAGE_INSTALLER_OK;
}

static ImageInstallerStatus_t ResumeBackup(BootMetadata_t *metadata)
{
    ImageInstallerStatus_t status;
    uint32_t internal_crc;
    uint32_t backup_crc = 0UL;
    BackupImageRecord_t requested;
    BackupImageRecord_t committed;

    if (BackupProgress_Validate(metadata->copy_offset) !=
        BACKUP_PROGRESS_VALID)
    {
        return IMAGE_INSTALLER_BACKUP_PROGRESS_INVALID;
    }

    while (metadata->copy_offset < APPLICATION_MAX_SIZE)
    {
        const uint32_t chunk_offset = metadata->copy_offset;
        const uint32_t chunk_length =
            BackupProgress_ChunkLength(chunk_offset);
        const uint32_t next_offset =
            BackupProgress_NextCheckpoint(chunk_offset);

        status = CopyActiveImageToBackupChunk(chunk_offset, chunk_length);
        if (status != IMAGE_INSTALLER_OK)
        {
            return status;
        }

        /*
         * A power cut during a 4 KiB W25Q sector copy leaves copy_offset at
         * the previous sector boundary. Recovery re-erases and rewrites that
         * entire sector.
         */
        metadata->copy_offset = next_offset;
        metadata->last_error = 0UL;

        if (CommitMetadata(metadata) != METADATA_STORAGE_OK)
        {
            return IMAGE_INSTALLER_METADATA_COMMIT_FAILED;
        }
    }

    internal_crc = CalculateInternalCrc(APPLICATION_MAX_SIZE);
    status = CalculateBackupCrc(&backup_crc);
    if (status != IMAGE_INSTALLER_OK)
    {
        return status;
    }
    if (internal_crc != backup_crc)
    {
        return IMAGE_INSTALLER_BACKUP_CRC_MISMATCH;
    }

    BackupImage_Init(&requested, metadata->active_version, backup_crc);
    if (BackupImageStorage_CommitHeader(&requested, &committed) !=
        BACKUP_IMAGE_STORAGE_OK)
    {
        return IMAGE_INSTALLER_BACKUP_HEADER_FAILED;
    }

    metadata->state = (uint32_t)UPDATE_INSTALLING;
    metadata->copy_offset = 0UL;
    metadata->last_error = 0UL;

    if (CommitMetadata(metadata) != METADATA_STORAGE_OK)
    {
        return IMAGE_INSTALLER_METADATA_COMMIT_FAILED;
    }

    return IMAGE_INSTALLER_OK;
}

static ImageInstallerStatus_t ResumePageCheckpointedCopy(
    const CandidateSource_t *source,
    BootMetadata_t *metadata)
{
    ImageInstallerStatus_t status;

    if (InstallProgress_Validate(
            source->image_size,
            metadata->copy_offset) != INSTALL_PROGRESS_VALID)
    {
        return IMAGE_INSTALLER_PROGRESS_INVALID;
    }

    while (metadata->copy_offset < source->image_size)
    {
        const uint32_t page_offset = metadata->copy_offset;
        const uint32_t next_offset =
            InstallProgress_NextCheckpoint(
                source->image_size,
                page_offset);

        status = ProgramAndVerifyCandidatePage(
            source,
            page_offset,
            metadata);
        if (status != IMAGE_INSTALLER_OK)
        {
            return status;
        }

        metadata->copy_offset = next_offset;
        metadata->last_error = 0UL;

        if (CommitMetadata(metadata) != METADATA_STORAGE_OK)
        {
            return IMAGE_INSTALLER_METADATA_COMMIT_FAILED;
        }
    }

    return IMAGE_INSTALLER_OK;
}

static ImageInstallerStatus_t VerifyInstalledApplication(
    const CandidateSource_t *source)
{
    volatile const uint32_t *vector =
        (volatile const uint32_t *)APPLICATION_START_ADDRESS;
    ApplicationVector_t application_vector;

    if (CalculateInternalCrc(source->image_size) !=
        source->image_crc32)
    {
        return IMAGE_INSTALLER_VERIFY_CRC_FAILED;
    }

    if (FullImage_ValidateVector(
            source->image_size,
            vector[0],
            vector[1]) != FULL_IMAGE_VALID)
    {
        return IMAGE_INSTALLER_VERIFY_VECTOR_FAILED;
    }

    if (ApplicationJump_Validate(
            APPLICATION_START_ADDRESS,
            &application_vector) != APPLICATION_VALIDATION_OK)
    {
        return IMAGE_INSTALLER_VERIFY_VECTOR_FAILED;
    }

    return IMAGE_INSTALLER_OK;
}

static ImageInstallerStatus_t TransitionToVerify(
    BootMetadata_t *metadata,
    uint32_t image_size)
{
    metadata->state = (uint32_t)UPDATE_VERIFYING_INSTALL;
    metadata->copy_offset = image_size;
    metadata->last_error = 0UL;

    if (CommitMetadata(metadata) != METADATA_STORAGE_OK)
    {
        return IMAGE_INSTALLER_METADATA_COMMIT_FAILED;
    }

    return IMAGE_INSTALLER_OK;
}

static ImageInstallerStatus_t BeginInstall(BootMetadata_t *metadata)
{
    metadata->state = (uint32_t)UPDATE_INSTALLING;
    metadata->copy_offset = 0UL;
    metadata->last_error = 0UL;

    if (CommitMetadata(metadata) != METADATA_STORAGE_OK)
    {
        return IMAGE_INSTALLER_METADATA_COMMIT_FAILED;
    }
    return IMAGE_INSTALLER_OK;
}

static ImageInstallerStatus_t TransitionToTrialBoot(
    BootMetadata_t *metadata,
    BootMetadata_t *result_metadata)
{
    /*
     * The incoming candidate is no longer required for recovery after internal
     * verify: rollback uses the separate validated backup. Clear the Phase-7
     * download checkpoint before publishing TRIAL_BOOT. A reset before the
     * metadata commit simply re-enters VERIFYING_INSTALL and retries.
     */
    if (DownloadCheckpointStorage_Clear() !=
        DOWNLOAD_CHECKPOINT_STORAGE_OK)
    {
        return IMAGE_INSTALLER_CHECKPOINT_CLEAR_FAILED;
    }

    metadata->state = (uint32_t)UPDATE_TRIAL_BOOT;
    metadata->copy_offset = 0UL;
    metadata->boot_attempts = 0UL;
    metadata->last_error = 0UL;

    if (CommitMetadata(metadata) != METADATA_STORAGE_OK)
    {
        return IMAGE_INSTALLER_TRIAL_TRANSITION_FAILED;
    }

    CopyResult(metadata, result_metadata);
    return IMAGE_INSTALLER_OK;
}

static uint32_t RollbackDiagnostic(const BootMetadata_t *metadata)
{
    if (metadata->state == (uint32_t)UPDATE_ROLLBACK)
    {
        return metadata->last_error;
    }

    if (metadata->boot_attempts >= BOOT_METADATA_MAX_BOOT_ATTEMPTS)
    {
        return IMAGE_INSTALLER_PHASE8_ROLLBACK_TRIAL_LIMIT_BASE |
               (metadata->boot_attempts & 0xFFUL);
    }

    if (metadata->state == (uint32_t)UPDATE_TRIAL_BOOT)
    {
        return IMAGE_INSTALLER_PHASE8_ROLLBACK_INVALID_TRIAL;
    }

    return IMAGE_INSTALLER_PHASE8_ROLLBACK_INSTALL_BASE;
}

static ImageInstallerStatus_t BeginRollback(
    BootMetadata_t *metadata,
    uint32_t diagnostic)
{
    metadata->state = (uint32_t)UPDATE_ROLLBACK;
    metadata->copy_offset = 0UL;
    metadata->last_error = diagnostic;

    if (CommitMetadata(metadata) != METADATA_STORAGE_OK)
    {
        return IMAGE_INSTALLER_METADATA_COMMIT_FAILED;
    }
    return IMAGE_INSTALLER_OK;
}

static ImageInstallerStatus_t ResumeRollbackCopy(BootMetadata_t *metadata)
{
    ImageInstallerStatus_t status;

    if (InstallProgress_Validate(APPLICATION_MAX_SIZE,
                                 metadata->copy_offset) !=
        INSTALL_PROGRESS_VALID)
    {
        return IMAGE_INSTALLER_PROGRESS_INVALID;
    }

    while (metadata->copy_offset < APPLICATION_MAX_SIZE)
    {
        const uint32_t page_offset = metadata->copy_offset;
        const uint32_t next_offset =
            InstallProgress_NextCheckpoint(APPLICATION_MAX_SIZE,
                                           page_offset);

        status = ProgramAndVerifyBackupPage(page_offset, metadata);
        if (status != IMAGE_INSTALLER_OK)
        {
            return status;
        }

        metadata->copy_offset = next_offset;
        /* Preserve rollback diagnostic in last_error across every checkpoint. */

        if (CommitMetadata(metadata) != METADATA_STORAGE_OK)
        {
            return IMAGE_INSTALLER_METADATA_COMMIT_FAILED;
        }
    }

    return IMAGE_INSTALLER_OK;
}

static ImageInstallerStatus_t VerifyRolledBackApplication(
    const BackupImageRecord_t *backup)
{
    volatile const uint32_t *vector =
        (volatile const uint32_t *)APPLICATION_START_ADDRESS;
    ApplicationVector_t application_vector;

    if (CalculateInternalCrc(APPLICATION_MAX_SIZE) != backup->image_crc32)
    {
        return IMAGE_INSTALLER_ROLLBACK_VERIFY_FAILED;
    }

    if (FullImage_ValidateVector(APPLICATION_MAX_SIZE,
                                 vector[0], vector[1]) != FULL_IMAGE_VALID)
    {
        return IMAGE_INSTALLER_ROLLBACK_VERIFY_FAILED;
    }

    if (ApplicationJump_Validate(APPLICATION_START_ADDRESS,
                                 &application_vector) !=
        APPLICATION_VALIDATION_OK)
    {
        return IMAGE_INSTALLER_ROLLBACK_VERIFY_FAILED;
    }

    return IMAGE_INSTALLER_OK;
}

static ImageInstallerStatus_t FinalizeRollback(
    BootMetadata_t *metadata,
    const BackupImageRecord_t *backup,
    uint32_t diagnostic,
    BootMetadata_t *result_metadata)
{
    (void)DownloadCheckpointStorage_Clear();

    metadata->state = (uint32_t)UPDATE_IDLE;
    metadata->active_version = backup->active_version;
    metadata->pending_version = 0UL;
    metadata->active_update_id = 0UL;
    metadata->received_size = 0UL;
    metadata->expected_size = 0UL;
    metadata->copy_offset = 0UL;
    metadata->boot_attempts = 0UL;
    /*
     * Unlike a successful confirmation, rollback retains a compact diagnostic
     * so hardware tests and field logs can prove why the previous candidate
     * was rejected. IDLE permits a non-zero last_error.
     */
    metadata->last_error = diagnostic;

    if (CommitMetadata(metadata) != METADATA_STORAGE_OK)
    {
        return IMAGE_INSTALLER_FINALIZE_FAILED;
    }

    CopyResult(metadata, result_metadata);
    return IMAGE_INSTALLER_OK;
}

ImageInstallerStatus_t ImageInstaller_ProcessRollback(
    const BootMetadata_t *metadata,
    BootMetadata_t *result_metadata)
{
    BootMetadata_t working;
    BackupImageRecord_t backup;
    ImageInstallerStatus_t status;
    uint32_t diagnostic;

    if (metadata == (const BootMetadata_t *)0)
    {
        return IMAGE_INSTALLER_ROLLBACK_SOURCE_INVALID;
    }

    working = *metadata;

    if (!ExternalFlashStorage_Init())
    {
        return IMAGE_INSTALLER_EXTERNAL_FLASH_INIT_FAILED;
    }

    status = ValidateBackupImage(&backup);
    if (status != IMAGE_INSTALLER_OK)
    {
        BestEffortMarkFailed(&working, IMAGE_INSTALLER_ROLLBACK_SOURCE_INVALID);
        return IMAGE_INSTALLER_ROLLBACK_SOURCE_INVALID;
    }

    diagnostic = RollbackDiagnostic(&working);

    if (working.state != (uint32_t)UPDATE_ROLLBACK)
    {
        status = BeginRollback(&working, diagnostic);
        if (status != IMAGE_INSTALLER_OK)
        {
            return status;
        }
    }
    else if (InstallProgress_Validate(APPLICATION_MAX_SIZE,
                                      working.copy_offset) !=
             INSTALL_PROGRESS_VALID)
    {
        BestEffortMarkFailed(&working, IMAGE_INSTALLER_PROGRESS_INVALID);
        return IMAGE_INSTALLER_PROGRESS_INVALID;
    }

    status = ResumeRollbackCopy(&working);
    if (status != IMAGE_INSTALLER_OK)
    {
        BestEffortMarkFailed(&working, status);
        return status;
    }

    status = VerifyRolledBackApplication(&backup);
    if (status != IMAGE_INSTALLER_OK)
    {
        BestEffortMarkFailed(&working, status);
        return status;
    }

    return FinalizeRollback(&working, &backup, diagnostic, result_metadata);
}

static ImageInstallerStatus_t RollbackAfterInstallFailure(
    BootMetadata_t *metadata,
    ImageInstallerStatus_t reason,
    BootMetadata_t *result_metadata)
{
    metadata->last_error =
        IMAGE_INSTALLER_PHASE8_ROLLBACK_INSTALL_BASE |
        ((uint32_t)reason & 0xFFUL);

    if (BeginRollback(metadata, metadata->last_error) != IMAGE_INSTALLER_OK)
    {
        return IMAGE_INSTALLER_METADATA_COMMIT_FAILED;
    }

    return ImageInstaller_ProcessRollback(metadata, result_metadata);
}

ImageInstallerStatus_t ImageInstaller_ProcessBasicFull(
    const BootMetadata_t *metadata,
    BootMetadata_t *result_metadata)
{
    BootMetadata_t working;
    CandidateSource_t source;
    BackupImageRecord_t backup;
    ImageInstallerStatus_t status;
    const uint8_t internal_may_be_modified =
        (uint8_t)((metadata != (const BootMetadata_t *)0) &&
                  ((metadata->state == (uint32_t)UPDATE_INSTALLING) ||
                   (metadata->state == (uint32_t)UPDATE_VERIFYING_INSTALL)));

    if (metadata == (const BootMetadata_t *)0)
    {
        return IMAGE_INSTALLER_HANDOFF_MISMATCH;
    }

    working = *metadata;

    if (!ExternalFlashStorage_Init())
    {
        return IMAGE_INSTALLER_EXTERNAL_FLASH_INIT_FAILED;
    }

    status = LoadCandidateSource(&working, &source);
    if (status != IMAGE_INSTALLER_OK)
    {
        if (internal_may_be_modified == 0U)
        {
            return RevertToActiveImage(
                &working,
                status,
                result_metadata);
        }

        return RollbackAfterInstallFailure(
            &working,
            status,
            result_metadata);
    }

    /*
     * Candidate source is revalidated on every recovery boot. Full updates
     * read from Incoming Artifact; Phase-13 deltas read the already verified
     * Reconstructed Image. The active application is not erased before the
     * backup reaches INSTALLING.
     */
    status = ValidateExternalSource(&source);
    if (status != IMAGE_INSTALLER_OK)
    {
        if (internal_may_be_modified == 0U)
        {
            return RevertToActiveImage(
                &working,
                status,
                result_metadata);
        }

        return RollbackAfterInstallFailure(
            &working,
            status,
            result_metadata);
    }

    if ((working.state == (uint32_t)UPDATE_ARTIFACT_READY) ||
        (working.state == (uint32_t)UPDATE_IMAGE_READY))
    {
        status = BeginBackup(&working);
        if (status != IMAGE_INSTALLER_OK)
        {
            BestEffortMarkFailed(&working, status);
            return status;
        }
    }

    if (working.state == (uint32_t)UPDATE_BACKING_UP)
    {
        status = ResumeBackup(&working);
        if (status != IMAGE_INSTALLER_OK)
        {
            /*
             * Internal app remains untouched until backup has completely
             * verified and the metadata state reaches INSTALLING.
             */
            return RevertToActiveImage(
                &working,
                status,
                result_metadata);
        }
    }

    status = ValidateBackupImage(&backup);
    if (status != IMAGE_INSTALLER_OK)
    {
        BestEffortMarkFailed(&working, status);
        return status;
    }

    if (working.state == (uint32_t)UPDATE_VERIFYING_INSTALL)
    {
        status = VerifyInstalledApplication(&source);
        if (status == IMAGE_INSTALLER_OK)
        {
            return TransitionToTrialBoot(
                &working,
                result_metadata);
        }

        status = BeginInstall(&working);
        if (status != IMAGE_INSTALLER_OK)
        {
            return status;
        }
    }
    else if (working.state == (uint32_t)UPDATE_INSTALLING)
    {
        if (InstallProgress_Validate(
                source.image_size,
                working.copy_offset) != INSTALL_PROGRESS_VALID)
        {
            return RollbackAfterInstallFailure(
                &working,
                IMAGE_INSTALLER_PROGRESS_INVALID,
                result_metadata);
        }
    }
    else
    {
        return RollbackAfterInstallFailure(
            &working,
            IMAGE_INSTALLER_HANDOFF_MISMATCH,
            result_metadata);
    }

    status = ResumePageCheckpointedCopy(
        &source,
        &working);
    if (status != IMAGE_INSTALLER_OK)
    {
        if (status == IMAGE_INSTALLER_METADATA_COMMIT_FAILED)
        {
            return status;
        }

        return RollbackAfterInstallFailure(
            &working,
            status,
            result_metadata);
    }

    status = TransitionToVerify(
        &working,
        source.image_size);
    if (status != IMAGE_INSTALLER_OK)
    {
        return status;
    }

    status = VerifyInstalledApplication(&source);
    if (status != IMAGE_INSTALLER_OK)
    {
        return RollbackAfterInstallFailure(
            &working,
            status,
            result_metadata);
    }

    return TransitionToTrialBoot(
        &working,
        result_metadata);
}

