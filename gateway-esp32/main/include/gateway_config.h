#ifndef GATEWAY_CONFIG_H
#define GATEWAY_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#include "driver/uart.h"

typedef struct
{
    uart_port_t uart_num;
    int tx_gpio;
    int rx_gpio;
    uint32_t baud_rate;
    uint32_t update_id;
    uint32_t target_version;
    bool autorun;
} GatewayConfig_t;

GatewayConfig_t GatewayConfig_Get(void);

#endif
