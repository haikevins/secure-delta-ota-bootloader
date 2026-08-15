#ifndef UART_H
#define UART_H

#include <stdbool.h>
#include <stdint.h>

#define UART_OTA_BAUD_RATE 115200UL

bool Uart_Init(void);
bool Uart_ReadByte(uint8_t *byte);
bool Uart_Write(const uint8_t *data, uint32_t length);
uint32_t Uart_GetRxOverflowCount(void);
uint32_t Uart_GetErrorCount(void);

#endif
