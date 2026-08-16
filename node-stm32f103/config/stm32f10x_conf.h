#ifndef STM32F10X_CONF_H
#define STM32F10X_CONF_H

/*
 * SPL configuration through external-flash integration.
 *
 * Only modules compiled by the current image are exposed here. Additional features
 * extend this file when USART, SPI, internal Flash, CRC and watchdog drivers
 * enter the build. Keeping the list synchronized prevents accidental linkage
 * against vendor modules that are not part of the image.
 */
#include "stm32f10x_flash.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_spi.h"
#include "misc.h"

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line);
#define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
#else
#define assert_param(expr) ((void)0U)
#endif

#endif /* STM32F10X_CONF_H */
