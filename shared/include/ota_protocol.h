#ifndef OTA_PROTOCOL_H
#define OTA_PROTOCOL_H

#include "project_types.h"

#define OTA_PACKET_MAGIC          0xA55AU
#define OTA_PROTOCOL_VERSION      1U
#define OTA_MAX_PAYLOAD_SIZE      256U
#define OTA_MAX_RETRY_COUNT       5U
#define OTA_RESPONSE_TIMEOUT_MS   1500U

#define OTA_PACKET_HEADER_SIZE    16U
#define OTA_PACKET_CRC_SIZE       4U
#define OTA_PACKET_MIN_RAW_SIZE   (OTA_PACKET_HEADER_SIZE + OTA_PACKET_CRC_SIZE)
#define OTA_PACKET_MAX_RAW_SIZE   (OTA_PACKET_HEADER_SIZE + OTA_MAX_PAYLOAD_SIZE + OTA_PACKET_CRC_SIZE)
#define OTA_MAX_ENCODED_FRAME     320U

#define OTA_HELLO_RESPONSE_PAYLOAD_SIZE 37U
#define OTA_ACK_PAYLOAD_SIZE            20U
#define OTA_START_PAYLOAD_SIZE          24U

#define OTA_CAP_FULL_IMAGE        (1UL << 0)
#define OTA_CAP_DELTA_IMAGE       (1UL << 1)
#define OTA_CAP_RESUME            (1UL << 2)
#define OTA_CAP_SIGNATURE_VERIFY  (1UL << 3)
#define OTA_CAP_ROLLBACK          (1UL << 4)

typedef enum
{
    OTA_CMD_HELLO   = 0x01,
    OTA_CMD_QUERY   = 0x02,
    OTA_CMD_START   = 0x10,
    OTA_CMD_DATA    = 0x11,
    OTA_CMD_FINISH  = 0x12,
    OTA_CMD_ABORT   = 0x13,
    OTA_CMD_RESUME  = 0x14,
    OTA_CMD_INSTALL = 0x20,
    OTA_CMD_STATUS  = 0x21,
    OTA_CMD_CONFIRM = 0x22,
    OTA_CMD_ACK     = 0x70,
    OTA_CMD_NACK    = 0x71
} OtaCommand_t;

typedef struct
{
    uint16_t magic;
    uint8_t protocol_version;
    uint8_t command;
    uint32_t update_id;
    uint32_t offset;
    uint16_t sequence;
    uint16_t payload_length;
    uint8_t payload[OTA_MAX_PAYLOAD_SIZE];
    uint32_t packet_crc32;
} OtaPacket_t;

#endif
