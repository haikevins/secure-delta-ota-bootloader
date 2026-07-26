#!/usr/bin/env python3
"""Decode a 2 KiB STM32 internal metadata-page dump."""
from __future__ import annotations

import argparse
import struct
import zlib
from dataclasses import dataclass
from pathlib import Path

MAGIC = 0x424D4554
FORMAT_VERSION = 1
RECORD_FORMAT = "<13I"
RECORD_SIZE = struct.calcsize(RECORD_FORMAT)
PAGE_SIZE = 1024
STATE_NAMES = [
    "IDLE", "RECEIVING", "ARTIFACT_READY", "VERIFYING_CONTAINER",
    "VERIFYING_BASE", "PATCHING", "IMAGE_READY", "BACKING_UP",
    "INSTALLING", "VERIFYING_INSTALL", "TRIAL_BOOT", "CONFIRMED",
    "ROLLBACK", "FAILED",
]


@dataclass
class Record:
    slot: str
    fields: tuple[int, ...]
    valid: bool
    reason: str

    @property
    def generation(self) -> int:
        return self.fields[1]


def decode(slot: str, page: bytes) -> Record:
    if len(page) != PAGE_SIZE:
        raise ValueError(f"slot {slot}: expected {PAGE_SIZE} bytes")
    fields = struct.unpack(RECORD_FORMAT, page[:RECORD_SIZE])
    magic, generation, version, state, *_rest, stored_crc = fields
    computed_crc = zlib.crc32(page[: RECORD_SIZE - 4]) & 0xFFFFFFFF

    if magic != MAGIC:
        return Record(slot, fields, False, f"bad magic 0x{magic:08X}")
    if generation == 0:
        return Record(slot, fields, False, "generation zero")
    if version != FORMAT_VERSION:
        return Record(slot, fields, False, f"unsupported version {version}")
    if state >= len(STATE_NAMES):
        return Record(slot, fields, False, f"invalid state {state}")
    if stored_crc != computed_crc:
        return Record(
            slot, fields, False,
            f"CRC mismatch stored=0x{stored_crc:08X} computed=0x{computed_crc:08X}",
        )
    return Record(slot, fields, True, "valid")


def is_newer(candidate: int, reference: int) -> bool:
    difference = (candidate - reference) & 0xFFFFFFFF
    return difference != 0 and difference < 0x80000000


def print_record(record: Record) -> None:
    f = record.fields
    state = STATE_NAMES[f[3]] if f[3] < len(STATE_NAMES) else f"UNKNOWN({f[3]})"
    print(f"Metadata {record.slot}: {'VALID' if record.valid else 'INVALID'}")
    print(f"  reason          : {record.reason}")
    print(f"  magic           : 0x{f[0]:08X}")
    print(f"  generation      : {f[1]}")
    print(f"  format_version  : {f[2]}")
    print(f"  state           : {state}")
    print(f"  active_version  : {f[4]}")
    print(f"  pending_version : {f[5]}")
    print(f"  update_id       : 0x{f[6]:08X}")
    print(f"  received/total  : {f[7]}/{f[8]}")
    print(f"  copy_offset     : {f[9]}")
    print(f"  boot_attempts   : {f[10]}")
    print(f"  last_error      : 0x{f[11]:08X}")
    print(f"  crc32           : 0x{f[12]:08X}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("dump", type=Path, help="2 KiB dump starting at 0x0800F800")
    args = parser.parse_args()
    data = args.dump.read_bytes()
    if len(data) != 2 * PAGE_SIZE:
        raise SystemExit(f"expected exactly 2048 bytes, got {len(data)}")

    a = decode("A @ 0x0800F800", data[:PAGE_SIZE])
    b = decode("B @ 0x0800FC00", data[PAGE_SIZE:])
    print_record(a)
    print_record(b)

    if a.valid and b.valid:
        selected = b if is_newer(b.generation, a.generation) else a
        print(f"Selected: Metadata {selected.slot}")
    elif a.valid:
        print("Selected: Metadata A")
    elif b.valid:
        print("Selected: Metadata B")
    else:
        print("Selected: none; bootloader will create defaults")


if __name__ == "__main__":
    main()
