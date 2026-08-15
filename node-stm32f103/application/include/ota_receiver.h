#ifndef OTA_RECEIVER_H
#define OTA_RECEIVER_H

#include <stdbool.h>
#include "ota_protocol.h"
#include "ota_response.h"

bool OtaReceiver_Init(void);
void OtaReceiver_ProcessPacket(const OtaPacket_t *request,
                               OtaPacket_t *response);
void OtaReceiver_GetResponseInfo(OtaResponseInfo_t *info);

#endif
