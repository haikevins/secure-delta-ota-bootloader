# Hardware-in-the-Loop Results

## Result

Deterministic physical fault matrix: **PASS — 9/9 scenarios**.

Machine-checkable evidence summary:

```text
HIL_EVIDENCE=PASS 9/9 PASS
rollback-reset
active_version=1
pending_version=0
boot_attempts=0
last_error=0x0008B003
app=v1
```

Target stack:

```text
STM32F103C8T6
ESP32 gateway
W25Q external SPI NOR
ST-Link SWD
USART1 OTA link
```

The suite validates the signed release → network gateway → UART receiver → external staging → bootloader verification/reconstruction → install/trial/rollback chain.

## Scenario matrix

| Scenario | Injected condition | Expected invariant | Result |
|---|---|---|---|
| `control-secure-delta` | no fault | signed delta installs and confirms v2 | PASS |
| `patch-reset` | reset during delta reconstruction | replay reconstruction safely; confirm v2 | PASS |
| `backup-reset` | reset at backup checkpoint boundary | resume backup safely; confirm v2 | PASS |
| `install-midpage-reset` | reset during internal-flash install | replay torn page safely; confirm v2 | PASS |
| `mqtt-drop-after-accepted` | MQTTS disconnect after command accepted | reconnect; no duplicate accepted update | PASS |
| `https-truncate` | artifact body truncated | gateway fails; STM32 never installs partial artifact | PASS |
| `tampered-signature` | ECDSA signature modified | STM32 rejects; v1 remains exact | PASS |
| `rollback-control` | signed but unhealthy candidate | failed trial policy restores exact v1 | PASS |
| `rollback-reset` | reset during rollback restore | resume restore and recover exact v1 | PASS |

## Reset/recovery invariants

### Reconstruction reset

Expected:

```text
persistent state identifies reconstruction recovery
active application remains safe
reconstruction is replayed
target is verified before install
candidate confirms v2
```

### Backup reset

Expected:

```text
4 KiB backup checkpoint is authoritative
already verified sectors are retained
backup continues safely
install proceeds only after backup is valid
```

### Install reset

Expected:

```text
first potentially torn 1 KiB internal page is not trusted
page is erased/replayed
install resumes from trusted external image
whole installed image is verified
```

### Rollback reset

Expected:

```text
rollback checkpoint survives reset
restore resumes from validated backup
final 38 KiB application region equals original v1
rollback diagnostic remains available
```

## Network fault invariants

### MQTTS disconnect after acceptance

The broker connection is deliberately dropped after the update command has already been accepted.

Required evidence:

```text
gateway reconnects
accepted command is not duplicated on STM32
OTA continues
STM32 metadata generation matches control update
```

### Truncated HTTPS

The HTTPS server deliberately serves only an artifact prefix.

Required evidence:

```text
gateway reports failure
no INSTALL is sent for the incomplete artifact
STM32 active application remains byte-for-byte unchanged
```

## Security-negative invariant

For the tampered-signature test, the artifact remains transport-valid enough to reach STM32. Only the ECDSA signature is modified.

This ensures the rejection occurs at the firmware-authentication boundary rather than being accidentally caught by a transport CRC first.

Required result:

```text
signature rejection on STM32
active v1 preserved
security diagnostic persisted
```

## Trial and rollback policy

The unhealthy candidate is signed and otherwise valid. It is therefore allowed to install and enter trial boot.

The application intentionally fails health confirmation. After the configured unconfirmed-attempt limit, the bootloader restores the validated backup.

The control rollback test verifies normal restoration. The rollback-reset test adds a reset during restore and verifies recovery remains exact.

## Fault witnesses

The suite records metadata-generation relationships as proof that intended fault hooks executed:

```text
patch-reset generation       = control generation + 1
backup-reset generation      = control generation + 1
install-midpage generation   = control generation + 1
MQTT disconnect generation   = control generation
rollback-reset generation    = rollback-control generation + 1
```

These checks prevent a false PASS where a reset hook was configured but never reached.

## Final hardware markers

Recorded final state:

```text
STM32_METADATA label=rollback-reset generation=74 state=0 active_version=1 pending_version=0 boot_attempts=0 last_error=0x0008B003
STM32_VERIFY=PASS label=rollback-reset app=v1
SCENARIO=PASS id=rollback-reset generation=74 gateway=EXPECTED_FAIL
FAULT_WITNESS=PASS id=patch-reset
FAULT_WITNESS=PASS id=backup-reset
FAULT_WITNESS=PASS id=install-midpage-reset
MQTT_ISOLATION=PASS
ROLLBACK_FAULT_WITNESS=PASS
HIL hardware test: PASS (9 deterministic scenarios)
```

Interpretation:

```text
state=0             IDLE
active_version=1    confirmed original application restored
pending_version=0   no pending candidate
boot_attempts=0     trial bookkeeping cleared
last_error=0x0008B003 rollback diagnostic intentionally retained
```

## Key custody during HIL

The test runner creates a temporary P-256 private key only for the HIL execution.

On exit, including failure paths, it restores:

- the original unprovisioned trusted-key header;
- the original runtime configuration;
- the original test CA file.

It also removes temporary HIL build directories and ESP-IDF generated output.

Recorded result:

```text
no HIL signing private key persisted
```

## Reproduce

```bash
source ~/esp/esp-idf/export.sh

export STM32_OPENOCD=/usr/bin/openocd
export STM32_OPENOCD_SCRIPTS=/usr/share/openocd/scripts

make hil-test \
  TOOLCHAIN=gcc \
  ESP32_PORT=/dev/ttyUSB0 \
  WIFI_SSID="your-ssid" \
  WIFI_PASSWORD="your-password" \
  SDOTA_HOST_IP=<PC_LAN_IP>
```

Optional ports default to:

```text
HTTPS_PORT=8443
MQTT_PORT=8883
```

See `README.md` for the complete hardware/environment variable reference.
