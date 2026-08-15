#include "gateway_config.h"

#include "phase11_runtime_config.h"
#include "sdkconfig.h"

#if defined(CONFIG_SDOTA_PHASE11_AUTORUN)
#define SDOTA_CONFIG_PHASE11_AUTORUN true
#else
#define SDOTA_CONFIG_PHASE11_AUTORUN false
#endif

#if defined(CONFIG_SDOTA_PHASE11_SINGLE_SHOT)
#define SDOTA_CONFIG_PHASE11_SINGLE_SHOT true
#else
#define SDOTA_CONFIG_PHASE11_SINGLE_SHOT false
#endif

GatewayConfig_t GatewayConfig_Get(void)
{
    GatewayConfig_t config = {
        .uart_num = (uart_port_t)CONFIG_SDOTA_STM32_UART_NUM,
        .tx_gpio = CONFIG_SDOTA_STM32_UART_TX_GPIO,
        .rx_gpio = CONFIG_SDOTA_STM32_UART_RX_GPIO,
        .baud_rate = 115200UL,

        .mqtt_client_id = CONFIG_SDOTA_MQTT_CLIENT_ID,
        .mqtt_topic_base = CONFIG_SDOTA_MQTT_TOPIC_BASE,
        .mqtt_device_id = CONFIG_SDOTA_MQTT_DEVICE_ID,
        .mqtt_username = CONFIG_SDOTA_MQTT_USERNAME,
        .mqtt_password = CONFIG_SDOTA_MQTT_PASSWORD,

        .wifi_timeout_ms = (uint32_t)CONFIG_SDOTA_WIFI_TIMEOUT_MS,
        .sntp_timeout_ms = (uint32_t)CONFIG_SDOTA_SNTP_TIMEOUT_MS,
        .https_timeout_ms = (uint32_t)CONFIG_SDOTA_HTTPS_TIMEOUT_MS,
        .mqtt_ready_timeout_ms =
            (uint32_t)CONFIG_SDOTA_MQTT_READY_TIMEOUT_MS,
        .mqtt_command_timeout_ms =
            (uint32_t)CONFIG_SDOTA_MQTT_COMMAND_TIMEOUT_MS,

        .autorun = SDOTA_CONFIG_PHASE11_AUTORUN,
        .single_shot = SDOTA_CONFIG_PHASE11_SINGLE_SHOT,

#if SDOTA_PHASE11_HW_OVERRIDE
        .wifi_ssid = SDOTA_PHASE11_HW_WIFI_SSID,
        .wifi_password = SDOTA_PHASE11_HW_WIFI_PASSWORD,
        .mqtt_uri = SDOTA_PHASE11_HW_MQTT_URI,
        .use_test_ca = SDOTA_PHASE11_HW_USE_TEST_CA != 0,
        .test_epoch = SDOTA_PHASE11_HW_TEST_EPOCH,
#else
        .wifi_ssid = CONFIG_SDOTA_WIFI_SSID,
        .wifi_password = CONFIG_SDOTA_WIFI_PASSWORD,
        .mqtt_uri = CONFIG_SDOTA_MQTT_URI,
        .use_test_ca = false,
        .test_epoch = 0LL,
#endif
    };

#if SDOTA_PHASE11_HW_OVERRIDE
    /*
     * Single-shot mode is forced only in the deterministic HIL build.
     * Production Kconfig remains the source of truth otherwise.
     */
    config.single_shot =
        SDOTA_PHASE11_HW_SINGLE_SHOT != 0;
#endif

    return config;
}
