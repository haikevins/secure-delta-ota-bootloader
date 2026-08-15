#include "gateway_config.h"

#include "phase10_runtime_config.h"
#include "sdkconfig.h"

GatewayConfig_t GatewayConfig_Get(void)
{
    GatewayConfig_t config = {
        .uart_num = (uart_port_t)CONFIG_SDOTA_STM32_UART_NUM,
        .tx_gpio = CONFIG_SDOTA_STM32_UART_TX_GPIO,
        .rx_gpio = CONFIG_SDOTA_STM32_UART_RX_GPIO,
        .baud_rate = 115200UL,

        .update_id = (uint32_t)CONFIG_SDOTA_PHASE10_UPDATE_ID,
        .target_version = (uint32_t)CONFIG_SDOTA_PHASE10_TARGET_VERSION,
        .wifi_timeout_ms = (uint32_t)CONFIG_SDOTA_WIFI_TIMEOUT_MS,
        .sntp_timeout_ms = (uint32_t)CONFIG_SDOTA_SNTP_TIMEOUT_MS,
        .https_timeout_ms = (uint32_t)CONFIG_SDOTA_HTTPS_TIMEOUT_MS,
        .autorun = CONFIG_SDOTA_PHASE10_AUTORUN != 0,

#if SDOTA_PHASE10_HW_OVERRIDE
        .wifi_ssid = SDOTA_PHASE10_HW_WIFI_SSID,
        .wifi_password = SDOTA_PHASE10_HW_WIFI_PASSWORD,
        .https_url = SDOTA_PHASE10_HW_HTTPS_URL,
        .use_test_ca = SDOTA_PHASE10_HW_USE_TEST_CA != 0,
        .test_epoch = SDOTA_PHASE10_HW_TEST_EPOCH,
#else
        .wifi_ssid = CONFIG_SDOTA_WIFI_SSID,
        .wifi_password = CONFIG_SDOTA_WIFI_PASSWORD,
        .https_url = CONFIG_SDOTA_HTTPS_URL,
        .use_test_ca = false,
        .test_epoch = 0LL,
#endif
    };

    return config;
}
