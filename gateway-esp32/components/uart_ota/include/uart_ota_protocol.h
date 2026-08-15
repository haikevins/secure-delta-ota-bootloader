#ifndef UART_OTA_PROTOCOL_H
#define UART_OTA_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UART_OTA_MAGIC                  0xA55AU
#define UART_OTA_PROTOCOL_VERSION       1U
#define UART_OTA_MAX_PAYLOAD            256U
#define UART_OTA_HEADER_SIZE            16U
#define UART_OTA_CRC_SIZE               4U
#define UART_OTA_MAX_RAW_PACKET         276U
#define UART_OTA_MAX_ENCODED_FRAME      320U

#define UART_OTA_HELLO_PAYLOAD_SIZE     37U
#define UART_OTA_ACK_PAYLOAD_SIZE       20U
#define UART_OTA_START_PAYLOAD_SIZE     24U

#define UART_OTA_CAP_FULL_IMAGE         (1UL << 0)
#define UART_OTA_CAP_RESUME             (1UL << 2)
#define UART_OTA_CAP_ROLLBACK           (1UL << 4)

typedef enum
{
    UART_OTA_CMD_HELLO   = 0x01,
    UART_OTA_CMD_QUERY   = 0x02,
    UART_OTA_CMD_START   = 0x10,
    UART_OTA_CMD_DATA    = 0x11,
    UART_OTA_CMD_FINISH  = 0x12,
    UART_OTA_CMD_ABORT   = 0x13,
    UART_OTA_CMD_RESUME  = 0x14,
    UART_OTA_CMD_INSTALL = 0x20,
    UART_OTA_CMD_STATUS  = 0x21,
    UART_OTA_CMD_CONFIRM = 0x22,
    UART_OTA_CMD_ACK     = 0x70,
    UART_OTA_CMD_NACK    = 0x71
} UartOtaCommand_t;

typedef enum
{
    UART_OTA_STATUS_OK                 = 0x00,
    UART_OTA_STATUS_INVALID_PACKET     = 0x01,
    UART_OTA_STATUS_INVALID_STATE      = 0x02,
    UART_OTA_STATUS_WRONG_SEQUENCE     = 0x03,
    UART_OTA_STATUS_WRONG_OFFSET       = 0x04,
    UART_OTA_STATUS_PACKET_CRC_ERROR   = 0x05,
    UART_OTA_STATUS_STORAGE_ERROR      = 0x06,
    UART_OTA_STATUS_IMAGE_TOO_LARGE    = 0x07,
    UART_OTA_STATUS_UPDATE_ID_MISMATCH = 0x08,
    UART_OTA_STATUS_BASE_MISMATCH      = 0x09,
    UART_OTA_STATUS_CONTAINER_ERROR    = 0x0A,
    UART_OTA_STATUS_SIGNATURE_ERROR    = 0x0B,
    UART_OTA_STATUS_VERSION_REJECTED   = 0x0C,
    UART_OTA_STATUS_BUSY               = 0x0D,
    UART_OTA_STATUS_RETRY_LATER        = 0x0E,
    UART_OTA_STATUS_INTERNAL_ERROR     = 0x0F
} UartOtaStatus_t;

typedef enum
{
    UART_OTA_UPDATE_IDLE = 0,
    UART_OTA_UPDATE_RECEIVING = 1,
    UART_OTA_UPDATE_ARTIFACT_READY = 2,
    UART_OTA_UPDATE_VERIFYING_CONTAINER = 3,
    UART_OTA_UPDATE_VERIFYING_BASE = 4,
    UART_OTA_UPDATE_PATCHING = 5,
    UART_OTA_UPDATE_IMAGE_READY = 6,
    UART_OTA_UPDATE_BACKING_UP = 7,
    UART_OTA_UPDATE_INSTALLING = 8,
    UART_OTA_UPDATE_VERIFYING_INSTALL = 9,
    UART_OTA_UPDATE_TRIAL_BOOT = 10,
    UART_OTA_UPDATE_CONFIRMED = 11,
    UART_OTA_UPDATE_ROLLBACK = 12,
    UART_OTA_UPDATE_FAILED = 13
} UartOtaUpdateState_t;

typedef struct
{
    uint8_t command;
    uint32_t update_id;
    uint32_t offset;
    uint16_t sequence;
    uint16_t payload_length;
    uint8_t payload[UART_OTA_MAX_PAYLOAD];
} UartOtaPacket_t;

typedef struct
{
    uint8_t status;
    uint8_t update_state;
    uint16_t acknowledged_sequence;
    uint32_t next_expected_offset;
    uint32_t received_size;
    uint32_t expected_size;
    uint32_t last_error_detail;
} UartOtaAckInfo_t;

typedef struct
{
    uint8_t protocol_version;
    uint32_t bootloader_version;
    uint32_t application_version;
    uint32_t product_id;
    uint32_t hardware_revision;
    uint32_t capability_flags;
    uint8_t update_state;
    uint8_t last_status;
    uint32_t active_update_id;
    uint32_t next_expected_offset;
    uint32_t expected_artifact_size;
} UartOtaHelloInfo_t;

uint32_t UartOta_Crc32(const void *data, size_t length);

bool UartOta_CobsEncode(const uint8_t *input,
                        size_t input_length,
                        uint8_t *output,
                        size_t output_capacity,
                        size_t *output_length);

bool UartOta_CobsDecode(const uint8_t *input,
                        size_t input_length,
                        uint8_t *output,
                        size_t output_capacity,
                        size_t *output_length);

bool UartOta_Serialize(const UartOtaPacket_t *packet,
                       uint8_t *raw,
                       size_t raw_capacity,
                       size_t *raw_length);

bool UartOta_Deserialize(const uint8_t *raw,
                         size_t raw_length,
                         UartOtaPacket_t *packet);

bool UartOta_EncodeFrame(const UartOtaPacket_t *packet,
                         uint8_t *frame,
                         size_t frame_capacity,
                         size_t *frame_length);

bool UartOta_DecodeFrame(const uint8_t *frame,
                         size_t frame_length,
                         UartOtaPacket_t *packet);

bool UartOta_ParseAck(const UartOtaPacket_t *packet,
                      UartOtaAckInfo_t *info);

bool UartOta_ParseHello(const UartOtaPacket_t *packet,
                        UartOtaHelloInfo_t *info);

void UartOta_BuildStartPayload(uint8_t payload[UART_OTA_START_PAYLOAD_SIZE],
                               uint32_t base_version,
                               uint32_t target_version,
                               uint32_t artifact_size,
                               uint32_t artifact_crc32);

#endif
