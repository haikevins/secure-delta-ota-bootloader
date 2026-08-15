#include "ota_agent.h"
#include <stdint.h>
#include "cobs.h"
#include "ota_packet.h"
#include "ota_parser.h"
#include "ota_receiver.h"
#include "ota_response.h"
#include "ota_status.h"
#include "uart.h"

static OtaParser_t g_parser;
static uint32_t g_last_rx_overflow_count;

static bool SendPacket(const OtaPacket_t *packet)
{
    uint8_t raw[OTA_PACKET_MAX_RAW_SIZE];
    uint8_t encoded[OTA_MAX_ENCODED_FRAME + 1U];
    uint32_t raw_length = 0UL;
    uint32_t encoded_length = 0UL;

    if (!OtaPacket_Serialize(packet, raw, sizeof(raw), &raw_length))
    {
        return false;
    }
    if (!Cobs_Encode(raw, raw_length, encoded,
                     OTA_MAX_ENCODED_FRAME, &encoded_length))
    {
        return false;
    }
    encoded[encoded_length] = 0U;
    return Uart_Write(encoded, encoded_length + 1UL);
}

bool OtaAgent_Init(void)
{
    OtaParser_Init(&g_parser);
    g_last_rx_overflow_count = 0UL;

    if (!Uart_Init()) { return false; }

    /* HELLO/QUERY remain available if external Flash init fails. */
    (void)OtaReceiver_Init();
    return true;
}

void OtaAgent_Process(void)
{
    uint8_t byte;
    const uint32_t overflow_count = Uart_GetRxOverflowCount();

    if (overflow_count != g_last_rx_overflow_count)
    {
        OtaParser_Reset(&g_parser);
        g_last_rx_overflow_count = overflow_count;
    }

    while (Uart_ReadByte(&byte))
    {
        OtaPacket_t request;
        OtaPacket_t response;
        const OtaParserEvent_t event =
            OtaParser_PushByte(&g_parser, byte, &request);

        if (event == OTA_PARSER_EVENT_PACKET)
        {
            OtaReceiver_ProcessPacket(&request, &response);
            (void)SendPacket(&response);
        }
        else if (event == OTA_PARSER_EVENT_PACKET_CRC_ERROR)
        {
            OtaResponseInfo_t info;
            OtaReceiver_GetResponseInfo(&info);
            OtaResponse_BuildNack(&request,
                                  OTA_STATUS_PACKET_CRC_ERROR,
                                  &info,
                                  &response);
            (void)SendPacket(&response);
        }
        else
        {
            /* Invalid framing/length/magic is discarded at the delimiter. */
        }
    }
}
