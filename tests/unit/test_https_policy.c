#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "https_download_policy.h"

static void TestHttpsUrl(void)
{
    assert(HttpsDownload_IsHttpsUrl("https://example.com/fw.bin"));
    assert(HttpsDownload_IsHttpsUrl("https://192.0.2.10:8443/fw.bin"));
    assert(!HttpsDownload_IsHttpsUrl("http://example.com/fw.bin"));
    assert(!HttpsDownload_IsHttpsUrl(""));
    assert(!HttpsDownload_IsHttpsUrl((const char *)0));
}

static void TestResponsePolicy(void)
{
    const uint32_t max_size = 38UL * 1024UL;

    assert(HttpsDownload_ValidateResponse(
               "https://example.com/fw.bin",
               200,
               10184,
               false,
               max_size) == HTTPS_DOWNLOAD_POLICY_OK);

    assert(HttpsDownload_ValidateResponse(
               "http://example.com/fw.bin",
               200,
               10184,
               false,
               max_size) == HTTPS_DOWNLOAD_POLICY_BAD_URL);

    assert(HttpsDownload_ValidateResponse(
               "https://example.com/fw.bin",
               404,
               10184,
               false,
               max_size) == HTTPS_DOWNLOAD_POLICY_BAD_STATUS);

    assert(HttpsDownload_ValidateResponse(
               "https://example.com/fw.bin",
               200,
               0,
               false,
               max_size) == HTTPS_DOWNLOAD_POLICY_MISSING_LENGTH);

    assert(HttpsDownload_ValidateResponse(
               "https://example.com/fw.bin",
               200,
               10184,
               true,
               max_size) ==
           HTTPS_DOWNLOAD_POLICY_CHUNKED_UNSUPPORTED);

    assert(HttpsDownload_ValidateResponse(
               "https://example.com/fw.bin",
               200,
               (int64_t)max_size + 1,
               false,
               max_size) == HTTPS_DOWNLOAD_POLICY_TOO_LARGE);
}

int main(void)
{
    TestHttpsUrl();
    TestResponsePolicy();

    puts("HTTPS response-policy host tests: PASS");
    return 0;
}
