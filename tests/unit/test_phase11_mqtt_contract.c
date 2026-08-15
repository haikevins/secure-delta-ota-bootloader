#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mqtt_orchestration_contract.h"

static MqttOtaCommand_t ValidCommand(void)
{
    MqttOtaCommand_t command = {
        .schema = MQTT_ORCH_SCHEMA_VERSION,
        .update_id = 0xB00B0001UL,
        .target_version = 2UL,
        .image_size = 10184UL,
        .image_crc32 = 0x8A5C459CUL,
        .url = "https://192.168.1.8:8443/phase11_candidate.bin",
    };

    return command;
}

static void TestCommandValidation(void)
{
    MqttOtaCommand_t command = ValidCommand();

    assert(MqttOrchestration_ValidateCommand(
               &command,
               38UL * 1024UL) == MQTT_ORCH_CONTRACT_OK);

    command.schema = 2UL;
    assert(MqttOrchestration_ValidateCommand(
               &command,
               38UL * 1024UL) == MQTT_ORCH_CONTRACT_BAD_SCHEMA);

    command = ValidCommand();
    command.update_id = 0UL;
    assert(MqttOrchestration_ValidateCommand(
               &command,
               38UL * 1024UL) == MQTT_ORCH_CONTRACT_BAD_UPDATE_ID);

    command = ValidCommand();
    command.target_version = 0UL;
    assert(MqttOrchestration_ValidateCommand(
               &command,
               38UL * 1024UL) ==
           MQTT_ORCH_CONTRACT_BAD_TARGET_VERSION);

    command = ValidCommand();
    command.image_size = (38UL * 1024UL) + 1UL;
    assert(MqttOrchestration_ValidateCommand(
               &command,
               38UL * 1024UL) == MQTT_ORCH_CONTRACT_BAD_SIZE);

    command = ValidCommand();
    strcpy(command.url, "http://192.168.1.8/fw.bin");
    assert(MqttOrchestration_ValidateCommand(
               &command,
               38UL * 1024UL) == MQTT_ORCH_CONTRACT_BAD_URL);
}

static void TestTopics(void)
{
    char command[128];
    char status[128];
    char progress[128];

    assert(MqttOrchestration_BuildTopics(
               "sdota",
               "bluepill-001",
               command,
               sizeof(command),
               status,
               sizeof(status),
               progress,
               sizeof(progress)) == MQTT_ORCH_CONTRACT_OK);

    assert(strcmp(command, "sdota/bluepill-001/command") == 0);
    assert(strcmp(status, "sdota/bluepill-001/status") == 0);
    assert(strcmp(progress, "sdota/bluepill-001/progress") == 0);
}

int main(void)
{
    TestCommandValidation();
    TestTopics();

    puts("Phase 11 MQTT orchestration contract host tests: PASS");
    return 0;
}
