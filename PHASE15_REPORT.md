# Phase 15 Report — Server and Release Pipeline

## Result

Phase 15 implements the signed firmware release lifecycle that Phase 14
deliberately left out.

The release pipeline creates immutable, signed full/delta SDOT assets,
cryptographically signs the release manifest, verifies every asset before
publication, serves release files over TLS and publishes the existing Phase-11
MQTT command over MQTTS QoS1.

## Security properties

- Signing private keys must stay outside the repository.
- POSIX private-key permissions must be `0600` or stricter.
- Release output contains the public key only.
- Existing release IDs cannot be overwritten.
- All MQTT/HTTPS production URLs are TLS-only.
- Release metadata is signed separately for server-side release integrity.
- Production server/publisher operations pin the authorized release public-key
  SHA-256 instead of trusting a public key shipped only inside the release.
- Device authenticity still terminates at the STM32's Phase-14 trust anchor.
- The server cannot authorize an unsigned or wrongly signed firmware image.

## Delta policy

A full signed container is always present.

When an exact base binary is supplied, a JojoDiff payload and signed delta SDOT
are generated. The delta is published only when the final signed delta
container meets the configured minimum savings threshold.

At command time, the server selects delta only for the exact matching base
version. All other older versions receive signed full firmware.

## ESP32 Phase-15 integration

The older Phase-11 gateway assumed all UART artifacts were raw full images and
limited HTTPS artifacts to 38 KiB. That is incompatible with the Phase-14 SDOT
header and with a maximum-size signed full image.

Phase 15 adds a small pure-C SDOT metadata parser to the ESP32. The cache export
now fills the frozen UART START fields correctly for full or delta SDOT and the
HTTPS/MQTT size boundary is raised to the 128 KiB STM32 Incoming partition.

Signature verification deliberately remains on STM32.

## CI/release authorization

The production workflow is `workflow_dispatch` only and references a protected
`firmware-production` environment. The production private key is expected as
an environment secret and is written only into the ephemeral runner temporary
directory.

Normal CI performs Phase-15 checks without production key access.

## Validation boundary

`make phase15-check` is the required host/server/release gate. It includes the
Phase-14 security gate and Phase-15 release/server/gateway integration tests.

Physical end-to-end server -> MQTTS -> ESP32 -> HTTPS -> UART -> secure STM32
installation is the final Phase-15 HIL gate.


## Host validation result

The direct Phase-15 checker completed successfully with the exact server,
release generator and pure-C ESP32 SDOT/UART metadata path:

```text
Phase 15 ESP32 secure-container UART contract: PASS
PHASE15_RELEASE=PASS release=fw-v2 target=v2 artifacts=2
PHASE15_DELTA_SAVINGS=89.77% threshold=20.00% included=yes
Phase 15 release selection: PASS delta=1174 full=11480
Phase 15 pinned release-key authorization: PASS
Phase 15 immutable-release overwrite rejection: PASS
Phase 15 private-key custody policy: PASS
Phase 15 verified TLS release serving: PASS
Phase 15 MQTTS command publication QoS1/PUBACK: PASS
Phase 15 tampered-release verification rejection: PASS
Secure Delta OTA Phase 15 server/release pipeline check: PASS
```

The local TLS regression CA includes critical CA/key-cert-signing extensions,
matching strict modern TLS verification behavior. HTTPS startup polling allows
enough time for release verification before declaring the server unavailable.

## MQTT URI build/runtime diagnostic hardening

During the first Phase-15 physical gateway test, the ESP32 joined Wi-Fi and
reported MQTT TLS connection timeouts, while host-side `tcpdump -ni any
'host <ESP32-IP> and tcp'` observed zero TCP packets from the ESP32.

To distinguish network isolation from a stale/wrong compiled broker URI, the
HIL runner now verifies the exact MQTT URI at three points:

1. generated `phase11_runtime_config.h`;
2. compiled `secure_delta_ota_gateway.bin`;
3. ESP32 runtime log immediately before `MqttOrchestrator_Start()`.

Expected diagnostics for host `192.168.1.8` are:

```text
P15_RUNTIME_CONFIG=PASS mqtt_uri=mqtts://192.168.1.8:8883
P15_COMPILED_MQTT_URI=PASS mqtt_uri=mqtts://192.168.1.8:8883 ...
P15_RUNTIME_MQTT_URI=mqtts://192.168.1.8:8883 test_ca=1
```

If all three match but no packet reaches the PC, the remaining fault is below
the application configuration layer (for example Wi-Fi client isolation or
network path filtering).

## Physical HIL secure-install wait hardening

A physical Phase-15 run established MQTT/TLS, fetched the signed delta over
HTTPS, transferred all 1446 bytes over UART, and received the STM32 INSTALL
ACK. The gateway then timed out while the STM32 was inside the bootloader
security/patch/install path.

Two HIL timing limits were too short for the Phase-14 secure path:

- ESP32 final-state wait: 55 s -> 120 s.
- Minimal TLS MQTT broker idle timeout for Phase 15: 45 s -> 150 s.

The Phase-11 broker keeps its 45 s default; Phase-15 explicitly opts into the
longer idle window. This preserves the earlier Phase-11 timing contract while
allowing ECDSA-P256 verification, delta reconstruction, backup/install, trial
boot and confirmation to finish on STM32F103 hardware.

## ESP32 Wi-Fi power-save reliability hardening

Physical HIL exposed an intermittent network-startup failure:

- without host-side ICMP traffic, the ESP32 could reach `GOT_IP` but MQTTS
  connection attempts timed out;
- while the host continuously pinged the ESP32, MQTTS, HTTPS and UART transfer
  proceeded normally;
- the ESP32 boot log reported modem power saving (`wifi:pm start, type: 1`).

The gateway now disables ESP-IDF modem power saving immediately after
`IP_EVENT_STA_GOT_IP` and verifies that the resulting mode is `WIFI_PS_NONE`
before any MQTTS/HTTPS socket is started. A short 250 ms netif/AP settling
delay follows the mode change.

Expected HIL marker:

```text
P15_WIFI_PS=PASS mode=none
```

This is a reliability policy for the OTA gateway: during secure update
transport, predictable ARP/TCP/TLS startup is preferred over modem power
savings.

## Final physical HIL result

The final Phase-15 run completed the production-shaped path on the ESP32 and
STM32 hardware without host-side keepalive traffic:

```text
P15_WIFI_PS=PASS mode=none
P11_MQTT=PASS
P11_COMMAND=PASS
P11_HTTPS=PASS
P11_UART_DATA 1446/1446
INSTALL ACK received
candidate v2 observed in TRIAL_BOOT
target v2 confirmed after trial
P11_FINAL=PASS app=v2 state=IDLE
P11_PIPELINE=PASS
P11_GATEWAY_HW_TEST=PASS
P11_BROKER_RESULT=PASS
P15_HTTPS_RELEASE_GET=PASS
P15_RELEASE_PIPELINE=PASS
Phase 15 server/release pipeline hardware test: PASS
```

The final STM32 metadata was `IDLE`, active version v2, no pending update, zero
boot attempts and `last_error == 0`. The installed candidate also passed
byte-for-byte verification. Phase 15 is therefore complete + hardware
verified.
