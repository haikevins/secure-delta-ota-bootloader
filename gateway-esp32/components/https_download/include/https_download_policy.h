#ifndef HTTPS_DOWNLOAD_POLICY_H
#define HTTPS_DOWNLOAD_POLICY_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    HTTPS_DOWNLOAD_POLICY_OK = 0,
    HTTPS_DOWNLOAD_POLICY_BAD_URL,
    HTTPS_DOWNLOAD_POLICY_BAD_STATUS,
    HTTPS_DOWNLOAD_POLICY_CHUNKED_UNSUPPORTED,
    HTTPS_DOWNLOAD_POLICY_MISSING_LENGTH,
    HTTPS_DOWNLOAD_POLICY_TOO_LARGE
} HttpsDownloadPolicyStatus_t;

bool HttpsDownload_IsHttpsUrl(const char *url);

HttpsDownloadPolicyStatus_t HttpsDownload_ValidateResponse(
    const char *url,
    int status_code,
    int64_t content_length,
    bool chunked,
    uint32_t max_image_size);

#endif
