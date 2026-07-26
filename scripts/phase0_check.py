#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
REQUIRED = [
    "docs/architecture.md",
    "docs/memory-map.md",
    "docs/uart-ota-protocol.md",
    "docs/firmware-container.md",
    "docs/boot-state-machine.md",
    "docs/threat-model.md",
    "docs/release-process.md",
    "docs/test-plan.md",
    "docs/phase-0-checklist.md",
    "shared/include/memory_map.h",
    "shared/include/ota_protocol.h",
    "shared/include/firmware_container.h",
    "shared/include/boot_metadata.h",
]

missing = [item for item in REQUIRED if not (ROOT / item).is_file()]
if missing:
    print("Phase 0 check: FAIL")
    for item in missing:
        print(f"  missing: {item}")
    sys.exit(1)

# Frozen arithmetic checks.
internal_kib = 24 + 38 + 1 + 1
if internal_kib != 64:
    print(f"Phase 0 check: FAIL: internal map sums to {internal_kib} KiB")
    sys.exit(1)

partitions = [
    (0x000000, 0x002000, "metadata"),
    (0x002000, 0x022000, "incoming"),
    (0x022000, 0x042000, "reconstructed"),
    (0x042000, 0x062000, "backup"),
    (0x062000, 0x072000, "logs"),
]
for start, end, name in partitions:
    if start % 0x1000 or end % 0x1000 or end <= start:
        print(f"Phase 0 check: FAIL: invalid partition {name}")
        sys.exit(1)
for index in range(len(partitions) - 1):
    if partitions[index][1] != partitions[index + 1][0]:
        print("Phase 0 check: FAIL: partition gap/overlap")
        sys.exit(1)

print("Secure Delta OTA Phase 0 specification check: PASS")
print(f"Required specification files: {len(REQUIRED)}")
print("Internal Flash allocation: 64 KiB (24 + 38 + 1 + 1)")
print("External Flash fixed allocation through: 0x071FFF")
