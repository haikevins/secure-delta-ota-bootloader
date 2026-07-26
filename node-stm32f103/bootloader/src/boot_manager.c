#include "boot_manager.h"

#include <stdint.h>

#include "application_jump.h"
#include "boot_decision.h"
#include "boot_metadata.h"
#include "metadata_storage.h"
#include "memory_map.h"
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

static uint8_t BootManager_ActionCanJump(BootAction_t action)
{
    return (uint8_t)((action == BOOT_ACTION_JUMP_ACTIVE) ||
                     (action == BOOT_ACTION_RESUME_DOWNLOAD) ||
                     (action == BOOT_ACTION_BOOT_TRIAL));
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
    uint8_t application_valid;

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
            /* Metadata failure must not brick a previously valid product image
             * during Phase 3. Use a finalized RAM default for this boot only. */
            metadata.generation = BOOT_METADATA_FIRST_GENERATION;
            BootMetadata_Finalize(&metadata);
        }
    }
    else if (storage_status != METADATA_STORAGE_OK)
    {
        BootManager_ShowFatalPulses(METADATA_ERROR_PULSES);
    }

    application_status = ApplicationJump_Validate(APPLICATION_START_ADDRESS,
                                                   &application_vector);
    application_valid = (uint8_t)(application_status ==
                                  APPLICATION_VALIDATION_OK);

    decision = BootDecision_Evaluate(&metadata, application_valid);

    if (BootManager_ActionCanJump(decision.action) != 0U)
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

    /* Update recovery actions are deliberately selected but not executed until
     * their owning phases. Remaining in the bootloader is safer than silently
     * jumping while an install/rollback state is pending. */
    BootManager_ShowFatalPulses(RECOVERY_ACTION_PULSES);
}
