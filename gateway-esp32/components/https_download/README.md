# https_download — HTTPS transport

This component performs an authenticated HTTPS GET and streams the response
directly into the ESP32 `stm32_cache` partition.

HTTPS transport policy:

- URL must use `https://`;
- TLS server verification is mandatory;
- HTTP status must be `200`;
- a positive `Content-Length` is required;
- chunked responses are rejected in HTTPS transport;
- the response may not exceed the STM32 application maximum;
- cache header is invalidated before download;
- cache data is written sequentially;
- the complete stored artifact is read back and CRC32 checked;
- cache header is published only after the complete download verifies.

Production uses the ESP x509 certificate bundle. The hardware test uses a
temporary private test CA generated on the developer PC; only the public CA
certificate is embedded into the ESP32 test build.
