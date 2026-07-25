# Custom UART OTA Protocol Version 1

Status: **Frozen for Phase 0**

## 1. Physical UART configuration

| Parameter | Value |
|---|---|
| STM32 peripheral | USART1 |
| STM32 TX/RX | PA9 / PA10 |
| Initial baud rate | 115200 |
| Data bits | 8 |
| Parity | None |
| Stop bits | 1 |
| Flow control | None |
| Duplex | Full duplex |

A future protocol capability may negotiate another baud rate, but version 1 interoperability always begins at 115200 8-N-1.

## 2. Framing

Each raw packet is COBS encoded and terminated by one `0x00` delimiter:

```text
COBS(raw_packet) 00
```

Rules:

- `0x00` never occurs inside an encoded frame.
- A decoder error discards the current frame and waits for the next delimiter.
- Inter-byte timeout may clear a partial frame, but timeout is not used as the primary framing mechanism.
- Maximum encoded frame buffer is 320 bytes for the initial 256-byte payload design.

## 3. Byte order and packing

- Multi-byte integers are little-endian.
- Wire structures are serialized field-by-field; C compiler struct padding must never be transmitted.
- CRC fields are transmitted little-endian.

## 4. Common packet format

```text
Offset  Size  Field
0       2     magic = 0xA55A
2       1     protocol_version = 1
3       1     command
4       4     update_id
8       4     offset
12      2     sequence
14      2     payload_length (0..256)
16      N     payload
16+N    4     packet_crc32
```

CRC32 covers bytes from `magic` through the final payload byte. It does not cover the CRC field itself or the COBS encoding.

```c
#define OTA_PACKET_MAGIC       0xA55AU
#define OTA_PROTOCOL_VERSION   1U
#define OTA_MAX_PAYLOAD_SIZE   256U
#define OTA_MAX_RETRY_COUNT    5U
#define OTA_RESPONSE_TIMEOUT_MS 1500U
```

## 5. Commands

| Code | Command | Direction | Purpose |
|---:|---|---|---|
| `0x01` | HELLO | Gateway → Node | Establish protocol and request capabilities |
| `0x02` | QUERY | Gateway → Node | Request current version, state and resume offset |
| `0x10` | START | Gateway → Node | Begin or restart an artifact transfer |
| `0x11` | DATA | Gateway → Node | Transfer artifact bytes |
| `0x12` | FINISH | Gateway → Node | Declare transfer complete and request artifact validation |
| `0x13` | ABORT | Gateway → Node | Abort current update safely |
| `0x14` | RESUME | Gateway → Node | Resume an existing matching update |
| `0x20` | INSTALL | Gateway → Node | Request reset into bootloader for installation |
| `0x21` | STATUS | Both | Request or publish detailed state |
| `0x22` | CONFIRM | Application internal API / optional wire diagnostic | Confirm trial image |
| `0x70` | ACK | Node → Gateway | Successful request response |
| `0x71` | NACK | Node → Gateway | Rejected request with status code |

## 6. Status codes

| Code | Symbol | Meaning |
|---:|---|---|
| `0x00` | OK | Request accepted |
| `0x01` | INVALID_PACKET | Malformed packet or unsupported length |
| `0x02` | INVALID_STATE | Command not allowed in current state |
| `0x03` | WRONG_SEQUENCE | Sequence does not match next expected packet |
| `0x04` | WRONG_OFFSET | Offset does not match next expected byte |
| `0x05` | PACKET_CRC_ERROR | Packet CRC32 mismatch |
| `0x06` | STORAGE_ERROR | External Flash read/write/erase failure |
| `0x07` | IMAGE_TOO_LARGE | Artifact or target exceeds configured partition |
| `0x08` | UPDATE_ID_MISMATCH | Command references another update |
| `0x09` | BASE_MISMATCH | Delta base version/hash does not match |
| `0x0A` | CONTAINER_ERROR | Container header or artifact validation failure |
| `0x0B` | SIGNATURE_ERROR | Signature authentication failure |
| `0x0C` | VERSION_REJECTED | Downgrade or unsupported target version |
| `0x0D` | BUSY | Node is processing another operation |
| `0x0E` | RETRY_LATER | Temporary condition; retry is permitted |
| `0x0F` | INTERNAL_ERROR | Non-recoverable internal failure |

## 7. HELLO and QUERY payloads

### HELLO request

No payload is required.

### HELLO/QUERY response payload

```text
protocol_version        u8
bootloader_version      u32
application_version     u32
product_id              u32
hardware_revision       u32
capability_flags        u32
update_state            u8
last_status              u8
reserved                 u16
active_update_id         u32
next_expected_offset     u32
expected_artifact_size   u32
```

Capability flags include:

```c
#define OTA_CAP_FULL_IMAGE       (1UL << 0)
#define OTA_CAP_DELTA_IMAGE      (1UL << 1)
#define OTA_CAP_RESUME           (1UL << 2)
#define OTA_CAP_SIGNATURE_VERIFY (1UL << 3)
#define OTA_CAP_ROLLBACK         (1UL << 4)
```

## 8. START payload

```text
artifact_type            u8    (1 = full, 2 = delta)
reserved                  u8
container_format_version u16
base_version             u32
target_version           u32
artifact_size            u32
artifact_crc32            u32
container_header_size    u32
```

START acceptance checks:

- state is IDLE, FAILED or resumable RECEIVING;
- artifact size fits incoming partition;
- container format is supported;
- target version policy is preliminarily acceptable;
- a new `update_id` invalidates prior incomplete incoming data only after metadata commit.

An accepted START returns `ACK` with `next_expected_offset = 0`.

## 9. DATA rules

- `payload_length` is 1–256 bytes.
- `offset` must equal the node's `next_expected_offset`.
- `sequence` starts at zero and increments modulo 65536.
- Node validates packet CRC before storage.
- Node writes bytes to `EXT_INCOMING_ADDRESS + offset`.
- Node verifies the written bytes or uses a storage policy that guarantees verification before ACK.
- ACK is sent only after metadata can recover the acknowledged progress.

### Duplicate handling

If a DATA packet repeats the last successfully acknowledged offset and its bytes match stored data, the node returns the same ACK without writing a second time. A conflicting duplicate returns NACK.

## 10. ACK/NACK payload

```text
status                    u8
update_state              u8
acknowledged_sequence     u16
next_expected_offset      u32
received_size             u32
expected_size             u32
last_error_detail         u32
```

## 11. FINISH behavior

The node checks:

1. `received_size == expected_artifact_size`;
2. complete incoming artifact CRC32;
3. minimum container header parsing and bounds;
4. metadata commit to `ARTIFACT_READY`.

FINISH does not perform installation. Installation occurs only after INSTALL or local policy resets into bootloader.

## 12. RESUME behavior

Gateway sends matching `update_id` and expected artifact identity. Node responds with current `next_expected_offset`. If update identity does not match, node returns `UPDATE_ID_MISMATCH` and requires START.

## 13. Retry policy

- Gateway response timeout: 1500 ms initially.
- Maximum retries per packet: 5.
- Retry the exact same packet before querying status.
- After retry exhaustion, issue QUERY/STATUS.
- Never advance offset without an ACK that contains the expected next offset.

## 14. Protocol state constraints

| State | Allowed major commands |
|---|---|
| IDLE | HELLO, QUERY, START |
| RECEIVING | HELLO, QUERY, DATA, FINISH, ABORT, RESUME |
| ARTIFACT_READY | HELLO, QUERY, INSTALL, ABORT |
| Bootloader processing states | QUERY/STATUS only; DATA rejected |
| TRIAL_BOOT | QUERY/STATUS; confirmation is local application action |
| FAILED | HELLO, QUERY, START, ABORT |

## 15. Security note

UART packet CRC protects transfer integrity, not authenticity. Secure authenticity is provided later by bootloader signature verification of the complete container.
