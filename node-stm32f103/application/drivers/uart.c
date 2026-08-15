#include "uart.h"
#include "stm32f10x.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_usart.h"
#include "misc.h"

#define UART_RX_BUFFER_SIZE       512U
#define UART_RX_BUFFER_MASK       (UART_RX_BUFFER_SIZE - 1U)
#define UART_TX_TIMEOUT_LOOPS     500000UL

#if ((UART_RX_BUFFER_SIZE & (UART_RX_BUFFER_SIZE - 1U)) != 0U)
#error UART_RX_BUFFER_SIZE must be a power of two
#endif

static volatile uint8_t g_rx_buffer[UART_RX_BUFFER_SIZE];
static volatile uint16_t g_rx_head;
static volatile uint16_t g_rx_tail;
static volatile uint32_t g_rx_overflow_count;
static volatile uint32_t g_uart_error_count;

bool Uart_Init(void)
{
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef usart;
    NVIC_InitTypeDef nvic;

    g_rx_head = 0U;
    g_rx_tail = 0U;
    g_rx_overflow_count = 0UL;
    g_uart_error_count = 0UL;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_USART1, ENABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = GPIO_Pin_9;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &gpio);

    gpio.GPIO_Pin = GPIO_Pin_10;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);

    USART_StructInit(&usart);
    usart.USART_BaudRate = UART_OTA_BAUD_RATE;
    usart.USART_WordLength = USART_WordLength_8b;
    usart.USART_StopBits = USART_StopBits_1;
    usart.USART_Parity = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &usart);

    nvic.NVIC_IRQChannel = USART1_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 2U;
    nvic.NVIC_IRQChannelSubPriority = 0U;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
    USART_Cmd(USART1, ENABLE);
    return true;
}

void USART1_IRQHandler(void)
{
    const uint32_t status = USART1->SR;

    if ((status & (USART_SR_RXNE | USART_SR_ORE |
                   USART_SR_NE | USART_SR_FE | USART_SR_PE)) != 0UL)
    {
        const uint8_t byte = (uint8_t)USART1->DR;

        if ((status & (USART_SR_ORE | USART_SR_NE |
                       USART_SR_FE | USART_SR_PE)) != 0UL)
        {
            ++g_uart_error_count;
        }

        if ((status & USART_SR_RXNE) != 0UL)
        {
            const uint16_t next =
                (uint16_t)((g_rx_head + 1U) & UART_RX_BUFFER_MASK);
            if (next == g_rx_tail)
            {
                ++g_rx_overflow_count;
            }
            else
            {
                g_rx_buffer[g_rx_head] = byte;
                g_rx_head = next;
            }
        }
    }
}

bool Uart_ReadByte(uint8_t *byte)
{
    uint16_t tail;
    if (byte == (uint8_t *)0) { return false; }

    tail = g_rx_tail;
    if (tail == g_rx_head) { return false; }

    *byte = g_rx_buffer[tail];
    g_rx_tail = (uint16_t)((tail + 1U) & UART_RX_BUFFER_MASK);
    return true;
}

bool Uart_Write(const uint8_t *data, uint32_t length)
{
    uint32_t i;

    if ((data == (const uint8_t *)0) && (length != 0UL)) { return false; }

    for (i = 0UL; i < length; ++i)
    {
        uint32_t timeout = UART_TX_TIMEOUT_LOOPS;
        while ((USART1->SR & USART_SR_TXE) == 0UL)
        {
            if (timeout == 0UL) { return false; }
            --timeout;
        }
        USART1->DR = data[i];
    }

    {
        uint32_t timeout = UART_TX_TIMEOUT_LOOPS;
        while ((USART1->SR & USART_SR_TC) == 0UL)
        {
            if (timeout == 0UL) { return false; }
            --timeout;
        }
    }
    return true;
}

uint32_t Uart_GetRxOverflowCount(void) { return g_rx_overflow_count; }
uint32_t Uart_GetErrorCount(void) { return g_uart_error_count; }
