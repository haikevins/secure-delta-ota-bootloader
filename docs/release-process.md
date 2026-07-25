# Firmware Release Process

Status: Phase 0 specification

```text
Build target application ELF
        |
        v
Extract deterministic application.bin
        |
        +-----------------------------+
        |                             |
        v                             v
Create full payload             Select exact base release
                                      |
                                      v
                              Create JojoDiff patch
        |                             |
        +---------------+-------------+
                        v
              Create version-1 header
                        |
                        v
         Calculate CRC32 and SHA-256 values
                        |
                        v
              Sign header plus payload
                        |
                        v
      Emit full/delta containers and manifest
                        |
                        v
             Upload artifacts through HTTPS
                        |
                        v
                Publish MQTT notification
```

## Release directory

```text
release-vX.Y.Z/
├── application-vX.Y.Z.bin
├── application-vX.Y.Z.full
├── application-vBASE-to-vX.Y.Z.delta
├── manifest.json
├── checksums.txt
└── release-notes.md
```

## Private-key policy

- Never commit signing keys.
- Never copy private signing keys to ESP32 or STM32.
- Local development may use disposable test keys stored outside the repository.
- CI secret integration is postponed until Phase 15.
