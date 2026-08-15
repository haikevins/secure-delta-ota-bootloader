#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "uart_ota_plan.h"
#include "uart_ota_protocol.h"

static void TestCrc(void)
{
    static const char text[] = "123456789";
    assert(UartOta_Crc32(text, 9U) == 0xCBF43926UL);
}

static void TestCodecRoundTrip(void)
{
    UartOtaPacket_t src;
    UartOtaPacket_t dst;
    uint8_t frame[UART_OTA_MAX_ENCODED_FRAME + 1U];
    size_t frame_length = 0U;

    memset(&src, 0, sizeof(src));
    src.command = UART_OTA_CMD_DATA;
    src.update_id = 0x90090001UL;
    src.offset = 4096UL;
    src.sequence = 16U;
    src.payload_length = 256U;

    for (uint32_t i = 0UL; i < src.payload_length; ++i)
    {
        src.payload[i] = (uint8_t)((i * 37UL) & 0xFFUL);
    }
    src.payload[0] = 0U;
    src.payload[31] = 0U;
    src.payload[255] = 0U;

    assert(UartOta_EncodeFrame(&src,
                               frame,
                               sizeof(frame),
                               &frame_length));
    assert(frame[frame_length - 1U] == 0U);

    for (size_t i = 0U; i + 1U < frame_length; ++i)
    {
        assert(frame[i] != 0U);
    }

    assert(UartOta_DecodeFrame(frame, frame_length, &dst));
    assert(dst.command == src.command);
    assert(dst.update_id == src.update_id);
    assert(dst.offset == src.offset);
    assert(dst.sequence == src.sequence);
    assert(dst.payload_length == src.payload_length);
    assert(memcmp(dst.payload, src.payload, src.payload_length) == 0);
}

static void TestStartPayload(void)
{
    UartOtaPacket_t packet;
    uint8_t raw[UART_OTA_MAX_RAW_PACKET];
    size_t raw_length = 0U;

    memset(&packet, 0, sizeof(packet));
    packet.command = UART_OTA_CMD_START;
    packet.update_id = 0x90090001UL;
    packet.payload_length = UART_OTA_START_PAYLOAD_SIZE;

    UartOta_BuildStartPayload(packet.payload,
                              1UL,
                              2UL,
                              10184UL,
                              0x12345678UL);

    assert(packet.payload[0] == 1U);
    assert(packet.payload[2] == 1U);
    assert(packet.payload[3] == 0U);
    assert(UartOta_Serialize(&packet, raw, sizeof(raw), &raw_length));
    assert(raw_length == UART_OTA_HEADER_SIZE +
                         UART_OTA_START_PAYLOAD_SIZE +
                         UART_OTA_CRC_SIZE);
}

static UartOtaHelloInfo_t Target(uint32_t app_version,
                                 uint8_t state,
                                 uint32_t update_id,
                                 uint32_t offset,
                                 uint32_t size)
{
    UartOtaHelloInfo_t info;
    memset(&info, 0, sizeof(info));
    info.protocol_version = UART_OTA_PROTOCOL_VERSION;
    info.application_version = app_version;
    info.update_state = state;
    info.active_update_id = update_id;
    info.next_expected_offset = offset;
    info.expected_artifact_size = size;
    return info;
}

static void TestPlans(void)
{
    const uint32_t update_id = 0x90090001UL;
    const uint32_t size = 10184UL;
    const uint32_t version = 2UL;
    UartOtaHelloInfo_t info;

    info = Target(1UL, UART_OTA_UPDATE_IDLE, 0UL, 0UL, 0UL);
    assert(UartOta_SelectPlan(&info, update_id, size, version) ==
           UART_OTA_PLAN_START_NEW);

    info = Target(1UL, UART_OTA_UPDATE_RECEIVING,
                  update_id, 4096UL, size);
    assert(UartOta_SelectPlan(&info, update_id, size, version) ==
           UART_OTA_PLAN_RESUME);

    info.next_expected_offset = 4352UL;
    assert(UartOta_SelectPlan(&info, update_id, size, version) ==
           UART_OTA_PLAN_RESUME);

    /* Persistent STM32 checkpoints are 4 KiB and therefore 256-aligned. */
    info.next_expected_offset = 4100UL;
    assert(UartOta_SelectPlan(&info, update_id, size, version) ==
           UART_OTA_PLAN_ABORT_FOREIGN);

    info = Target(1UL, UART_OTA_UPDATE_RECEIVING,
                  0xDEADBEEFUL, 4096UL, size);
    assert(UartOta_SelectPlan(&info, update_id, size, version) ==
           UART_OTA_PLAN_ABORT_FOREIGN);

    info = Target(1UL, UART_OTA_UPDATE_ARTIFACT_READY,
                  update_id, size, size);
    assert(UartOta_SelectPlan(&info, update_id, size, version) ==
           UART_OTA_PLAN_INSTALL_READY);

    info = Target(2UL, UART_OTA_UPDATE_TRIAL_BOOT,
                  update_id, size, size);
    assert(UartOta_SelectPlan(&info, update_id, size, version) ==
           UART_OTA_PLAN_WAIT_TARGET);

    info = Target(2UL, UART_OTA_UPDATE_IDLE, 0UL, 0UL, 0UL);
    assert(UartOta_SelectPlan(&info, update_id, size, version) ==
           UART_OTA_PLAN_ALREADY_TARGET);
}

int main(void)
{
    TestCrc();
    TestCodecRoundTrip();
    TestStartPayload();
    TestPlans();
    puts("Phase 9 ESP32 gateway protocol host tests: PASS");
    return 0;
}
