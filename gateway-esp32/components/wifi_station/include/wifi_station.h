#ifndef WIFI_STATION_H
#define WIFI_STATION_H

#include <stdint.h>

#include "esp_err.h"

esp_err_t WifiStation_Connect(const char *ssid,
                              const char *password,
                              uint32_t timeout_ms);

#endif
