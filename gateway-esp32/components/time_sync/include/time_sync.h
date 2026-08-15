#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t TimeSync_UseTestEpoch(int64_t epoch_seconds);
esp_err_t TimeSync_Sntp(const char *server, uint32_t timeout_ms);
bool TimeSync_IsSane(void);

#endif
