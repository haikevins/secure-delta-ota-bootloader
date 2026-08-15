#include "mqtt_orchestrator.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "mqtt_client.h"

#define MQTT_ORCH_CONNECTED_BIT BIT0
#define MQTT_ORCH_READY_BIT     BIT1
#define MQTT_ORCH_PUBLISHED_BIT BIT2

#define MQTT_ORCH_COMMAND_QUEUE_LENGTH 1U

static const char *TAG = "mqtt_orch";

static EventGroupHandle_t Events(MqttOrchestrator_t *orchestrator)
{
    return (EventGroupHandle_t)orchestrator->events;
}

static QueueHandle_t CommandQueue(MqttOrchestrator_t *orchestrator)
{
    return (QueueHandle_t)orchestrator->command_queue;
}

static esp_mqtt_client_handle_t Client(MqttOrchestrator_t *orchestrator)
{
    return (esp_mqtt_client_handle_t)orchestrator->client;
}

static bool TopicMatches(const char *expected,
                         const char *actual,
                         int actual_length)
{
    size_t expected_length;

    if ((expected == NULL) || (actual == NULL) ||
        (actual_length < 0))
    {
        return false;
    }

    expected_length = strlen(expected);
    return (expected_length == (size_t)actual_length) &&
           (memcmp(expected, actual, expected_length) == 0);
}

static bool JsonUint32(const cJSON *root,
                       const char *name,
                       uint32_t *value_out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    double value;

    if ((item == NULL) || !cJSON_IsNumber(item) ||
        (value_out == NULL))
    {
        return false;
    }

    value = item->valuedouble;
    if ((value < 0.0) || (value > 4294967295.0) ||
        ((double)((uint32_t)value) != value))
    {
        return false;
    }

    *value_out = (uint32_t)value;
    return true;
}

static esp_err_t ParseCommandJson(const char *payload,
                                  size_t payload_length,
                                  MqttOtaCommand_t *command)
{
    cJSON *root = NULL;
    const cJSON *cmd;
    const cJSON *url;
    MqttOrchestrationContractStatus_t contract;
    esp_err_t status = ESP_ERR_INVALID_ARG;

    if ((payload == NULL) || (command == NULL) ||
        (payload_length == 0U) ||
        (payload_length > MQTT_ORCH_MAX_PAYLOAD_LENGTH))
    {
        return ESP_ERR_INVALID_ARG;
    }

    root = cJSON_ParseWithLength(payload, payload_length);
    if ((root == NULL) || !cJSON_IsObject(root))
    {
        goto cleanup;
    }

    memset(command, 0, sizeof(*command));

    cmd = cJSON_GetObjectItemCaseSensitive(root, "cmd");
    url = cJSON_GetObjectItemCaseSensitive(root, "url");

    if ((cmd == NULL) || !cJSON_IsString(cmd) ||
        (cmd->valuestring == NULL) ||
        (strcmp(cmd->valuestring, "update") != 0) ||
        (url == NULL) || !cJSON_IsString(url) ||
        (url->valuestring == NULL) ||
        (strlen(url->valuestring) > MQTT_ORCH_MAX_URL_LENGTH))
    {
        goto cleanup;
    }

    if (!JsonUint32(root, "schema", &command->schema) ||
        !JsonUint32(root, "update_id", &command->update_id) ||
        !JsonUint32(root, "target_version", &command->target_version) ||
        !JsonUint32(root, "size", &command->image_size) ||
        !JsonUint32(root, "crc32", &command->image_crc32))
    {
        goto cleanup;
    }

    memcpy(command->url,
           url->valuestring,
           strlen(url->valuestring) + 1U);

    contract = MqttOrchestration_ValidateCommand(
        command,
        128UL * 1024UL);
    if (contract != MQTT_ORCH_CONTRACT_OK)
    {
        ESP_LOGW(TAG,
                 "command contract rejected status=%d",
                 (int)contract);
        goto cleanup;
    }

    status = ESP_OK;

cleanup:
    if (root != NULL)
    {
        cJSON_Delete(root);
    }
    return status;
}

static void ResetFragment(MqttOrchestrator_t *orchestrator)
{
    orchestrator->fragment_total = 0UL;
    orchestrator->fragment_received = 0UL;
    orchestrator->fragment_active = false;
    orchestrator->fragment_buffer[0] = '\0';
}

static void HandleData(MqttOrchestrator_t *orchestrator,
                       const esp_mqtt_event_handle_t event)
{
    MqttOtaCommand_t command;

    if ((orchestrator == NULL) || (event == NULL) ||
        (event->data == NULL) ||
        (event->data_len < 0) ||
        (event->total_data_len <= 0) ||
        (event->current_data_offset < 0))
    {
        return;
    }

    if (event->current_data_offset == 0)
    {
        ResetFragment(orchestrator);

        if (!TopicMatches(orchestrator->command_topic,
                          event->topic,
                          event->topic_len))
        {
            return;
        }

        if (event->total_data_len >
            (int)MQTT_ORCH_MAX_PAYLOAD_LENGTH)
        {
            ESP_LOGW(TAG,
                     "command payload too large: %d",
                     event->total_data_len);
            return;
        }

        orchestrator->fragment_total =
            (uint32_t)event->total_data_len;
        orchestrator->fragment_active = true;
    }

    if (!orchestrator->fragment_active ||
        ((uint32_t)event->total_data_len !=
         orchestrator->fragment_total) ||
        ((uint32_t)event->current_data_offset !=
         orchestrator->fragment_received) ||
        ((uint32_t)event->data_len >
         (orchestrator->fragment_total -
          orchestrator->fragment_received)))
    {
        ResetFragment(orchestrator);
        return;
    }

    memcpy(&orchestrator->fragment_buffer[
               orchestrator->fragment_received],
           event->data,
           (size_t)event->data_len);

    orchestrator->fragment_received +=
        (uint32_t)event->data_len;

    if (orchestrator->fragment_received !=
        orchestrator->fragment_total)
    {
        return;
    }

    orchestrator->fragment_buffer[
        orchestrator->fragment_total] = '\0';

    if (ParseCommandJson(orchestrator->fragment_buffer,
                         orchestrator->fragment_total,
                         &command) == ESP_OK)
    {
        if (xQueueSend(CommandQueue(orchestrator),
                       &command,
                       0) != pdTRUE)
        {
            ESP_LOGW(TAG,
                     "command queue busy; update_id=0x%08" PRIX32,
                     command.update_id);
        }
        else
        {
            ESP_LOGI(TAG,
                     "command queued update_id=0x%08" PRIX32
                     " target=v%" PRIu32,
                     command.update_id,
                     command.target_version);
        }
    }
    else
    {
        ESP_LOGW(TAG, "invalid MQTT update command");
    }

    ResetFragment(orchestrator);
}

static void MqttEventHandler(void *handler_args,
                             esp_event_base_t base,
                             int32_t event_id,
                             void *event_data)
{
    MqttOrchestrator_t *orchestrator =
        (MqttOrchestrator_t *)handler_args;
    esp_mqtt_event_handle_t event =
        (esp_mqtt_event_handle_t)event_data;

    (void)base;

    if ((orchestrator == NULL) || (event == NULL))
    {
        return;
    }

    switch ((esp_mqtt_event_id_t)event_id)
    {
        case MQTT_EVENT_CONNECTED:
        {
            int message_id;

            xEventGroupSetBits(
                Events(orchestrator),
                MQTT_ORCH_CONNECTED_BIT);

            message_id = esp_mqtt_client_subscribe_single(
                Client(orchestrator),
                orchestrator->command_topic,
                1);

            if (message_id < 0)
            {
                ESP_LOGE(TAG, "command subscribe failed");
            }
            break;
        }

        case MQTT_EVENT_SUBSCRIBED:
            xEventGroupSetBits(
                Events(orchestrator),
                MQTT_ORCH_READY_BIT);
            break;

        case MQTT_EVENT_DISCONNECTED:
            xEventGroupClearBits(
                Events(orchestrator),
                MQTT_ORCH_CONNECTED_BIT |
                MQTT_ORCH_READY_BIT);
            ResetFragment(orchestrator);
            break;

        case MQTT_EVENT_DATA:
            HandleData(orchestrator, event);
            break;

        case MQTT_EVENT_PUBLISHED:
            orchestrator->last_published_message_id =
                event->msg_id;
            xEventGroupSetBits(
                Events(orchestrator),
                MQTT_ORCH_PUBLISHED_BIT);
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGW(TAG, "MQTT_EVENT_ERROR");
            break;

        default:
            break;
    }
}

esp_err_t MqttOrchestrator_Start(
    MqttOrchestrator_t *orchestrator,
    const MqttOrchestratorConfig_t *config)
{
    esp_mqtt_client_config_t mqtt_config;
    MqttOrchestrationContractStatus_t topic_status;
    EventBits_t bits;
    esp_err_t status;

    if ((orchestrator == NULL) || (config == NULL) ||
        (config->broker_uri == NULL) ||
        (config->client_id == NULL) ||
        (config->topic_base == NULL) ||
        (config->device_id == NULL) ||
        (config->ready_timeout_ms == 0UL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if ((strncmp(config->broker_uri,
                 "mqtts://",
                 sizeof("mqtts://") - 1U) != 0) ||
        ((config->cert_pem == NULL) &&
         !config->use_crt_bundle))
    {
        return ESP_ERR_INVALID_ARG;
    }

    memset(orchestrator, 0, sizeof(*orchestrator));
    orchestrator->last_published_message_id = -1;

    topic_status = MqttOrchestration_BuildTopics(
        config->topic_base,
        config->device_id,
        orchestrator->command_topic,
        sizeof(orchestrator->command_topic),
        orchestrator->status_topic,
        sizeof(orchestrator->status_topic),
        orchestrator->progress_topic,
        sizeof(orchestrator->progress_topic));
    if (topic_status != MQTT_ORCH_CONTRACT_OK)
    {
        return ESP_ERR_INVALID_ARG;
    }

    orchestrator->events = xEventGroupCreate();
    orchestrator->command_queue = xQueueCreate(
        MQTT_ORCH_COMMAND_QUEUE_LENGTH,
        sizeof(MqttOtaCommand_t));

    if ((orchestrator->events == NULL) ||
        (orchestrator->command_queue == NULL))
    {
        status = ESP_ERR_NO_MEM;
        goto fail;
    }

    memset(&mqtt_config, 0, sizeof(mqtt_config));
    mqtt_config.broker.address.uri = config->broker_uri;
    mqtt_config.credentials.client_id = config->client_id;

    if ((config->username != NULL) &&
        (config->username[0] != '\0'))
    {
        mqtt_config.credentials.username = config->username;
    }
    if ((config->password != NULL) &&
        (config->password[0] != '\0'))
    {
        mqtt_config.credentials.authentication.password =
            config->password;
    }

    if (config->cert_pem != NULL)
    {
        mqtt_config.broker.verification.certificate =
            config->cert_pem;
    }
    else
    {
        mqtt_config.broker.verification.crt_bundle_attach =
            esp_crt_bundle_attach;
    }

    orchestrator->client = esp_mqtt_client_init(&mqtt_config);
    if (orchestrator->client == NULL)
    {
        status = ESP_ERR_NO_MEM;
        goto fail;
    }

    status = esp_mqtt_client_register_event(
        Client(orchestrator),
        ESP_EVENT_ANY_ID,
        MqttEventHandler,
        orchestrator);
    if (status != ESP_OK)
    {
        goto fail;
    }

    status = esp_mqtt_client_start(Client(orchestrator));
    if (status != ESP_OK)
    {
        goto fail;
    }

    bits = xEventGroupWaitBits(
        Events(orchestrator),
        MQTT_ORCH_READY_BIT,
        pdFALSE,
        pdTRUE,
        pdMS_TO_TICKS(config->ready_timeout_ms));

    if ((bits & MQTT_ORCH_READY_BIT) == 0U)
    {
        status = ESP_ERR_TIMEOUT;
        goto fail_started;
    }

    ESP_LOGI(TAG,
             "MQTT ready command=%s status=%s progress=%s",
             orchestrator->command_topic,
             orchestrator->status_topic,
             orchestrator->progress_topic);
    return ESP_OK;

fail_started:
    (void)esp_mqtt_client_stop(Client(orchestrator));

fail:
    if (orchestrator->client != NULL)
    {
        (void)esp_mqtt_client_destroy(Client(orchestrator));
        orchestrator->client = NULL;
    }
    if (orchestrator->command_queue != NULL)
    {
        vQueueDelete(CommandQueue(orchestrator));
        orchestrator->command_queue = NULL;
    }
    if (orchestrator->events != NULL)
    {
        vEventGroupDelete(Events(orchestrator));
        orchestrator->events = NULL;
    }
    return status;
}

esp_err_t MqttOrchestrator_WaitCommand(
    MqttOrchestrator_t *orchestrator,
    MqttOtaCommand_t *command,
    uint32_t timeout_ms)
{
    TickType_t ticks;

    if ((orchestrator == NULL) || (command == NULL) ||
        (orchestrator->command_queue == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    ticks = (timeout_ms == 0UL)
                ? portMAX_DELAY
                : pdMS_TO_TICKS(timeout_ms);

    if (xQueueReceive(CommandQueue(orchestrator),
                      command,
                      ticks) != pdTRUE)
    {
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

static esp_err_t EnqueueJson(MqttOrchestrator_t *orchestrator,
                             const char *topic,
                             const char *payload,
                             int qos,
                             bool retain)
{
    int message_id;

    if ((orchestrator == NULL) ||
        (Client(orchestrator) == NULL) ||
        (topic == NULL) || (payload == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    message_id = esp_mqtt_client_enqueue(
        Client(orchestrator),
        topic,
        payload,
        0,
        qos,
        retain ? 1 : 0,
        qos > 0);

    return (message_id >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t MqttOrchestrator_PublishStatus(
    MqttOrchestrator_t *orchestrator,
    const char *state,
    uint32_t update_id,
    uint32_t target_version,
    const char *detail,
    bool retain)
{
    char payload[384];
    int written;

    if ((orchestrator == NULL) || (state == NULL) ||
        (detail == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    written = snprintf(
        payload,
        sizeof(payload),
        "{\"schema\":1,\"state\":\"%s\","
        "\"update_id\":%" PRIu32 ","
        "\"target_version\":%" PRIu32 ","
        "\"detail\":\"%s\"}",
        state,
        update_id,
        target_version,
        detail);

    if ((written < 0) || ((size_t)written >= sizeof(payload)))
    {
        return ESP_ERR_INVALID_SIZE;
    }

    return EnqueueJson(orchestrator,
                       orchestrator->status_topic,
                       payload,
                       1,
                       retain);
}

esp_err_t MqttOrchestrator_PublishProgress(
    MqttOrchestrator_t *orchestrator,
    const char *stage,
    uint32_t update_id,
    uint32_t current,
    uint32_t total)
{
    char payload[256];
    uint32_t percent;
    int written;

    if ((orchestrator == NULL) || (stage == NULL) ||
        (total == 0UL) || (current > total))
    {
        return ESP_ERR_INVALID_ARG;
    }

    percent = (uint32_t)(
        ((uint64_t)current * 100ULL) / total);

    written = snprintf(
        payload,
        sizeof(payload),
        "{\"schema\":1,\"stage\":\"%s\","
        "\"update_id\":%" PRIu32 ","
        "\"current\":%" PRIu32 ","
        "\"total\":%" PRIu32 ","
        "\"percent\":%" PRIu32 "}",
        stage,
        update_id,
        current,
        total,
        percent);

    if ((written < 0) || ((size_t)written >= sizeof(payload)))
    {
        return ESP_ERR_INVALID_SIZE;
    }

    /*
     * QoS-0 progress is telemetry, not durable state. Send it immediately
     * from the Phase-11 worker instead of enqueue(store=false), because
     * ESP-MQTT does not retain QoS-0 messages in that enqueue mode.
     */
    return (esp_mqtt_client_publish(
                Client(orchestrator),
                orchestrator->progress_topic,
                payload,
                0,
                0,
                0) >= 0)
               ? ESP_OK
               : ESP_FAIL;
}

esp_err_t MqttOrchestrator_PublishStatusAndWait(
    MqttOrchestrator_t *orchestrator,
    const char *state,
    uint32_t update_id,
    uint32_t target_version,
    const char *detail,
    bool retain,
    uint32_t ack_timeout_ms)
{
    char payload[384];
    int written;
    int message_id;
    TickType_t deadline;

    if ((orchestrator == NULL) || (state == NULL) ||
        (detail == NULL) || (ack_timeout_ms == 0UL) ||
        (Client(orchestrator) == NULL) ||
        (Events(orchestrator) == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    written = snprintf(
        payload,
        sizeof(payload),
        "{\"schema\":1,\"state\":\"%s\","
        "\"update_id\":%" PRIu32 ","
        "\"target_version\":%" PRIu32 ","
        "\"detail\":\"%s\"}",
        state,
        update_id,
        target_version,
        detail);

    if ((written < 0) || ((size_t)written >= sizeof(payload)))
    {
        return ESP_ERR_INVALID_SIZE;
    }

    /*
     * Clear stale ACK state before publishing. MQTT_EVENT_PUBLISHED stores
     * the broker-acknowledged QoS-1 message ID. Checking the stored ID both
     * before and after waiting also covers the race where PUBACK arrives
     * immediately after esp_mqtt_client_publish() returns.
     */
    xEventGroupClearBits(
        Events(orchestrator),
        MQTT_ORCH_PUBLISHED_BIT);
    orchestrator->last_published_message_id = -1;

    message_id = esp_mqtt_client_publish(
        Client(orchestrator),
        orchestrator->status_topic,
        payload,
        0,
        1,
        retain ? 1 : 0);
    if (message_id < 0)
    {
        return ESP_FAIL;
    }

    if (orchestrator->last_published_message_id == message_id)
    {
        return ESP_OK;
    }

    deadline = xTaskGetTickCount() +
        pdMS_TO_TICKS(ack_timeout_ms);

    for (;;)
    {
        TickType_t now;
        TickType_t remaining;
        EventBits_t bits;

        now = xTaskGetTickCount();
        if ((int32_t)(deadline - now) <= 0)
        {
            return ESP_ERR_TIMEOUT;
        }

        remaining = deadline - now;

        bits = xEventGroupWaitBits(
            Events(orchestrator),
            MQTT_ORCH_PUBLISHED_BIT,
            pdTRUE,
            pdFALSE,
            remaining);

        if (orchestrator->last_published_message_id == message_id)
        {
            return ESP_OK;
        }

        if ((bits & MQTT_ORCH_PUBLISHED_BIT) == 0U)
        {
            return ESP_ERR_TIMEOUT;
        }
    }
}

esp_err_t MqttOrchestrator_Stop(MqttOrchestrator_t *orchestrator)
{
    esp_err_t status = ESP_OK;

    if (orchestrator == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (orchestrator->client != NULL)
    {
        status = esp_mqtt_client_stop(Client(orchestrator));
        (void)esp_mqtt_client_destroy(Client(orchestrator));
        orchestrator->client = NULL;
    }

    if (orchestrator->command_queue != NULL)
    {
        vQueueDelete(CommandQueue(orchestrator));
        orchestrator->command_queue = NULL;
    }

    if (orchestrator->events != NULL)
    {
        vEventGroupDelete(Events(orchestrator));
        orchestrator->events = NULL;
    }

    ResetFragment(orchestrator);
    return status;
}
