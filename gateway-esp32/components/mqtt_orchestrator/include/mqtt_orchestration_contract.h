#ifndef MQTT_ORCHESTRATION_CONTRACT_H
#define MQTT_ORCHESTRATION_CONTRACT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MQTT_ORCH_SCHEMA_VERSION       1UL
#define MQTT_ORCH_MAX_URL_LENGTH       255U
#define MQTT_ORCH_MAX_TOPIC_LENGTH     127U
#define MQTT_ORCH_MAX_PAYLOAD_LENGTH   767U

typedef enum
{
    MQTT_ORCH_CONTRACT_OK = 0,
    MQTT_ORCH_CONTRACT_INVALID_ARGUMENT,
    MQTT_ORCH_CONTRACT_BAD_SCHEMA,
    MQTT_ORCH_CONTRACT_BAD_COMMAND,
    MQTT_ORCH_CONTRACT_BAD_UPDATE_ID,
    MQTT_ORCH_CONTRACT_BAD_TARGET_VERSION,
    MQTT_ORCH_CONTRACT_BAD_SIZE,
    MQTT_ORCH_CONTRACT_BAD_CRC,
    MQTT_ORCH_CONTRACT_BAD_URL,
    MQTT_ORCH_CONTRACT_TOPIC_TOO_LONG
} MqttOrchestrationContractStatus_t;

typedef struct
{
    uint32_t schema;
    uint32_t update_id;
    uint32_t target_version;
    uint32_t image_size;
    uint32_t image_crc32;
    char url[MQTT_ORCH_MAX_URL_LENGTH + 1U];
} MqttOtaCommand_t;

MqttOrchestrationContractStatus_t MqttOrchestration_ValidateCommand(
    const MqttOtaCommand_t *command,
    uint32_t max_image_size);

bool MqttOrchestration_IsHttpsUrl(const char *url);

MqttOrchestrationContractStatus_t MqttOrchestration_BuildTopics(
    const char *base,
    const char *device_id,
    char *command_topic,
    size_t command_topic_size,
    char *status_topic,
    size_t status_topic_size,
    char *progress_topic,
    size_t progress_topic_size);

#endif
