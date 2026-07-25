#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OPENOCD_BIN="${OPENOCD:-openocd}"
INTERFACE_CFG="${OPENOCD_INTERFACE:-interface/stlink.cfg}"
TARGET_CFG="${OPENOCD_TARGET:-target/stm32f1x.cfg}"

command -v "$OPENOCD_BIN" >/dev/null 2>&1 || {
    echo "OpenOCD not found: $OPENOCD_BIN" >&2
    exit 1
}

# A blank TOOLCHAIN environment variable must not disable Makefile auto-detection.
MAKE_ARGS=()
if [[ -n "${TOOLCHAIN:-}" ]]; then
    case "$TOOLCHAIN" in
        gcc|clang) MAKE_ARGS+=("TOOLCHAIN=$TOOLCHAIN") ;;
        *)
            echo "Unsupported TOOLCHAIN='$TOOLCHAIN'; choose gcc or clang" >&2
            exit 1
            ;;
    esac
else
    unset TOOLCHAIN
fi

make -C "$ROOT_DIR" application "${MAKE_ARGS[@]}"
IMAGE="$ROOT_DIR/node-stm32f103/application/out/application.hex"
"$OPENOCD_BIN" -f "$INTERFACE_CFG" -f "$TARGET_CFG" \
    -c "program $IMAGE verify reset exit"
