#include "gateway_manager.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "artifact_cache.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "gateway_config.h"
#include "https_download.h"
#include "mqtt_orchestrator.h"
#include "time_sync.h"
#include "uart_ota.h"
#include "wifi_station.h"

#define STM32_APPLICATION_MAX_SIZE (38UL * 1024UL)

static const char *TAG = "gateway";

extern const uint8_t phase11_test_ca_pem_start[]
    asm("_binary_phase11_test_ca_pem_start");

typedef struct
{
    MqttOrchestrator_t *mqtt;
    uint32_t update_id;
    const char *stage;
    uint32_t last_percent;
} Phase11ProgressContext_t;

static void ProgressReport(Phase11ProgressContext_t *context,
                           uint32_t current,
                           uint32_t total,
                           const char *log_marker)
{
    uint32_t percent;

    if ((context == NULL) || (context->mqtt == NULL) ||
        (context->stage == NULL) || (total == 0UL))
    {
        return;
    }

    percent = (uint32_t)(
        ((uint64_t)current * 100ULL) / total);

    if ((percent >= (context->last_percent + 10UL)) ||
        (current == total))
    {
        context->last_percent = percent;

        ESP_LOGI(TAG,
                 "%s %" PRIu32 "/%" PRIu32
                 " (%" PRIu32 "%%)",
                 log_marker,
                 current,
                 total,
                 percent);

        (void)MqttOrchestrator_PublishProgress(
            context->mqtt,
            context->stage,
            context->update_id,
            current,
            total);
    }
}

static void DownloadProgress(void *context,
                             uint32_t downloaded,
                             uint32_t total)
{
    ProgressReport((Phase11ProgressContext_t *)context,
                   downloaded,
                   total,
                   "P11_HTTPS_DATA");
}

static void UartProgress(void *context,
                         uint32_t sent,
                         uint32_t total)
{
    ProgressReport((Phase11ProgressContext_t *)context,
                   sent,
                   total,
                   "P11_UART_DATA");
}

static esp_err_t ValidateConfiguration(const GatewayConfig_t *config)
{
    if (config == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!config->autorun)
    {
        ESP_LOGW(TAG, "Phase-11 autorun disabled");
        return ESP_ERR_INVALID_STATE;
    }

    if ((config->wifi_ssid == NULL) ||
        (config->wifi_ssid[0] == '\0') ||
        (config->wifi_password == NULL) ||
        (config->mqtt_uri == NULL) ||
        (strncmp(config->mqtt_uri,
                 "mqtts://",
                 sizeof("mqtts://") - 1U) != 0) ||
        (config->mqtt_client_id == NULL) ||
        (config->mqtt_client_id[0] == '\0') ||
        (config->mqtt_topic_base == NULL) ||
        (config->mqtt_topic_base[0] == '\0') ||
        (config->mqtt_device_id == NULL) ||
        (config->mqtt_device_id[0] == '\0') ||
        (config->mqtt_ready_timeout_ms == 0UL))
    {
        ESP_LOGE(TAG, "Phase-11 configuration invalid");
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

static esp_err_t PublishFailure(MqttOrchestrator_t *mqtt,
                                const MqttOtaCommand_t *command,
                                esp_err_t error,
                                const char *detail)
{
    const uint32_t update_id =
        (command != NULL) ? command->update_id : 0UL;
    const uint32_t target_version =
        (command != NULL) ? command->target_version : 0UL;

    ESP_LOGE(TAG,
             "Phase-11 update failed: %s (%s)",
             detail,
             esp_err_to_name(error));

    (void)MqttOrchestrator_PublishStatus(
        mqtt,
        "failed",
        update_id,
        target_version,
        detail,
        true);

    return error;
}

static esp_err_t ProcessCommand(
    const GatewayConfig_t *config,
    MqttOrchestrator_t *mqtt,
    UartOtaClient_t *uart,
    const MqttOtaCommand_t *command,
    const char *test_ca)
{
    ArtifactCache_t cache;
    HttpsDownloadConfig_t download_config;
    HttpsDownloadResult_t download_result;
    UartOtaArtifact_t artifact;
    UartOtaHelloInfo_t initial;
    UartOtaHelloInfo_t final_info;
    Phase11ProgressContext_t https_progress;
    Phase11ProgressContext_t uart_progress;
    esp_err_t status;

    if ((config == NULL) || (mqtt == NULL) ||
        (uart == NULL) || (command == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG,
             "P11_COMMAND=PASS update_id=0x%08" PRIX32
             " target=v%" PRIu32
             " size=%" PRIu32
             " crc32=0x%08" PRIX32,
             command->update_id,
             command->target_version,
             command->image_size,
             command->image_crc32);

    status = UartOta_Query(uart, &initial);
    if (status != ESP_OK)
    {
        return PublishFailure(mqtt,
                              command,
                              status,
                              "stm32_query_failed");
    }

    ESP_LOGI(TAG,
             "P11_HELLO=PASS app=v%" PRIu32
             " state=%u caps=0x%08" PRIX32,
             initial.application_version,
             initial.update_state,
             initial.capability_flags);

    if ((initial.application_version == command->target_version) &&
        (initial.update_state == UART_OTA_UPDATE_IDLE))
    {
        status = MqttOrchestrator_PublishStatusAndWait(
            mqtt,
            "confirmed",
            command->update_id,
            command->target_version,
            "already_current",
            true,
            5000UL);
        if (status != ESP_OK)
        {
            return status;
        }

        ESP_LOGI(TAG,
                 "P11_FINAL=PASS app=v%" PRIu32
                 " state=IDLE already_current",
                 initial.application_version);
        return ESP_OK;
    }

    status = MqttOrchestrator_PublishStatus(
        mqtt,
        "accepted",
        command->update_id,
        command->target_version,
        "downloading",
        true);
    if (status != ESP_OK)
    {
        return status;
    }

    memset(&https_progress, 0, sizeof(https_progress));
    https_progress.mqtt = mqtt;
    https_progress.update_id = command->update_id;
    https_progress.stage = "https";

    memset(&download_config, 0, sizeof(download_config));
    memset(&download_result, 0, sizeof(download_result));

    download_config.url = command->url;
    download_config.cert_pem = test_ca;
    download_config.use_crt_bundle = !config->use_test_ca;
    download_config.update_id = command->update_id;
    download_config.target_version = command->target_version;
    download_config.max_image_size = STM32_APPLICATION_MAX_SIZE;
    download_config.timeout_ms = config->https_timeout_ms;
    download_config.progress = DownloadProgress;
    download_config.progress_context = &https_progress;

    ESP_LOGI(TAG, "P11_HTTPS_GET %s", command->url);

    status = HttpsDownload_ToCache(
        &download_config,
        &cache,
        &download_result);
    if (status != ESP_OK)
    {
        return PublishFailure(mqtt,
                              command,
                              status,
                              "https_download_failed");
    }

    if (download_result.downloaded_size != command->image_size)
    {
        return PublishFailure(mqtt,
                              command,
                              ESP_ERR_INVALID_SIZE,
                              "mqtt_size_mismatch");
    }

    if (download_result.image_crc32 != command->image_crc32)
    {
        return PublishFailure(mqtt,
                              command,
                              ESP_ERR_INVALID_CRC,
                              "mqtt_crc_mismatch");
    }

    ESP_LOGI(TAG,
             "P11_HTTPS=PASS status=%d size=%" PRIu32
             " crc32=0x%08" PRIX32,
             download_result.status_code,
             download_result.downloaded_size,
             download_result.image_crc32);

    (void)MqttOrchestrator_PublishStatus(
        mqtt,
        "downloaded",
        command->update_id,
        command->target_version,
        "cache_verified",
        true);

    status = ArtifactCache_AsUartArtifact(&cache, &artifact);
    if (status != ESP_OK)
    {
        return PublishFailure(mqtt,
                              command,
                              status,
                              "cache_export_failed");
    }

    memset(&uart_progress, 0, sizeof(uart_progress));
    uart_progress.mqtt = mqtt;
    uart_progress.update_id = command->update_id;
    uart_progress.stage = "uart";

    UartOta_SetProgressCallback(
        uart,
        UartProgress,
        &uart_progress);

    (void)MqttOrchestrator_PublishStatus(
        mqtt,
        "installing",
        command->update_id,
        command->target_version,
        "uart_transfer",
        true);

    status = UartOta_TransferInstallAndWait(
        uart,
        &artifact,
        &final_info);
    if (status != ESP_OK)
    {
        return PublishFailure(mqtt,
                              command,
                              status,
                              "stm32_install_or_trial_failed");
    }

    if ((final_info.application_version != command->target_version) ||
        (final_info.update_state != UART_OTA_UPDATE_IDLE))
    {
        return PublishFailure(mqtt,
                              command,
                              ESP_ERR_INVALID_STATE,
                              "stm32_final_state_mismatch");
    }

    status = MqttOrchestrator_PublishStatusAndWait(
        mqtt,
        "confirmed",
        command->update_id,
        command->target_version,
        "trial_confirmed",
        true,
        5000UL);
    if (status != ESP_OK)
    {
        return status;
    }

    ESP_LOGI(TAG,
             "P11_FINAL=PASS app=v%" PRIu32 " state=IDLE",
             final_info.application_version);
    return ESP_OK;
}

esp_err_t GatewayManager_RunPhase11(void)
{
    const GatewayConfig_t config = GatewayConfig_Get();
    MqttOrchestrator_t mqtt;
    MqttOrchestratorConfig_t mqtt_config;
    UartOtaClient_t uart;
    UartOtaConfig_t uart_config;
    MqttOtaCommand_t command;
    const char *test_ca = NULL;
    esp_err_t status;

    ESP_LOGI(TAG,
             "Phase 11 MQTT Orchestration Gateway: "
             "tx_gpio=%d rx_gpio=%d device=%s",
             config.tx_gpio,
             config.rx_gpio,
             config.mqtt_device_id);

    status = ValidateConfiguration(&config);
    if (status != ESP_OK)
    {
        return status;
    }

    status = WifiStation_Connect(
        config.wifi_ssid,
        config.wifi_password,
        config.wifi_timeout_ms);
    if (status != ESP_OK)
    {
        ESP_LOGE(TAG,
                 "Wi-Fi failed: %s",
                 esp_err_to_name(status));
        return status;
    }

    ESP_LOGI(TAG, "P11_WIFI=PASS");

    if (config.use_test_ca)
    {
        status = TimeSync_UseTestEpoch(config.test_epoch);
        test_ca = (const char *)phase11_test_ca_pem_start;
        if (status != ESP_OK)
        {
            return status;
        }
        ESP_LOGI(TAG, "P11_TIME=PASS mode=test-bootstrap");
    }
    else
    {
        status = TimeSync_Sntp(
            "pool.ntp.org",
            config.sntp_timeout_ms);
        if (status != ESP_OK)
        {
            return status;
        }
        ESP_LOGI(TAG, "P11_TIME=PASS mode=sntp");
    }

    memset(&mqtt_config, 0, sizeof(mqtt_config));
    mqtt_config.broker_uri = config.mqtt_uri;
    mqtt_config.client_id = config.mqtt_client_id;
    mqtt_config.topic_base = config.mqtt_topic_base;
    mqtt_config.device_id = config.mqtt_device_id;
    mqtt_config.username = config.mqtt_username;
    mqtt_config.password = config.mqtt_password;
    mqtt_config.cert_pem = test_ca;
    mqtt_config.use_crt_bundle = !config.use_test_ca;
    mqtt_config.ready_timeout_ms =
        config.mqtt_ready_timeout_ms;

    status = MqttOrchestrator_Start(&mqtt, &mqtt_config);
    if (status != ESP_OK)
    {
        ESP_LOGE(TAG,
                 "MQTT start failed: %s",
                 esp_err_to_name(status));
        return status;
    }

    ESP_LOGI(TAG, "P11_MQTT=PASS subscribed=command");

    memset(&uart_config, 0, sizeof(uart_config));
    uart_config.uart_num = config.uart_num;
    uart_config.tx_gpio = config.tx_gpio;
    uart_config.rx_gpio = config.rx_gpio;
    uart_config.baud_rate = config.baud_rate;
    uart_config.response_timeout_ms = 1500UL;
    uart_config.retry_count = 5UL;

    status = UartOta_Init(&uart, &uart_config);
    if (status != ESP_OK)
    {
        (void)MqttOrchestrator_Stop(&mqtt);
        return status;
    }

    status = MqttOrchestrator_PublishStatus(
        &mqtt,
        "online",
        0UL,
        0UL,
        "ready",
        true);
    if (status != ESP_OK)
    {
        (void)MqttOrchestrator_Stop(&mqtt);
        return status;
    }

    for (;;)
    {
        memset(&command, 0, sizeof(command));

        status = MqttOrchestrator_WaitCommand(
            &mqtt,
            &command,
            config.mqtt_command_timeout_ms);
        if (status != ESP_OK)
        {
            if (config.single_shot)
            {
                (void)MqttOrchestrator_Stop(&mqtt);
                return status;
            }
            continue;
        }

        status = ProcessCommand(
            &config,
            &mqtt,
            &uart,
            &command,
            test_ca);

        if (config.single_shot)
        {
            /*
             * Allow the MQTT task to flush queued QoS-1 final status before
             * the hardware runner observes PASS and stops the test.
             */
            vTaskDelay(pdMS_TO_TICKS(500));
            (void)MqttOrchestrator_Stop(&mqtt);
            return status;
        }

        if (status != ESP_OK)
        {
            ESP_LOGW(TAG,
                     "update command failed; waiting for next MQTT command");
        }
    }
}
