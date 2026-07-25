#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"

#define STATUS_LED_PORT GPIOC
#define STATUS_LED_PIN  GPIO_Pin_13

static void Phase1_Delay(volatile uint32_t cycles)
{
    while (cycles > 0UL)
    {
        __NOP();
        --cycles;
    }
}

int main(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = STATUS_LED_PIN;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(STATUS_LED_PORT, &gpio);

    for (;;)
    {
        GPIO_WriteBit(STATUS_LED_PORT, STATUS_LED_PIN, Bit_RESET);
        Phase1_Delay(900000UL);
        GPIO_WriteBit(STATUS_LED_PORT, STATUS_LED_PIN, Bit_SET);
        Phase1_Delay(900000UL);
    }
}
