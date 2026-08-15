# Firmware Server

The server directory remains intentionally small until the release/server
phase.

Phase 12 adds the JSON schema for host-generated delta metadata:

```text
server/schemas/phase12-delta-manifest.schema.json
```

The actual delta artifact and manifest are generated under `dist/phase12/` by:

```bash
make phase12-delta
```

MQTT orchestration is implemented in the ESP32 Phase-11 gateway. Signed release
publication and private-key handling remain a later phase; private signing keys
must never be committed to this repository.
