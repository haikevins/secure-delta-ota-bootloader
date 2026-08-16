#include "trial_confirmation.h"

#include <stdint.h>

#include "boot_metadata.h"
#include "firmware_version.h"
#include "metadata_storage.h"
#include "stm32f10x.h"

#ifndef TRIAL_CONFIRM_DELAY_MS
#define TRIAL_CONFIRM_DELAY_MS 750UL
#endif

static uint8_t g_trial_active;
static uint8_t g_trial_confirmed;
static uint32_t g_trial_start_ms;

static bool LoadTrialMetadata(BootMetadata_t *metadata)
{
    const MetadataStorageStatus_t status =
        MetadataStorage_Load(metadata, (BootMetadataSlot_t *)0);

    return (status == METADATA_STORAGE_OK) &&
           (metadata->state == (uint32_t)UPDATE_TRIAL_BOOT) &&
           (metadata->pending_version == APPLICATION_VERSION) &&
           (metadata->boot_attempts > 0UL) &&
           (metadata->boot_attempts <= BOOT_METADATA_MAX_BOOT_ATTEMPTS);
}

bool TrialConfirmation_Init(uint32_t now_ms)
{
    BootMetadata_t metadata;

    g_trial_active = 0U;
    g_trial_confirmed = 0U;
    g_trial_start_ms = now_ms;

    if (LoadTrialMetadata(&metadata))
    {
        g_trial_active = 1U;
    }

    return true;
}

bool TrialConfirmation_IsActive(void)
{
    return g_trial_active != 0U;
}

bool TrialConfirmation_ConfirmNow(uint32_t *detail)
{
    BootMetadata_t metadata;
    BootMetadata_t committed;
    MetadataStorageStatus_t status;

    if (!LoadTrialMetadata(&metadata))
    {
        if (detail != (uint32_t *)0)
        {
            *detail = 1UL;
        }
        return false;
    }

    metadata.state = (uint32_t)UPDATE_CONFIRMED;
    metadata.copy_offset = 0UL;
    metadata.last_error = 0UL;

    status = MetadataStorage_Commit(&metadata,
                                    &committed,
                                    (BootMetadataSlot_t *)0);
    if (status != METADATA_STORAGE_OK)
    {
        if (detail != (uint32_t *)0)
        {
            *detail = 0x100UL | (uint32_t)status;
        }
        return false;
    }

    g_trial_confirmed = 1U;
    if (detail != (uint32_t *)0)
    {
        *detail = 0UL;
    }
    return true;
}

void TrialConfirmation_Process(uint32_t now_ms)
{
#if defined(DISABLE_TRIAL_CONFIRM)
    (void)now_ms;
#else
    if ((g_trial_active != 0U) &&
        (g_trial_confirmed == 0U) &&
        ((uint32_t)(now_ms - g_trial_start_ms) >=
         TRIAL_CONFIRM_DELAY_MS))
    {
        uint32_t detail = 0UL;

        /*
         * Reaching this point proves clocks, SysTick, GPIO setup, UART/OTA
         * agent initialization and the main loop have all started normally.
         */
        if (TrialConfirmation_ConfirmNow(&detail))
        {
            __DSB();
            NVIC_SystemReset();
            for (;;)
            {
                __NOP();
            }
        }
    }
#endif
}
