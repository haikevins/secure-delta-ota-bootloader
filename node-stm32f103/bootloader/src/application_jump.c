#include "application_jump.h"

#include <stddef.h>

#include "memory_map.h"
#include "stm32f10x.h"
#include "stm32f10x_rcc.h"

#define VECTOR_TABLE_ALIGNMENT       0x200UL
#define THUMB_STATE_BIT              0x00000001UL
#define ADDRESS_WITHOUT_THUMB_MASK   0xFFFFFFFEUL
#define REQUIRED_STACK_ALIGNMENT     8UL
#define NVIC_REGISTER_BANK_COUNT     8UL

extern void ApplicationJump_SetStackAndBranch(
    uint32_t initial_stack_pointer,
    uint32_t reset_handler_address) __attribute__((noreturn));

static uint8_t ApplicationJump_IsStackPointerInRange(uint32_t stack_pointer)
{
    return (uint8_t)((stack_pointer >= INTERNAL_SRAM_BASE) &&
                     (stack_pointer <= INTERNAL_SRAM_END));
}

static uint8_t ApplicationJump_IsResetHandlerInRange(uint32_t reset_handler)
{
    const uint32_t code_address = reset_handler & ADDRESS_WITHOUT_THUMB_MASK;

    return (uint8_t)((code_address >= APPLICATION_START_ADDRESS) &&
                     (code_address < BOOT_METADATA_ADDRESS));
}

ApplicationValidationStatus_t ApplicationJump_Validate(
    uint32_t vector_table_address,
    ApplicationVector_t *application_vector)
{
    volatile const uint32_t *vector_table;
    uint32_t initial_stack_pointer;
    uint32_t reset_handler_address;

    if ((vector_table_address & (VECTOR_TABLE_ALIGNMENT - 1UL)) != 0UL)
    {
        return APPLICATION_VALIDATION_BAD_VECTOR_ALIGNMENT;
    }

    if ((vector_table_address < APPLICATION_START_ADDRESS) ||
        (vector_table_address > (BOOT_METADATA_ADDRESS - (2UL * sizeof(uint32_t)))))
    {
        return APPLICATION_VALIDATION_BAD_VECTOR_RANGE;
    }

    vector_table = (volatile const uint32_t *)vector_table_address;
    initial_stack_pointer = vector_table[0];
    reset_handler_address = vector_table[1];

    if (ApplicationJump_IsStackPointerInRange(initial_stack_pointer) == 0U)
    {
        return APPLICATION_VALIDATION_BAD_STACK_RANGE;
    }

    if ((initial_stack_pointer & (REQUIRED_STACK_ALIGNMENT - 1UL)) != 0UL)
    {
        return APPLICATION_VALIDATION_BAD_STACK_ALIGNMENT;
    }

    if ((reset_handler_address & THUMB_STATE_BIT) == 0UL)
    {
        return APPLICATION_VALIDATION_BAD_RESET_THUMB_BIT;
    }

    if (ApplicationJump_IsResetHandlerInRange(reset_handler_address) == 0U)
    {
        return APPLICATION_VALIDATION_BAD_RESET_RANGE;
    }

    if (application_vector != NULL)
    {
        application_vector->vector_table_address = vector_table_address;
        application_vector->initial_stack_pointer = initial_stack_pointer;
        application_vector->reset_handler_address = reset_handler_address;
    }

    return APPLICATION_VALIDATION_OK;
}

static void ApplicationJump_StopSystemTick(void)
{
    SysTick->CTRL = 0UL;
    SysTick->LOAD = 0UL;
    SysTick->VAL = 0UL;
}

static void ApplicationJump_DisableAndClearInterrupts(void)
{
    uint32_t bank;

    __disable_irq();

    for (bank = 0UL; bank < NVIC_REGISTER_BANK_COUNT; ++bank)
    {
        NVIC->ICER[bank] = 0xFFFFFFFFUL;
        NVIC->ICPR[bank] = 0xFFFFFFFFUL;
    }

    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
}

void ApplicationJump_Execute(const ApplicationVector_t *application_vector)
{
    uint32_t initial_stack_pointer;
    uint32_t reset_handler_address;
    uint32_t vector_table_address;

    if (application_vector == NULL)
    {
        for (;;)
        {
            __NOP();
        }
    }

    initial_stack_pointer = application_vector->initial_stack_pointer;
    reset_handler_address = application_vector->reset_handler_address;
    vector_table_address = application_vector->vector_table_address;

    ApplicationJump_StopSystemTick();
    ApplicationJump_DisableAndClearInterrupts();

    /* Return the clock tree to its reset-like HSI state. The application
     * SystemInit() owns the complete clock configuration after the handoff. */
    RCC_DeInit();

    SCB->VTOR = vector_table_address;
    __DSB();
    __ISB();

    /* The assembly helper changes MSP and branches without a compiler-generated
     * stack frame. It also restores reset-like CONTROL/PRIMASK state. */
    ApplicationJump_SetStackAndBranch(initial_stack_pointer,
                                      reset_handler_address);
}
