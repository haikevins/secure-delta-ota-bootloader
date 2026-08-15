#ifndef PHASE11_RUNTIME_CONFIG_H
#define PHASE11_RUNTIME_CONFIG_H

/*
 * The hardware-test runner temporarily overwrites this file before the
 * ESP-IDF build and restores it afterward. Production builds leave override
 * disabled and use Kconfig values.
 */
#define SDOTA_PHASE11_HW_OVERRIDE          0
#define SDOTA_PHASE11_HW_WIFI_SSID         ""
#define SDOTA_PHASE11_HW_WIFI_PASSWORD     ""
#define SDOTA_PHASE11_HW_MQTT_URI          ""
#define SDOTA_PHASE11_HW_USE_TEST_CA       0
#define SDOTA_PHASE11_HW_TEST_EPOCH        0LL
#define SDOTA_PHASE11_HW_SINGLE_SHOT       0

#endif
