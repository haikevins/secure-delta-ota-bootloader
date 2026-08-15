#ifndef OTA_RESPONSE_H
#define OTA_RESPONSE_H

#include <stdint.h>
#include "ota_protocol.h"
#include "ota_status.h"

typedef struct
{
    uint8_t update_state;
    uint8_t last_status;
    uint32_t active_update_id;
    uint32_t next_expected_offset;
    uint32_t received_size;
    uint32_t expected_size;
    uint32_t last_error_detail;
    uint32_t capability_flags;
} OtaResponseInfo_t;

void OtaResponse_BuildAck(const OtaPacket_t *request,
                          const OtaResponseInfo_t *info,
                          OtaPacket_t *response);
void OtaResponse_BuildNack(const OtaPacket_t *request,
                           OtaStatus_t status,
                           const OtaResponseInfo_t *info,
                           OtaPacket_t *response);
void OtaResponse_BuildHelloQuery(const OtaPacket_t *request,
                                 const OtaResponseInfo_t *info,
                                 OtaPacket_t *response);

#endif
