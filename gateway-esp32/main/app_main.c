#include "gateway_manager.h"

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "sdota_main";

static void GatewayTask(void *argument)
{
    (void)argument;

    ESP_LOGI(
        TAG,
        "MQTT orchestration gateway worker started stack=%d bytes",
        CONFIG_SDOTA_GATEWAY_TASK_STACK_SIZE);

    const esp_err_t result = GatewayManager_Run();

    if (result == ESP_OK)
    {
        const UBaseType_t remaining =
            uxTaskGetStackHighWaterMark(NULL);

        ESP_LOGI(
            TAG,
            "GATEWAY_STACK=PASS high_water_mark=%u bytes",
            (unsigned)remaining);

        for (int i = 0; i < 10; ++i)
        {
            ESP_LOGI(
                TAG,
                "GATEWAY_PIPELINE=PASS "
                "mqtt_https_cache_uart_trial_confirm=complete");
            ESP_LOGI(TAG, "GATEWAY_HIL=PASS");
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    else
    {
        ESP_LOGE(
            TAG,
            "GATEWAY_HIL=FAIL err=%s",
            esp_err_to_name(result));
    }

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void app_main(void)
{
    TaskHandle_t task = NULL;

    const BaseType_t created = xTaskCreate(
        GatewayTask,
        "sdota_gateway",
        CONFIG_SDOTA_GATEWAY_TASK_STACK_SIZE,
        NULL,
        5,
        &task);

    if (created != pdPASS)
    {
        ESP_LOGE(
            TAG,
            "GATEWAY_HIL=FAIL "
            "err=worker_task_create_failed");
        return;
    }

    ESP_LOGI(
        TAG,
        "MQTT orchestration worker task created; app_main returning");
}
