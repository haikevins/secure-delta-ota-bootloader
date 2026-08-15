#include "https_download_policy.h"

#include <stddef.h>
#include <string.h>

bool HttpsDownload_IsHttpsUrl(const char *url)
{
    static const char prefix[] = "https://";

    if (url == NULL)
    {
        return false;
    }

    return strncmp(url, prefix, sizeof(prefix) - 1U) == 0;
}

HttpsDownloadPolicyStatus_t HttpsDownload_ValidateResponse(
    const char *url,
    int status_code,
    int64_t content_length,
    bool chunked,
    uint32_t max_image_size)
{
    if (!HttpsDownload_IsHttpsUrl(url))
    {
        return HTTPS_DOWNLOAD_POLICY_BAD_URL;
    }

    if (status_code != 200)
    {
        return HTTPS_DOWNLOAD_POLICY_BAD_STATUS;
    }

    /*
     * Phase 10 deliberately requires Content-Length. That lets the gateway
     * erase exactly the needed cache range before streaming and prevents an
     * unbounded response from consuming the partition.
     */
    if (chunked)
    {
        return HTTPS_DOWNLOAD_POLICY_CHUNKED_UNSUPPORTED;
    }

    if (content_length <= 0)
    {
        return HTTPS_DOWNLOAD_POLICY_MISSING_LENGTH;
    }

    if (((uint64_t)content_length > (uint64_t)max_image_size) ||
        ((uint64_t)content_length > UINT32_MAX))
    {
        return HTTPS_DOWNLOAD_POLICY_TOO_LARGE;
    }

    return HTTPS_DOWNLOAD_POLICY_OK;
}
