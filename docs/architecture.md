# System Architecture Specification

Status: **Frozen through external-flash integration**

## 1. Objective

Build a secure and recoverable firmware update system for STM32F103C8T6 that supports both full-image OTA and delta OTA while keeping networking and certificate handling outside the constrained STM32 node.

## 2. System boundary

```text
Developer / CI
  | build old.bin and new.bin
  | create full container and delta container
  | hash and sign release artifacts
  v
Firmware Server
  | MQTT: update notification and status orchestration
  | HTTPS: manifest and artifact transfer
  v
ESP32 Gateway
  | validates download completion
  | caches artifact
  | selects delta or full artifact
  | sends artifact over custom UART OTA protocol
  v
STM32F103 Application
  | receives UART packets
  | validates packet CRC and ordering
  | stores artifact in external SPI NOR Flash
  | records resumable download progress
  | requests reset for installation
  v
STM32F103 Bootloader
  | validates container and product policy
  | validates base image for delta updates
  | reconstructs target image into external Flash
  | backs up current application
  | installs and verifies target image
  | performs trial boot and rollback
```

## 3. Responsibilities

### 3.1 Firmware Server / CI

- Build application binary reproducibly.
- Produce a full container.
- Produce delta patch from an explicitly selected base firmware.
- Calculate payload and image hashes.
- Sign header plus payload using a private key that is never stored on ESP32 or STM32.
- Publish manifest and artifacts through HTTPS.
- Publish update availability through MQTT.

### 3.2 ESP32 Gateway

- Connect to Wi-Fi.
- Subscribe to MQTT update notifications.
- Download manifest and artifact over HTTPS with certificate validation.
- Cache the complete artifact before STM32 transfer.
- Query STM32 identity, version, capability and resume offset.
- Use custom UART OTA commands with ACK/NACK, retry and resume.
- Publish update progress and failure information.
- Never possess the firmware signing private key.

### 3.3 STM32 Application

- Run the product application.
- Receive custom UART OTA frames.
- Decode COBS frames and validate packet CRC32.
- Enforce `update_id`, sequence and offset rules.
- Store the artifact only in the external `Incoming Artifact` partition.
- Update redundant download metadata.
- Mark `ARTIFACT_READY` only after complete artifact CRC validation.
- Request a reset; it must not erase or overwrite the active application.
- Confirm a successfully trial-booted application after self-test.

### 3.4 STM32 Bootloader

- Remain immutable during normal OTA operation.
- Select boot, install, recovery or rollback behavior from metadata.
- Validate application vector table and address range before jumping.
- Validate secure container fields, version policy, hashes and signature.
- For delta images, verify base version and base SHA-256 before patching.
- Reconstruct the target image into external Flash, never directly over the active application.
- Verify reconstructed target SHA-256.
- Back up the active application and verify backup before erasing it.
- Install page-by-page using idempotent progress metadata.
- Verify installed application.
- Start `TRIAL_BOOT`; rollback after failed confirmation policy.

### 3.5 External W25Q32 Flash

Stores:

- redundant metadata mirror;
- downloaded full/delta container;
- reconstructed target firmware;
- backup of the previous internal application;
- update logs and reserved future data.

## 4. Architectural rules

1. Transport is independent from patch/install logic.
2. ESP32 may pre-check an artifact, but STM32 bootloader remains the final trust boundary.
3. Application code never writes bootloader Flash.
4. Application code never installs firmware directly into active internal Flash.
5. A delta update is accepted only when base version and base hash match.
6. Any delta-specific error may request a full-image fallback.
7. Installation begins only after the incoming artifact and reconstructed target image have been validated.
8. Backup is retained until the trial application is confirmed.
9. Metadata updates use two copies, CRC32 and monotonic generation number.
10. initial scaffold excludes bootloader self-update, AES encryption, LoRa, CAN/UDS and multi-node scheduling.

## 5. Initial hardware assignments

| Function | Peripheral / pin |
|---|---|
| ESP32 ↔ STM32 OTA | USART1 |
| STM32 TX | PA9 |
| STM32 RX | PA10 |
| SPI NOR clock | PA5 / SPI1_SCK |
| SPI NOR MISO | PA6 / SPI1_MISO |
| SPI NOR MOSI | PA7 / SPI1_MOSI |
| SPI NOR CS | PB0 GPIO output |
| Debug | Shared USART for OTA/debug or optional USART2 for dedicated debug |

## 6. Update selection policy

Use delta only when all conditions are true:

- node supports the delta container format;
- `current_version == base_version`;
- current application SHA-256 equals `base_image_sha256`;
- delta artifact is smaller than the full artifact by a configured threshold;
- reconstructed image fits application limits.

Otherwise, use the full container.

## 7. initial scaffold acceptance

- Hardware, memory and protocol decisions are explicitly documented.
- No internal or external partition overlaps.
- UART packet fields and state rules are frozen at protocol version 1.
- Boot/update transitions and power-loss behavior are documented.
- Security trust boundary and out-of-scope items are documented.


## signed secure container trust-boundary update

The bootloader now enforces firmware authenticity using signed SDOT containers.
ESP32, HTTPS, MQTT and UART remain transport/orchestration layers and do not
become firmware trust anchors.

For both full and delta updates:

```text
transport integrity != firmware authenticity
```

CRC32 remains useful for transfer/storage corruption detection. SHA-256 binds
the exact base/target image bytes, and ECDSA P-256 authenticates the signed
container.

Unsigned legacy installation is disabled by default.
