# Phase 5 Report — UART Protocol with PC Python

Phase 5 implements protocol v1 on the STM32 application and a matching Python
PC client. USART1 uses PA9/PA10 at 115200 8-N-1 with interrupt-driven RX.

The protocol path is:

```text
PC Python -> COBS/CRC32 UART -> STM32 parser -> OTA receiver
          -> W25Q Incoming Artifact -> ACK/NACK
```

DATA is stored and verified in external Flash. FINISH computes the CRC32 of the
complete received artifact and enters the runtime state `UPDATE_ARTIFACT_READY`.

Phase 5 deliberately does not parse/install the firmware container. Resume is
same-boot only; persistent recovery remains a later phase.

Validation:

```bash
make phase5-check
python3 -m pip install -r tools/requirements-phase5.txt
make flash-combined
make phase5-hw-test PORT=/dev/ttyUSB0
```

Measured application binary: **8156 bytes** (Clang/LLD validation build).


## Hardware result

Confirmed on the real board:

```text
HELLO: PASS
CRC NACK: PASS
Sequence NACK: PASS
Duplicate DATA ACK: PASS
FINISH retry ACK: PASS
Phase 5 UART protocol hardware self-test: PASS
```
