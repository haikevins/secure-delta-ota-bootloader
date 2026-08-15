#include "mqtt_orchestration_contract.h"

#include <stdio.h>
#include <string.h>

bool MqttOrchestration_IsHttpsUrl(const char *url)
{
    static const char prefix[] = "https://";

    if (url == NULL)
    {
        return false;
    }

    return strncmp(url, prefix, sizeof(prefix) - 1U) == 0;
}

MqttOrchestrationContractStatus_t MqttOrchestration_ValidateCommand(
    const MqttOtaCommand_t *command,
    uint32_t max_image_size)
{
    if ((command == NULL) || (max_image_size == 0UL))
    {
        return MQTT_ORCH_CONTRACT_INVALID_ARGUMENT;
    }

    if (command->schema != MQTT_ORCH_SCHEMA_VERSION)
    {
        return MQTT_ORCH_CONTRACT_BAD_SCHEMA;
    }

    if (command->update_id == 0UL)
    {
        return MQTT_ORCH_CONTRACT_BAD_UPDATE_ID;
    }

    if (command->target_version == 0UL)
    {
        return MQTT_ORCH_CONTRACT_BAD_TARGET_VERSION;
    }

    if ((command->image_size < 8UL) ||
        (command->image_size > max_image_size))
    {
        return MQTT_ORCH_CONTRACT_BAD_SIZE;
    }

    /*
     * CRC32 value zero is legal, so no sentinel rejection is applied.
     * It remains an integrity check, not an authenticity mechanism.
     */

    if ((command->url[0] == '\0') ||
        !MqttOrchestration_IsHttpsUrl(command->url))
    {
        return MQTT_ORCH_CONTRACT_BAD_URL;
    }

    return MQTT_ORCH_CONTRACT_OK;
}

static MqttOrchestrationContractStatus_t BuildOneTopic(
    const char *base,
    const char *device_id,
    const char *suffix,
    char *output,
    size_t output_size)
{
    int written;

    if ((base == NULL) || (device_id == NULL) ||
        (suffix == NULL) || (output == NULL) ||
        (output_size == 0U) ||
        (base[0] == '\0') || (device_id[0] == '\0'))
    {
        return MQTT_ORCH_CONTRACT_INVALID_ARGUMENT;
    }

    written = snprintf(output,
                       output_size,
                       "%s/%s/%s",
                       base,
                       device_id,
                       suffix);
    if ((written < 0) || ((size_t)written >= output_size))
    {
        return MQTT_ORCH_CONTRACT_TOPIC_TOO_LONG;
    }

    return MQTT_ORCH_CONTRACT_OK;
}

MqttOrchestrationContractStatus_t MqttOrchestration_BuildTopics(
    const char *base,
    const char *device_id,
    char *command_topic,
    size_t command_topic_size,
    char *status_topic,
    size_t status_topic_size,
    char *progress_topic,
    size_t progress_topic_size)
{
    MqttOrchestrationContractStatus_t status;

    status = BuildOneTopic(base,
                           device_id,
                           "command",
                           command_topic,
                           command_topic_size);
    if (status != MQTT_ORCH_CONTRACT_OK)
    {
        return status;
    }

    status = BuildOneTopic(base,
                           device_id,
                           "status",
                           status_topic,
                           status_topic_size);
    if (status != MQTT_ORCH_CONTRACT_OK)
    {
        return status;
    }

    return BuildOneTopic(base,
                         device_id,
                         "progress",
                         progress_topic,
                         progress_topic_size);
}
