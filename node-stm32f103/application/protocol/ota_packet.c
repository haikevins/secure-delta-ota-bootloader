#include "ota_packet.h"
#include "crc32.h"

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
    return (uint16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8U));
}

static uint32_t GetU32Le(const uint8_t *src)
{
    return (uint32_t)src[0] |
           ((uint32_t)src[1] << 8U) |
           ((uint32_t)src[2] << 16U) |
           ((uint32_t)src[3] << 24U);
}

bool OtaPacket_Serialize(const OtaPacket_t *packet,
                         uint8_t *raw, uint32_t raw_capacity,
                         uint32_t *raw_length)
{
    uint32_t i;
    uint32_t length;
    uint32_t crc;

    if ((packet == (const OtaPacket_t *)0) || (raw == (uint8_t *)0) ||
        (raw_length == (uint32_t *)0) ||
        (packet->payload_length > OTA_MAX_PAYLOAD_SIZE))
    {
        return false;
    }

    length = OTA_PACKET_HEADER_SIZE +
             (uint32_t)packet->payload_length +
             OTA_PACKET_CRC_SIZE;
    if (raw_capacity < length) { return false; }

    PutU16Le(&raw[0], packet->magic);
    raw[2] = packet->protocol_version;
    raw[3] = packet->command;
    PutU32Le(&raw[4], packet->update_id);
    PutU32Le(&raw[8], packet->offset);
    PutU16Le(&raw[12], packet->sequence);
    PutU16Le(&raw[14], packet->payload_length);

    for (i = 0UL; i < (uint32_t)packet->payload_length; ++i)
    {
        raw[OTA_PACKET_HEADER_SIZE + i] = packet->payload[i];
    }

    crc = Crc32_Calculate(raw,
                          OTA_PACKET_HEADER_SIZE +
                          (uint32_t)packet->payload_length);
    PutU32Le(&raw[OTA_PACKET_HEADER_SIZE + packet->payload_length], crc);
    *raw_length = length;
    return true;
}

OtaPacketStatus_t OtaPacket_Deserialize(const uint8_t *raw,
                                        uint32_t raw_length,
                                        OtaPacket_t *packet)
{
    uint32_t expected_length;
    uint32_t i;
    uint32_t stored_crc;
    uint32_t computed_crc;

    if ((raw == (const uint8_t *)0) || (packet == (OtaPacket_t *)0))
    {
        return OTA_PACKET_STATUS_INVALID_ARGUMENT;
    }
    if (raw_length < OTA_PACKET_MIN_RAW_SIZE)
    {
        return OTA_PACKET_STATUS_TOO_SHORT;
    }

    packet->magic = GetU16Le(&raw[0]);
    packet->protocol_version = raw[2];
    packet->command = raw[3];
    packet->update_id = GetU32Le(&raw[4]);
    packet->offset = GetU32Le(&raw[8]);
    packet->sequence = GetU16Le(&raw[12]);
    packet->payload_length = GetU16Le(&raw[14]);

    if (packet->magic != OTA_PACKET_MAGIC) { return OTA_PACKET_STATUS_BAD_MAGIC; }
    if (packet->protocol_version != OTA_PROTOCOL_VERSION)
    {
        return OTA_PACKET_STATUS_BAD_VERSION;
    }
    if (packet->payload_length > OTA_MAX_PAYLOAD_SIZE)
    {
        return OTA_PACKET_STATUS_BAD_LENGTH;
    }

    expected_length = OTA_PACKET_HEADER_SIZE +
                      (uint32_t)packet->payload_length +
                      OTA_PACKET_CRC_SIZE;
    if (raw_length != expected_length) { return OTA_PACKET_STATUS_BAD_LENGTH; }

    for (i = 0UL; i < (uint32_t)packet->payload_length; ++i)
    {
        packet->payload[i] = raw[OTA_PACKET_HEADER_SIZE + i];
    }

    stored_crc = GetU32Le(&raw[OTA_PACKET_HEADER_SIZE + packet->payload_length]);
    packet->packet_crc32 = stored_crc;
    computed_crc = Crc32_Calculate(raw,
                                   OTA_PACKET_HEADER_SIZE +
                                   (uint32_t)packet->payload_length);
    if (stored_crc != computed_crc) { return OTA_PACKET_STATUS_BAD_CRC; }

    return OTA_PACKET_STATUS_OK;
}
