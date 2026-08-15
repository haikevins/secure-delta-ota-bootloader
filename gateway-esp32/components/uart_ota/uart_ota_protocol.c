#include "uart_ota_protocol.h"

#include <string.h>

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

static uint16_t GetU16Le(const uint8_t *src)
{
    return (uint16_t)((uint16_t)src[0] |
                      ((uint16_t)src[1] << 8U));
}

static uint32_t GetU32Le(const uint8_t *src)
{
    return (uint32_t)src[0] |
           ((uint32_t)src[1] << 8U) |
           ((uint32_t)src[2] << 16U) |
           ((uint32_t)src[3] << 24U);
}

uint32_t UartOta_Crc32(const void *data, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFUL;

    if ((bytes == NULL) && (length != 0U))
    {
        return 0UL;
    }

    for (size_t i = 0U; i < length; ++i)
    {
        crc ^= bytes[i];
        for (uint32_t bit = 0UL; bit < 8UL; ++bit)
        {
            const uint32_t mask =
                (uint32_t)(0UL - (crc & 1UL));
            crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
        }
    }

    return crc ^ 0xFFFFFFFFUL;
}

bool UartOta_CobsEncode(const uint8_t *input,
                        size_t input_length,
                        uint8_t *output,
                        size_t output_capacity,
                        size_t *output_length)
{
    size_t read_index = 0U;
    size_t write_index = 1U;
    size_t code_index = 0U;
    uint8_t code = 1U;

    if ((output == NULL) || (output_length == NULL) ||
        ((input == NULL) && (input_length != 0U)) ||
        (output_capacity == 0U))
    {
        return false;
    }

    while (read_index < input_length)
    {
        if (input[read_index] == 0U)
        {
            output[code_index] = code;
            code = 1U;
            code_index = write_index;
            if (write_index >= output_capacity)
            {
                return false;
            }
            ++write_index;
            ++read_index;
        }
        else
        {
            if (write_index >= output_capacity)
            {
                return false;
            }
            output[write_index++] = input[read_index++];
            ++code;

            if (code == 0xFFU)
            {
                output[code_index] = code;
                code = 1U;
                code_index = write_index;
                if (write_index >= output_capacity)
                {
                    return false;
                }
                ++write_index;
            }
        }
    }

    output[code_index] = code;
    *output_length = write_index;
    return true;
}

bool UartOta_CobsDecode(const uint8_t *input,
                        size_t input_length,
                        uint8_t *output,
                        size_t output_capacity,
                        size_t *output_length)
{
    size_t read_index = 0U;
    size_t write_index = 0U;

    if ((input == NULL) || (output == NULL) ||
        (output_length == NULL) || (input_length == 0U))
    {
        return false;
    }

    while (read_index < input_length)
    {
        const uint8_t code = input[read_index++];
        const size_t count = (size_t)code - 1U;

        if (code == 0U)
        {
            return false;
        }
        if (count > (input_length - read_index))
        {
            return false;
        }
        if (count > (output_capacity - write_index))
        {
            return false;
        }

        for (size_t i = 0U; i < count; ++i)
        {
            output[write_index++] = input[read_index++];
        }

        if ((code != 0xFFU) && (read_index < input_length))
        {
            if (write_index >= output_capacity)
            {
                return false;
            }
            output[write_index++] = 0U;
        }
    }

    *output_length = write_index;
    return true;
}

bool UartOta_Serialize(const UartOtaPacket_t *packet,
                       uint8_t *raw,
                       size_t raw_capacity,
                       size_t *raw_length)
{
    size_t length;
    uint32_t crc;

    if ((packet == NULL) || (raw == NULL) || (raw_length == NULL) ||
        (packet->payload_length > UART_OTA_MAX_PAYLOAD))
    {
        return false;
    }

    length = UART_OTA_HEADER_SIZE +
             (size_t)packet->payload_length +
             UART_OTA_CRC_SIZE;

    if (raw_capacity < length)
    {
        return false;
    }

    PutU16Le(&raw[0], UART_OTA_MAGIC);
    raw[2] = UART_OTA_PROTOCOL_VERSION;
    raw[3] = packet->command;
    PutU32Le(&raw[4], packet->update_id);
    PutU32Le(&raw[8], packet->offset);
    PutU16Le(&raw[12], packet->sequence);
    PutU16Le(&raw[14], packet->payload_length);

    if (packet->payload_length != 0U)
    {
        memcpy(&raw[UART_OTA_HEADER_SIZE],
               packet->payload,
               packet->payload_length);
    }

    crc = UartOta_Crc32(
        raw,
        UART_OTA_HEADER_SIZE + (size_t)packet->payload_length);
    PutU32Le(&raw[UART_OTA_HEADER_SIZE + packet->payload_length], crc);

    *raw_length = length;
    return true;
}

bool UartOta_Deserialize(const uint8_t *raw,
                         size_t raw_length,
                         UartOtaPacket_t *packet)
{
    uint16_t payload_length;
    size_t expected_length;
    uint32_t stored_crc;
    uint32_t computed_crc;

    if ((raw == NULL) || (packet == NULL) ||
        (raw_length < (UART_OTA_HEADER_SIZE + UART_OTA_CRC_SIZE)))
    {
        return false;
    }

    if (GetU16Le(&raw[0]) != UART_OTA_MAGIC ||
        raw[2] != UART_OTA_PROTOCOL_VERSION)
    {
        return false;
    }

    payload_length = GetU16Le(&raw[14]);
    if (payload_length > UART_OTA_MAX_PAYLOAD)
    {
        return false;
    }

    expected_length = UART_OTA_HEADER_SIZE +
                      (size_t)payload_length +
                      UART_OTA_CRC_SIZE;
    if (raw_length != expected_length)
    {
        return false;
    }

    stored_crc = GetU32Le(&raw[UART_OTA_HEADER_SIZE + payload_length]);
    computed_crc = UartOta_Crc32(
        raw,
        UART_OTA_HEADER_SIZE + (size_t)payload_length);
    if (stored_crc != computed_crc)
    {
        return false;
    }

    memset(packet, 0, sizeof(*packet));
    packet->command = raw[3];
    packet->update_id = GetU32Le(&raw[4]);
    packet->offset = GetU32Le(&raw[8]);
    packet->sequence = GetU16Le(&raw[12]);
    packet->payload_length = payload_length;

    if (payload_length != 0U)
    {
        memcpy(packet->payload,
               &raw[UART_OTA_HEADER_SIZE],
               payload_length);
    }

    return true;
}

bool UartOta_EncodeFrame(const UartOtaPacket_t *packet,
                         uint8_t *frame,
                         size_t frame_capacity,
                         size_t *frame_length)
{
    uint8_t raw[UART_OTA_MAX_RAW_PACKET];
    size_t raw_length = 0U;
    size_t encoded_length = 0U;

    if ((frame == NULL) || (frame_length == NULL) ||
        (frame_capacity < 2U))
    {
        return false;
    }

    if (!UartOta_Serialize(packet, raw, sizeof(raw), &raw_length))
    {
        return false;
    }

    if (!UartOta_CobsEncode(raw,
                            raw_length,
                            frame,
                            frame_capacity - 1U,
                            &encoded_length))
    {
        return false;
    }

    if (encoded_length >= frame_capacity)
    {
        return false;
    }

    frame[encoded_length] = 0U;
    *frame_length = encoded_length + 1U;
    return true;
}

bool UartOta_DecodeFrame(const uint8_t *frame,
                         size_t frame_length,
                         UartOtaPacket_t *packet)
{
    uint8_t raw[UART_OTA_MAX_RAW_PACKET];
    size_t raw_length = 0U;

    if ((frame == NULL) || (packet == NULL) || (frame_length == 0U))
    {
        return false;
    }

    if (frame[frame_length - 1U] == 0U)
    {
        --frame_length;
    }
    if (frame_length == 0U)
    {
        return false;
    }

    if (!UartOta_CobsDecode(frame,
                            frame_length,
                            raw,
                            sizeof(raw),
                            &raw_length))
    {
        return false;
    }

    return UartOta_Deserialize(raw, raw_length, packet);
}

bool UartOta_ParseAck(const UartOtaPacket_t *packet,
                      UartOtaAckInfo_t *info)
{
    if ((packet == NULL) || (info == NULL) ||
        ((packet->command != UART_OTA_CMD_ACK) &&
         (packet->command != UART_OTA_CMD_NACK)) ||
        (packet->payload_length != UART_OTA_ACK_PAYLOAD_SIZE))
    {
        return false;
    }

    info->status = packet->payload[0];
    info->update_state = packet->payload[1];
    info->acknowledged_sequence = GetU16Le(&packet->payload[2]);
    info->next_expected_offset = GetU32Le(&packet->payload[4]);
    info->received_size = GetU32Le(&packet->payload[8]);
    info->expected_size = GetU32Le(&packet->payload[12]);
    info->last_error_detail = GetU32Le(&packet->payload[16]);
    return true;
}

bool UartOta_ParseHello(const UartOtaPacket_t *packet,
                        UartOtaHelloInfo_t *info)
{
    const uint8_t *p;

    if ((packet == NULL) || (info == NULL) ||
        (packet->command != UART_OTA_CMD_ACK) ||
        (packet->payload_length != UART_OTA_HELLO_PAYLOAD_SIZE))
    {
        return false;
    }

    p = packet->payload;
    info->protocol_version = p[0];
    info->bootloader_version = GetU32Le(&p[1]);
    info->application_version = GetU32Le(&p[5]);
    info->product_id = GetU32Le(&p[9]);
    info->hardware_revision = GetU32Le(&p[13]);
    info->capability_flags = GetU32Le(&p[17]);
    info->update_state = p[21];
    info->last_status = p[22];
    info->active_update_id = GetU32Le(&p[25]);
    info->next_expected_offset = GetU32Le(&p[29]);
    info->expected_artifact_size = GetU32Le(&p[33]);
    return true;
}

void UartOta_BuildStartPayload(uint8_t payload[UART_OTA_START_PAYLOAD_SIZE],
                               uint32_t base_version,
                               uint32_t target_version,
                               uint32_t artifact_size,
                               uint32_t artifact_crc32)
{
    memset(payload, 0, UART_OTA_START_PAYLOAD_SIZE);
    payload[0] = 1U; /* FW_IMAGE_FULL */
    payload[1] = 0U;
    PutU16Le(&payload[2], 1U); /* container-format version */
    PutU32Le(&payload[4], base_version);
    PutU32Le(&payload[8], target_version);
    PutU32Le(&payload[12], artifact_size);
    PutU32Le(&payload[16], artifact_crc32);
    PutU32Le(&payload[20], 0UL); /* raw app image, no container header yet */
}
