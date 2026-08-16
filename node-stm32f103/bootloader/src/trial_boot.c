#include "trial_boot.h"

#include <stdint.h>

#include "metadata_storage.h"
#include "stm32f10x_iwdg.h"

#define TRIAL_IWDG_RELOAD 3000U

static MetadataStorageStatus_t Commit(BootMetadata_t *metadata,
                                      BootMetadata_t *committed)
{
    BootMetadata_t result;
    const MetadataStorageStatus_t status =
        MetadataStorage_Commit(metadata, &result, (BootMetadataSlot_t *)0);

    if (status == METADATA_STORAGE_OK)
    {
        *metadata = result;
        if (committed != (BootMetadata_t *)0)
        {
            *committed = result;
        }
    }

    return status;
}

TrialBootStatus_t TrialBoot_PrepareAttempt(
    const BootMetadata_t *metadata,
    BootMetadata_t *committed_metadata)
{
    BootMetadata_t working;

    if ((metadata == (const BootMetadata_t *)0) ||
        (metadata->state != (uint32_t)UPDATE_TRIAL_BOOT))
    {
        return TRIAL_BOOT_STATUS_INVALID_STATE;
    }

    if (metadata->boot_attempts >= BOOT_METADATA_MAX_BOOT_ATTEMPTS)
    {
        return TRIAL_BOOT_STATUS_ATTEMPT_LIMIT;
    }

    working = *metadata;
    ++working.boot_attempts;
    working.last_error = 0UL;

    if (Commit(&working, committed_metadata) != METADATA_STORAGE_OK)
    {
        return TRIAL_BOOT_STATUS_METADATA_COMMIT_FAILED;
    }

    return TRIAL_BOOT_STATUS_OK;
}

TrialBootStatus_t TrialBoot_FinalizeConfirmation(
    const BootMetadata_t *metadata,
    BootMetadata_t *committed_metadata)
{
    BootMetadata_t working;

    if ((metadata == (const BootMetadata_t *)0) ||
        (metadata->state != (uint32_t)UPDATE_CONFIRMED) ||
        (metadata->pending_version == 0UL))
    {
        return TRIAL_BOOT_STATUS_INVALID_STATE;
    }

    working = *metadata;
    working.state = (uint32_t)UPDATE_IDLE;
    working.active_version = working.pending_version;
    working.pending_version = 0UL;
    working.active_update_id = 0UL;
    working.received_size = 0UL;
    working.expected_size = 0UL;
    working.copy_offset = 0UL;
    working.boot_attempts = 0UL;
    working.last_error = 0UL;

    if (Commit(&working, committed_metadata) != METADATA_STORAGE_OK)
    {
        return TRIAL_BOOT_STATUS_METADATA_COMMIT_FAILED;
    }

    return TRIAL_BOOT_STATUS_OK;
}

void TrialBoot_StartWatchdog(void)
{
    /*
     * LSI is nominally ~40 kHz. Prescaler 64 / reload 3000 gives a several
     * second health window across normal LSI tolerance. Healthy trial boot and rollback
     * candidates confirm well before this deadline.
     */
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(IWDG_Prescaler_64);
    IWDG_SetReload(TRIAL_IWDG_RELOAD);
    IWDG_ReloadCounter();
    IWDG_Enable();
}
