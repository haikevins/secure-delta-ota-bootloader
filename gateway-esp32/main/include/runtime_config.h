#ifndef SDOTA_RUNTIME_CONFIG_H
#define SDOTA_RUNTIME_CONFIG_H

/*
 * The hardware-test runner temporarily overwrites this file before the
 * ESP-IDF build and restores it afterward. Production builds leave override
 * disabled and use Kconfig values.
 */
#define SDOTA_RUNTIME_OVERRIDE          0
#define SDOTA_RUNTIME_WIFI_SSID         ""
#define SDOTA_RUNTIME_WIFI_PASSWORD     ""
#define SDOTA_RUNTIME_MQTT_URI          ""
#define SDOTA_RUNTIME_USE_TEST_CA       0
#define SDOTA_RUNTIME_TEST_EPOCH        0LL
#define SDOTA_RUNTIME_SINGLE_SHOT       0

#endif
