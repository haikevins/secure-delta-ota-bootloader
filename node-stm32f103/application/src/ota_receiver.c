#include "ota_receiver.h"
#include "boot_metadata.h"
#include "crc32.h"
#include "external_flash_storage.h"
#include "firmware_container.h"
#include "memory_map.h"
#include "ota_status.h"
#include "spi_flash.h"

typedef struct
{
    uint8_t state;
    uint8_t last_status;
    uint8_t storage_ready;
    uint8_t has_last_data;
    uint32_t update_id;
    uint32_t expected_size;
    uint32_t next_offset;
    uint32_t artifact_crc32;
    uint32_t last_error_detail;
    uint32_t erased_sector_bitmap;
    uint32_t last_data_offset;
    uint16_t expected_sequence;
    uint16_t last_data_sequence;
    uint16_t last_data_length;
} OtaReceiverSession_t;

static OtaReceiverSession_t g_session;

static uint16_t GetU16Le(const uint8_t *src)
{
    return (uint16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8U));
}

static uint32_t GetU32Le(const uint8_t *src)
{
    return (uint32_t)src[0] |
           ((uint32_t)src[1] << 8U) |
           ((uint32_t)src[2] << 16U) |
           ((uint32_t)src[3] << 24U);
}

static void ResetTransfer(void)
{
    g_session.state = (uint8_t)UPDATE_IDLE;
    g_session.update_id = 0UL;
    g_session.expected_size = 0UL;
    g_session.next_offset = 0UL;
    g_session.artifact_crc32 = 0UL;
    g_session.erased_sector_bitmap = 0UL;
    g_session.expected_sequence = 0U;
    g_session.has_last_data = 0U;
    g_session.last_data_offset = 0UL;
    g_session.last_data_sequence = 0U;
    g_session.last_data_length = 0U;
}

void OtaReceiver_GetResponseInfo(OtaResponseInfo_t *info)
{
    if (info == (OtaResponseInfo_t *)0) { return; }

    info->update_state = g_session.state;
    info->last_status = g_session.last_status;
    info->active_update_id = g_session.update_id;
    info->next_expected_offset = g_session.next_offset;
    info->received_size = g_session.next_offset;
    info->expected_size = g_session.expected_size;
    info->last_error_detail = g_session.last_error_detail;
    info->capability_flags = (g_session.storage_ready != 0U)
                                 ? (OTA_CAP_FULL_IMAGE | OTA_CAP_RESUME)
                                 : 0UL;
}

static void SetStatus(OtaStatus_t status, uint32_t detail)
{
    g_session.last_status = (uint8_t)status;
    g_session.last_error_detail = detail;
}

static void Ack(const OtaPacket_t *request, OtaPacket_t *response)
{
    OtaResponseInfo_t info;
    SetStatus(OTA_STATUS_OK, 0UL);
    OtaReceiver_GetResponseInfo(&info);
    OtaResponse_BuildAck(request, &info, response);
}

static void Nack(const OtaPacket_t *request,
                 OtaStatus_t status,
                 uint32_t detail,
                 OtaPacket_t *response)
{
    OtaResponseInfo_t info;
    SetStatus(status, detail);
    OtaReceiver_GetResponseInfo(&info);
    OtaResponse_BuildNack(request, status, &info, response);
}

static bool EnsureIncomingSectorsErased(uint32_t offset, uint32_t length)
{
    uint32_t first_sector;
    uint32_t last_sector;
    uint32_t sector;

    if (length == 0UL) { return false; }

    first_sector = offset / EXT_FLASH_SECTOR_SIZE;
    last_sector = (offset + length - 1UL) / EXT_FLASH_SECTOR_SIZE;

    for (sector = first_sector; sector <= last_sector; ++sector)
    {
        const uint32_t mask = 1UL << sector;
        if ((g_session.erased_sector_bitmap & mask) == 0UL)
        {
            if (!ExternalFlashStorage_EraseRange(
                    EXTERNAL_FLASH_PARTITION_INCOMING,
                    sector * EXT_FLASH_SECTOR_SIZE,
                    EXT_FLASH_SECTOR_SIZE))
            {
                return false;
            }
            g_session.erased_sector_bitmap |= mask;
        }
    }
    return true;
}

static bool StoredDataMatches(const OtaPacket_t *request)
{
    uint8_t readback[OTA_MAX_PAYLOAD_SIZE];
    uint32_t i;

    if (!ExternalFlashStorage_Read(EXTERNAL_FLASH_PARTITION_INCOMING,
                                   request->offset,
                                   readback,
                                   request->payload_length))
    {
        return false;
    }

    for (i = 0UL; i < (uint32_t)request->payload_length; ++i)
    {
        if (readback[i] != request->payload[i]) { return false; }
    }
    return true;
}

static bool CalculateIncomingCrc(uint32_t length, uint32_t *crc_out)
{
    uint8_t buffer[256];
    uint32_t offset = 0UL;
    uint32_t running = CRC32_IEEE_INITIAL_VALUE;

    if (crc_out == (uint32_t *)0) { return false; }

    while (offset < length)
    {
        uint32_t chunk = length - offset;
        if (chunk > sizeof(buffer)) { chunk = sizeof(buffer); }

        if (!ExternalFlashStorage_Read(EXTERNAL_FLASH_PARTITION_INCOMING,
                                       offset, buffer, chunk))
        {
            return false;
        }
        running = Crc32_Update(running, buffer, chunk);
        offset += chunk;
    }

    *crc_out = running ^ CRC32_IEEE_FINAL_XOR;
    return true;
}

static void ProcessStart(const OtaPacket_t *request, OtaPacket_t *response)
{
    uint8_t artifact_type;
    uint16_t container_version;
    uint32_t artifact_size;
    uint32_t artifact_crc32;

    if (request->payload_length != OTA_START_PAYLOAD_SIZE)
    {
        Nack(request, OTA_STATUS_INVALID_PACKET, 0UL, response);
        return;
    }
    if ((g_session.state != (uint8_t)UPDATE_IDLE) &&
        (g_session.state != (uint8_t)UPDATE_FAILED) &&
        (g_session.state != (uint8_t)UPDATE_RECEIVING))
    {
        Nack(request, OTA_STATUS_INVALID_STATE, 0UL, response);
        return;
    }
    if (g_session.storage_ready == 0U)
    {
        Nack(request, OTA_STATUS_STORAGE_ERROR,
             (uint32_t)SpiFlash_GetLastStatus(), response);
        return;
    }
    if (request->update_id == 0UL)
    {
        Nack(request, OTA_STATUS_INVALID_PACKET, 0UL, response);
        return;
    }

    artifact_type = request->payload[0];
    container_version = GetU16Le(&request->payload[2]);
    artifact_size = GetU32Le(&request->payload[12]);
    artifact_crc32 = GetU32Le(&request->payload[16]);

    if (artifact_type != (uint8_t)FW_IMAGE_FULL)
    {
        Nack(request, OTA_STATUS_INVALID_PACKET, artifact_type, response);
        return;
    }
    if (container_version != FW_CONTAINER_FORMAT_VERSION)
    {
        Nack(request, OTA_STATUS_CONTAINER_ERROR,
             (uint32_t)container_version, response);
        return;
    }
    if ((artifact_size == 0UL) || (artifact_size > EXT_INCOMING_SIZE))
    {
        Nack(request, OTA_STATUS_IMAGE_TOO_LARGE, artifact_size, response);
        return;
    }

    g_session.state = (uint8_t)UPDATE_RECEIVING;
    g_session.update_id = request->update_id;
    g_session.expected_size = artifact_size;
    g_session.next_offset = 0UL;
    g_session.artifact_crc32 = artifact_crc32;
    g_session.erased_sector_bitmap = 0UL;
    g_session.expected_sequence = 0U;
    g_session.has_last_data = 0U;
    Ack(request, response);
}

static void ProcessData(const OtaPacket_t *request, OtaPacket_t *response)
{
    if (g_session.state != (uint8_t)UPDATE_RECEIVING)
    {
        Nack(request, OTA_STATUS_INVALID_STATE, 0UL, response);
        return;
    }
    if (request->update_id != g_session.update_id)
    {
        Nack(request, OTA_STATUS_UPDATE_ID_MISMATCH, 0UL, response);
        return;
    }
    if ((request->payload_length == 0U) ||
        (request->payload_length > OTA_MAX_PAYLOAD_SIZE))
    {
        Nack(request, OTA_STATUS_INVALID_PACKET, 0UL, response);
        return;
    }

    if ((g_session.has_last_data != 0U) &&
        (request->sequence == g_session.last_data_sequence) &&
        (request->offset == g_session.last_data_offset) &&
        (request->payload_length == g_session.last_data_length))
    {
        if (StoredDataMatches(request))
        {
            Ack(request, response);
        }
        else
        {
            Nack(request, OTA_STATUS_WRONG_OFFSET, request->offset, response);
        }
        return;
    }

    if (request->sequence != g_session.expected_sequence)
    {
        Nack(request, OTA_STATUS_WRONG_SEQUENCE,
             (uint32_t)g_session.expected_sequence, response);
        return;
    }
    if (request->offset != g_session.next_offset)
    {
        Nack(request, OTA_STATUS_WRONG_OFFSET,
             g_session.next_offset, response);
        return;
    }
    if ((uint32_t)request->payload_length >
        (g_session.expected_size - g_session.next_offset))
    {
        Nack(request, OTA_STATUS_WRONG_OFFSET,
             g_session.expected_size, response);
        return;
    }

    if (!EnsureIncomingSectorsErased(request->offset, request->payload_length))
    {
        Nack(request, OTA_STATUS_STORAGE_ERROR,
             (uint32_t)SpiFlash_GetLastStatus(), response);
        return;
    }

    if (!ExternalFlashStorage_Write(EXTERNAL_FLASH_PARTITION_INCOMING,
                                    request->offset,
                                    request->payload,
                                    request->payload_length) ||
        !ExternalFlashStorage_Verify(EXTERNAL_FLASH_PARTITION_INCOMING,
                                     request->offset,
                                     request->payload,
                                     request->payload_length))
    {
        Nack(request, OTA_STATUS_STORAGE_ERROR,
             (uint32_t)SpiFlash_GetLastStatus(), response);
        return;
    }

    g_session.has_last_data = 1U;
    g_session.last_data_offset = request->offset;
    g_session.last_data_sequence = request->sequence;
    g_session.last_data_length = request->payload_length;
    g_session.next_offset += (uint32_t)request->payload_length;
    g_session.expected_sequence = (uint16_t)(g_session.expected_sequence + 1U);
    Ack(request, response);
}

static void ProcessFinish(const OtaPacket_t *request, OtaPacket_t *response)
{
    uint32_t artifact_crc = 0UL;

    if (request->payload_length != 0U)
    {
        Nack(request, OTA_STATUS_INVALID_PACKET, 0UL, response);
        return;
    }

    /* FINISH is idempotent so an ACK lost on UART can be retried safely. */
    if ((g_session.state == (uint8_t)UPDATE_ARTIFACT_READY) &&
        (request->update_id == g_session.update_id) &&
        (g_session.next_offset == g_session.expected_size))
    {
        Ack(request, response);
        return;
    }

    if (g_session.state != (uint8_t)UPDATE_RECEIVING)
    {
        Nack(request, OTA_STATUS_INVALID_STATE, 0UL, response);
        return;
    }
    if (request->update_id != g_session.update_id)
    {
        Nack(request, OTA_STATUS_UPDATE_ID_MISMATCH, 0UL, response);
        return;
    }
    if (g_session.next_offset != g_session.expected_size)
    {
        Nack(request, OTA_STATUS_WRONG_OFFSET,
             g_session.next_offset, response);
        return;
    }

    if (!CalculateIncomingCrc(g_session.expected_size, &artifact_crc))
    {
        Nack(request, OTA_STATUS_STORAGE_ERROR,
             (uint32_t)SpiFlash_GetLastStatus(), response);
        return;
    }

    if (artifact_crc != g_session.artifact_crc32)
    {
        g_session.state = (uint8_t)UPDATE_FAILED;
        Nack(request, OTA_STATUS_CONTAINER_ERROR, artifact_crc, response);
        return;
    }

    g_session.state = (uint8_t)UPDATE_ARTIFACT_READY;
    Ack(request, response);
}

bool OtaReceiver_Init(void)
{
    g_session.storage_ready = ExternalFlashStorage_Init() ? 1U : 0U;
    g_session.last_status = (g_session.storage_ready != 0U)
                                ? (uint8_t)OTA_STATUS_OK
                                : (uint8_t)OTA_STATUS_STORAGE_ERROR;
    g_session.last_error_detail = (g_session.storage_ready != 0U)
                                      ? 0UL
                                      : (uint32_t)SpiFlash_GetLastStatus();
    ResetTransfer();
    return g_session.storage_ready != 0U;
}

void OtaReceiver_ProcessPacket(const OtaPacket_t *request,
                               OtaPacket_t *response)
{
    OtaResponseInfo_t info;

    if ((request == (const OtaPacket_t *)0) ||
        (response == (OtaPacket_t *)0))
    {
        return;
    }

    switch ((OtaCommand_t)request->command)
    {
        case OTA_CMD_HELLO:
        case OTA_CMD_QUERY:
        case OTA_CMD_STATUS:
            if (request->payload_length != 0U)
            {
                Nack(request, OTA_STATUS_INVALID_PACKET, 0UL, response);
                return;
            }
            SetStatus(OTA_STATUS_OK, 0UL);
            OtaReceiver_GetResponseInfo(&info);
            OtaResponse_BuildHelloQuery(request, &info, response);
            return;

        case OTA_CMD_START:
            ProcessStart(request, response);
            return;

        case OTA_CMD_DATA:
            ProcessData(request, response);
            return;

        case OTA_CMD_FINISH:
            ProcessFinish(request, response);
            return;

        case OTA_CMD_ABORT:
            if (request->payload_length != 0U)
            {
                Nack(request, OTA_STATUS_INVALID_PACKET, 0UL, response);
                return;
            }
            ResetTransfer();
            Ack(request, response);
            return;

        case OTA_CMD_RESUME:
            if (request->payload_length != 0U)
            {
                Nack(request, OTA_STATUS_INVALID_PACKET, 0UL, response);
                return;
            }
            if (g_session.state != (uint8_t)UPDATE_RECEIVING)
            {
                Nack(request, OTA_STATUS_INVALID_STATE, 0UL, response);
                return;
            }
            if (request->update_id != g_session.update_id)
            {
                Nack(request, OTA_STATUS_UPDATE_ID_MISMATCH, 0UL, response);
                return;
            }
            Ack(request, response);
            return;

        case OTA_CMD_INSTALL:
        case OTA_CMD_CONFIRM:
            Nack(request, OTA_STATUS_INVALID_STATE, 0UL, response);
            return;

        case OTA_CMD_ACK:
        case OTA_CMD_NACK:
        default:
            Nack(request, OTA_STATUS_INVALID_PACKET,
                 request->command, response);
            return;
    }
}
