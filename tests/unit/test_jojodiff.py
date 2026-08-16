#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
from pathlib import Path
import random
import sys

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools/jojodiff_patch.py"

spec = importlib.util.spec_from_file_location("jojodiff_patch", TOOL)
assert spec and spec.loader
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)

ESC = module.ESC


def test_length_boundaries() -> None:
    values = [
        1,
        2,
        252,
        253,
        254,
        508,
        509,
        510,
        0xFFFF,
        0x10000,
        0x12345678,
    ]

    for value in values:
        encoded = module.encode_length(value)
        decoded, offset = module.decode_length(encoded, 0)
        assert decoded == value
        assert offset == len(encoded)


def test_escape_roundtrip() -> None:
    source = bytes(range(256)) * 2
    target = (
        source[:40]
        + bytes([ESC, module.MOD, ESC, module.INS, 0x00, 0xFF])
        + source[46:]
        + bytes([ESC, ESC, 1, 2, 3])
    )

    patch, _ = module.generate_patch(source, target)
    assert bytes((ESC, ESC)) in patch
    assert module.apply_patch(source, patch) == target


def mutate(rng: random.Random, source: bytes) -> bytes:
    data = bytearray(source)

    for _ in range(rng.randint(1, 8)):
        op = rng.choice(("replace", "insert", "delete"))

        if op == "replace" and data:
            start = rng.randrange(len(data))
            length = rng.randint(1, min(32, len(data) - start))
            for i in range(start, start + length):
                data[i] = rng.randrange(256)

        elif op == "insert":
            start = rng.randrange(len(data) + 1)
            length = rng.randint(1, 32)
            payload = bytes(rng.randrange(256) for _ in range(length))
            data[start:start] = payload

        elif op == "delete" and data:
            start = rng.randrange(len(data))
            length = rng.randint(1, min(32, len(data) - start))
            del data[start:start + length]

    return bytes(data)


def test_deterministic_random_roundtrip() -> None:
    rng = random.Random(0x1200D17A)

    for case in range(80):
        size = rng.randint(1, 2048)
        source = bytes(rng.randrange(256) for _ in range(size))
        target = mutate(rng, source)

        patch1, stats1 = module.generate_patch(source, target)
        patch2, stats2 = module.generate_patch(source, target)

        assert patch1 == patch2, case
        assert stats1 == stats2, case
        assert module.apply_patch(source, patch1) == target, case

        info = module.inspect_patch(len(source), patch1)
        assert info["target_size"] == len(target), case
        assert info["BKT"] == 0, case


def test_gap_shapes() -> None:
    vectors = [
        (b"abcdefgh", b"abcdWXYZ"),       # equal-size MOD
        (b"abcdefgh", b"abcdXY"),         # MOD + DEL
        (b"abcdef", b"abcdWXYZ"),         # MOD + INS
        (b"abcdefgh", b"abcdefghTAIL"),   # terminal INS
        (b"abcdTAIL", b"abcd"),           # terminal DEL
        (b"", b"hello"),                  # pure INS
        (b"hello", b""),                  # pure DEL
    ]

    for source, target in vectors:
        patch, _ = module.generate_patch(source, target)
        assert module.apply_patch(source, patch) == target


def main() -> int:
    test_length_boundaries()
    test_escape_roundtrip()
    test_deterministic_random_roundtrip()
    test_gap_shapes()
    print(
        "delta generation JojoDiff-compatible generator property tests: PASS "
        "(lengths, escapes, MOD/INS/DEL/EQL, 80 deterministic mutations)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
