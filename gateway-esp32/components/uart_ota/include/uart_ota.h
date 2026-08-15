#ifndef UART_OTA_H
#define UART_OTA_H

#include <stddef.h>
#include <stdint.h>

#include "driver/uart.h"
#include "esp_err.h"

#include "uart_ota_protocol.h"

typedef esp_err_t (*UartOtaArtifactReadFn)(void *context,
                                          uint32_t offset,
                                          uint8_t *buffer,
                                          size_t length);

typedef void (*UartOtaProgressFn)(void *context,
                                  uint32_t sent,
                                  uint32_t total);

typedef struct
{
    uint32_t update_id;
    uint32_t base_version;
    uint32_t target_version;
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t container_header_size;
    uint8_t artifact_type;
    UartOtaArtifactReadFn read;
    void *read_context;
} UartOtaArtifact_t;

typedef struct
{
    uart_port_t uart_num;
    int tx_gpio;
    int rx_gpio;
    uint32_t baud_rate;
    uint32_t response_timeout_ms;
    uint32_t retry_count;
} UartOtaConfig_t;

typedef struct
{
    UartOtaConfig_t config;
    UartOtaProgressFn progress;
    void *progress_context;
} UartOtaClient_t;

esp_err_t UartOta_Init(UartOtaClient_t *client,
                       const UartOtaConfig_t *config);

void UartOta_SetProgressCallback(UartOtaClient_t *client,
                                 UartOtaProgressFn callback,
                                 void *context);

esp_err_t UartOta_Query(UartOtaClient_t *client,
                        UartOtaHelloInfo_t *info);

esp_err_t UartOta_TransferInstallAndWait(UartOtaClient_t *client,
                                         const UartOtaArtifact_t *artifact,
                                         UartOtaHelloInfo_t *final_info);

#endif
