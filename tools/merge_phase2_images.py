#!/usr/bin/env python3
"""Create a flashable Phase 2 image containing bootloader and application."""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

FLASH_BASE = 0x08000000
APPLICATION_ADDRESS = 0x08006000
BOOT_METADATA_ADDRESS = 0x0800FC00
ERASED_BYTE = 0xFF


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(64 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--bootloader",
        type=Path,
        default=root / "node-stm32f103/bootloader/out/bootloader.bin",
    )
    parser.add_argument(
        "--application",
        type=Path,
        default=root / "node-stm32f103/application/out/application.bin",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=root / "dist/secure-delta-ota-phase2.bin",
    )
    args = parser.parse_args()

    bootloader = args.bootloader.read_bytes()
    application = args.application.read_bytes()
    application_offset = APPLICATION_ADDRESS - FLASH_BASE
    metadata_offset = BOOT_METADATA_ADDRESS - FLASH_BASE

    if len(bootloader) > application_offset:
        raise SystemExit("bootloader exceeds its 24 KiB partition")
    if len(application) > metadata_offset - application_offset:
        raise SystemExit("application exceeds its 39 KiB partition")

    image_size = application_offset + len(application)
    merged = bytearray([ERASED_BYTE]) * image_size
    merged[0 : len(bootloader)] = bootloader
    merged[application_offset : application_offset + len(application)] = application

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(merged)

    manifest = args.output.with_suffix(".txt")
    manifest.write_text(
        "\n".join(
            [
                "Secure Delta OTA Phase 2 combined image",
                f"flash_base=0x{FLASH_BASE:08X}",
                f"bootloader_size={len(bootloader)}",
                f"application_address=0x{APPLICATION_ADDRESS:08X}",
                f"application_size={len(application)}",
                f"combined_size={len(merged)}",
                f"bootloader_sha256={sha256(args.bootloader)}",
                f"application_sha256={sha256(args.application)}",
                f"combined_sha256={sha256(args.output)}",
                "metadata_page=left_erased",
                "",
            ]
        ),
        encoding="utf-8",
    )

    print(f"Created {args.output.relative_to(root)} ({len(merged)} bytes)")
    print(f"Created {manifest.relative_to(root)}")


if __name__ == "__main__":
    main()
