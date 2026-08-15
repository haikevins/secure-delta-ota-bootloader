# Firmware Release Server — Phase 15

Phase 15 implements the server and publication side of Secure Delta OTA.

The server is deliberately small and uses only the Python standard library.
OpenSSL CLI is used to verify the detached ECDSA release-manifest signature.

## Pin the authorized release key

Production serving/command publication must not trust only the public key
shipped inside a release. Configure the authorized public SPKI SHA-256
fingerprint separately:

```bash
export PHASE15_TRUSTED_KEY_SHA256=<64-hex-SPKI-SHA256>
```

## Verify a release

```bash
python3 server/app/main.py verify \
  --release-root dist/releases \
  --release-id fw-v3 \
  --trusted-key-sha256 "$PHASE15_TRUSTED_KEY_SHA256"
```

## Serve releases over HTTPS

```bash
python3 server/app/main.py serve \
  --release-root dist/releases \
  --bind 0.0.0.0 \
  --port 8443 \
  --cert /etc/sdota/server.pem \
  --key /etc/sdota/server.key \
  --trusted-key-sha256 "$PHASE15_TRUSTED_KEY_SHA256"
```

Available paths are intentionally limited to `/healthz` and versioned release
files under `/releases/<release-id>/...`.

## Build or publish an MQTT command

```bash
python3 server/app/main.py command \
  --release-root dist/releases \
  --release-id fw-v3 \
  --current-version 2 \
  --device-id bluepill-001 \
  --trusted-key-sha256 "$PHASE15_TRUSTED_KEY_SHA256"
```

```bash
python3 server/app/main.py publish \
  --release-root dist/releases \
  --release-id fw-v3 \
  --current-version 2 \
  --device-id bluepill-001 \
  --broker-uri mqtts://mqtt.example:8883 \
  --ca /etc/sdota/mqtt-ca.pem \
  --trusted-key-sha256 "$PHASE15_TRUSTED_KEY_SHA256"
```

The server chooses an exact-base delta when available and otherwise selects the
signed full SDOT. MQTT publication is QoS1 and waits for PUBACK.

See:

- `server/schemas/phase15-release-manifest.schema.json`
- `docs/phase-15-server-release-pipeline.md`
- `PHASE15_REPORT.md`
