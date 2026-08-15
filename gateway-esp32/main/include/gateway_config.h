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

    const char *wifi_ssid;
    const char *wifi_password;
    const char *https_url;

    uint32_t update_id;
    uint32_t target_version;
    uint32_t wifi_timeout_ms;
    uint32_t sntp_timeout_ms;
    uint32_t https_timeout_ms;

    bool autorun;
    bool use_test_ca;
    int64_t test_epoch;
} GatewayConfig_t;

GatewayConfig_t GatewayConfig_Get(void);

#endif
