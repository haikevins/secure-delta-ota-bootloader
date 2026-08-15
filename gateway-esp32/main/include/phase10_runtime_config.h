#ifndef PHASE10_RUNTIME_CONFIG_H
#define PHASE10_RUNTIME_CONFIG_H

/*
 * Hardware-test runner temporarily overwrites this file before ESP-IDF build
 * and restores it afterward. Production builds leave override disabled and use
 * Kconfig values.
 */
#define SDOTA_PHASE10_HW_OVERRIDE         0
#define SDOTA_PHASE10_HW_WIFI_SSID        ""
#define SDOTA_PHASE10_HW_WIFI_PASSWORD    ""
#define SDOTA_PHASE10_HW_HTTPS_URL        ""
#define SDOTA_PHASE10_HW_USE_TEST_CA      0
#define SDOTA_PHASE10_HW_TEST_EPOCH       0LL

#endif
