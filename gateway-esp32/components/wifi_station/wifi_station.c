#include "wifi_station.h"

#include <stddef.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAILED_BIT    BIT1
#define WIFI_MAX_RETRIES   10

static const char *TAG = "wifi_station";

static EventGroupHandle_t s_event_group;
static int s_retry_count;

static void WifiEventHandler(void *arg,
                             esp_event_base_t event_base,
                             int32_t event_id,
                             void *event_data)
{
    (void)arg;
    (void)event_data;

    if ((event_base == WIFI_EVENT) &&
        (event_id == WIFI_EVENT_STA_START))
    {
        (void)esp_wifi_connect();
    }
    else if ((event_base == WIFI_EVENT) &&
             (event_id == WIFI_EVENT_STA_DISCONNECTED))
    {
        if (s_retry_count < WIFI_MAX_RETRIES)
        {
            ++s_retry_count;
            (void)esp_wifi_connect();
        }
        else if (s_event_group != NULL)
        {
            xEventGroupSetBits(s_event_group, WIFI_FAILED_BIT);
        }
    }
    else if ((event_base == IP_EVENT) &&
             (event_id == IP_EVENT_STA_GOT_IP))
    {
        s_retry_count = 0;
        if (s_event_group != NULL)
        {
            xEventGroupSetBits(s_event_group, WIFI_CONNECTED_BIT);
        }
    }
}

static esp_err_t InitNvs(void)
{
    esp_err_t status = nvs_flash_init();

    if ((status == ESP_ERR_NVS_NO_FREE_PAGES) ||
        (status == ESP_ERR_NVS_NEW_VERSION_FOUND))
    {
        status = nvs_flash_erase();
        if (status != ESP_OK)
        {
            return status;
        }
        status = nvs_flash_init();
    }

    return status;
}

esp_err_t WifiStation_Connect(const char *ssid,
                              const char *password,
                              uint32_t timeout_ms)
{
    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    wifi_config_t wifi_config;
    esp_event_handler_instance_t wifi_instance;
    esp_event_handler_instance_t ip_instance;
    EventBits_t bits;
    size_t ssid_length;
    size_t password_length;
    esp_err_t status;

    if ((ssid == NULL) || (password == NULL) ||
        (timeout_ms == 0UL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    ssid_length = strlen(ssid);
    password_length = strlen(password);

    if ((ssid_length == 0U) ||
        (ssid_length > sizeof(wifi_config.sta.ssid)) ||
        (password_length > sizeof(wifi_config.sta.password)))
    {
        return ESP_ERR_INVALID_ARG;
    }

    status = InitNvs();
    if (status != ESP_OK)
    {
        return status;
    }

    status = esp_netif_init();
    if ((status != ESP_OK) && (status != ESP_ERR_INVALID_STATE))
    {
        return status;
    }

    status = esp_event_loop_create_default();
    if ((status != ESP_OK) && (status != ESP_ERR_INVALID_STATE))
    {
        return status;
    }

    if (esp_netif_create_default_wifi_sta() == NULL)
    {
        return ESP_FAIL;
    }

    s_event_group = xEventGroupCreate();
    if (s_event_group == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    status = esp_wifi_init(&init_config);
    if (status != ESP_OK)
    {
        vEventGroupDelete(s_event_group);
        s_event_group = NULL;
        return status;
    }

    status = esp_event_handler_instance_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &WifiEventHandler,
        NULL,
        &wifi_instance);
    if (status != ESP_OK)
    {
        return status;
    }

    status = esp_event_handler_instance_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &WifiEventHandler,
        NULL,
        &ip_instance);
    if (status != ESP_OK)
    {
        return status;
    }

    memset(&wifi_config, 0, sizeof(wifi_config));
    memcpy(wifi_config.sta.ssid, ssid, ssid_length);
    memcpy(wifi_config.sta.password, password, password_length);

    status = esp_wifi_set_mode(WIFI_MODE_STA);
    if (status != ESP_OK)
    {
        return status;
    }

    status = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (status != ESP_OK)
    {
        return status;
    }

    s_retry_count = 0;
    status = esp_wifi_start();
    if (status != ESP_OK)
    {
        return status;
    }

    bits = xEventGroupWaitBits(
        s_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAILED_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(timeout_ms));

    if ((bits & WIFI_CONNECTED_BIT) != 0U)
    {
        wifi_ps_type_t power_save = WIFI_PS_MIN_MODEM;

        /*
         * The OTA gateway is latency/reliability sensitive while carrying
         * MQTTS + HTTPS traffic.  ESP-IDF defaults to WIFI_PS_MIN_MODEM;
         * disable modem power-save after GOT_IP so ARP/TCP/TLS startup is
         * not dependent on inbound traffic keeping the station awake.
         */
        status = esp_wifi_set_ps(WIFI_PS_NONE);
        if (status != ESP_OK)
        {
            ESP_LOGE(TAG,
                     "failed to disable Wi-Fi power save: %s",
                     esp_err_to_name(status));
            return status;
        }

        status = esp_wifi_get_ps(&power_save);
        if (status != ESP_OK)
        {
            ESP_LOGE(TAG,
                     "failed to read Wi-Fi power-save mode: %s",
                     esp_err_to_name(status));
            return status;
        }

        if (power_save != WIFI_PS_NONE)
        {
            ESP_LOGE(TAG,
                     "Wi-Fi power-save mode verification failed: %d",
                     (int)power_save);
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "P15_WIFI_PS=PASS mode=none");
        ESP_LOGI(TAG, "Wi-Fi connected to SSID '%s'", ssid);

        /*
         * Give the netif/AP a short settling interval after changing the
         * power-save policy before starting the first TLS socket.
         */
        vTaskDelay(pdMS_TO_TICKS(250U));
        return ESP_OK;
    }

    if ((bits & WIFI_FAILED_BIT) != 0U)
    {
        ESP_LOGE(TAG, "Wi-Fi connection failed after retries");
        return ESP_FAIL;
    }

    ESP_LOGE(TAG, "Wi-Fi connection timed out");
    return ESP_ERR_TIMEOUT;
}
