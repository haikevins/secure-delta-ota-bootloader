#include "boot_manager.h"

#include <stdint.h>

#include "application_jump.h"
#include "boot_decision.h"
#include "delta_patcher.h"
#include "boot_metadata.h"
#include "image_installer.h"
#include "secure_container.h"
#include "metadata_storage.h"
#include "memory_map.h"
#include "trial_boot.h"
#include "stm32f10x.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"

#define STATUS_LED_PORT             GPIOC
#define STATUS_LED_PIN              GPIO_Pin_13
#define STARTUP_BLINK_COUNT         5UL
#define STARTUP_ON_TIME_MS          100UL
#define STARTUP_OFF_TIME_MS         100UL
#define STARTUP_FINAL_PAUSE_MS      1000UL
#define ERROR_ON_TIME_MS            120UL
#define ERROR_OFF_TIME_MS           180UL
#define ERROR_PAUSE_MS              900UL
#define METADATA_ERROR_PULSES       8UL
#define RECOVERY_ACTION_PULSES      9UL
#define INSTALL_ERROR_PULSES        10UL
#define TRIAL_ERROR_PULSES          11UL
#define ROLLBACK_ERROR_PULSES       12UL
#define DELTA_ERROR_PULSES          13UL
#define SECURE_ERROR_PULSES         14UL
#define MAX_BOOT_TRANSITIONS        8UL

static volatile uint32_t g_boot_tick_ms;

void SysTick_Handler(void)
{
    ++g_boot_tick_ms;
}

static void BootManager_LedInit(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = STATUS_LED_PIN;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(STATUS_LED_PORT, &gpio);

    GPIO_SetBits(STATUS_LED_PORT, STATUS_LED_PIN);
}

static void BootManager_LedSet(uint8_t enabled)
{
    if (enabled != 0U)
    {
        GPIO_ResetBits(STATUS_LED_PORT, STATUS_LED_PIN);
    }
    else
    {
        GPIO_SetBits(STATUS_LED_PORT, STATUS_LED_PIN);
    }
}

static void BootManager_DelayMs(uint32_t duration_ms)
{
    const uint32_t start = g_boot_tick_ms;

    while ((uint32_t)(g_boot_tick_ms - start) < duration_ms)
    {
        __WFI();
    }
}

static void BootManager_ShowStartupWindow(void)
{
    uint32_t blink;

    for (blink = 0UL; blink < STARTUP_BLINK_COUNT; ++blink)
    {
        BootManager_LedSet(1U);
        BootManager_DelayMs(STARTUP_ON_TIME_MS);
        BootManager_LedSet(0U);
        BootManager_DelayMs(STARTUP_OFF_TIME_MS);
    }

    BootManager_DelayMs(STARTUP_FINAL_PAUSE_MS);
}

static uint32_t BootManager_ApplicationErrorPulseCount(
    ApplicationValidationStatus_t status)
{
    switch (status)
    {
        case APPLICATION_VALIDATION_BAD_VECTOR_ALIGNMENT:
            return 1UL;
        case APPLICATION_VALIDATION_BAD_VECTOR_RANGE:
            return 2UL;
        case APPLICATION_VALIDATION_BAD_STACK_RANGE:
            return 3UL;
        case APPLICATION_VALIDATION_BAD_STACK_ALIGNMENT:
            return 4UL;
        case APPLICATION_VALIDATION_BAD_RESET_THUMB_BIT:
            return 5UL;
        case APPLICATION_VALIDATION_BAD_RESET_RANGE:
            return 6UL;
        case APPLICATION_VALIDATION_OK:
        default:
            return 7UL;
    }
}

static void BootManager_ShowFatalPulses(uint32_t pulse_count)
    __attribute__((noreturn));

static void BootManager_ShowFatalPulses(uint32_t pulse_count)
{
    for (;;)
    {
        uint32_t pulse;

        for (pulse = 0UL; pulse < pulse_count; ++pulse)
        {
            BootManager_LedSet(1U);
            BootManager_DelayMs(ERROR_ON_TIME_MS);
            BootManager_LedSet(0U);
            BootManager_DelayMs(ERROR_OFF_TIME_MS);
        }

        BootManager_DelayMs(ERROR_PAUSE_MS);
    }
}

static uint8_t BootManager_ActionNeedsInstallWork(BootAction_t action)
{
    return (uint8_t)((action == BOOT_ACTION_PROCESS_ARTIFACT) ||
                     (action == BOOT_ACTION_CONTINUE_BACKUP) ||
                     (action == BOOT_ACTION_RESUME_INSTALL) ||
                     (action == BOOT_ACTION_VERIFY_INSTALL));
}

static void BootManager_RefreshApplication(
    ApplicationVector_t *application_vector,
    ApplicationValidationStatus_t *application_status,
    uint8_t *application_valid)
{
    *application_status = ApplicationJump_Validate(APPLICATION_START_ADDRESS,
                                                   application_vector);
    *application_valid =
        (uint8_t)(*application_status == APPLICATION_VALIDATION_OK);
}

void BootManager_Run(void)
{
    ApplicationVector_t application_vector;
    ApplicationValidationStatus_t application_status;
    BootMetadata_t metadata;
    BootMetadata_t committed_metadata;
    BootMetadataSlot_t active_slot;
    MetadataStorageStatus_t storage_status;
    BootDecision_t decision;
    ImageInstallerStatus_t installer_status;
    DeltaPatcherStatus_t delta_status;
    SecureContainerStatus_t secure_status;
    TrialBootStatus_t trial_status;
    uint8_t application_valid;
    uint32_t transitions;

    BootManager_LedInit();

    if (SysTick_Config(SystemCoreClock / 1000UL) != 0UL)
    {
        BootManager_ShowFatalPulses(7UL);
    }

    storage_status = MetadataStorage_Load(&metadata, &active_slot);
    if (storage_status == METADATA_STORAGE_DEFAULTS_USED)
    {
        storage_status = MetadataStorage_Commit(&metadata,
                                                &committed_metadata,
                                                &active_slot);
        if (storage_status == METADATA_STORAGE_OK)
        {
            metadata = committed_metadata;
        }
        else
        {
            /*
             * Retain the persistent metadata non-bricking fallback for a previously valid
             * factory image if the very first metadata commit fails.
             */
            metadata.generation = BOOT_METADATA_FIRST_GENERATION;
            BootMetadata_Finalize(&metadata);
        }
    }
    else if (storage_status != METADATA_STORAGE_OK)
    {
        BootManager_ShowFatalPulses(METADATA_ERROR_PULSES);
    }

    BootManager_RefreshApplication(&application_vector,
                                   &application_status,
                                   &application_valid);

    for (transitions = 0UL;
         transitions < MAX_BOOT_TRANSITIONS;
         ++transitions)
    {
        decision = BootDecision_Evaluate(&metadata, application_valid);

        if ((decision.action == BOOT_ACTION_PROCESS_ARTIFACT) ||
            (decision.action == BOOT_ACTION_RESTART_VALIDATION) ||
            (decision.action == BOOT_ACTION_RESTART_PATCH))
        {
            if (SecureContainer_IsIncoming() != 0U)
            {
                secure_status =
                    SecureContainer_Process(
                        &metadata,
                        &committed_metadata);

                if ((secure_status != SECURE_CONTAINER_OK) &&
                    (secure_status != SECURE_CONTAINER_SOURCE_REJECTED))
                {
                    BootManager_ShowFatalPulses(
                        SECURE_ERROR_PULSES);
                }

                metadata = committed_metadata;
                BootManager_RefreshApplication(
                    &application_vector,
                    &application_status,
                    &application_valid);
                continue;
            }

#if SECURE_CONTAINER_ALLOW_UNSIGNED_LEGACY != 0
            if (DeltaPatcher_IsDeltaArtifact() != 0U)
            {
                delta_status =
                    DeltaPatcher_Process(
                        &metadata,
                        &committed_metadata);

                if ((delta_status != DELTA_PATCHER_OK) &&
                    (delta_status != DELTA_PATCHER_SOURCE_REJECTED))
                {
                    BootManager_ShowFatalPulses(
                        DELTA_ERROR_PULSES);
                }

                metadata = committed_metadata;
                BootManager_RefreshApplication(
                    &application_vector,
                    &application_status,
                    &application_valid);
                continue;
            }

            if (decision.action != BOOT_ACTION_PROCESS_ARTIFACT)
            {
                BootManager_ShowFatalPulses(
                    DELTA_ERROR_PULSES);
            }
#else
            /*
             * Secure signed secure container builds reject every unsigned legacy artifact.
             * SecureContainer_Process performs a safe IDLE rejection and
             * preserves the active application.
             */
            secure_status =
                SecureContainer_Process(
                    &metadata,
                    &committed_metadata);

            if (secure_status != SECURE_CONTAINER_SOURCE_REJECTED)
            {
                BootManager_ShowFatalPulses(
                    SECURE_ERROR_PULSES);
            }

            metadata = committed_metadata;
            BootManager_RefreshApplication(
                &application_vector,
                &application_status,
                &application_valid);
            continue;
#endif
        }

        if (BootManager_ActionNeedsInstallWork(decision.action) != 0U)
        {
            installer_status =
                ImageInstaller_ProcessBasicFull(&metadata,
                                                &committed_metadata);

            if ((installer_status != IMAGE_INSTALLER_OK) &&
                (installer_status != IMAGE_INSTALLER_SOURCE_REJECTED))
            {
                BootManager_ShowFatalPulses(INSTALL_ERROR_PULSES);
            }

            metadata = committed_metadata;
            BootManager_RefreshApplication(&application_vector,
                                           &application_status,
                                           &application_valid);
            continue;
        }

        if (decision.action == BOOT_ACTION_FINALIZE_CONFIRMATION)
        {
            trial_status =
                TrialBoot_FinalizeConfirmation(&metadata,
                                               &committed_metadata);
            if (trial_status != TRIAL_BOOT_STATUS_OK)
            {
                BootManager_ShowFatalPulses(TRIAL_ERROR_PULSES);
            }

            metadata = committed_metadata;
            BootManager_RefreshApplication(&application_vector,
                                           &application_status,
                                           &application_valid);
            continue;
        }

        if (decision.action == BOOT_ACTION_RESUME_ROLLBACK)
        {
            installer_status =
                ImageInstaller_ProcessRollback(&metadata,
                                               &committed_metadata);
            if (installer_status != IMAGE_INSTALLER_OK)
            {
                BootManager_ShowFatalPulses(ROLLBACK_ERROR_PULSES);
            }

            metadata = committed_metadata;
            BootManager_RefreshApplication(&application_vector,
                                           &application_status,
                                           &application_valid);
            continue;
        }

        if (decision.action == BOOT_ACTION_BOOT_TRIAL)
        {
            /*
             * Increment and persist the attempt BEFORE jumping. A reset or
             * power loss immediately after this commit therefore counts as a
             * failed trial rather than creating an infinite boot loop.
             */
            trial_status = TrialBoot_PrepareAttempt(&metadata,
                                                    &committed_metadata);
            if (trial_status != TRIAL_BOOT_STATUS_OK)
            {
                BootManager_ShowFatalPulses(TRIAL_ERROR_PULSES);
            }
            metadata = committed_metadata;

            BootManager_ShowStartupWindow();

            /*
             * IWDG is enabled only for a trial boot. The candidate must reach
             * its health-confirmation path and reset back through the
             * bootloader before the watchdog expires.
             */
            TrialBoot_StartWatchdog();
            ApplicationJump_Execute(&application_vector);
        }

        if ((decision.action == BOOT_ACTION_JUMP_ACTIVE) ||
            (decision.action == BOOT_ACTION_RESUME_DOWNLOAD))
        {
            BootManager_ShowStartupWindow();
            ApplicationJump_Execute(&application_vector);
        }

        if ((application_valid == 0U) &&
            (decision.reason == BOOT_DECISION_REASON_APPLICATION_INVALID))
        {
            BootManager_ShowFatalPulses(
                BootManager_ApplicationErrorPulseCount(application_status));
        }

        /*
         * Delta/secure-only states still deliberately remain in recovery until
         * the owning recovery action is executed.
         */
        BootManager_ShowFatalPulses(RECOVERY_ACTION_PULSES);
    }

    BootManager_ShowFatalPulses(RECOVERY_ACTION_PULSES);
}
