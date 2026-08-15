#include "image_installer.h"

#include <stdint.h>

#include "application_jump.h"
#include "crc32.h"
#include "external_flash_storage.h"
#include "full_image_validation.h"
#include "memory_map.h"
#include "metadata_storage.h"
#include "spi_flash.h"
#include "stm32f10x.h"
#include "stm32f10x_flash.h"
#include "update_handoff.h"
#include "update_handoff_storage.h"

#define INSTALL_BUFFER_SIZE 256UL
#define PHASE6_ERROR_BASE    0x00060000UL

static uint32_t InstallerError(ImageInstallerStatus_t status)
{
    return PHASE6_ERROR_BASE | (uint32_t)status;
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

static uint8_t RecordMatchesMetadata(const UpdateHandoffRecord_t *record,
                                     const BootMetadata_t *metadata)
{
    return (uint8_t)(
        (record->update_id == metadata->active_update_id) &&
        (record->target_version == metadata->pending_version) &&
        (record->image_size == metadata->expected_size) &&
        (metadata->received_size == metadata->expected_size));
}

static MetadataStorageStatus_t CommitMetadata(BootMetadata_t *metadata)
{
    BootMetadata_t committed;
    const MetadataStorageStatus_t status =
        MetadataStorage_Commit(metadata, &committed, (BootMetadataSlot_t *)0);
    if (status == METADATA_STORAGE_OK) { *metadata = committed; }
    return status;
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

    if (CommitMetadata(metadata) != METADATA_STORAGE_OK)
    {
        return IMAGE_INSTALLER_METADATA_COMMIT_FAILED;
    }
    if (result_metadata != (BootMetadata_t *)0) { *result_metadata = *metadata; }
    return IMAGE_INSTALLER_SOURCE_REJECTED;
}

static void BestEffortMarkFailed(BootMetadata_t *metadata,
                                 ImageInstallerStatus_t reason)
{
    metadata->state = (uint32_t)UPDATE_FAILED;
    metadata->last_error = InstallerError(reason);
    (void)CommitMetadata(metadata);
}

static ImageInstallerStatus_t CalculateExternalCrc(
    uint32_t length,
    uint32_t *crc_out)
{
    uint8_t buffer[INSTALL_BUFFER_SIZE];
    uint32_t offset = 0UL;
    uint32_t running = CRC32_IEEE_INITIAL_VALUE;

    if (crc_out == (uint32_t *)0)
    {
        return IMAGE_INSTALLER_SOURCE_READ_FAILED;
    }

    while (offset < length)
    {
        uint32_t chunk = length - offset;
        if (chunk > sizeof(buffer)) { chunk = sizeof(buffer); }

        if (!ExternalFlashStorage_Read(EXTERNAL_FLASH_PARTITION_INCOMING,
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

static ImageInstallerStatus_t ValidateExternalSource(
    const UpdateHandoffRecord_t *record)
{
    uint8_t vector[8];
    uint32_t crc;
    ImageInstallerStatus_t crc_status;

    if (!ExternalFlashStorage_Read(EXTERNAL_FLASH_PARTITION_INCOMING,
                                   0UL, vector, sizeof(vector)))
    {
        return IMAGE_INSTALLER_SOURCE_READ_FAILED;
    }

    if (FullImage_ValidateVector(record->image_size,
                                 GetU32Le(&vector[0]),
                                 GetU32Le(&vector[4])) != FULL_IMAGE_VALID)
    {
        return IMAGE_INSTALLER_SOURCE_VECTOR_INVALID;
    }

    crc_status = CalculateExternalCrc(record->image_size, &crc);
    if (crc_status != IMAGE_INSTALLER_OK) { return crc_status; }
    if (crc != record->image_crc32)
    {
        return IMAGE_INSTALLER_SOURCE_CRC_MISMATCH;
    }

    return IMAGE_INSTALLER_OK;
}

static ImageInstallerStatus_t EraseAndProgramApplication(
    const UpdateHandoffRecord_t *record)
{
    uint8_t buffer[INSTALL_BUFFER_SIZE];
    uint32_t page;
    uint32_t offset = 0UL;
    uint32_t primask;
    FLASH_Status flash_status;

    primask = GetPrimask();
    __disable_irq();

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);

    for (page = APPLICATION_START_ADDRESS;
         page < APPLICATION_END_ADDRESS;
         page += INTERNAL_FLASH_PAGE_SIZE)
    {
        flash_status = FLASH_ErasePage(page);
        if (flash_status != FLASH_COMPLETE)
        {
            FLASH_Lock();
            RestorePrimask(primask);
            return IMAGE_INSTALLER_FLASH_ERASE_FAILED;
        }
    }

    while (offset < record->image_size)
    {
        uint32_t chunk = record->image_size - offset;
        uint32_t i;

        if (chunk > sizeof(buffer)) { chunk = sizeof(buffer); }

        if (!ExternalFlashStorage_Read(EXTERNAL_FLASH_PARTITION_INCOMING,
                                       offset, buffer, chunk))
        {
            FLASH_Lock();
            RestorePrimask(primask);
            return IMAGE_INSTALLER_SOURCE_READ_FAILED;
        }

        for (i = 0UL; i < chunk; i += 2UL)
        {
            const uint8_t high =
                ((i + 1UL) < chunk) ? buffer[i + 1UL] : 0xFFU;
            const uint16_t halfword =
                (uint16_t)((uint16_t)buffer[i] |
                           ((uint16_t)high << 8U));

            flash_status = FLASH_ProgramHalfWord(
                APPLICATION_START_ADDRESS + offset + i,
                halfword);
            if (flash_status != FLASH_COMPLETE)
            {
                FLASH_Lock();
                RestorePrimask(primask);
                return IMAGE_INSTALLER_FLASH_PROGRAM_FAILED;
            }
        }

        offset += chunk;
    }

    FLASH_Lock();
    RestorePrimask(primask);
    return IMAGE_INSTALLER_OK;
}

static uint32_t CalculateInternalCrc(uint32_t length)
{
    uint8_t buffer[INSTALL_BUFFER_SIZE];
    uint32_t offset = 0UL;
    uint32_t running = CRC32_IEEE_INITIAL_VALUE;

    while (offset < length)
    {
        uint32_t chunk = length - offset;
        uint32_t i;
        volatile const uint8_t *source;

        if (chunk > sizeof(buffer)) { chunk = sizeof(buffer); }
        source = (volatile const uint8_t *)(APPLICATION_START_ADDRESS + offset);
        for (i = 0UL; i < chunk; ++i) { buffer[i] = source[i]; }
        running = Crc32_Update(running, buffer, chunk);
        offset += chunk;
    }

    return running ^ CRC32_IEEE_FINAL_XOR;
}

static ImageInstallerStatus_t VerifyInstalledApplication(
    const UpdateHandoffRecord_t *record)
{
    volatile const uint32_t *vector =
        (volatile const uint32_t *)APPLICATION_START_ADDRESS;
    ApplicationVector_t application_vector;

    if (CalculateInternalCrc(record->image_size) != record->image_crc32)
    {
        return IMAGE_INSTALLER_VERIFY_CRC_FAILED;
    }

    if (FullImage_ValidateVector(record->image_size,
                                 vector[0], vector[1]) != FULL_IMAGE_VALID)
    {
        return IMAGE_INSTALLER_VERIFY_VECTOR_FAILED;
    }

    if (ApplicationJump_Validate(APPLICATION_START_ADDRESS,
                                 &application_vector) != APPLICATION_VALIDATION_OK)
    {
        return IMAGE_INSTALLER_VERIFY_VECTOR_FAILED;
    }

    return IMAGE_INSTALLER_OK;
}

static ImageInstallerStatus_t FinalizeSuccessfulInstall(
    BootMetadata_t *metadata,
    const UpdateHandoffRecord_t *record,
    BootMetadata_t *result_metadata)
{
    metadata->state = (uint32_t)UPDATE_IDLE;
    metadata->active_version = record->target_version;
    metadata->pending_version = 0UL;
    metadata->active_update_id = 0UL;
    metadata->received_size = 0UL;
    metadata->expected_size = 0UL;
    metadata->copy_offset = 0UL;
    metadata->boot_attempts = 0UL;
    metadata->last_error = 0UL;

    if (CommitMetadata(metadata) != METADATA_STORAGE_OK)
    {
        return IMAGE_INSTALLER_FINALIZE_FAILED;
    }

    if (result_metadata != (BootMetadata_t *)0) { *result_metadata = *metadata; }
    return IMAGE_INSTALLER_OK;
}

ImageInstallerStatus_t ImageInstaller_ProcessBasicFull(
    const BootMetadata_t *metadata,
    BootMetadata_t *result_metadata)
{
    BootMetadata_t working;
    UpdateHandoffRecord_t record;
    UpdateHandoffStorageStatus_t handoff_status;
    ImageInstallerStatus_t status;
    const uint8_t install_already_started =
        (uint8_t)((metadata != (const BootMetadata_t *)0) &&
                  (metadata->state != (uint32_t)UPDATE_ARTIFACT_READY));

    if (metadata == (const BootMetadata_t *)0)
    {
        return IMAGE_INSTALLER_HANDOFF_MISMATCH;
    }
    working = *metadata;

    if (!ExternalFlashStorage_Init())
    {
        return IMAGE_INSTALLER_EXTERNAL_FLASH_INIT_FAILED;
    }

    handoff_status = UpdateHandoffStorage_Load(&record,
                                               (UpdateHandoffSlot_t *)0);
    if (handoff_status != UPDATE_HANDOFF_STORAGE_OK)
    {
        if (install_already_started == 0U)
        {
            return RevertToActiveImage(&working,
                                       IMAGE_INSTALLER_HANDOFF_LOAD_FAILED,
                                       result_metadata);
        }
        BestEffortMarkFailed(&working, IMAGE_INSTALLER_HANDOFF_LOAD_FAILED);
        return IMAGE_INSTALLER_HANDOFF_LOAD_FAILED;
    }

    if (RecordMatchesMetadata(&record, &working) == 0U)
    {
        if (install_already_started == 0U)
        {
            return RevertToActiveImage(&working,
                                       IMAGE_INSTALLER_HANDOFF_MISMATCH,
                                       result_metadata);
        }
        BestEffortMarkFailed(&working, IMAGE_INSTALLER_HANDOFF_MISMATCH);
        return IMAGE_INSTALLER_HANDOFF_MISMATCH;
    }

    if (working.state == (uint32_t)UPDATE_VERIFYING_INSTALL)
    {
        status = VerifyInstalledApplication(&record);
        if (status == IMAGE_INSTALLER_OK)
        {
            return FinalizeSuccessfulInstall(&working, &record,
                                             result_metadata);
        }
        /* Coarse Phase-6 recovery: reinstall from offset zero. Phase 7 adds
         * persistent copy checkpoints instead of restarting the whole copy. */
    }

    status = ValidateExternalSource(&record);
    if (status != IMAGE_INSTALLER_OK)
    {
        if (install_already_started == 0U)
        {
            return RevertToActiveImage(&working, status, result_metadata);
        }
        BestEffortMarkFailed(&working, status);
        return status;
    }

    working.state = (uint32_t)UPDATE_INSTALLING;
    working.copy_offset = 0UL;
    working.last_error = 0UL;
    if (CommitMetadata(&working) != METADATA_STORAGE_OK)
    {
        return IMAGE_INSTALLER_METADATA_COMMIT_FAILED;
    }

    status = EraseAndProgramApplication(&record);
    if (status != IMAGE_INSTALLER_OK)
    {
        BestEffortMarkFailed(&working, status);
        return status;
    }

    working.state = (uint32_t)UPDATE_VERIFYING_INSTALL;
    working.copy_offset = record.image_size;
    if (CommitMetadata(&working) != METADATA_STORAGE_OK)
    {
        BestEffortMarkFailed(&working, IMAGE_INSTALLER_METADATA_COMMIT_FAILED);
        return IMAGE_INSTALLER_METADATA_COMMIT_FAILED;
    }

    status = VerifyInstalledApplication(&record);
    if (status != IMAGE_INSTALLER_OK)
    {
        BestEffortMarkFailed(&working, status);
        return status;
    }

    return FinalizeSuccessfulInstall(&working, &record, result_metadata);
}
