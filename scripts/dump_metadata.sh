#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OPENOCD_BIN="${OPENOCD:-openocd}"
INTERFACE_CFG="${OPENOCD_INTERFACE:-interface/stlink.cfg}"
TARGET_CFG="${OPENOCD_TARGET:-target/stm32f1x.cfg}"
OUTPUT="${1:-$ROOT_DIR/dist/metadata-pages.bin}"

command -v "$OPENOCD_BIN" >/dev/null 2>&1 || {
    echo "OpenOCD not found: $OPENOCD_BIN" >&2
    exit 1
}
mkdir -p "$(dirname "$OUTPUT")"
"$OPENOCD_BIN" -f "$INTERFACE_CFG" -f "$TARGET_CFG" \
    -c "init; reset halt; dump_image $OUTPUT 0x0800F800 0x800; shutdown"
python3 "$ROOT_DIR/tools/inspect_metadata.py" "$OUTPUT"
