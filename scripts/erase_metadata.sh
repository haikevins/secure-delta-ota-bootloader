#!/usr/bin/env bash
set -euo pipefail
OPENOCD_BIN="${OPENOCD:-openocd}"
INTERFACE_CFG="${OPENOCD_INTERFACE:-interface/stlink.cfg}"
TARGET_CFG="${OPENOCD_TARGET:-target/stm32f1x.cfg}"

command -v "$OPENOCD_BIN" >/dev/null 2>&1 || {
    echo "OpenOCD not found: $OPENOCD_BIN" >&2
    exit 1
}
"$OPENOCD_BIN" -f "$INTERFACE_CFG" -f "$TARGET_CFG" \
    -c "init; reset halt; flash erase_address 0x0800F800 0x800; reset run; shutdown"
