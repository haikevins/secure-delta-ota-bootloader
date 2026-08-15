#include "gateway_manager.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "artifact_cache.h"
#include "esp_log.h"

#include "gateway_config.h"
#include "https_download.h"
#include "https_download_policy.h"
#include "time_sync.h"
#include "uart_ota.h"
#include "wifi_station.h"

#define STM32_APPLICATION_MAX_SIZE (38UL * 1024UL)

static const char *TAG = "gateway";

extern const uint8_t phase10_test_ca_pem_start[]
    asm("_binary_phase10_test_ca_pem_start");

static void DownloadProgress(void *context,
                             uint32_t downloaded,
                             uint32_t total)
{
    uint32_t *last_report = (uint32_t *)context;

    if ((last_report == NULL) || (total == 0UL))
    {
        return;
    }

    const uint32_t percent =
        (uint32_t)(((uint64_t)downloaded * 100ULL) / total);

    if ((percent >= (*last_report + 10UL)) ||
        (downloaded == total))
    {
        *last_report = percent;
        ESP_LOGI(TAG,
                 "P10_HTTPS_DATA %" PRIu32 "/%" PRIu32
                 " (%" PRIu32 "%%)",
                 downloaded,
                 total,
                 percent);
    }
}

static void UartProgress(void *context,
                         uint32_t sent,
                         uint32_t total)
{
    uint32_t *last_report = (uint32_t *)context;

    if ((last_report == NULL) || (total == 0UL))
    {
        return;
    }

    const uint32_t percent =
        (uint32_t)(((uint64_t)sent * 100ULL) / total);

    if ((percent >= (*last_report + 10UL)) ||
        (sent == total))
    {
        *last_report = percent;
        ESP_LOGI(TAG,
                 "P10_UART_DATA %" PRIu32 "/%" PRIu32
                 " (%" PRIu32 "%%)",
                 sent,
                 total,
                 percent);
    }
}

static esp_err_t ValidateConfiguration(const GatewayConfig_t *config)
{
    if (config == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!config->autorun)
    {
        ESP_LOGW(TAG, "Phase-10 autorun disabled");
        return ESP_ERR_INVALID_STATE;
    }

    if ((config->wifi_ssid == NULL) ||
        (config->wifi_ssid[0] == '\0'))
    {
        ESP_LOGE(TAG,
                 "Wi-Fi SSID is empty; configure SDOTA_WIFI_SSID");
        return ESP_ERR_INVALID_ARG;
    }

    if ((config->wifi_password == NULL) ||
        (config->https_url == NULL) ||
        !HttpsDownload_IsHttpsUrl(config->https_url))
    {
        ESP_LOGE(TAG,
                 "Phase-10 HTTPS configuration is invalid");
        return ESP_ERR_INVALID_ARG;
    }

    if ((config->update_id == 0UL) ||
        (config->target_version == 0UL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

esp_err_t GatewayManager_RunPhase10(void)
{
    const GatewayConfig_t config = GatewayConfig_Get();
    ArtifactCache_t cache;
    HttpsDownloadConfig_t download_config;
    HttpsDownloadResult_t download_result;
    UartOtaArtifact_t artifact;
    UartOtaClient_t client;
    UartOtaHelloInfo_t initial;
    UartOtaHelloInfo_t final_info;
    UartOtaConfig_t uart_config;
    uint32_t last_download_progress = 0UL;
    uint32_t last_uart_progress = 0UL;
    const char *test_ca = NULL;
    esp_err_t status;

    ESP_LOGI(TAG,
             "Phase 10 HTTPS Gateway: tx_gpio=%d rx_gpio=%d "
             "target=v%" PRIu32,
             config.tx_gpio,
             config.rx_gpio,
             config.target_version);

    status = ValidateConfiguration(&config);
    if (status != ESP_OK)
    {
        return status;
    }

    status = WifiStation_Connect(config.wifi_ssid,
                                 config.wifi_password,
                                 config.wifi_timeout_ms);
    if (status != ESP_OK)
    {
        ESP_LOGE(TAG, "Wi-Fi failed: %s", esp_err_to_name(status));
        return status;
    }

    ESP_LOGI(TAG, "P10_WIFI=PASS");

    if (config.use_test_ca)
    {
        /*
         * Hardware self-test uses a short-lived private CA generated on the
         * developer PC. The runner provides a current bootstrap epoch so TLS
         * validity can be checked even without Internet/SNTP access.
         */
        status = TimeSync_UseTestEpoch(config.test_epoch);
        test_ca = (const char *)phase10_test_ca_pem_start;
        if (status != ESP_OK)
        {
            ESP_LOGE(TAG,
                     "test time bootstrap failed: %s",
                     esp_err_to_name(status));
            return status;
        }
        ESP_LOGI(TAG, "P10_TIME=PASS mode=test-bootstrap");
    }
    else
    {
        status = TimeSync_Sntp("pool.ntp.org",
                               config.sntp_timeout_ms);
        if (status != ESP_OK)
        {
            return status;
        }
        ESP_LOGI(TAG, "P10_TIME=PASS mode=sntp");
    }

    memset(&download_config, 0, sizeof(download_config));
    memset(&download_result, 0, sizeof(download_result));

    download_config.url = config.https_url;
    download_config.cert_pem = test_ca;
    download_config.use_crt_bundle = !config.use_test_ca;
    download_config.update_id = config.update_id;
    download_config.target_version = config.target_version;
    download_config.max_image_size = STM32_APPLICATION_MAX_SIZE;
    download_config.timeout_ms = config.https_timeout_ms;
    download_config.progress = DownloadProgress;
    download_config.progress_context = &last_download_progress;

    ESP_LOGI(TAG, "HTTPS GET %s", config.https_url);

    status = HttpsDownload_ToCache(&download_config,
                                   &cache,
                                   &download_result);
    if (status != ESP_OK)
    {
        ESP_LOGE(TAG,
                 "HTTPS download failed: %s",
                 esp_err_to_name(status));
        return status;
    }

    ESP_LOGI(TAG,
             "P10_HTTPS=PASS status=%d size=%" PRIu32
             " crc32=0x%08" PRIX32,
             download_result.status_code,
             download_result.downloaded_size,
             download_result.image_crc32);

    status = ArtifactCache_AsUartArtifact(&cache, &artifact);
    if (status != ESP_OK)
    {
        return status;
    }

    memset(&uart_config, 0, sizeof(uart_config));
    uart_config.uart_num = config.uart_num;
    uart_config.tx_gpio = config.tx_gpio;
    uart_config.rx_gpio = config.rx_gpio;
    uart_config.baud_rate = config.baud_rate;
    uart_config.response_timeout_ms = 1500UL;
    uart_config.retry_count = 5UL;

    status = UartOta_Init(&client, &uart_config);
    if (status != ESP_OK)
    {
        ESP_LOGE(TAG,
                 "UART init failed: %s",
                 esp_err_to_name(status));
        return status;
    }

    UartOta_SetProgressCallback(&client,
                                UartProgress,
                                &last_uart_progress);

    status = UartOta_Query(&client, &initial);
    if (status != ESP_OK)
    {
        ESP_LOGE(TAG,
                 "STM32 QUERY failed: %s",
                 esp_err_to_name(status));
        return status;
    }

    ESP_LOGI(TAG,
             "P10_HELLO=PASS app=v%" PRIu32
             " state=%u caps=0x%08" PRIX32,
             initial.application_version,
             initial.update_state,
             initial.capability_flags);

    status = UartOta_TransferInstallAndWait(&client,
                                            &artifact,
                                            &final_info);
    if (status != ESP_OK)
    {
        ESP_LOGE(TAG,
                 "STM32 OTA failed/rolled back: %s",
                 esp_err_to_name(status));
        return status;
    }

    if ((final_info.application_version != config.target_version) ||
        (final_info.update_state != UART_OTA_UPDATE_IDLE))
    {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG,
             "P10_FINAL=PASS app=v%" PRIu32 " state=IDLE",
             final_info.application_version);
    return ESP_OK;
}
