#!/usr/bin/env python3
"""Deterministic JojoDiff-compatible patch generator/applicator.

delta generation emits the subset of the JojoDiff stream understood by JANPatch:
EQL, MOD, INS and DEL. Matching blocks are monotonic, so BKT is not needed.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
from difflib import SequenceMatcher
from pathlib import Path
import sys

ESC = 0xA7
MOD = 0xA6
INS = 0xA5
DEL = 0xA4
EQL = 0xA3
BKT = 0xA2

OPS = {MOD, INS, DEL, EQL, BKT}


class PatchFormatError(ValueError):
    pass


@dataclass(frozen=True)
class PatchStats:
    equal_bytes: int = 0
    modified_bytes: int = 0
    inserted_bytes: int = 0
    deleted_bytes: int = 0
    operations: int = 0


def encode_length(length: int) -> bytes:
    if length <= 0:
        raise ValueError("length must be positive")
    if length <= 252:
        return bytes((length - 1,))
    if length <= 508:
        return bytes((252, length - 253))
    if length <= 0xFFFF:
        return bytes((253, (length >> 8) & 0xFF, length & 0xFF))
    if length <= 0xFFFFFFFF:
        return bytes((254,)) + length.to_bytes(4, "big")
    raise ValueError("length exceeds JojoDiff 32-bit range")


def decode_length(patch: bytes, offset: int) -> tuple[int, int]:
    if offset >= len(patch):
        raise PatchFormatError("truncated length")

    prefix = patch[offset]
    offset += 1

    if prefix <= 251:
        return prefix + 1, offset

    if prefix == 252:
        if offset >= len(patch):
            raise PatchFormatError("truncated 2-byte length")
        return 253 + patch[offset], offset + 1

    if prefix == 253:
        if offset + 2 > len(patch):
            raise PatchFormatError("truncated 16-bit length")
        return int.from_bytes(patch[offset:offset + 2], "big"), offset + 2

    if prefix == 254:
        if offset + 4 > len(patch):
            raise PatchFormatError("truncated 32-bit length")
        return int.from_bytes(patch[offset:offset + 4], "big"), offset + 4

    raise PatchFormatError("invalid JojoDiff length prefix 0xFF")


def escape_data(data: bytes) -> bytes:
    # A literal ESC is encoded ESC ESC so it cannot be confused with an op.
    return data.replace(bytes((ESC,)), bytes((ESC, ESC)))


def emit_op(opcode: int, payload: bytes = b"") -> bytes:
    if opcode not in OPS:
        raise ValueError(f"invalid opcode 0x{opcode:02X}")
    return bytes((ESC, opcode)) + payload


def emit_length_op(opcode: int, length: int) -> bytes:
    if opcode not in (EQL, DEL, BKT):
        raise ValueError("length operation must be EQL, DEL or BKT")
    return emit_op(opcode, encode_length(length))


def emit_data_op(opcode: int, data: bytes) -> bytes:
    if opcode not in (MOD, INS):
        raise ValueError("data operation must be MOD or INS")
    if not data:
        return b""
    return emit_op(opcode, escape_data(data))


def _emit_gap(source: bytes,
              target: bytes,
              source_start: int,
              source_end: int,
              target_start: int,
              target_end: int) -> tuple[bytes, PatchStats]:
    old = source[source_start:source_end]
    new = target[target_start:target_end]

    common = min(len(old), len(new))
    output = bytearray()
    modified = inserted = deleted = operations = 0

    if common:
        replacement = new[:common]
        output += emit_data_op(MOD, replacement)
        modified += common
        operations += 1

    if len(new) > common:
        insertion = new[common:]
        output += emit_data_op(INS, insertion)
        inserted += len(insertion)
        operations += 1

    if len(old) > common:
        count = len(old) - common
        output += emit_length_op(DEL, count)
        deleted += count
        operations += 1

    return bytes(output), PatchStats(
        modified_bytes=modified,
        inserted_bytes=inserted,
        deleted_bytes=deleted,
        operations=operations,
    )


def generate_patch(source: bytes,
                   target: bytes,
                   *,
                   autojunk: bool = False) -> tuple[bytes, PatchStats]:
    matcher = SequenceMatcher(
        None,
        source,
        target,
        autojunk=autojunk,
    )

    patch = bytearray()
    equal = modified = inserted = deleted = operations = 0
    source_pos = 0
    target_pos = 0

    for block in matcher.get_matching_blocks():
        if block.a < source_pos or block.b < target_pos:
            raise AssertionError("SequenceMatcher produced non-monotonic block")

        if block.a != source_pos or block.b != target_pos:
            gap, gap_stats = _emit_gap(
                source,
                target,
                source_pos,
                block.a,
                target_pos,
                block.b,
            )
            patch += gap
            modified += gap_stats.modified_bytes
            inserted += gap_stats.inserted_bytes
            deleted += gap_stats.deleted_bytes
            operations += gap_stats.operations

        if block.size:
            patch += emit_length_op(EQL, block.size)
            equal += block.size
            operations += 1

        source_pos = block.a + block.size
        target_pos = block.b + block.size

    return bytes(patch), PatchStats(
        equal_bytes=equal,
        modified_bytes=modified,
        inserted_bytes=inserted,
        deleted_bytes=deleted,
        operations=operations,
    )


def _copy_literal(patch: bytes,
                  offset: int) -> tuple[bytes, int]:
    out = bytearray()

    while offset < len(patch):
        value = patch[offset]
        offset += 1

        if value != ESC:
            out.append(value)
            continue

        if offset >= len(patch):
            # JANPatch treats EOF after ESC as end of the data operation.
            break

        next_value = patch[offset]
        offset += 1

        if next_value == ESC:
            out.append(ESC)
            continue

        if next_value in OPS:
            return bytes(out), offset - 2

        out.extend((ESC, next_value))

    return bytes(out), offset


def apply_patch(source: bytes, patch: bytes) -> bytes:
    source_pos = 0
    patch_pos = 0
    target = bytearray()

    while patch_pos < len(patch):
        first = patch[patch_pos]

        if first == ESC:
            if patch_pos + 1 >= len(patch):
                raise PatchFormatError("truncated operation escape")
            opcode = patch[patch_pos + 1]
            patch_pos += 2
        else:
            # JojoDiff >=0.8.5 permits MOD as the default operation.
            opcode = MOD

        if opcode == EQL:
            length, patch_pos = decode_length(patch, patch_pos)
            if source_pos + length > len(source):
                raise PatchFormatError("EQL reads beyond source")
            target += source[source_pos:source_pos + length]
            source_pos += length

        elif opcode == DEL:
            length, patch_pos = decode_length(patch, patch_pos)
            if source_pos + length > len(source):
                raise PatchFormatError("DEL reads beyond source")
            source_pos += length

        elif opcode == BKT:
            length, patch_pos = decode_length(patch, patch_pos)
            if length > source_pos:
                raise PatchFormatError("BKT seeks before source start")
            source_pos -= length

        elif opcode in (MOD, INS):
            data_start = patch_pos
            data, patch_pos = _copy_literal(patch, data_start)
            target += data
            if opcode == MOD:
                if source_pos + len(data) > len(source):
                    raise PatchFormatError("MOD reads beyond source")
                source_pos += len(data)

        else:
            # Matches JANPatch's compatibility behavior: an unknown escaped
            # pair at operation boundary belongs to MOD literal data.
            patch_pos -= 2
            data, patch_pos = _copy_literal(patch, patch_pos)
            target += data
            if source_pos + len(data) > len(source):
                raise PatchFormatError("default MOD reads beyond source")
            source_pos += len(data)

    return bytes(target)


def inspect_patch(source_size: int, patch: bytes) -> dict[str, int]:
    # Apply against zero-filled source only for stream accounting. Literal
    # bytes and source contents do not affect operation counts.
    source = bytes(source_size)
    source_pos = patch_pos = target_size = 0
    counts = {"EQL": 0, "MOD": 0, "INS": 0, "DEL": 0, "BKT": 0}

    while patch_pos < len(patch):
        if patch[patch_pos] == ESC:
            if patch_pos + 1 >= len(patch):
                raise PatchFormatError("truncated operation escape")
            opcode = patch[patch_pos + 1]
            patch_pos += 2
        else:
            opcode = MOD

        if opcode in (EQL, DEL, BKT):
            length, patch_pos = decode_length(patch, patch_pos)
            if opcode == EQL:
                counts["EQL"] += 1
                source_pos += length
                target_size += length
            elif opcode == DEL:
                counts["DEL"] += 1
                source_pos += length
            else:
                counts["BKT"] += 1
                source_pos -= length
        elif opcode in (MOD, INS):
            data, patch_pos = _copy_literal(patch, patch_pos)
            key = "MOD" if opcode == MOD else "INS"
            counts[key] += 1
            target_size += len(data)
            if opcode == MOD:
                source_pos += len(data)
        else:
            patch_pos -= 2
            data, patch_pos = _copy_literal(patch, patch_pos)
            counts["MOD"] += 1
            source_pos += len(data)
            target_size += len(data)

        if source_pos < 0 or source_pos > source_size:
            raise PatchFormatError("patch source cursor outside source")

    counts["source_consumed_or_position"] = source_pos
    counts["target_size"] = target_size
    return counts


def _cmd_generate(args: argparse.Namespace) -> int:
    source = args.source.read_bytes()
    target = args.target.read_bytes()
    patch, stats = generate_patch(source, target)
    reconstructed = apply_patch(source, patch)

    if reconstructed != target:
        raise SystemExit("internal round-trip verification failed")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(patch)

    ratio = (len(patch) / len(target)) if target else 0.0
    savings = 1.0 - ratio if target else 0.0
    print(f"source={len(source)}")
    print(f"target={len(target)}")
    print(f"patch={len(patch)}")
    print(f"patch_ratio={ratio:.6f}")
    print(f"savings={savings:.2%}")
    print(f"equal_bytes={stats.equal_bytes}")
    print(f"modified_bytes={stats.modified_bytes}")
    print(f"inserted_bytes={stats.inserted_bytes}")
    print(f"deleted_bytes={stats.deleted_bytes}")
    print(f"operations={stats.operations}")
    print("roundtrip=PASS")
    return 0


def _cmd_apply(args: argparse.Namespace) -> int:
    source = args.source.read_bytes()
    patch = args.patch.read_bytes()
    output = apply_patch(source, patch)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(output)
    print(f"output={len(output)}")
    return 0


def _cmd_inspect(args: argparse.Namespace) -> int:
    patch = args.patch.read_bytes()
    for key, value in inspect_patch(args.source_size, patch).items():
        print(f"{key}={value}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate/apply JojoDiff-compatible patch streams"
    )
    sub = parser.add_subparsers(dest="command", required=True)

    generate = sub.add_parser("generate")
    generate.add_argument("source", type=Path)
    generate.add_argument("target", type=Path)
    generate.add_argument("output", type=Path)
    generate.set_defaults(func=_cmd_generate)

    apply = sub.add_parser("apply")
    apply.add_argument("source", type=Path)
    apply.add_argument("patch", type=Path)
    apply.add_argument("output", type=Path)
    apply.set_defaults(func=_cmd_apply)

    inspect = sub.add_parser("inspect")
    inspect.add_argument("patch", type=Path)
    inspect.add_argument("--source-size", type=int, required=True)
    inspect.set_defaults(func=_cmd_inspect)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
