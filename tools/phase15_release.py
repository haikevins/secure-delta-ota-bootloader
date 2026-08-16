#!/usr/bin/env python3
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from urllib.parse import urlparse
import zlib

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from server.app.models.sdot import parse_sdot  # noqa: E402
from server.app.services.manifest_service import (  # noqa: E402
    verify_release_directory,
)
from server.app.services.signing_service import (  # noqa: E402
    public_key_pem,
    public_key_sha256,
    sign_bytes_der,
    validate_private_key_path,
)
from tools.jojodiff_patch import apply_patch, generate_patch  # noqa: E402
from tools.phase14_secure_container import (  # noqa: E402
    IMAGE_DELTA,
    IMAGE_FULL,
    pack_header,
    sign_ecdsa_p256,
    verify_openssl,
)

PRODUCT_ID = 0x00001001
HARDWARE_REVISION = 1
MAX_APPLICATION_SIZE = 38 * 1024
MAX_ARTIFACT_SIZE = 128 * 1024
SDOT_SIGNATURE_SIZE = 64


class ReleaseError(RuntimeError):
    pass


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def validate_https_base_url(url: str) -> str:
    value = url.rstrip("/")
    parsed = urlparse(value)
    if parsed.scheme != "https" or not parsed.netloc:
        raise ReleaseError("base URL must use https://")
    if parsed.username is not None or parsed.password is not None:
        raise ReleaseError("base URL must not contain credentials")
    if parsed.query or parsed.fragment:
        raise ReleaseError("base URL must not contain query/fragment")
    return value


def validate_application(image: bytes, label: str) -> None:
    if not 8 <= len(image) <= MAX_APPLICATION_SIZE:
        raise ReleaseError(
            f"{label} size must be 8..{MAX_APPLICATION_SIZE} bytes"
        )

    msp = int.from_bytes(image[0:4], "little")
    reset = int.from_bytes(image[4:8], "little")
    if not 0x20000000 <= msp <= 0x20005000 or (msp & 7):
        raise ReleaseError(f"{label} invalid MSP 0x{msp:08X}")
    if (reset & 1) == 0:
        raise ReleaseError(f"{label} reset handler is not Thumb")
    reset_code = reset & ~1
    if not 0x08006000 <= reset_code < 0x08006000 + len(image):
        raise ReleaseError(
            f"{label} reset handler outside image: 0x{reset:08X}"
        )


def created_utc(explicit: str | None) -> str:
    if explicit:
        try:
            parsed = datetime.fromisoformat(explicit.replace("Z", "+00:00"))
        except ValueError as exc:
            raise ReleaseError("--created-utc must be ISO-8601") from exc
        if parsed.tzinfo is None:
            raise ReleaseError("--created-utc must include timezone")
        return parsed.astimezone(timezone.utc).isoformat().replace("+00:00", "Z")

    epoch = os.environ.get("SOURCE_DATE_EPOCH", "").strip()
    if epoch:
        try:
            value = int(epoch)
        except ValueError as exc:
            raise ReleaseError("SOURCE_DATE_EPOCH must be integer seconds") from exc
        return (
            datetime.fromtimestamp(value, timezone.utc)
            .isoformat()
            .replace("+00:00", "Z")
        )

    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def source_revision(explicit: str | None) -> str:
    if explicit:
        return explicit

    result = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    if result.returncode == 0:
        value = result.stdout.strip()
        if value:
            return value
    return "unknown"


def build_sdot(
    *,
    image_type: int,
    payload: bytes,
    target: bytes,
    base: bytes | None,
    base_version: int,
    target_version: int,
    key: Path,
    key_id: int,
) -> bytes:
    header = pack_header(
        image_type=image_type,
        base_version=base_version,
        target_version=target_version,
        payload=payload,
        target=target,
        base=base,
        key_id=key_id,
    )
    signed = header + payload
    signature = sign_ecdsa_p256(key, signed)
    if len(signature) != SDOT_SIGNATURE_SIZE:
        raise ReleaseError("P-256 signature did not encode to 64-byte raw r||s")
    verify_openssl(key, signed, signature)

    container = signed + signature
    if len(container) > MAX_ARTIFACT_SIZE:
        raise ReleaseError("SDOT exceeds 128 KiB STM32 incoming partition")
    return container


def artifact_record(
    kind: str,
    filename: str,
    url: str,
    data: bytes,
) -> dict[str, object]:
    info = parse_sdot(data)
    if info.kind != kind:
        raise ReleaseError("internal SDOT kind mismatch")

    return {
        "kind": kind,
        "filename": filename,
        "url": url,
        "base_version": info.base_version,
        "target_version": info.target_version,
        "size": len(data),
        "crc32": crc32(data),
        "sha256": sha256(data),
        "payload_size": info.payload_size,
        "target_size": info.target_size,
        "key_id": info.key_id,
    }


def write_checksums(directory: Path) -> None:
    lines: list[str] = []
    for path in sorted(directory.iterdir(), key=lambda p: p.name):
        if not path.is_file() or path.name == "checksums.txt":
            continue
        lines.append(f"{sha256(path.read_bytes())}  {path.name}")
    (directory / "checksums.txt").write_text(
        "\n".join(lines) + "\n",
        encoding="utf-8",
    )


def default_notes(
    release_id: str,
    target_version: int,
    base_version: int,
    delta_included: bool,
    channel: str,
) -> str:
    delta_text = (
        f"- Signed delta from v{base_version}: included\n"
        if delta_included
        else "- Signed delta: not included\n"
    )
    return (
        f"# Firmware {release_id}\n\n"
        f"- Channel: `{channel}`\n"
        f"- Target application version: `v{target_version}`\n"
        f"{delta_text}"
        "- Container: `SDOT v1 + SCX1`\n"
        "- Authentication: `ECDSA P-256 / SHA-256`\n"
        "- Device anti-downgrade remains enforced by the STM32 bootloader.\n"
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Create an immutable Phase-15 signed firmware release"
    )
    parser.add_argument("--target", type=Path, required=True)
    parser.add_argument("--target-version", type=int, required=True)
    parser.add_argument("--base", type=Path)
    parser.add_argument("--base-version", type=int, default=0)
    parser.add_argument("--key", type=Path, required=True)
    parser.add_argument("--key-id", type=lambda x: int(x, 0), required=True)
    parser.add_argument("--base-url", required=True)
    parser.add_argument("--channel", choices=["stable", "beta", "dev"], default="stable")
    parser.add_argument("--release-id")
    parser.add_argument("--output-root", type=Path, default=ROOT / "dist/releases")
    parser.add_argument("--notes", type=Path)
    parser.add_argument("--source-revision")
    parser.add_argument("--created-utc")
    parser.add_argument("--min-delta-savings-percent", type=float, default=20.0)
    args = parser.parse_args()

    try:
        if args.target_version <= 0:
            raise ReleaseError("target version must be positive")
        if not 1 <= args.key_id <= 0xFFFFFFFF:
            raise ReleaseError("key-id must fit non-zero uint32")
        if not 0.0 <= args.min_delta_savings_percent < 100.0:
            raise ReleaseError("delta savings threshold must be in [0,100)")

        target_path = args.target.resolve()
        if not target_path.is_file():
            raise ReleaseError(f"target application not found: {target_path}")
        target = target_path.read_bytes()
        validate_application(target, "target")

        base: bytes | None = None
        if args.base is not None:
            base_path = args.base.resolve()
            if not base_path.is_file():
                raise ReleaseError(f"base application not found: {base_path}")
            base = base_path.read_bytes()
            validate_application(base, "base")
            if args.base_version <= 0 or args.target_version <= args.base_version:
                raise ReleaseError(
                    "delta requires 0 < base-version < target-version"
                )
        elif args.base_version != 0:
            raise ReleaseError("--base-version requires --base")

        key = validate_private_key_path(args.key, ROOT)
        base_url = validate_https_base_url(args.base_url)
        release_id = args.release_id or f"fw-v{args.target_version}"

        if (
            not release_id
            or any(c not in "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-" for c in release_id)
            or not release_id[0].isalnum()
            or len(release_id) > 64
        ):
            raise ReleaseError("release-id syntax invalid")

        output_root = args.output_root.resolve()
        output_root.mkdir(parents=True, exist_ok=True)
        final_dir = output_root / release_id
        if final_dir.exists():
            raise ReleaseError(
                f"release already exists and is immutable: {final_dir}"
            )

        with tempfile.TemporaryDirectory(
            prefix=f".{release_id}.",
            dir=output_root,
        ) as td:
            staging = Path(td)

            full_name = f"application-v{args.target_version}.full.sdot"
            full = build_sdot(
                image_type=IMAGE_FULL,
                payload=target,
                target=target,
                base=None,
                base_version=0,
                target_version=args.target_version,
                key=key,
                key_id=args.key_id,
            )
            (staging / full_name).write_bytes(full)

            binary_name = f"application-v{args.target_version}.bin"
            (staging / binary_name).write_bytes(target)

            artifacts: list[dict[str, object]] = [
                artifact_record(
                    "full",
                    full_name,
                    f"{base_url}/releases/{release_id}/{full_name}",
                    full,
                )
            ]

            delta_included = False
            delta_savings = None
            patch_stats: dict[str, int] | None = None

            if base is not None:
                patch, stats = generate_patch(base, target, autojunk=False)
                if apply_patch(base, patch) != target:
                    raise ReleaseError("delta generator round-trip failed")

                delta_name = (
                    f"application-v{args.base_version}-to-v"
                    f"{args.target_version}.delta.sdot"
                )
                delta = build_sdot(
                    image_type=IMAGE_DELTA,
                    payload=patch,
                    target=target,
                    base=base,
                    base_version=args.base_version,
                    target_version=args.target_version,
                    key=key,
                    key_id=args.key_id,
                )

                delta_savings = (1.0 - (len(delta) / len(full))) * 100.0
                if delta_savings >= args.min_delta_savings_percent:
                    (staging / delta_name).write_bytes(delta)
                    artifacts.insert(
                        0,
                        artifact_record(
                            "delta",
                            delta_name,
                            f"{base_url}/releases/{release_id}/{delta_name}",
                            delta,
                        ),
                    )
                    delta_included = True

                patch_stats = {
                    "patch_size": len(patch),
                    "equal_bytes": stats.equal_bytes,
                    "modified_bytes": stats.modified_bytes,
                    "inserted_bytes": stats.inserted_bytes,
                    "deleted_bytes": stats.deleted_bytes,
                    "operations": stats.operations,
                }

            public_pem = public_key_pem(key)
            (staging / "signing-public.pem").write_bytes(public_pem)

            notes = (
                args.notes.read_text(encoding="utf-8")
                if args.notes is not None
                else default_notes(
                    release_id,
                    args.target_version,
                    args.base_version,
                    delta_included,
                    args.channel,
                )
            )
            (staging / "release-notes.md").write_text(
                notes.rstrip() + "\n",
                encoding="utf-8",
            )

            manifest: dict[str, object] = {
                "schema": 1,
                "release_id": release_id,
                "channel": args.channel,
                "created_utc": created_utc(args.created_utc),
                "source_revision": source_revision(args.source_revision),
                "product_id": PRODUCT_ID,
                "hardware_revision": HARDWARE_REVISION,
                "target_version": args.target_version,
                "signing_key": {
                    "key_id": args.key_id,
                    "algorithm": "ECDSA-P256-SHA256",
                    "public_key_sha256": public_key_sha256(key),
                },
                "target": {
                    "filename": binary_name,
                    "size": len(target),
                    "crc32": crc32(target),
                    "sha256": sha256(target),
                },
                "artifacts": artifacts,
                "delta_policy": {
                    "requested": base is not None,
                    "base_version": args.base_version,
                    "minimum_savings_percent": args.min_delta_savings_percent,
                    "actual_savings_percent": (
                        round(delta_savings, 4)
                        if delta_savings is not None
                        else None
                    ),
                    "included": delta_included,
                    "patch_stats": patch_stats,
                },
                "release_notes": "release-notes.md",
            }

            manifest_bytes = (
                json.dumps(
                    manifest,
                    indent=2,
                    sort_keys=True,
                    separators=(",", ": "),
                )
                + "\n"
            ).encode("utf-8")
            (staging / "manifest.json").write_bytes(manifest_bytes)
            (staging / "manifest.json.sig").write_bytes(
                sign_bytes_der(key, manifest_bytes)
            )

            write_checksums(staging)

            # Atomic publication inside one filesystem.
            os.replace(staging, final_dir)

        verified = verify_release_directory(final_dir)

        print(
            f"PHASE15_RELEASE=PASS release={verified.release_id} "
            f"target=v{verified.target_version} artifacts={len(verified.artifacts)}"
        )
        print(f"PHASE15_KEY_ID=0x{verified.key_id:08X}")
        print(
            "PHASE15_ARTIFACTS="
            + ",".join(a.kind for a in verified.artifacts)
        )
        if delta_savings is not None:
            print(
                f"PHASE15_DELTA_SAVINGS={delta_savings:.2f}% "
                f"threshold={args.min_delta_savings_percent:.2f}% "
                f"included={'yes' if delta_included else 'no'}"
            )
        print(f"PHASE15_RELEASE_DIR={final_dir}")
        print("PHASE15_PRIVATE_KEY_PERSISTED=no")
        return 0

    except (ReleaseError, RuntimeError, ValueError) as exc:
        print(f"Phase 15 release: FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
