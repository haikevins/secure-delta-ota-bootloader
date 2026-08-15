#include "gateway_manager.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

#include "artifact_cache.h"
#include "esp_log.h"

#include "gateway_config.h"
#include "uart_ota.h"

static const char *TAG = "gateway";

extern const uint8_t phase9_candidate_bin_start[]
    asm("_binary_phase9_candidate_bin_start");
extern const uint8_t phase9_candidate_bin_end[]
    asm("_binary_phase9_candidate_bin_end");

static void Progress(void *context, uint32_t sent, uint32_t total)
{
    uint32_t *last_report = (uint32_t *)context;

    if ((last_report == NULL) || (total == 0UL))
    {
        return;
    }

    const uint32_t percent =
        (uint32_t)(((uint64_t)sent * 100ULL) / total);

    if ((percent >= (*last_report + 10UL)) || (sent == total))
    {
        *last_report = percent;
        ESP_LOGI(TAG,
                 "P9_DATA %" PRIu32 "/%" PRIu32 " (%" PRIu32 "%%)",
                 sent, total, percent);
    }
}

esp_err_t GatewayManager_RunPhase9(void)
{
    const GatewayConfig_t config = GatewayConfig_Get();
    const uint8_t *candidate = phase9_candidate_bin_start;
    const size_t candidate_size =
        (size_t)(phase9_candidate_bin_end - phase9_candidate_bin_start);
    ArtifactCache_t cache;
    UartOtaArtifact_t artifact;
    UartOtaClient_t client;
    UartOtaHelloInfo_t initial;
    UartOtaHelloInfo_t final_info;
    UartOtaConfig_t uart_config;
    uint32_t last_progress = 0UL;
    esp_err_t status;

    ESP_LOGI(TAG,
             "Phase 9 ESP32 UART Gateway: tx_gpio=%d rx_gpio=%d "
             "candidate=%u bytes target=v%" PRIu32,
             config.tx_gpio,
             config.rx_gpio,
             (unsigned)candidate_size,
             config.target_version);

    if (!config.autorun)
    {
        ESP_LOGW(TAG, "Phase-9 autorun disabled by menuconfig");
        return ESP_ERR_INVALID_STATE;
    }

    status = ArtifactCache_Open(&cache);
    if ((status != ESP_OK) ||
        (cache.header.update_id != config.update_id) ||
        (cache.header.target_version != config.target_version) ||
        (cache.header.image_size != candidate_size) ||
        (cache.header.image_crc32 !=
         UartOta_Crc32(candidate, candidate_size)))
    {
        ESP_LOGI(TAG, "seeding STM32 artifact cache");
        status = ArtifactCache_Seed(&cache,
                                    candidate,
                                    candidate_size,
                                    config.update_id,
                                    config.target_version);
        if (status != ESP_OK)
        {
            ESP_LOGE(TAG, "cache seed failed: %s",
                     esp_err_to_name(status));
            return status;
        }
    }

    ESP_LOGI(TAG,
             "P9_CACHE=PASS update_id=0x%08" PRIX32
             " size=%" PRIu32 " crc32=0x%08" PRIX32,
             cache.header.update_id,
             cache.header.image_size,
             cache.header.image_crc32);

    status = ArtifactCache_AsUartArtifact(&cache, &artifact);
    if (status != ESP_OK)
    {
        return status;
    }

    uart_config.uart_num = config.uart_num;
    uart_config.tx_gpio = config.tx_gpio;
    uart_config.rx_gpio = config.rx_gpio;
    uart_config.baud_rate = config.baud_rate;
    uart_config.response_timeout_ms = 1500UL;
    uart_config.retry_count = 5UL;

    status = UartOta_Init(&client, &uart_config);
    if (status != ESP_OK)
    {
        ESP_LOGE(TAG, "UART init failed: %s",
                 esp_err_to_name(status));
        return status;
    }

    UartOta_SetProgressCallback(&client, Progress, &last_progress);

    status = UartOta_Query(&client, &initial);
    if (status != ESP_OK)
    {
        ESP_LOGE(TAG, "STM32 HELLO/QUERY failed: %s",
                 esp_err_to_name(status));
        return status;
    }

    ESP_LOGI(TAG,
             "P9_HELLO=PASS app=v%" PRIu32
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
             "P9_FINAL=PASS app=v%" PRIu32 " state=IDLE",
             final_info.application_version);
    return ESP_OK;
}
