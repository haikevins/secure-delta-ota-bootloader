#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

if [[ -n "${TOOLCHAIN:-}" ]]; then
    make phase2-check "TOOLCHAIN=${TOOLCHAIN}"
else
    make phase2-check
fi
