# Phase 4 OpenOCD Connection Fix

Observed failure:

```text
Error: init mode failed (unable to connect to the target)
```

This occurs at OpenOCD `init`, before the Phase 4 firmware is programmed, so
it is not an SPI NOR JEDEC/program/erase failure.

The hardware test runner now tries:

1. normal SWD;
2. SWD at 200 kHz;
3. connect-under-reset at 200 kHz.

The third strategy requires ST-LINK NRST connected to STM32 NRST. If all
three strategies fail, the script prints a connection-only OpenOCD command
and stops without reporting an SPI Flash driver failure.
