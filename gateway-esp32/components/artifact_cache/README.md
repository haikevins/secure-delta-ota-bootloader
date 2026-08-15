# artifact_cache — Phase 10

The ESP32 `stm32_cache` partition remains the persistent source consumed by
the UART gateway.

Phase 10 adds a transactional streaming writer for HTTPS:

```text
invalidate header
erase data range
stream sequential bytes
streaming CRC32
full stored-image readback CRC32
publish header last
```

A reset or power loss before the final header commit leaves no valid cache
record. `ArtifactCache_Open()` also recomputes the complete stored image CRC
before exposing an artifact to the UART layer.

The persistent header format remains compatible with Phase 9.
