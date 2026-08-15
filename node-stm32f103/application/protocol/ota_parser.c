#include "ota_parser.h"
#include "cobs.h"
#include "ota_packet.h"

void OtaParser_Reset(OtaParser_t *parser)
{
    if (parser == (OtaParser_t *)0) { return; }
    parser->encoded_length = 0UL;
    parser->overflowed = 0U;
}

void OtaParser_Init(OtaParser_t *parser)
{
    OtaParser_Reset(parser);
}

OtaParserEvent_t OtaParser_PushByte(OtaParser_t *parser,
                                    uint8_t byte,
                                    OtaPacket_t *packet)
{
    uint8_t raw[OTA_PACKET_MAX_RAW_SIZE];
    uint32_t raw_length = 0UL;
    OtaPacketStatus_t status;

    if ((parser == (OtaParser_t *)0) || (packet == (OtaPacket_t *)0))
    {
        return OTA_PARSER_EVENT_INVALID_FRAME;
    }

    if (byte != 0U)
    {
        if (parser->overflowed != 0U) { return OTA_PARSER_EVENT_NONE; }
        if (parser->encoded_length >= OTA_MAX_ENCODED_FRAME)
        {
            parser->overflowed = 1U;
            return OTA_PARSER_EVENT_NONE;
        }
        parser->encoded[parser->encoded_length++] = byte;
        return OTA_PARSER_EVENT_NONE;
    }

    if ((parser->encoded_length == 0UL) && (parser->overflowed == 0U))
    {
        return OTA_PARSER_EVENT_NONE;
    }

    if (parser->overflowed != 0U)
    {
        OtaParser_Reset(parser);
        return OTA_PARSER_EVENT_INVALID_FRAME;
    }

    if (!Cobs_Decode(parser->encoded, parser->encoded_length,
                     raw, sizeof(raw), &raw_length))
    {
        OtaParser_Reset(parser);
        return OTA_PARSER_EVENT_INVALID_FRAME;
    }

    status = OtaPacket_Deserialize(raw, raw_length, packet);
    OtaParser_Reset(parser);

    if (status == OTA_PACKET_STATUS_OK) { return OTA_PARSER_EVENT_PACKET; }
    if (status == OTA_PACKET_STATUS_BAD_CRC)
    {
        return OTA_PARSER_EVENT_PACKET_CRC_ERROR;
    }
    return OTA_PARSER_EVENT_INVALID_FRAME;
}
