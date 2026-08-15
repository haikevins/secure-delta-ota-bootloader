#ifndef OTA_PARSER_H
#define OTA_PARSER_H

#include <stdint.h>
#include "ota_protocol.h"

typedef enum
{
    OTA_PARSER_EVENT_NONE = 0,
    OTA_PARSER_EVENT_PACKET,
    OTA_PARSER_EVENT_INVALID_FRAME,
    OTA_PARSER_EVENT_PACKET_CRC_ERROR
} OtaParserEvent_t;

typedef struct
{
    uint8_t encoded[OTA_MAX_ENCODED_FRAME];
    uint32_t encoded_length;
    uint8_t overflowed;
} OtaParser_t;

void OtaParser_Init(OtaParser_t *parser);
void OtaParser_Reset(OtaParser_t *parser);
OtaParserEvent_t OtaParser_PushByte(OtaParser_t *parser,
                                    uint8_t byte,
                                    OtaPacket_t *packet);

#endif
