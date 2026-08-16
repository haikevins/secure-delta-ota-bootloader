#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import re
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from server.app.services.firmware_service import (  # noqa: E402
    FirmwareSelectionError,
    build_mqtt_command,
    encode_command,
)
from server.app.services.https_service import serve_https  # noqa: E402
from server.app.services.manifest_service import (  # noqa: E402
    ManifestError,
    verify_release_directory,
)
from server.app.services.mqtt_service import (  # noqa: E402
    MqttPublishConfig,
    publish_qos1,
)


def parse_u32(value: str) -> int:
    number = int(value, 0)
    if not 0 <= number <= 0xFFFFFFFF:
        raise argparse.ArgumentTypeError("must fit uint32")
    return number


_SHA256_HEX = re.compile(r"^[0-9a-fA-F]{64}$")


def trusted_key_hash(args: argparse.Namespace, *, required: bool) -> str | None:
    value = getattr(args, "trusted_key_sha256", None)
    if value is None:
        value = os.environ.get("SDOTA_TRUSTED_KEY_SHA256")
    if value is not None:
        value = value.strip().lower()
    if not value:
        if required:
            raise ManifestError(
                "trusted release key pin required: use --trusted-key-sha256 "
                "or SDOTA_TRUSTED_KEY_SHA256"
            )
        return None
    if not _SHA256_HEX.fullmatch(value):
        raise ManifestError(
            "trusted release key SHA-256 must be exactly 64 hex characters"
        )
    return value


def release_dir(root: Path, release_id: str) -> Path:
    return root.resolve() / release_id


def command_for(args: argparse.Namespace):
    directory = release_dir(args.release_root, args.release_id)
    manifest = verify_release_directory(
        directory,
        trusted_public_key_sha256=trusted_key_hash(args, required=True),
    )
    artifact, command = build_mqtt_command(
        manifest,
        args.current_version,
        update_id=args.update_id,
        prefer_delta=not args.full_only,
    )
    return manifest, artifact, command


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Secure Delta OTA release server and MQTT publisher"
    )
    sub = parser.add_subparsers(dest="action", required=True)

    verify = sub.add_parser("verify")
    verify.add_argument("--release-root", type=Path, required=True)
    verify.add_argument("--release-id", required=True)
    verify.add_argument(
        "--trusted-key-sha256",
        help="optional pinned SHA-256 of trusted public SPKI DER",
    )

    serve = sub.add_parser("serve")
    serve.add_argument("--release-root", type=Path, required=True)
    serve.add_argument("--bind", default="0.0.0.0")
    serve.add_argument("--port", type=int, default=8443)
    serve.add_argument("--cert", type=Path, required=True)
    serve.add_argument("--key", type=Path, required=True)
    serve.add_argument(
        "--trusted-key-sha256",
        help="required production release-key pin; env fallback supported",
    )

    command = sub.add_parser("command")
    command.add_argument("--release-root", type=Path, required=True)
    command.add_argument("--release-id", required=True)
    command.add_argument("--current-version", type=parse_u32, required=True)
    command.add_argument("--update-id", type=parse_u32)
    command.add_argument("--device-id", default="bluepill-001")
    command.add_argument("--topic-base", default="sdota")
    command.add_argument("--full-only", action="store_true")
    command.add_argument(
        "--trusted-key-sha256",
        help="required release-key pin; env fallback supported",
    )

    publish = sub.add_parser("publish")
    publish.add_argument("--release-root", type=Path, required=True)
    publish.add_argument("--release-id", required=True)
    publish.add_argument("--current-version", type=parse_u32, required=True)
    publish.add_argument("--update-id", type=parse_u32)
    publish.add_argument("--device-id", required=True)
    publish.add_argument("--topic-base", default="sdota")
    publish.add_argument("--full-only", action="store_true")
    publish.add_argument("--broker-uri", required=True)
    publish.add_argument("--ca", required=True)
    publish.add_argument(
        "--trusted-key-sha256",
        help="required release-key pin; env fallback supported",
    )
    publish.add_argument(
        "--client-id",
        default="sdota-release-publisher",
    )
    publish.add_argument(
        "--username",
        default=os.environ.get("SDOTA_MQTT_USERNAME"),
    )
    publish.add_argument(
        "--password",
        default=os.environ.get("SDOTA_MQTT_PASSWORD"),
    )

    args = parser.parse_args()

    try:
        if args.action == "verify":
            manifest = verify_release_directory(
                release_dir(args.release_root, args.release_id),
                trusted_public_key_sha256=trusted_key_hash(
                    args,
                    required=False,
                ),
            )
            print(
                f"RELEASE_VERIFY=PASS "
                f"release={manifest.release_id} "
                f"target=v{manifest.target_version} "
                f"artifacts={len(manifest.artifacts)}"
            )
            return 0

        if args.action == "serve":
            # Production serving must pin the authorized release public key;
            # trusting the public key shipped inside a release would be
            # self-authentication rather than an authorization boundary.
            trusted_hash = trusted_key_hash(args, required=True)
            found = 0
            for directory in sorted(args.release_root.glob("*")):
                if not directory.is_dir():
                    continue
                verify_release_directory(
                    directory,
                    trusted_public_key_sha256=trusted_hash,
                )
                found += 1
            if found == 0:
                raise ManifestError("release root contains no verified releases")

            serve_https(
                args.release_root,
                args.bind,
                args.port,
                args.cert,
                args.key,
            )
            return 0

        manifest, artifact, payload = command_for(args)
        topic = f"{args.topic_base}/{args.device_id}/command"
        encoded = encode_command(payload)

        if args.action == "command":
            print(f"RELEASE_SELECTED={artifact.kind} file={artifact.filename}")
            print(f"RELEASE_TOPIC={topic}")
            print(encoded.decode("utf-8").rstrip())
            return 0

        publish_qos1(
            MqttPublishConfig(
                broker_uri=args.broker_uri,
                ca_file=args.ca,
                client_id=args.client_id,
                username=args.username,
                password=args.password,
            ),
            topic,
            encoded.rstrip(b"\n"),
        )
        print(
            f"MQTT_PUBLISH=PASS topic={topic} "
            f"release={manifest.release_id} "
            f"artifact={artifact.kind} target=v{manifest.target_version}"
        )
        print(encoded.decode("utf-8").rstrip())
        return 0

    except (ManifestError, FirmwareSelectionError, RuntimeError) as exc:
        print(f"Release server: FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
