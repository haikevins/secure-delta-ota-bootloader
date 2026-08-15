#include "gateway_config.h"

#include "sdkconfig.h"

GatewayConfig_t GatewayConfig_Get(void)
{
    GatewayConfig_t config = {
        .uart_num = (uart_port_t)CONFIG_SDOTA_STM32_UART_NUM,
        .tx_gpio = CONFIG_SDOTA_STM32_UART_TX_GPIO,
        .rx_gpio = CONFIG_SDOTA_STM32_UART_RX_GPIO,
        .baud_rate = 115200UL,
        .update_id = (uint32_t)CONFIG_SDOTA_PHASE9_UPDATE_ID,
        .target_version = (uint32_t)CONFIG_SDOTA_PHASE9_TARGET_VERSION,
        .autorun = CONFIG_SDOTA_PHASE9_AUTORUN != 0,
    };

    return config;
}
