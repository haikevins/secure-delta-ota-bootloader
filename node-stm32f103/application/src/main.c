#include <stdint.h>
#include "memory_map.h"
#include "ota_agent.h"
#include "phase4_flash_selftest.h"
#include "trial_confirmation.h"
#include "stm32f10x.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"

#define STATUS_LED_PORT          GPIOC
#define STATUS_LED_PIN           GPIO_Pin_13
#define HEARTBEAT_PERIOD_MS      1000UL
#define HEARTBEAT_ON_TIME_MS     100UL
#define ERROR_TOGGLE_DELAY       180000UL

volatile uint32_t g_application_tick_ms;

void SysTick_Handler(void)
{
    ++g_application_tick_ms;
}

static void Application_LedInit(void)
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

static void Application_LedSet(uint8_t enabled)
{
    if (enabled != 0U) { GPIO_ResetBits(STATUS_LED_PORT, STATUS_LED_PIN); }
    else { GPIO_SetBits(STATUS_LED_PORT, STATUS_LED_PIN); }
}

static void Application_FatalBlink(void) __attribute__((noreturn));

static void Application_FatalBlink(void)
{
    for (;;)
    {
        volatile uint32_t delay;
        GPIO_WriteBit(STATUS_LED_PORT, STATUS_LED_PIN,
                      (BitAction)(1UL - GPIO_ReadOutputDataBit(
                          STATUS_LED_PORT, STATUS_LED_PIN)));
        for (delay = 0UL; delay < ERROR_TOGGLE_DELAY; ++delay) { __NOP(); }
    }
}

int main(void)
{
    Application_LedInit();

    if (SCB->VTOR != APPLICATION_START_ADDRESS) { Application_FatalBlink(); }
    if (SysTick_Config(SystemCoreClock / 1000UL) != 0UL)
    {
        Application_FatalBlink();
    }

#if defined(PHASE4_HW_TEST)
    Phase4FlashSelfTest_Run();
    if (g_phase4_flash_test_status != PHASE4_FLASH_TEST_PASS)
    {
        Application_FatalBlink();
    }
#else
    if (!OtaAgent_Init()) { Application_FatalBlink(); }
    if (!TrialConfirmation_Init(g_application_tick_ms))
    {
        Application_FatalBlink();
    }
#endif

    for (;;)
    {
        const uint32_t phase = g_application_tick_ms % HEARTBEAT_PERIOD_MS;
#if !defined(PHASE4_HW_TEST)
        OtaAgent_Process();
        TrialConfirmation_Process(g_application_tick_ms);
#endif
        Application_LedSet((uint8_t)(phase < HEARTBEAT_ON_TIME_MS));
        __WFI();
    }
}
