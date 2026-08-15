#ifndef UART_OTA_PLAN_H
#define UART_OTA_PLAN_H

#include <stdint.h>

#include "uart_ota_protocol.h"

typedef enum
{
    UART_OTA_PLAN_ALREADY_TARGET = 0,
    UART_OTA_PLAN_START_NEW,
    UART_OTA_PLAN_RESUME,
    UART_OTA_PLAN_INSTALL_READY,
    UART_OTA_PLAN_WAIT_TARGET,
    UART_OTA_PLAN_ABORT_FOREIGN,
    UART_OTA_PLAN_ERROR
} UartOtaPlan_t;

UartOtaPlan_t UartOta_SelectPlan(const UartOtaHelloInfo_t *target,
                                 uint32_t artifact_update_id,
                                 uint32_t artifact_size,
                                 uint32_t target_version);

#endif
