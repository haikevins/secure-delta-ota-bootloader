#ifndef HTTPS_DOWNLOAD_H
#define HTTPS_DOWNLOAD_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "artifact_cache.h"

typedef void (*HttpsDownloadProgressFn)(void *context,
                                        uint32_t downloaded,
                                        uint32_t total);

typedef struct
{
    const char *url;
    const char *cert_pem;
    bool use_crt_bundle;
    uint32_t update_id;
    uint32_t target_version;
    uint32_t max_image_size;
    uint32_t timeout_ms;
    HttpsDownloadProgressFn progress;
    void *progress_context;
} HttpsDownloadConfig_t;

typedef struct
{
    int status_code;
    uint32_t content_length;
    uint32_t downloaded_size;
    uint32_t image_crc32;
} HttpsDownloadResult_t;

esp_err_t HttpsDownload_ToCache(const HttpsDownloadConfig_t *config,
                                ArtifactCache_t *cache,
                                HttpsDownloadResult_t *result);

#endif
