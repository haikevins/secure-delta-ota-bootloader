from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
from typing import Any
from urllib.parse import urlparse
import zlib

from server.app.models.release import (
    ReleaseArtifact,
    ReleaseManifest,
)
from server.app.models.sdot import parse_sdot
from server.app.services.signing_service import verify_signature_der

_RELEASE_ID = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$")
_SHA256 = re.compile(r"^[0-9a-f]{64}$")
MAX_INCOMING_ARTIFACT = 128 * 1024


class ManifestError(RuntimeError):
    pass


def _int(value: Any, name: str, minimum: int = 0) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        raise ManifestError(f"{name} must be an integer >= {minimum}")
    return value


def _https_url(value: Any, name: str) -> str:
    if not isinstance(value, str):
        raise ManifestError(f"{name} must be a string")
    parsed = urlparse(value)
    if parsed.scheme != "https" or not parsed.netloc:
        raise ManifestError(f"{name} must use https://")
    if parsed.username is not None or parsed.password is not None:
        raise ManifestError(f"{name} must not contain URL credentials")
    return value


def _safe_filename(value: Any, name: str) -> str:
    if not isinstance(value, str) or not value:
        raise ManifestError(f"{name} must be a non-empty string")
    if value != Path(value).name or "/" in value or "\\" in value:
        raise ManifestError(f"{name} must be a plain filename")
    if value in {".", ".."}:
        raise ManifestError(f"{name} is invalid")
    return value


def load_manifest(path: Path) -> ReleaseManifest:
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ManifestError(f"cannot read manifest: {exc}") from exc

    if not isinstance(raw, dict):
        raise ManifestError("manifest root must be an object")
    if raw.get("schema") != 1:
        raise ManifestError("unsupported release manifest schema")

    release_id = raw.get("release_id")
    if not isinstance(release_id, str) or not _RELEASE_ID.fullmatch(release_id):
        raise ManifestError("release_id has invalid syntax")

    channel = raw.get("channel")
    if channel not in {"stable", "beta", "dev"}:
        raise ManifestError("channel must be stable, beta or dev")

    product_id = _int(raw.get("product_id"), "product_id", 1)
    hardware_revision = _int(
        raw.get("hardware_revision"), "hardware_revision", 1
    )
    target_version = _int(raw.get("target_version"), "target_version", 1)

    key = raw.get("signing_key")
    if not isinstance(key, dict):
        raise ManifestError("signing_key must be an object")
    key_id = _int(key.get("key_id"), "signing_key.key_id", 1)
    public_key_hash = key.get("public_key_sha256")
    if (not isinstance(public_key_hash, str) or
            not _SHA256.fullmatch(public_key_hash)):
        raise ManifestError("signing_key.public_key_sha256 must be SHA-256 hex")
    if key.get("algorithm") != "ECDSA-P256-SHA256":
        raise ManifestError("unsupported signing key algorithm")

    artifacts_raw = raw.get("artifacts")
    if not isinstance(artifacts_raw, list) or not artifacts_raw:
        raise ManifestError("artifacts must be a non-empty array")

    artifacts: list[ReleaseArtifact] = []
    kinds_seen: set[tuple[str, int]] = set()
    full_count = 0

    for index, item in enumerate(artifacts_raw):
        if not isinstance(item, dict):
            raise ManifestError(f"artifacts[{index}] must be an object")

        kind = item.get("kind")
        if kind not in {"full", "delta"}:
            raise ManifestError(f"artifacts[{index}].kind invalid")

        filename = _safe_filename(
            item.get("filename"),
            f"artifacts[{index}].filename",
        )
        url = _https_url(item.get("url"), f"artifacts[{index}].url")
        base_version = _int(
            item.get("base_version"),
            f"artifacts[{index}].base_version",
            0,
        )
        artifact_target = _int(
            item.get("target_version"),
            f"artifacts[{index}].target_version",
            1,
        )
        size = _int(item.get("size"), f"artifacts[{index}].size", 1)
        crc32 = _int(item.get("crc32"), f"artifacts[{index}].crc32", 0)
        sha256 = item.get("sha256")
        payload_size = _int(
            item.get("payload_size"),
            f"artifacts[{index}].payload_size",
            1,
        )
        target_size = _int(
            item.get("target_size"),
            f"artifacts[{index}].target_size",
            8,
        )
        artifact_key_id = _int(
            item.get("key_id"),
            f"artifacts[{index}].key_id",
            1,
        )

        if size > MAX_INCOMING_ARTIFACT:
            raise ManifestError(
                f"artifacts[{index}] exceeds 128 KiB STM32 incoming partition"
            )
        if crc32 > 0xFFFFFFFF:
            raise ManifestError(f"artifacts[{index}].crc32 exceeds uint32")
        if not isinstance(sha256, str) or not _SHA256.fullmatch(sha256):
            raise ManifestError(f"artifacts[{index}].sha256 invalid")
        if artifact_target != target_version:
            raise ManifestError(f"artifacts[{index}] target version mismatch")
        if artifact_key_id != key_id:
            raise ManifestError(f"artifacts[{index}] key_id mismatch")

        if kind == "full":
            full_count += 1
            if base_version != 0:
                raise ManifestError("full artifact must have base_version=0")
        else:
            if base_version <= 0 or base_version >= target_version:
                raise ManifestError("delta artifact base version invalid")

        key_tuple = (kind, base_version)
        if key_tuple in kinds_seen:
            raise ManifestError("duplicate release artifact selector")
        kinds_seen.add(key_tuple)

        artifacts.append(
            ReleaseArtifact(
                kind=kind,
                filename=filename,
                url=url,
                base_version=base_version,
                target_version=artifact_target,
                size=size,
                crc32=crc32,
                sha256=sha256,
                payload_size=payload_size,
                target_size=target_size,
                key_id=artifact_key_id,
            )
        )

    if full_count != 1:
        raise ManifestError("release must contain exactly one full artifact")

    return ReleaseManifest(
        raw=raw,
        release_id=release_id,
        channel=channel,
        product_id=product_id,
        hardware_revision=hardware_revision,
        target_version=target_version,
        key_id=key_id,
        public_key_sha256=public_key_hash,
        artifacts=tuple(artifacts),
    )


def verify_release_directory(
    release_dir: Path,
    trusted_public_key_sha256: str | None = None,
) -> ReleaseManifest:
    release_dir = release_dir.resolve()
    manifest_path = release_dir / "manifest.json"
    signature_path = release_dir / "manifest.json.sig"
    public_key_path = release_dir / "signing-public.pem"

    manifest = load_manifest(manifest_path)

    if release_dir.name != manifest.release_id:
        raise ManifestError("release directory name must equal release_id")

    if not signature_path.is_file() or not public_key_path.is_file():
        raise ManifestError("release manifest signature/public key missing")

    # Fingerprint the canonical public SubjectPublicKeyInfo DER.
    import subprocess
    result = subprocess.run(
        [
            "openssl", "pkey",
            "-pubin", "-in", str(public_key_path),
            "-pubout", "-outform", "DER",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if result.returncode != 0:
        raise ManifestError("release signing public key is invalid")
    public_hash = hashlib.sha256(result.stdout).hexdigest()
    if public_hash != manifest.public_key_sha256:
        raise ManifestError("release signing public key fingerprint mismatch")

    if trusted_public_key_sha256 is not None:
        trusted = trusted_public_key_sha256.strip().lower()
        if not _SHA256.fullmatch(trusted):
            raise ManifestError(
                "trusted release public-key SHA-256 must be 64 lowercase hex chars"
            )
        if public_hash != trusted:
            raise ManifestError(
                "release signing public key does not match pinned server trust anchor"
            )

    try:
        verify_signature_der(
            public_key_path,
            manifest_path.read_bytes(),
            signature_path.read_bytes(),
        )
    except Exception as exc:
        raise ManifestError(f"release manifest signature invalid: {exc}") from exc

    for artifact in manifest.artifacts:
        path = release_dir / artifact.filename
        if not path.is_file():
            raise ManifestError(f"artifact missing: {artifact.filename}")
        data = path.read_bytes()
        if len(data) != artifact.size:
            raise ManifestError(f"artifact size mismatch: {artifact.filename}")
        if (zlib.crc32(data) & 0xFFFFFFFF) != artifact.crc32:
            raise ManifestError(f"artifact CRC32 mismatch: {artifact.filename}")
        if hashlib.sha256(data).hexdigest() != artifact.sha256:
            raise ManifestError(f"artifact SHA-256 mismatch: {artifact.filename}")

        try:
            sdot = parse_sdot(data)
        except ValueError as exc:
            raise ManifestError(
                f"SDOT validation failed for {artifact.filename}: {exc}"
            ) from exc

        if (
            sdot.kind != artifact.kind
            or sdot.base_version != artifact.base_version
            or sdot.target_version != artifact.target_version
            or sdot.container_size != artifact.size
            or sdot.container_crc32 != artifact.crc32
            or sdot.container_sha256 != artifact.sha256
            or sdot.payload_size != artifact.payload_size
            or sdot.target_size != artifact.target_size
            or sdot.key_id != artifact.key_id
            or sdot.product_id != manifest.product_id
            or sdot.hardware_revision != manifest.hardware_revision
        ):
            raise ManifestError(
                f"SDOT metadata mismatch: {artifact.filename}"
            )

    return manifest
