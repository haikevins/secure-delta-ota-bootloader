#include "time_sync.h"

#include <stdbool.h>
#include <stddef.h>
#include <sys/time.h>
#include <time.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"

#define TIME_SYNC_MIN_SANE_EPOCH 1704067200LL /* 2024-01-01 UTC */

static const char *TAG = "time_sync";

bool TimeSync_IsSane(void)
{
    const time_t now = time(NULL);
    return ((int64_t)now >= TIME_SYNC_MIN_SANE_EPOCH);
}

esp_err_t TimeSync_UseTestEpoch(int64_t epoch_seconds)
{
    struct timeval tv;

    if (epoch_seconds < TIME_SYNC_MIN_SANE_EPOCH)
    {
        return ESP_ERR_INVALID_ARG;
    }

    tv.tv_sec = (time_t)epoch_seconds;
    tv.tv_usec = 0;

    if (settimeofday(&tv, NULL) != 0)
    {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG,
             "using HTTPS transport test bootstrap epoch=%lld",
             (long long)epoch_seconds);
    return ESP_OK;
}

esp_err_t TimeSync_Sntp(const char *server, uint32_t timeout_ms)
{
    esp_err_t status;

    if ((server == NULL) || (server[0] == '\0') ||
        (timeout_ms == 0UL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (TimeSync_IsSane())
    {
        return ESP_OK;
    }

    /*
     * ESP_NETIF_SNTP_DEFAULT_CONFIG() expands to a brace initializer.
     * It must be used while defining the object, not as a later assignment.
     */
    esp_sntp_config_t config =
        ESP_NETIF_SNTP_DEFAULT_CONFIG(server);

    status = esp_netif_sntp_init(&config);
    if (status != ESP_OK)
    {
        return status;
    }

    status = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(timeout_ms));
    esp_netif_sntp_deinit();

    if (status == ESP_OK)
    {
        ESP_LOGI(TAG, "SNTP synchronization complete");
    }
    else
    {
        ESP_LOGE(TAG, "SNTP synchronization failed: %s",
                 esp_err_to_name(status));
    }

    return status;
}
