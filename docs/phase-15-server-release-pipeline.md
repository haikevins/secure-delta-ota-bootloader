# Phase 15 — Server and Release Pipeline

## Scope

Phase 15 turns the Phase-14 signed `SDOT` format into an operational release
system:

```text
application.bin
    |
    +--> signed full SDOT
    |
exact previous release
    |
    +--> JojoDiff patch --> signed delta SDOT
                              |
                              v
                     immutable release directory
                              |
                    manifest + detached signature
                              |
             +----------------+----------------+
             |                                 |
             v                                 v
        HTTPS release server              MQTTS command
                                               |
                                               v
                                      ESP32 secure gateway
                                               |
                                              UART
                                               |
                                               v
                                       STM32 bootloader
```

Firmware bytes still travel over HTTPS. MQTT carries only orchestration
metadata.

## Release contents

A release is published under `dist/releases/fw-vN/`:

```text
application-vN.bin
application-vN.full.sdot
application-vBASE-to-vN.delta.sdot   # only when eligible
manifest.json
manifest.json.sig
signing-public.pem
checksums.txt
release-notes.md
```

`application-vN.bin` is retained so a later release can use it as an exact
delta base. The STM32 receives only signed `.sdot` artifacts.

## Private-key custody

`tools/phase15_release.py` refuses private keys located anywhere inside the
repository. On POSIX systems it also rejects a key readable or writable by
group/other users.

A release contains only the public key. The private key is never copied into
the release tree.

Production CI is designed around the GitHub Actions
`firmware-production` environment. The signing key is expected in the
environment secret `PHASE15_SIGNING_KEY_PEM`, which is materialized only in
the runner temporary directory with mode `0600` and deleted on job exit.

The deployed STM32 remains the final firmware-authenticity trust boundary:
the public key embedded in the bootloader must correspond to the release
`key_id`.

The server also has an **independent release-key pin**. Production
`serve`/`command`/`publish` operations require the SHA-256 fingerprint of the
authorized public SubjectPublicKeyInfo through
`PHASE15_TRUSTED_KEY_SHA256` or `--trusted-key-sha256`. This prevents a
self-consistent but attacker-replaced `manifest.json` + signature + public key
set from becoming an authorized server release.

## Manifest

`manifest.json` records:

- product/hardware identity;
- target firmware version;
- release channel and source revision;
- signing `key_id` and public-key fingerprint;
- exact target application hash/CRC;
- signed full/delta container URL, size, CRC32 and SHA-256;
- delta selection/eligibility information.

`manifest.json.sig` is a detached ECDSA-P256/SHA-256 signature over the exact
manifest bytes. This signature protects release metadata for server-side audit
and publication. Device firmware authenticity does not depend on the manifest
signature: the STM32 independently verifies the signature inside each SDOT.

## Artifact selection

The server selects a delta only when its `base_version` exactly equals the
device's current application version. Otherwise it falls back to the signed
full container.

If the device is already at or newer than the release target, no update command
is generated.

The MQTT command remains compatible with the frozen Phase-11 contract:

```json
{
  "schema": 1,
  "cmd": "update",
  "update_id": 3491033089,
  "target_version": 2,
  "size": 1446,
  "crc32": 1234567890,
  "url": "https://firmware.example/releases/fw-v2/application-v1-to-v2.delta.sdot"
}
```

## HTTPS server

The Phase-15 server is intentionally small and dependency-free. It serves only:

```text
/healthz
/releases/<release-id>/<approved release file>
```

It does not provide directory listing or arbitrary repository files. TLS is
mandatory and the server requires a certificate/key pair.

Example:

```bash
python3 server/app/main.py serve \
  --release-root dist/releases \
  --bind 0.0.0.0 \
  --port 8443 \
  --cert /etc/sdota/server.pem \
  --key /etc/sdota/server.key
```

Before serving, every release directory is fully verified against the
externally pinned release-key fingerprint.

Example trust configuration:

```bash
export PHASE15_TRUSTED_KEY_SHA256=<64-hex-SPKI-SHA256>
```

## MQTT publication

Generate a command without sending it:

```bash
python3 server/app/main.py command \
  --release-root dist/releases \
  --release-id fw-v2 \
  --current-version 1 \
  --device-id bluepill-001
```

Publish QoS1 over TLS and wait for PUBACK:

```bash
python3 server/app/main.py publish \
  --release-root dist/releases \
  --release-id fw-v2 \
  --current-version 1 \
  --device-id bluepill-001 \
  --broker-uri mqtts://mqtt.example:8883 \
  --ca /etc/sdota/mqtt-ca.pem
```

Optional MQTT credentials are read from:

```text
PHASE15_MQTT_USERNAME
PHASE15_MQTT_PASSWORD
```

## ESP32 integration

Phase 15 closes a gap between Phase 14 and the older Phase-11 gateway.

The gateway now:

- accepts artifacts up to the STM32 128 KiB Incoming partition, not just the
  38 KiB application size;
- parses the canonical SDOT header from its HTTPS cache;
- carries `artifact_type`, `base_version` and `container_header_size=140`
  into the UART START packet;
- requires STM32 delta capability for signed delta;
- requires STM32 signature-verification capability for SDOT.

The ESP32 does **not** replace the bootloader signature check. It only extracts
routing metadata needed by the UART protocol.

## Release creation

Local example:

```bash
chmod 600 /secure/location/firmware-signing.pem

python3 tools/phase15_release.py \
  --target node-stm32f103/application/out-release/application.bin \
  --target-version 3 \
  --base /secure/releases/application-v2.bin \
  --base-version 2 \
  --key /secure/location/firmware-signing.pem \
  --key-id 0x14000001 \
  --base-url https://firmware.example \
  --channel stable
```

Release directories are immutable. Re-running with the same release ID fails
instead of overwriting an existing release.

## CI authorization boundary

`.github/workflows/firmware-release.yml` is manual-only. The secret-bearing job
references the `firmware-production` environment and has a single release
concurrency group. Configure that environment with required reviewers and
restrict which refs are allowed to deploy.

The normal build/test workflows do not receive the production signing secret.
The protected release job also requires the non-secret environment variable
`PHASE15_TRUSTED_KEY_SHA256`; the freshly generated release must verify against
that pin before GitHub release publication.

## Validation

```bash
make phase15-check
```

The host check covers:

- Phase-14 security regression first;
- release creation with full + eligible delta;
- exact-base delta selection and full fallback;
- key custody rejection;
- immutable release overwrite rejection;
- detached manifest signature verification;
- SDOT metadata/hash/CRC verification;
- TLS HTTPS byte-for-byte serving;
- MQTTS QoS1 publish + PUBACK;
- tampered release rejection;
- exact ESP32 SDOT-to-UART START contract in host C.

Physical ESP32+STM32 end-to-end validation is provided by
`make phase15-hw-test ...` and remains the final HIL gate.
