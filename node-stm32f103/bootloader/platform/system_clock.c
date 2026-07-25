#include <stdbool.h>

#include "stm32f10x.h"

#ifndef PROJECT_VECTOR_TABLE_OFFSET
#error "PROJECT_VECTOR_TABLE_OFFSET must be supplied by the image Makefile"
#endif

#if ((PROJECT_VECTOR_TABLE_OFFSET & 0x1FFUL) != 0UL)
#error "The Cortex-M3 vector table offset must be aligned to 0x200 bytes"
#endif

#define HSE_STARTUP_TIMEOUT_LOOPS 0x5000UL

uint32_t SystemCoreClock = HSI_VALUE;

static bool SystemClock_TryConfigure72MHz(void)
{
    uint32_t timeout = HSE_STARTUP_TIMEOUT_LOOPS;

    RCC->CR |= RCC_CR_HSEON;
    while (((RCC->CR & RCC_CR_HSERDY) == 0UL) && (timeout > 0UL))
    {
        --timeout;
    }

    if ((RCC->CR & RCC_CR_HSERDY) == 0UL)
    {
        return false;
    }

    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;

    RCC->CFGR &= ~(RCC_CFGR_HPRE |
                   RCC_CFGR_PPRE1 |
                   RCC_CFGR_PPRE2 |
                   RCC_CFGR_ADCPRE |
                   RCC_CFGR_PLLSRC |
                   RCC_CFGR_PLLXTPRE |
                   RCC_CFGR_PLLMULL |
                   RCC_CFGR_SW);

    RCC->CFGR |= RCC_CFGR_HPRE_DIV1 |
                 RCC_CFGR_PPRE1_DIV2 |
                 RCC_CFGR_PPRE2_DIV1 |
                 RCC_CFGR_ADCPRE_DIV6 |
                 RCC_CFGR_PLLSRC |
                 RCC_CFGR_PLLMULL9;

    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0UL)
    {
        /* HSE was stable; wait for PLL lock. */
    }

    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL)
    {
        /* Wait until PLL is the system clock source. */
    }

    return true;
}

void SystemInit(void)
{
    RCC->CR |= RCC_CR_HSION;
    RCC->CFGR &= 0xF8FF0000UL;
    RCC->CR &= 0xFEF6FFFFUL;
    RCC->CR &= 0xFFFBFFFFUL;
    RCC->CFGR &= 0xFF80FFFFUL;
    RCC->CIR = 0x009F0000UL;

    if (SystemClock_TryConfigure72MHz())
    {
        SystemCoreClock = 72000000UL;
    }
    else
    {
        RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_HSI;
        SystemCoreClock = HSI_VALUE;
    }

    SCB->VTOR = FLASH_BASE | PROJECT_VECTOR_TABLE_OFFSET;
}

void SystemCoreClockUpdate(void)
{
    const uint32_t source = RCC->CFGR & RCC_CFGR_SWS;

    if (source == RCC_CFGR_SWS_PLL)
    {
        SystemCoreClock = 72000000UL;
    }
    else if (source == RCC_CFGR_SWS_HSE)
    {
        SystemCoreClock = HSE_VALUE;
    }
    else
    {
        SystemCoreClock = HSI_VALUE;
    }
}
