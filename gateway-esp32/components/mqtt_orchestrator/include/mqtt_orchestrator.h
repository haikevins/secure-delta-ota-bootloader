#ifndef MQTT_ORCHESTRATOR_H
#define MQTT_ORCHESTRATOR_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "mqtt_orchestration_contract.h"

typedef struct
{
    const char *broker_uri;
    const char *client_id;
    const char *topic_base;
    const char *device_id;
    const char *username;
    const char *password;
    const char *cert_pem;
    bool use_crt_bundle;
    uint32_t ready_timeout_ms;
} MqttOrchestratorConfig_t;

typedef struct
{
    void *client;
    void *events;
    void *command_queue;

    char command_topic[MQTT_ORCH_MAX_TOPIC_LENGTH + 1U];
    char status_topic[MQTT_ORCH_MAX_TOPIC_LENGTH + 1U];
    char progress_topic[MQTT_ORCH_MAX_TOPIC_LENGTH + 1U];

    char fragment_buffer[MQTT_ORCH_MAX_PAYLOAD_LENGTH + 1U];
    uint32_t fragment_total;
    uint32_t fragment_received;
    bool fragment_active;

    volatile int last_published_message_id;
} MqttOrchestrator_t;

esp_err_t MqttOrchestrator_Start(
    MqttOrchestrator_t *orchestrator,
    const MqttOrchestratorConfig_t *config);

esp_err_t MqttOrchestrator_WaitCommand(
    MqttOrchestrator_t *orchestrator,
    MqttOtaCommand_t *command,
    uint32_t timeout_ms);

esp_err_t MqttOrchestrator_PublishStatus(
    MqttOrchestrator_t *orchestrator,
    const char *state,
    uint32_t update_id,
    uint32_t target_version,
    const char *detail,
    bool retain);

esp_err_t MqttOrchestrator_PublishProgress(
    MqttOrchestrator_t *orchestrator,
    const char *stage,
    uint32_t update_id,
    uint32_t current,
    uint32_t total);

esp_err_t MqttOrchestrator_PublishStatusAndWait(
    MqttOrchestrator_t *orchestrator,
    const char *state,
    uint32_t update_id,
    uint32_t target_version,
    const char *detail,
    bool retain,
    uint32_t ack_timeout_ms);

esp_err_t MqttOrchestrator_Stop(MqttOrchestrator_t *orchestrator);

#endif
