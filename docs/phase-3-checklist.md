# Phase 3 Checklist — Metadata and Boot Decision

## Source and build

- [x] CRC32 IEEE implementation shared by metadata and future protocol code.
- [x] Persistent record layout frozen at 52 bytes.
- [x] Metadata A/B occupy separate 1 KiB internal-Flash pages.
- [x] Application linker budget reduced to 38 KiB.
- [x] Bootloader links the SPL internal-Flash driver.
- [x] Metadata storage reads, validates, commits and verifies records.
- [x] Boot decision is implemented as a host-testable pure module.

## Redundancy and validation

- [x] Erased/invalid A and B produce defaults.
- [x] One corrupted slot falls back to the other slot.
- [x] Both valid slots select the newer generation.
- [x] Commit targets the older/invalid slot.
- [x] Partial program is rejected through CRC validation.
- [x] Generation wrap from `0xFFFFFFFF` to `1` is covered.
- [x] Invalid state/progress/state-specific fields are rejected.

## Automated checks

Run:

```bash
make phase3-check
```

Expected final line:

```text
Secure Delta OTA Phase 3 metadata/boot-decision check: PASS
```

## Hardware checks

1. Run `make erase-metadata`, then flash the combined image.
2. Confirm the first reset initializes slot A and then boots the application.
3. Reset repeatedly; the application must still boot and generation must not
   increment on read-only boots.
4. Inspect `0x0800F800`: magic must be `0x424D4554`, generation `1`, version `1`.
5. Corrupt one word in slot A while a valid slot B exists; bootloader must select B.
6. Set a valid later-phase state and confirm the nine-pulse recovery indication.

These checks have been confirmed on the real Blue Pill, including selection
of a newer valid B slot, fallback to A after corrupting B's CRC, and successful
return to the application with `VTOR=0x08006000`.

## Metadata inspection

```bash
make dump-metadata
# or: python3 tools/inspect_metadata.py dist/metadata-pages.bin
```
