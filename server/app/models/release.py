from __future__ import annotations

from dataclasses import dataclass
from typing import Any


@dataclass(frozen=True)
class ReleaseArtifact:
    kind: str
    filename: str
    url: str
    base_version: int
    target_version: int
    size: int
    crc32: int
    sha256: str
    payload_size: int
    target_size: int
    key_id: int


@dataclass(frozen=True)
class ReleaseManifest:
    raw: dict[str, Any]
    release_id: str
    channel: str
    product_id: int
    hardware_revision: int
    target_version: int
    key_id: int
    public_key_sha256: str
    artifacts: tuple[ReleaseArtifact, ...]
