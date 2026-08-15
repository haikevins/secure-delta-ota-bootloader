#include "uart_ota.h"

#include <inttypes.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "uart_ota_plan.h"

#define UART_OTA_RX_BUFFER_SIZE      1024
#define UART_OTA_CONNECT_TIMEOUT_MS  12000UL
#define UART_OTA_FINAL_TIMEOUT_MS    55000UL
#define UART_OTA_POLL_DELAY_MS       250UL

static const char *TAG = "uart_ota";

static int64_t DeadlineUs(uint32_t timeout_ms)
{
    return esp_timer_get_time() + ((int64_t)timeout_ms * 1000LL);
}

static bool DeadlineExpired(int64_t deadline_us)
{
    return esp_timer_get_time() >= deadline_us;
}

static esp_err_t SendFrame(const UartOtaClient_t *client,
                           const UartOtaPacket_t *packet)
{
    uint8_t frame[UART_OTA_MAX_ENCODED_FRAME + 1U];
    size_t frame_length = 0U;
    int written;

    if (!UartOta_EncodeFrame(packet,
                             frame,
                             sizeof(frame),
                             &frame_length))
    {
        return ESP_ERR_INVALID_SIZE;
    }

    written = uart_write_bytes(client->config.uart_num,
                               frame,
                               frame_length);
    if (written != (int)frame_length)
    {
        return ESP_FAIL;
    }

    return uart_wait_tx_done(
        client->config.uart_num,
        pdMS_TO_TICKS(client->config.response_timeout_ms));
}

static esp_err_t ReadFrame(const UartOtaClient_t *client,
                           UartOtaPacket_t *packet,
                           uint32_t timeout_ms)
{
    uint8_t encoded[UART_OTA_MAX_ENCODED_FRAME];
    size_t encoded_length = 0U;
    bool overflow = false;
    const int64_t deadline = DeadlineUs(timeout_ms);

    while (!DeadlineExpired(deadline))
    {
        uint8_t byte = 0U;
        const int read = uart_read_bytes(client->config.uart_num,
                                         &byte,
                                         1U,
                                         pdMS_TO_TICKS(20));

        if (read < 0)
        {
            return ESP_FAIL;
        }
        if (read == 0)
        {
            continue;
        }

        if (byte != 0U)
        {
            if (!overflow)
            {
                if (encoded_length < sizeof(encoded))
                {
                    encoded[encoded_length++] = byte;
                }
                else
                {
                    overflow = true;
                }
            }
            continue;
        }

        if (overflow)
        {
            encoded_length = 0U;
            overflow = false;
            continue;
        }

        if (encoded_length == 0U)
        {
            continue;
        }

        if (UartOta_DecodeFrame(encoded, encoded_length, packet))
        {
            return ESP_OK;
        }

        encoded_length = 0U;
    }

    return ESP_ERR_TIMEOUT;
}

static esp_err_t Request(UartOtaClient_t *client,
                         const UartOtaPacket_t *request,
                         UartOtaPacket_t *response)
{
    esp_err_t last_error = ESP_ERR_TIMEOUT;

    for (uint32_t attempt = 0UL;
         attempt < client->config.retry_count;
         ++attempt)
    {
        esp_err_t status = SendFrame(client, request);

        if (status != ESP_OK)
        {
            last_error = status;
            continue;
        }

        const int64_t deadline =
            DeadlineUs(client->config.response_timeout_ms);

        while (!DeadlineExpired(deadline))
        {
            UartOtaPacket_t candidate;
            status = ReadFrame(client, &candidate, 100UL);
            if (status == ESP_ERR_TIMEOUT)
            {
                continue;
            }
            if (status != ESP_OK)
            {
                last_error = status;
                break;
            }

            if ((candidate.command != UART_OTA_CMD_ACK) &&
                (candidate.command != UART_OTA_CMD_NACK))
            {
                continue;
            }

            if (candidate.sequence != request->sequence)
            {
                continue;
            }

            /*
             * HELLO/QUERY are sent with update_id 0. All transfer-control
             * requests use the artifact update_id and must match.
             */
            if ((request->update_id != 0UL) &&
                (candidate.update_id != request->update_id))
            {
                continue;
            }

            *response = candidate;
            return ESP_OK;
        }

        ESP_LOGW(TAG,
                 "retry command=0x%02X attempt=%" PRIu32 "/%" PRIu32,
                 request->command,
                 attempt + 1UL,
                 client->config.retry_count);
    }

    return last_error;
}

static esp_err_t RequireAck(const UartOtaPacket_t *response,
                            UartOtaAckInfo_t *ack)
{
    if (!UartOta_ParseAck(response, ack))
    {
        return ESP_ERR_INVALID_RESPONSE;
    }

    if ((response->command != UART_OTA_CMD_ACK) ||
        (ack->status != UART_OTA_STATUS_OK))
    {
        ESP_LOGE(TAG,
                 "NACK status=0x%02X state=%u next=%" PRIu32
                 " detail=0x%08" PRIX32,
                 ack->status,
                 ack->update_state,
                 ack->next_expected_offset,
                 ack->last_error_detail);
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}

static void InitPacket(UartOtaPacket_t *packet,
                       uint8_t command,
                       uint32_t update_id,
                       uint32_t offset,
                       uint16_t sequence)
{
    memset(packet, 0, sizeof(*packet));
    packet->command = command;
    packet->update_id = update_id;
    packet->offset = offset;
    packet->sequence = sequence;
}

static esp_err_t QueryOnce(UartOtaClient_t *client,
                           UartOtaHelloInfo_t *info)
{
    UartOtaPacket_t request;
    UartOtaPacket_t response;
    esp_err_t status;

    InitPacket(&request, UART_OTA_CMD_QUERY, 0UL, 0UL, 0U);

    status = Request(client, &request, &response);
    if (status != ESP_OK)
    {
        return status;
    }

    if (!UartOta_ParseHello(&response, info))
    {
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (info->protocol_version != UART_OTA_PROTOCOL_VERSION)
    {
        return ESP_ERR_INVALID_VERSION;
    }

    return ESP_OK;
}

esp_err_t UartOta_Init(UartOtaClient_t *client,
                       const UartOtaConfig_t *config)
{
    uart_config_t uart_config;
    esp_err_t status;

    if ((client == NULL) || (config == NULL) ||
        (config->baud_rate == 0UL) ||
        (config->response_timeout_ms == 0UL) ||
        (config->retry_count == 0UL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    memset(client, 0, sizeof(*client));
    client->config = *config;

    memset(&uart_config, 0, sizeof(uart_config));
    uart_config.baud_rate = (int)config->baud_rate;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_config.source_clk = UART_SCLK_DEFAULT;

    status = uart_param_config(config->uart_num, &uart_config);
    if (status != ESP_OK)
    {
        return status;
    }

    status = uart_set_pin(config->uart_num,
                          config->tx_gpio,
                          config->rx_gpio,
                          UART_PIN_NO_CHANGE,
                          UART_PIN_NO_CHANGE);
    if (status != ESP_OK)
    {
        return status;
    }

    if (!uart_is_driver_installed(config->uart_num))
    {
        status = uart_driver_install(config->uart_num,
                                     UART_OTA_RX_BUFFER_SIZE,
                                     0,
                                     0,
                                     NULL,
                                     0);
        if (status != ESP_OK)
        {
            return status;
        }
    }

    (void)uart_flush_input(config->uart_num);
    return ESP_OK;
}

void UartOta_SetProgressCallback(UartOtaClient_t *client,
                                 UartOtaProgressFn callback,
                                 void *context)
{
    if (client == NULL)
    {
        return;
    }

    client->progress = callback;
    client->progress_context = context;
}

esp_err_t UartOta_Query(UartOtaClient_t *client,
                        UartOtaHelloInfo_t *info)
{
    const int64_t deadline = DeadlineUs(UART_OTA_CONNECT_TIMEOUT_MS);
    esp_err_t last_error = ESP_ERR_TIMEOUT;

    if ((client == NULL) || (info == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    while (!DeadlineExpired(deadline))
    {
        last_error = QueryOnce(client, info);
        if (last_error == ESP_OK)
        {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    return last_error;
}

static esp_err_t AbortTransfer(UartOtaClient_t *client,
                               uint32_t update_id)
{
    UartOtaPacket_t request;
    UartOtaPacket_t response;
    UartOtaAckInfo_t ack;
    esp_err_t status;

    InitPacket(&request,
               UART_OTA_CMD_ABORT,
               update_id,
               0UL,
               0U);

    status = Request(client, &request, &response);
    if (status != ESP_OK)
    {
        return status;
    }

    return RequireAck(&response, &ack);
}

static esp_err_t StartTransfer(UartOtaClient_t *client,
                               const UartOtaArtifact_t *artifact,
                               uint32_t base_version)
{
    UartOtaPacket_t request;
    UartOtaPacket_t response;
    UartOtaAckInfo_t ack;
    esp_err_t status;

    InitPacket(&request,
               UART_OTA_CMD_START,
               artifact->update_id,
               0UL,
               0U);
    request.payload_length = UART_OTA_START_PAYLOAD_SIZE;

    UartOta_BuildStartPayload(request.payload,
                              base_version,
                              artifact->target_version,
                              artifact->image_size,
                              artifact->image_crc32);

    status = Request(client, &request, &response);
    if (status != ESP_OK)
    {
        return status;
    }

    return RequireAck(&response, &ack);
}

static esp_err_t ResumeTransfer(UartOtaClient_t *client,
                                const UartOtaArtifact_t *artifact,
                                uint32_t *offset_out)
{
    UartOtaPacket_t request;
    UartOtaPacket_t response;
    UartOtaAckInfo_t ack;
    esp_err_t status;

    InitPacket(&request,
               UART_OTA_CMD_RESUME,
               artifact->update_id,
               0UL,
               0U);

    status = Request(client, &request, &response);
    if (status != ESP_OK)
    {
        return status;
    }

    status = RequireAck(&response, &ack);
    if (status != ESP_OK)
    {
        return status;
    }

    if ((ack.next_expected_offset > artifact->image_size) ||
        ((ack.next_expected_offset % UART_OTA_MAX_PAYLOAD) != 0UL))
    {
        return ESP_ERR_INVALID_SIZE;
    }

    *offset_out = ack.next_expected_offset;
    return ESP_OK;
}

static esp_err_t SendData(UartOtaClient_t *client,
                          const UartOtaArtifact_t *artifact,
                          uint32_t start_offset)
{
    uint32_t offset = start_offset;
    uint16_t sequence =
        (uint16_t)((start_offset / UART_OTA_MAX_PAYLOAD) & 0xFFFFUL);

    while (offset < artifact->image_size)
    {
        UartOtaPacket_t request;
        UartOtaPacket_t response;
        UartOtaAckInfo_t ack;
        uint32_t chunk = artifact->image_size - offset;
        esp_err_t status;

        if (chunk > UART_OTA_MAX_PAYLOAD)
        {
            chunk = UART_OTA_MAX_PAYLOAD;
        }

        InitPacket(&request,
                   UART_OTA_CMD_DATA,
                   artifact->update_id,
                   offset,
                   sequence);
        request.payload_length = (uint16_t)chunk;

        status = artifact->read(artifact->read_context,
                                offset,
                                request.payload,
                                chunk);
        if (status != ESP_OK)
        {
            return status;
        }

        status = Request(client, &request, &response);
        if (status != ESP_OK)
        {
            return status;
        }

        status = RequireAck(&response, &ack);
        if (status != ESP_OK)
        {
            return status;
        }

        if (ack.next_expected_offset != (offset + chunk))
        {
            return ESP_ERR_INVALID_RESPONSE;
        }

        offset += chunk;
        sequence = (uint16_t)(sequence + 1U);

        if (client->progress != NULL)
        {
            client->progress(client->progress_context,
                             offset,
                             artifact->image_size);
        }
    }

    return ESP_OK;
}

static esp_err_t FinishTransfer(UartOtaClient_t *client,
                                const UartOtaArtifact_t *artifact)
{
    UartOtaPacket_t request;
    UartOtaPacket_t response;
    UartOtaAckInfo_t ack;
    esp_err_t status;
    const uint16_t sequence =
        (uint16_t)(((artifact->image_size +
                     UART_OTA_MAX_PAYLOAD - 1UL) /
                    UART_OTA_MAX_PAYLOAD) & 0xFFFFUL);

    InitPacket(&request,
               UART_OTA_CMD_FINISH,
               artifact->update_id,
               artifact->image_size,
               sequence);

    status = Request(client, &request, &response);
    if (status != ESP_OK)
    {
        return status;
    }

    status = RequireAck(&response, &ack);
    if (status != ESP_OK)
    {
        return status;
    }

    if (ack.update_state != UART_OTA_UPDATE_ARTIFACT_READY)
    {
        return ESP_ERR_INVALID_STATE;
    }

    /* Verify the idempotent FINISH retry contract from Phase 5. */
    status = Request(client, &request, &response);
    if (status != ESP_OK)
    {
        return status;
    }

    status = RequireAck(&response, &ack);
    if (status != ESP_OK)
    {
        return status;
    }

    return (ack.update_state == UART_OTA_UPDATE_ARTIFACT_READY)
               ? ESP_OK
               : ESP_ERR_INVALID_STATE;
}

static esp_err_t Install(UartOtaClient_t *client,
                         const UartOtaArtifact_t *artifact)
{
    UartOtaPacket_t request;
    UartOtaPacket_t response;
    UartOtaAckInfo_t ack;
    esp_err_t status;

    InitPacket(&request,
               UART_OTA_CMD_INSTALL,
               artifact->update_id,
               artifact->image_size,
               0U);

    status = Request(client, &request, &response);
    if (status != ESP_OK)
    {
        return status;
    }

    return RequireAck(&response, &ack);
}

static esp_err_t WaitForFinal(UartOtaClient_t *client,
                              const UartOtaArtifact_t *artifact,
                              UartOtaHelloInfo_t *final_info)
{
    const int64_t deadline = DeadlineUs(UART_OTA_FINAL_TIMEOUT_MS);
    bool saw_trial = false;

    while (!DeadlineExpired(deadline))
    {
        UartOtaHelloInfo_t info;
        esp_err_t status = QueryOnce(client, &info);

        if (status != ESP_OK)
        {
            /*
             * Bootloader owns the MCU during backup/install/rollback and has
             * no UART service. Missing replies here are expected.
             */
            vTaskDelay(pdMS_TO_TICKS(UART_OTA_POLL_DELAY_MS));
            continue;
        }

        if ((info.application_version == artifact->target_version) &&
            (info.update_state == UART_OTA_UPDATE_TRIAL_BOOT))
        {
            saw_trial = true;
            ESP_LOGI(TAG,
                     "candidate v%" PRIu32 " observed in TRIAL_BOOT",
                     info.application_version);
        }

        if ((info.application_version == artifact->target_version) &&
            (info.update_state == UART_OTA_UPDATE_IDLE))
        {
            if (final_info != NULL)
            {
                *final_info = info;
            }
            ESP_LOGI(TAG,
                     "target v%" PRIu32 " confirmed%s",
                     info.application_version,
                     saw_trial ? " after trial" : "");
            return ESP_OK;
        }

        /*
         * If the old application is back in IDLE after we have seen trial,
         * Phase 8 rolled back the candidate.
         */
        if (saw_trial &&
            (info.application_version != artifact->target_version) &&
            (info.update_state == UART_OTA_UPDATE_IDLE))
        {
            if (final_info != NULL)
            {
                *final_info = info;
            }
            return ESP_ERR_INVALID_STATE;
        }

        vTaskDelay(pdMS_TO_TICKS(UART_OTA_POLL_DELAY_MS));
    }

    return ESP_ERR_TIMEOUT;
}

esp_err_t UartOta_TransferInstallAndWait(UartOtaClient_t *client,
                                         const UartOtaArtifact_t *artifact,
                                         UartOtaHelloInfo_t *final_info)
{
    UartOtaHelloInfo_t target;
    UartOtaPlan_t plan;
    esp_err_t status;
    uint32_t start_offset = 0UL;
    uint32_t base_version = 0UL;

    if ((client == NULL) || (artifact == NULL) ||
        (artifact->read == NULL) ||
        (artifact->update_id == 0UL) ||
        (artifact->target_version == 0UL) ||
        (artifact->image_size == 0UL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    status = UartOta_Query(client, &target);
    if (status != ESP_OK)
    {
        return status;
    }

    ESP_LOGI(TAG,
             "STM32 protocol=%u app=%" PRIu32
             " state=%u caps=0x%08" PRIX32,
             target.protocol_version,
             target.application_version,
             target.update_state,
             target.capability_flags);

    if ((target.capability_flags & UART_OTA_CAP_FULL_IMAGE) == 0UL)
    {
        return ESP_ERR_NOT_SUPPORTED;
    }

    base_version = target.application_version;

    for (uint32_t transitions = 0UL; transitions < 4UL; ++transitions)
    {
        plan = UartOta_SelectPlan(&target,
                                  artifact->update_id,
                                  artifact->image_size,
                                  artifact->target_version);

        switch (plan)
        {
            case UART_OTA_PLAN_ALREADY_TARGET:
                if (final_info != NULL)
                {
                    *final_info = target;
                }
                return ESP_OK;

            case UART_OTA_PLAN_ABORT_FOREIGN:
                status = AbortTransfer(client, target.active_update_id);
                if (status != ESP_OK)
                {
                    return status;
                }
                status = UartOta_Query(client, &target);
                if (status != ESP_OK)
                {
                    return status;
                }
                continue;

            case UART_OTA_PLAN_START_NEW:
                status = StartTransfer(client, artifact, base_version);
                if (status != ESP_OK)
                {
                    return status;
                }
                start_offset = 0UL;
                break;

            case UART_OTA_PLAN_RESUME:
                status = ResumeTransfer(client, artifact, &start_offset);
                if (status != ESP_OK)
                {
                    return status;
                }
                ESP_LOGI(TAG,
                         "resuming update at offset=%" PRIu32,
                         start_offset);
                break;

            case UART_OTA_PLAN_INSTALL_READY:
                status = Install(client, artifact);
                if (status != ESP_OK)
                {
                    /*
                     * INSTALL deliberately resets the STM32 after replying.
                     * The ACK can be lost at that boundary. Final target state
                     * is authoritative, exactly like the Phase-6 PC client.
                     */
                    ESP_LOGW(TAG,
                             "INSTALL ACK uncertain: %s; waiting for target",
                             esp_err_to_name(status));
                }
                return WaitForFinal(client, artifact, final_info);

            case UART_OTA_PLAN_WAIT_TARGET:
                return WaitForFinal(client, artifact, final_info);

            case UART_OTA_PLAN_ERROR:
            default:
                return ESP_ERR_INVALID_STATE;
        }

        status = SendData(client, artifact, start_offset);
        if (status != ESP_OK)
        {
            return status;
        }

        status = FinishTransfer(client, artifact);
        if (status != ESP_OK)
        {
            return status;
        }

        ESP_LOGI(TAG,
                 "artifact ready size=%" PRIu32
                 " crc32=0x%08" PRIX32,
                 artifact->image_size,
                 artifact->image_crc32);

        status = Install(client, artifact);
        if (status == ESP_OK)
        {
            ESP_LOGI(TAG, "INSTALL ACK received");
        }
        else
        {
            ESP_LOGW(TAG,
                     "INSTALL ACK uncertain: %s; waiting for target",
                     esp_err_to_name(status));
        }

        return WaitForFinal(client, artifact, final_info);
    }

    return ESP_ERR_INVALID_STATE;
}
