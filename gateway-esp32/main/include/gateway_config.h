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

    const char *mqtt_uri;
    const char *mqtt_client_id;
    const char *mqtt_topic_base;
    const char *mqtt_device_id;
    const char *mqtt_username;
    const char *mqtt_password;

    uint32_t wifi_timeout_ms;
    uint32_t sntp_timeout_ms;
    uint32_t https_timeout_ms;
    uint32_t mqtt_ready_timeout_ms;
    uint32_t mqtt_command_timeout_ms;

    bool autorun;
    bool use_test_ca;
    bool single_shot;
    int64_t test_epoch;
} GatewayConfig_t;

GatewayConfig_t GatewayConfig_Get(void);

#endif
