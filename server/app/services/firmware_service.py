from __future__ import annotations

import json
import secrets

from server.app.models.release import (
    ReleaseArtifact,
    ReleaseManifest,
)


class FirmwareSelectionError(RuntimeError):
    pass


def select_artifact(
    manifest: ReleaseManifest,
    current_version: int,
    *,
    prefer_delta: bool = True,
) -> ReleaseArtifact:
    if current_version < 0:
        raise FirmwareSelectionError("current_version must be non-negative")
    if current_version >= manifest.target_version:
        raise FirmwareSelectionError(
            "device is already current or newer than this release"
        )

    if prefer_delta and current_version > 0:
        for artifact in manifest.artifacts:
            if (
                artifact.kind == "delta"
                and artifact.base_version == current_version
            ):
                return artifact

    for artifact in manifest.artifacts:
        if artifact.kind == "full":
            return artifact

    raise FirmwareSelectionError("release does not contain a full artifact")


def build_mqtt_command(
    manifest: ReleaseManifest,
    current_version: int,
    *,
    update_id: int | None = None,
    prefer_delta: bool = True,
) -> tuple[ReleaseArtifact, dict[str, object]]:
    artifact = select_artifact(
        manifest,
        current_version,
        prefer_delta=prefer_delta,
    )

    if update_id is None:
        update_id = 0
        while update_id == 0:
            update_id = secrets.randbits(32)

    if not 1 <= update_id <= 0xFFFFFFFF:
        raise FirmwareSelectionError("update_id must fit non-zero uint32")

    command: dict[str, object] = {
        "schema": 1,
        "cmd": "update",
        "update_id": update_id,
        "target_version": manifest.target_version,
        "size": artifact.size,
        "crc32": artifact.crc32,
        "url": artifact.url,
    }
    return artifact, command


def encode_command(command: dict[str, object]) -> bytes:
    return (
        json.dumps(
            command,
            sort_keys=True,
            separators=(",", ":"),
        )
        + "\n"
    ).encode("utf-8")
