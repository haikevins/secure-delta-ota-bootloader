#include "https_download.h"

#include <inttypes.h>
#include <stddef.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "https_download_policy.h"

#define HTTPS_DOWNLOAD_BUFFER_SIZE 1024U

static const char *TAG = "https_download";

static int64_t DeadlineUs(uint32_t timeout_ms)
{
    return esp_timer_get_time() + ((int64_t)timeout_ms * 1000LL);
}

static esp_err_t PolicyToError(HttpsDownloadPolicyStatus_t policy)
{
    switch (policy)
    {
        case HTTPS_DOWNLOAD_POLICY_OK:
            return ESP_OK;
        case HTTPS_DOWNLOAD_POLICY_TOO_LARGE:
            return ESP_ERR_INVALID_SIZE;
        case HTTPS_DOWNLOAD_POLICY_BAD_URL:
            return ESP_ERR_INVALID_ARG;
        case HTTPS_DOWNLOAD_POLICY_BAD_STATUS:
        case HTTPS_DOWNLOAD_POLICY_CHUNKED_UNSUPPORTED:
        case HTTPS_DOWNLOAD_POLICY_MISSING_LENGTH:
        default:
            return ESP_ERR_INVALID_RESPONSE;
    }
}

esp_err_t HttpsDownload_ToCache(const HttpsDownloadConfig_t *config,
                                ArtifactCache_t *cache,
                                HttpsDownloadResult_t *result)
{
    esp_http_client_config_t http_config;
    esp_http_client_handle_t client = NULL;
    ArtifactCacheWriter_t writer;
    uint8_t buffer[HTTPS_DOWNLOAD_BUFFER_SIZE];
    int64_t content_length;
    int64_t inactivity_deadline;
    uint32_t total = 0UL;
    int status_code;
    bool writer_started = false;
    bool chunked;
    esp_err_t status = ESP_FAIL;

    memset(&writer, 0, sizeof(writer));

    if ((config == NULL) || (cache == NULL) ||
        (config->url == NULL) ||
        (config->update_id == 0UL) ||
        (config->target_version == 0UL) ||
        (config->max_image_size == 0UL) ||
        (config->timeout_ms == 0UL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!HttpsDownload_IsHttpsUrl(config->url))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if ((config->cert_pem == NULL) && !config->use_crt_bundle)
    {
        /*
         * Phase 10 never permits HTTPS without server authentication.
         */
        return ESP_ERR_INVALID_ARG;
    }

    memset(&http_config, 0, sizeof(http_config));
    http_config.url = config->url;
    http_config.timeout_ms = (int)config->timeout_ms;
    http_config.keep_alive_enable = true;

    if (config->cert_pem != NULL)
    {
        http_config.cert_pem = config->cert_pem;
    }
    else
    {
        http_config.crt_bundle_attach = esp_crt_bundle_attach;
    }

    client = esp_http_client_init(&http_config);
    if (client == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    status = esp_http_client_set_method(client, HTTP_METHOD_GET);
    if (status != ESP_OK)
    {
        goto cleanup;
    }

    status = esp_http_client_open(client, 0);
    if (status != ESP_OK)
    {
        goto cleanup;
    }

    content_length = esp_http_client_fetch_headers(client);
    if (content_length < 0)
    {
        status = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    status_code = esp_http_client_get_status_code(client);
    chunked = esp_http_client_is_chunked_response(client);

    {
        const HttpsDownloadPolicyStatus_t policy =
            HttpsDownload_ValidateResponse(
                config->url,
                status_code,
                content_length,
                chunked,
                config->max_image_size);

        status = PolicyToError(policy);
        if (status != ESP_OK)
        {
            ESP_LOGE(TAG,
                     "response rejected: status=%d len=%" PRId64
                     " chunked=%u policy=%d",
                     status_code,
                     content_length,
                     chunked ? 1U : 0U,
                     (int)policy);
            goto cleanup;
        }
    }

    status = ArtifactCache_BeginWrite(
        &writer,
        (uint32_t)content_length,
        config->update_id,
        config->target_version);
    if (status != ESP_OK)
    {
        goto cleanup;
    }
    writer_started = true;

    inactivity_deadline = DeadlineUs(config->timeout_ms);

    while (total < (uint32_t)content_length)
    {
        const uint32_t remaining =
            (uint32_t)content_length - total;
        const int requested =
            (remaining > sizeof(buffer))
                ? (int)sizeof(buffer)
                : (int)remaining;

        const int read = esp_http_client_read(
            client,
            (char *)buffer,
            requested);

        if (read == -ESP_ERR_HTTP_EAGAIN)
        {
            if (esp_timer_get_time() >= inactivity_deadline)
            {
                status = ESP_ERR_TIMEOUT;
                goto cleanup;
            }
            continue;
        }

        if (read < 0)
        {
            status = ESP_FAIL;
            goto cleanup;
        }

        if (read == 0)
        {
            status = ESP_ERR_INVALID_RESPONSE;
            goto cleanup;
        }

        inactivity_deadline = DeadlineUs(config->timeout_ms);

        status = ArtifactCache_Write(
            &writer,
            buffer,
            (size_t)read);
        if (status != ESP_OK)
        {
            goto cleanup;
        }

        total += (uint32_t)read;

        if (config->progress != NULL)
        {
            config->progress(config->progress_context,
                             total,
                             (uint32_t)content_length);
        }
    }

    if (!esp_http_client_is_complete_data_received(client))
    {
        status = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    status = ArtifactCache_Commit(&writer, cache);
    if (status != ESP_OK)
    {
        goto cleanup;
    }
    writer_started = false;

    if (result != NULL)
    {
        result->status_code = status_code;
        result->content_length = (uint32_t)content_length;
        result->downloaded_size = total;
        result->image_crc32 = cache->header.image_crc32;
    }

    ESP_LOGI(TAG,
             "HTTPS cache commit size=%" PRIu32
             " crc32=0x%08" PRIX32,
             total,
             cache->header.image_crc32);

    status = ESP_OK;

cleanup:
    if (writer_started)
    {
        const esp_err_t abort_status = ArtifactCache_Abort(&writer);
        if ((status == ESP_OK) && (abort_status != ESP_OK))
        {
            status = abort_status;
        }
    }

    if (client != NULL)
    {
        (void)esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }

    return status;
}
