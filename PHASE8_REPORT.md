# Phase 8 Report — Trial Boot and Rollback

Phase 8 introduces a verified rollback copy and defers version promotion until
the new application proves that it can boot and reach its health-confirm path.

Implemented path:

```text
PC full OTA
  -> ARTIFACT_READY
  -> BACKING_UP previous 38 KiB app to W25Q
  -> INSTALLING candidate
  -> VERIFYING_INSTALL
  -> TRIAL_BOOT
       -> healthy confirm -> CONFIRMED -> IDLE/new active version
       -> 3 IWDG resets   -> ROLLBACK -> IDLE/previous active version
```

Power-loss behavior from Phase 7 remains in force:

- UART receive checkpoint: 4 KiB.
- Active-image backup checkpoint: 4 KiB W25Q sector.
- Candidate install checkpoint: 1 KiB internal Flash page.
- Rollback checkpoint: 1 KiB internal Flash page.

Trial behavior:

- `active_version` is unchanged while the new image is only a trial.
- `boot_attempts` is persisted before every trial jump.
- Maximum unconfirmed attempts: 3.
- IWDG is started immediately before the trial jump.
- A healthy application confirms after the normal runtime subsystems start.
- `CMD_CONFIRM (0x22)` is also supported while in trial state.
- Invalid trial vectors or the attempt limit select rollback.

Rollback behavior:

- complete 38 KiB application region is restored from W25Q Backup;
- backup header and image CRC are validated before restore;
- each internal 1 KiB page is erase/program/verified before checkpoint commit;
- final full-region CRC and application vector are verified;
- trial-limit rollback after 3 attempts leaves diagnostic `0x0008B003`.

Measured Clang/LLD validation build:

```text
Bootloader:          11240 bytes / 24 KiB
Application v1:      11876 bytes / 38 KiB
Healthy candidate v2: 11888 bytes / 38 KiB
Bad candidate v3:    11756 bytes / 38 KiB
Combined Phase 8:    36452 bytes
```

Validation completed in the packaging environment:

- Phase 0 regression: PASS
- Phase 1 regression: PASS
- Phase 2 regression: PASS
- Phase 3 regression: PASS
- Phase 4 regression: PASS
- Phase 5 regression: PASS
- Phase 6 regression: PASS
- Phase 7 regression: PASS
- Phase 8 host C trial/rollback tests: PASS
- Phase 8 backup/trial/rollback model: PASS
- Phase 8 build/link/symbol checks: PASS

Physical Phase-8 hardware validation remains to be run on the board:

```bash
make phase8-hw-test PORT=/dev/ttyUSB0
```

Security boundary is unchanged: CRC32 is integrity only. Cryptographic
authenticity remains a later secure-container phase.
