#include "ota_response.h"
#include "firmware_container.h"
#include "firmware_version.h"

static void PutU16Le(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8U);
}

static void PutU32Le(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8U);
    dst[2] = (uint8_t)(value >> 16U);
    dst[3] = (uint8_t)(value >> 24U);
}

static void InitResponse(const OtaPacket_t *request,
                         uint8_t command,
                         OtaPacket_t *response)
{
    response->magic = OTA_PACKET_MAGIC;
    response->protocol_version = OTA_PROTOCOL_VERSION;
    response->command = command;
    response->update_id = request->update_id;
    response->offset = 0UL;
    response->sequence = request->sequence;
    response->payload_length = 0U;
    response->packet_crc32 = 0UL;
}

static void BuildStatus(const OtaPacket_t *request,
                        OtaStatus_t status,
                        const OtaResponseInfo_t *info,
                        OtaPacket_t *response)
{
    InitResponse(request,
                 (status == OTA_STATUS_OK) ? (uint8_t)OTA_CMD_ACK
                                           : (uint8_t)OTA_CMD_NACK,
                 response);
    response->offset = info->next_expected_offset;
    response->payload_length = OTA_ACK_PAYLOAD_SIZE;
    response->payload[0] = (uint8_t)status;
    response->payload[1] = info->update_state;
    PutU16Le(&response->payload[2], request->sequence);
    PutU32Le(&response->payload[4], info->next_expected_offset);
    PutU32Le(&response->payload[8], info->received_size);
    PutU32Le(&response->payload[12], info->expected_size);
    PutU32Le(&response->payload[16], info->last_error_detail);
}

void OtaResponse_BuildAck(const OtaPacket_t *request,
                          const OtaResponseInfo_t *info,
                          OtaPacket_t *response)
{
    BuildStatus(request, OTA_STATUS_OK, info, response);
}

void OtaResponse_BuildNack(const OtaPacket_t *request,
                           OtaStatus_t status,
                           const OtaResponseInfo_t *info,
                           OtaPacket_t *response)
{
    BuildStatus(request, status, info, response);
}

void OtaResponse_BuildHelloQuery(const OtaPacket_t *request,
                                 const OtaResponseInfo_t *info,
                                 OtaPacket_t *response)
{
    InitResponse(request, (uint8_t)OTA_CMD_ACK, response);
    response->offset = info->next_expected_offset;
    response->payload_length = OTA_HELLO_RESPONSE_PAYLOAD_SIZE;

    response->payload[0] = OTA_PROTOCOL_VERSION;
    PutU32Le(&response->payload[1], BOOTLOADER_VERSION);
    PutU32Le(&response->payload[5], APPLICATION_VERSION);
    PutU32Le(&response->payload[9], PRODUCT_ID_STM32F103_NODE);
    PutU32Le(&response->payload[13], HARDWARE_REVISION_1);
    PutU32Le(&response->payload[17], info->capability_flags);
    response->payload[21] = info->update_state;
    response->payload[22] = info->last_status;
    PutU16Le(&response->payload[23], 0U);
    PutU32Le(&response->payload[25], info->active_update_id);
    PutU32Le(&response->payload[29], info->next_expected_offset);
    PutU32Le(&response->payload[33], info->expected_size);
}
