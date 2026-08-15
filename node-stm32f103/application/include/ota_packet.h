#ifndef OTA_PACKET_H
#define OTA_PACKET_H

#include <stdbool.h>
#include <stdint.h>
#include "ota_protocol.h"

typedef enum
{
    OTA_PACKET_STATUS_OK = 0,
    OTA_PACKET_STATUS_INVALID_ARGUMENT,
    OTA_PACKET_STATUS_TOO_SHORT,
    OTA_PACKET_STATUS_BAD_MAGIC,
    OTA_PACKET_STATUS_BAD_VERSION,
    OTA_PACKET_STATUS_BAD_LENGTH,
    OTA_PACKET_STATUS_BAD_CRC
} OtaPacketStatus_t;

bool OtaPacket_Serialize(const OtaPacket_t *packet,
                         uint8_t *raw, uint32_t raw_capacity,
                         uint32_t *raw_length);
OtaPacketStatus_t OtaPacket_Deserialize(const uint8_t *raw,
                                        uint32_t raw_length,
                                        OtaPacket_t *packet);

#endif
