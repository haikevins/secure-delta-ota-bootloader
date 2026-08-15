# Phase 5 — UART Protocol with PC Python

Status: **implemented; physical UART validation pending**

## Scope

Phase 5 implements the frozen protocol between a PC and the STM32 application:

- USART1 PA9/PA10, 115200 8-N-1;
- RX interrupt + 512-byte ring buffer;
- COBS framing + `0x00` delimiter;
- little-endian field serialization;
- IEEE CRC32 packet checking;
- 256-byte maximum DATA payload;
- HELLO, QUERY, STATUS, START, DATA, FINISH, ABORT, RESUME;
- ACK/NACK, sequence and offset checking;
- exact duplicate DATA retry handling;
- DATA write/verify to external Incoming Artifact partition;
- FINISH CRC32 over the stored artifact;
- PC client with 1500 ms timeout and five retries.

Phase 5 stops at `UPDATE_ARTIFACT_READY`. It does not install firmware.

## Wiring

```text
STM32 PA9  TX  -> USB-UART RX
STM32 PA10 RX  <- USB-UART TX
STM32 GND      -> USB-UART GND
```

Use 3.3 V UART logic.

## Retry behavior

DATA is acknowledged only after external Flash write/verify. An exact duplicate
of the most recently accepted DATA packet is compared against stored bytes and
ACKed again. FINISH is also idempotent so a lost FINISH ACK can be retried.

## External Flash erase policy

START does not erase all 128 KiB at once. The receiver lazily erases each 4 KiB
Incoming sector when the first DATA packet reaches it. This keeps a single
request inside the UART timeout budget.

## Resume boundary

RESUME works while the same application instance remains running. Phase 5 does
not persist transfer checkpoints across reset/power loss; that remains the
power-loss recovery phase.

## PC use

```bash
python3 -m pip install -r tools/requirements-phase5.txt
python3 tools/uart_ota_sender.py --port /dev/ttyUSB0 hello
python3 tools/uart_ota_sender.py --port /dev/ttyUSB0 query
python3 tools/uart_ota_sender.py --port /dev/ttyUSB0 send file.bin --update-id 0x12345678
```

Hardware diagnostic:

```bash
make phase5-hw-test PORT=/dev/ttyUSB0
```

It tests HELLO, bad-CRC NACK, wrong-sequence NACK, DATA write/verify, duplicate
DATA retry, FINISH, FINISH retry and final QUERY. The default test transfers
1024 bytes and destructively uses only `0x002000-0x002FFF`.
