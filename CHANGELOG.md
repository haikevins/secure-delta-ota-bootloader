# Changelog

All notable project-level changes are documented in this file.

This changelog tracks the **source repository and portfolio release line**. Device firmware packages produced by the protected release pipeline use a separate `fw-v<version>` identifier because firmware versions are monotonic application build numbers rather than semantic versions of this repository.

## [1.0.0]

First stable source/portfolio release of the Secure Delta OTA Bootloader project.

### Added

- STM32F103 bootloader with application handoff, redundant persistent metadata, trial boot, and automatic rollback.
- External W25Q SPI NOR staging with resumable receive, reconstruction, backup, install, and recovery state.
- Full and JojoDiff-based delta OTA paths with exact-base validation and signed full-image fallback.
- SDOT secure firmware containers with SHA-256 image binding, ECDSA P-256 signatures, product/hardware policy, anti-downgrade checks, and key-ID selection.
- ESP32 gateway with MQTTS orchestration, HTTPS artifact download, persistent cache, and framed UART delivery to STM32.
- Immutable signed firmware-release tooling, manifest verification, release-key authorization checks, and protected CI release workflow.
- Host/unit/integration checks, warning-clean firmware builds, reproducible benchmark automation, and deterministic physical HIL fault injection.
- Architecture, protocol, security, recovery, verification, and portfolio documentation.

### Verified

- Integrated project validation gate: PASS.
- Warning-clean STM32 firmware build: PASS.
- Deterministic hardware fault matrix: **9/9 PASS**.
- Checked-in benchmark reference: **89.77% signed-delta size savings** versus the signed full-image artifact.
- Tampered-signature rejection, interrupted download handling, reset-safe reconstruction/backup/install, trial failure, and rollback recovery are covered by the documented validation evidence.

### Repository quality

- Fixed the firmware-release workflow syntax so GitHub Actions can parse the workflow correctly.
- Added contribution guidance, security reporting guidance, issue/PR templates, and stricter STM32 toolchain detection.
- Regenerated repository integrity manifests and made the root README a concise project landing page with links to detailed engineering evidence.

See [`docs/project-release-v1.0.0.md`](docs/project-release-v1.0.0.md) for the release scope, evidence snapshot, security posture, and publication notes.
