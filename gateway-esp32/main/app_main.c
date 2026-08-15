#include "gateway_manager.h"

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sdota_main";

void app_main(void)
{
    esp_err_t result = GatewayManager_RunPhase9();

    if (result == ESP_OK)
    {
        /*
         * Repeat the final marker so the PC hardware runner can attach after
         * idf.py flash has already reset the ESP32 and still observe PASS.
         */
        for (int i = 0; i < 10; ++i)
        {
            ESP_LOGI(TAG, "P9_GATEWAY_HW_TEST=PASS");
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    else
    {
        ESP_LOGE(TAG, "P9_GATEWAY_HW_TEST=FAIL err=%s",
                 esp_err_to_name(result));
    }

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
