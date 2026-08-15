#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "cobs.h"
#include "ota_packet.h"
#include "ota_parser.h"
#include "ota_protocol.h"
#include "ota_response.h"
#include "ota_status.h"

static void TestCobs(void)
{
    const uint8_t input[] = {0U, 1U, 2U, 0U, 0xFEU, 0xFFU};
    uint8_t encoded[32], decoded[32];
    uint32_t enc = 0UL, dec = 0UL, i;
    assert(Cobs_Encode(input, sizeof(input), encoded, sizeof(encoded), &enc));
    for (i = 0UL; i < enc; ++i) { assert(encoded[i] != 0U); }
    assert(Cobs_Decode(encoded, enc, decoded, sizeof(decoded), &dec));
    assert(dec == sizeof(input));
    for (i = 0UL; i < dec; ++i) { assert(decoded[i] == input[i]); }
}

static void TestPacketAndParser(void)
{
    OtaPacket_t src = {0}, dst = {0};
    OtaParser_t parser;
    uint8_t raw[OTA_PACKET_MAX_RAW_SIZE];
    uint8_t enc[OTA_MAX_ENCODED_FRAME];
    uint32_t raw_len = 0UL, enc_len = 0UL, i;

    src.magic = OTA_PACKET_MAGIC;
    src.protocol_version = OTA_PROTOCOL_VERSION;
    src.command = (uint8_t)OTA_CMD_DATA;
    src.update_id = 0x12345678UL;
    src.offset = 0x100UL;
    src.sequence = 7U;
    src.payload_length = 6U;
    src.payload[0] = 0U; src.payload[1] = 1U; src.payload[2] = 2U;
    src.payload[3] = 0U; src.payload[4] = 0xFEU; src.payload[5] = 0xFFU;

    assert(OtaPacket_Serialize(&src, raw, sizeof(raw), &raw_len));
    assert(OtaPacket_Deserialize(raw, raw_len, &dst) == OTA_PACKET_STATUS_OK);
    assert(dst.update_id == src.update_id && dst.sequence == 7U);
    raw[16] ^= 1U;
    assert(OtaPacket_Deserialize(raw, raw_len, &dst) == OTA_PACKET_STATUS_BAD_CRC);
    raw[16] ^= 1U;

    assert(Cobs_Encode(raw, raw_len, enc, sizeof(enc), &enc_len));
    OtaParser_Init(&parser);
    for (i = 0UL; i < enc_len; ++i)
    {
        assert(OtaParser_PushByte(&parser, enc[i], &dst) == OTA_PARSER_EVENT_NONE);
    }
    assert(OtaParser_PushByte(&parser, 0U, &dst) == OTA_PARSER_EVENT_PACKET);
}

static void TestAck(void)
{
    OtaPacket_t req = {0}, resp = {0};
    OtaResponseInfo_t info = {0};
    req.update_id = 1UL;
    req.sequence = 0x1234U;
    info.update_state = 1U;
    info.next_expected_offset = 256UL;
    info.received_size = 256UL;
    info.expected_size = 1024UL;
    OtaResponse_BuildAck(&req, &info, &resp);
    assert(resp.command == OTA_CMD_ACK);
    assert(resp.payload_length == OTA_ACK_PAYLOAD_SIZE);
    assert(resp.payload[0] == OTA_STATUS_OK);
    assert(resp.payload[2] == 0x34U && resp.payload[3] == 0x12U);
}

int main(void)
{
    TestCobs();
    TestPacketAndParser();
    TestAck();
    puts("Phase 5 C protocol tests: PASS");
    return 0;
}
