#!/usr/bin/env python3
"""Phase 14 host/build/security validation.

The checker provisions an ephemeral P-256 public key into the bootloader only
for the duration of the test. The matching private key lives in a temporary
directory and is deleted automatically. The repository trust-anchor header is
restored in finally even when a check fails.
"""
from __future__ import annotations

import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BOOT = ROOT / "node-stm32f103/bootloader"
APP = ROOT / "node-stm32f103/application"
TRUSTED_HEADER = BOOT / "include/phase14_trusted_key.h"

KEY_ID = 0x14000001
BOOT_MAX = 24 * 1024
APP_MAX = 38 * 1024

BASE = APP / "out-phase14-v1/application.bin"
TARGET = APP / "out-phase14-v2/application.bin"
V3 = APP / "out-phase14-v3/application.bin"


def fail(message: str) -> None:
    print(f"Phase 14 check: FAIL: {message}")
    raise SystemExit(1)


def run(
    command: list[str],
    *,
    cwd: Path = ROOT,
    timeout: int = 180,
) -> str:
    result = subprocess.run(
        command,
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env=os.environ.copy(),
        timeout=timeout,
        check=False,
    )
    print(result.stdout, end="")
    if result.returncode != 0:
        fail(
            f"command returned {result.returncode}: "
            + " ".join(command)
        )
    return result.stdout


def choose_toolchain() -> str:
    requested = os.environ.get("TOOLCHAIN", "").strip()
    if requested:
        return requested
    if shutil.which("arm-none-eabi-gcc"):
        return "gcc"
    if (
        shutil.which("clang")
        and shutil.which("ld.lld")
        and shutil.which("llvm-objcopy")
    ):
        return "clang"
    fail("no supported STM32 ARM toolchain found")
    raise AssertionError


def require_tool(name: str) -> str:
    found = shutil.which(name)
    if found is None:
        fail(f"required host tool not found: {name}")
    return found


def build_application(
    toolchain: str,
    version: int,
    build_dir: str,
    out_dir: str,
) -> None:
    flags = f"-DAPPLICATION_VERSION=0x{version:08X}UL"
    run(
        [
            "make",
            f"TOOLCHAIN={toolchain}",
            f"BUILD_DIR={build_dir}",
            f"OUT_DIR={out_dir}",
            f"PROJECT_CFLAGS={flags}",
            "clean",
        ],
        cwd=APP,
    )
    run(
        [
            "make",
            f"TOOLCHAIN={toolchain}",
            f"BUILD_DIR={build_dir}",
            f"OUT_DIR={out_dir}",
            f"PROJECT_CFLAGS={flags}",
            "all",
        ],
        cwd=APP,
    )


def public_raw_from_private(private_key: Path, output: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="phase14-pubder-") as td:
        der = Path(td) / "public.der"
        run(
            [
                "openssl",
                "pkey",
                "-in",
                str(private_key),
                "-pubout",
                "-outform",
                "DER",
                "-out",
                str(der),
            ]
        )
        data = der.read_bytes()

    if len(data) < 65 or data[-65] != 0x04:
        fail("OpenSSL P-256 public key was not uncompressed")
    output.write_bytes(data[-64:])


def compile_host_tests(cc: str, output_dir: Path) -> dict[str, Path]:
    output_dir.mkdir(parents=True, exist_ok=True)

    crypto = output_dir / "phase14_crypto"
    container = output_dir / "phase14_container"
    janpatch = output_dir / "phase14_secure_janpatch"
    verifier = output_dir / "phase14_secure_verifier"

    run(
        [
            cc,
            "-std=c11",
            "-O2",
            "-Wall",
            "-Wextra",
            "-Wpedantic",
            "-Werror",
            "-Ishared/include",
            "-Inode-stm32f103/bootloader/crypto",
            "shared/src/sha256.c",
            "node-stm32f103/bootloader/crypto/ecdsa_p256.c",
            "tests/host/test_phase14_crypto.c",
            "-o",
            str(crypto),
        ]
    )

    run(
        [
            cc,
            "-std=c11",
            "-O2",
            "-Wall",
            "-Wextra",
            "-Wpedantic",
            "-Werror",
            "-Ishared/include",
            "shared/src/firmware_container.c",
            "tests/host/test_phase14_container.c",
            "-o",
            str(container),
        ]
    )

    run(
        [
            cc,
            "-std=c11",
            "-O2",
            "-Wall",
            "-Wextra",
            "-Wpedantic",
            "-Werror",
            "-DJANPATCH_PORT_HOST_TEST=1",
            "-Ishared/include",
            "-Inode-stm32f103/common/include",
            "-Inode-stm32f103/bootloader/patch",
            "shared/src/firmware_container.c",
            "node-stm32f103/bootloader/patch/janpatch_port.c",
            "tests/host/test_phase14_secure_janpatch.c",
            "-o",
            str(janpatch),
        ]
    )

    # gc-sections discards SecureContainer_Process() from this host binary.
    # The test intentionally links and executes the exact bootloader
    # SecureContainer_LoadVerifiedInfo() implementation.
    run(
        [
            cc,
            "-std=c11",
            "-O2",
            "-Wall",
            "-Wextra",
            "-Wpedantic",
            "-Werror",
            "-ffunction-sections",
            "-fdata-sections",
            "-Wl,--gc-sections",
            "-Ishared/include",
            "-Inode-stm32f103/common/include",
            "-Inode-stm32f103/bootloader/include",
            "-Inode-stm32f103/bootloader/crypto",
            "-Inode-stm32f103/bootloader/patch",
            "shared/src/crc32.c",
            "shared/src/sha256.c",
            "shared/src/firmware_container.c",
            "node-stm32f103/bootloader/crypto/ecdsa_p256.c",
            "node-stm32f103/bootloader/src/secure_container.c",
            "node-stm32f103/bootloader/patch/janpatch_port.c",
            "tests/host/test_phase14_secure_verifier.c",
            "-o",
            str(verifier),
        ]
    )

    return {
        "crypto": crypto,
        "container": container,
        "janpatch": janpatch,
        "verifier": verifier,
    }


def static_contract_checks() -> None:
    required = [
        "shared/include/firmware_container.h",
        "shared/src/firmware_container.c",
        "shared/include/sha256.h",
        "shared/src/sha256.c",
        "node-stm32f103/bootloader/crypto/ecdsa_p256.h",
        "node-stm32f103/bootloader/crypto/ecdsa_p256.c",
        "node-stm32f103/bootloader/include/secure_container.h",
        "node-stm32f103/bootloader/src/secure_container.c",
        "node-stm32f103/bootloader/include/phase14_trusted_key.h",
        "tools/phase14_keytool.py",
        "tools/phase14_secure_container.py",
        "tools/merge_images.py",
        "scripts/phase14_check.py",
        "scripts/phase14_hw_test.py",
        "tests/host/test_phase14_crypto.c",
        "tests/host/test_phase14_container.c",
        "tests/host/test_phase14_secure_janpatch.c",
        "tests/host/test_phase14_secure_verifier.c",
        "docs/phase-14-secure-container.md",
        "docs/phase-14-checklist.md",
        "PHASE14_REPORT.md",
    ]

    missing = [p for p in required if not (ROOT / p).is_file()]
    if missing:
        fail("missing Phase-14 files: " + ", ".join(missing))

    header = (
        ROOT / "shared/include/firmware_container.h"
    ).read_text(encoding="utf-8")
    secure = (
        ROOT / "node-stm32f103/bootloader/src/secure_container.c"
    ).read_text(encoding="utf-8")
    patcher = (
        ROOT / "node-stm32f103/bootloader/patch/janpatch_port.c"
    ).read_text(encoding="utf-8")
    receiver = (
        ROOT / "node-stm32f103/application/src/ota_receiver.c"
    ).read_text(encoding="utf-8")
    manager = (
        ROOT / "node-stm32f103/bootloader/src/boot_manager.c"
    ).read_text(encoding="utf-8")
    installer = (
        ROOT / "node-stm32f103/bootloader/src/image_installer.c"
    ).read_text(encoding="utf-8")
    sender = (
        ROOT / "tools/uart_ota_sender.py"
    ).read_text(encoding="utf-8")
    merge_tool = (
        ROOT / "tools/merge_images.py"
    ).read_text(encoding="utf-8")

    for token in [
        "#define FW_CONTAINER_FIXED_HEADER_SIZE        120U",
        "#define FW_CONTAINER_EXTENSION_SIZE           20U",
        "#define FW_SIGNATURE_ECDSA_P256               1U",
        "#define FW_ECDSA_P256_RAW_SIGNATURE_SIZE      64U",
        "#define FW_SHA256_SIZE                        32U",
        "#define PHASE14_ALLOW_UNSIGNED_LEGACY         0U",
    ]:
        if token not in header:
            fail(f"secure-container contract missing: {token}")

    for token in [
        "SecureContainer_LoadVerifiedInfo",
        "EcdsaP256_VerifyDigest",
        "Sha256_Update",
        "FW_CONTAINER_HEADER_SIZE",
        "SECURE_CONTAINER_SIGNATURE_INVALID",
        "SECURE_CONTAINER_VERSION_REJECTED",
        "SECURE_CONTAINER_BASE_HASH_MISMATCH",
        "UPDATE_IMAGE_READY",
    ]:
        if token not in secure:
            fail(f"secure bootloader path missing: {token}")

    if "ctx->stream->patch_offset + ctx->patch_position" not in patcher:
        fail("secure Janpatch stream does not honor patch_offset")
    if "DELTA_PATCH_HEADER_SIZE + ctx->patch_position" in patcher:
        fail("legacy D13P patch offset leaked into generic stream reader")

    for token in [
        "OTA_CAP_SIGNATURE_VERIFY",
        "FW_CONTAINER_HEADER_SIZE",
        "OTA_STATUS_SIGNATURE_ERROR",
        "OTA_STATUS_VERSION_REJECTED",
    ]:
        if token not in receiver:
            fail(f"STM32 app secure OTA contract missing: {token}")

    if "SecureContainer_Process" not in manager:
        fail("boot manager does not route secure containers")
    if "SecureContainer_LoadVerifiedInfo" not in installer:
        fail("installer recovery does not re-authenticate signed container")

    for token in [
        "def parse_phase14_secure_container",
        "def secure_ota(",
        "CAP_SIGNATURE_VERIFY",
        "PHASE14_CONTAINER_HEADER_SIZE = 140",
    ]:
        if token not in sender:
            fail(f"PC secure OTA sender missing: {token}")


    for token in [
        "def display_path(path: Path, root: Path)",
        "except ValueError:",
        "display_path(output, root)",
        "display_path(manifest, root)",
    ]:
        if token not in merge_tool:
            fail(f"absolute-output merge regression missing: {token}")

    if "output.relative_to(root)" in merge_tool:
        fail("merge_images.py still rejects output outside repository")


def no_private_key_in_repo() -> None:
    markers = (
        b"-----BEGIN PRIVATE KEY-----",
        b"-----BEGIN EC PRIVATE KEY-----",
        b"-----BEGIN ENCRYPTED PRIVATE KEY-----",
    )

    offenders: list[str] = []
    for path in ROOT.rglob("*"):
        if not path.is_file():
            continue
        if any(part in {".git", "build", "build-host"} for part in path.parts):
            continue
        try:
            data = path.read_bytes()
        except OSError:
            continue
        lines = data.splitlines()
        if any(
            any(line.startswith(marker) for marker in markers)
            for line in lines
        ):
            offenders.append(path.relative_to(ROOT).as_posix())

    if offenders:
        fail("private key material found in repository: " + ", ".join(offenders))


def main() -> int:
    static_contract_checks()
    require_tool("openssl")
    cc = require_tool("cc") if shutil.which("cc") else require_tool("gcc")
    toolchain = choose_toolchain()

    run(
        [
            "python3",
            "-m",
            "py_compile",
            "tools/phase14_keytool.py",
            "tools/phase14_secure_container.py",
            "tools/uart_ota_sender.py",
            "tools/ota_uart_protocol.py",
            "scripts/phase14_check.py",
            "scripts/phase14_hw_test.py",
        ]
    )
    run(["python3", "tests/unit/test_phase12_jojodiff.py"])

    original_trust = TRUSTED_HEADER.read_bytes()
    boot_size = 0
    base_size = target_size = v3_size = 0
    patch_size = delta_size = full_size = 0

    try:
        with tempfile.TemporaryDirectory(prefix="phase14-check-") as td:
            temp = Path(td)
            private_key = temp / "signing-private.pem"
            public_raw = temp / "public.raw"
            patch = temp / "application-v1-to-v2.jdiff"
            delta = temp / "application-v1-to-v2.sdot"
            full_v2 = temp / "application-v2-full.sdot"
            full_v3 = temp / "application-v3-full.sdot"

            run(
                [
                    "openssl",
                    "genpkey",
                    "-algorithm",
                    "EC",
                    "-pkeyopt",
                    "ec_paramgen_curve:P-256",
                    "-out",
                    str(private_key),
                ]
            )

            run(
                [
                    "python3",
                    "tools/phase14_keytool.py",
                    str(private_key),
                    "--key-id",
                    f"0x{KEY_ID:08X}",
                    "--output",
                    str(TRUSTED_HEADER),
                ]
            )

            build_application(
                toolchain, 1, "build-phase14-v1", "out-phase14-v1"
            )
            build_application(
                toolchain, 2, "build-phase14-v2", "out-phase14-v2"
            )
            build_application(
                toolchain, 3, "build-phase14-v3", "out-phase14-v3"
            )

            for image in (BASE, TARGET, V3):
                if not image.is_file():
                    fail(f"application build did not create {image}")
                if image.stat().st_size > APP_MAX:
                    fail(f"{image.name} exceeds 38 KiB")

            base_size = BASE.stat().st_size
            target_size = TARGET.stat().st_size
            v3_size = V3.stat().st_size

            run(
                [
                    "make",
                    f"TOOLCHAIN={toolchain}",
                    "clean",
                ],
                cwd=BOOT,
            )
            run(
                [
                    "make",
                    f"TOOLCHAIN={toolchain}",
                    "all",
                ],
                cwd=BOOT,
            )

            boot_bin = BOOT / "out/bootloader.bin"
            if not boot_bin.is_file():
                fail("provisioned bootloader binary missing")
            boot_size = boot_bin.stat().st_size
            if boot_size > BOOT_MAX:
                fail(
                    f"provisioned secure bootloader is {boot_size} bytes, "
                    f"exceeds 24 KiB"
                )
            print(
                f"Phase 14 provisioned bootloader size: PASS "
                f"{boot_size}/{BOOT_MAX} bytes"
            )

            run(
                [
                    "python3",
                    "tools/jojodiff_patch.py",
                    "generate",
                    str(BASE),
                    str(TARGET),
                    str(patch),
                ]
            )
            patch_size = patch.stat().st_size

            common = [
                "--key",
                str(private_key),
                "--key-id",
                f"0x{KEY_ID:08X}",
            ]

            run(
                [
                    "python3",
                    "tools/phase14_secure_container.py",
                    "build",
                    "--type",
                    "delta",
                    "--payload",
                    str(patch),
                    "--base",
                    str(BASE),
                    "--target",
                    str(TARGET),
                    "--base-version",
                    "1",
                    "--target-version",
                    "2",
                    *common,
                    "--output",
                    str(delta),
                ]
            )
            run(
                [
                    "python3",
                    "tools/phase14_secure_container.py",
                    "build",
                    "--type",
                    "full",
                    "--payload",
                    str(TARGET),
                    "--target",
                    str(TARGET),
                    "--target-version",
                    "2",
                    *common,
                    "--output",
                    str(full_v2),
                ]
            )
            run(
                [
                    "python3",
                    "tools/phase14_secure_container.py",
                    "build",
                    "--type",
                    "full",
                    "--payload",
                    str(V3),
                    "--target",
                    str(V3),
                    "--target-version",
                    "3",
                    *common,
                    "--output",
                    str(full_v3),
                ]
            )

            delta_size = delta.stat().st_size
            full_size = full_v3.stat().st_size

            for container in (delta, full_v2, full_v3):
                run(
                    [
                        "python3",
                        "tools/phase14_secure_container.py",
                        "inspect",
                        str(container),
                    ]
                )

            public_raw_from_private(private_key, public_raw)

            host = compile_host_tests(cc, temp / "host")

            # Verify the real container signature with the exact MCU crypto.
            delta_bytes = delta.read_bytes()
            digest = temp / "signed-digest.bin"
            signature = temp / "signature.raw"
            digest.write_bytes(
                hashlib.sha256(delta_bytes[:-64]).digest()
            )
            signature.write_bytes(delta_bytes[-64:])

            run(
                [
                    str(host["crypto"]),
                    str(public_raw),
                    str(digest),
                    str(signature),
                ],
                timeout=90,
            )

            # Exercise additional independently generated signatures against
            # the exact ECDSA verifier, not just one release container.
            for index in range(4):
                message = temp / f"message-{index}.bin"
                der_signature = temp / f"message-{index}.der"
                raw_signature = temp / f"message-{index}.raw"
                message_digest = temp / f"message-{index}.sha256"

                message.write_bytes(
                    hashlib.sha256(
                        f"phase14-vector-{index}".encode("ascii")
                    ).digest()
                    + bytes(range(index, index + 37))
                )

                run(
                    [
                        "openssl",
                        "dgst",
                        "-sha256",
                        "-sign",
                        str(private_key),
                        "-out",
                        str(der_signature),
                        str(message),
                    ]
                )

                sys.path.insert(0, str(ROOT / "tools"))
                from phase14_secure_container import der_to_raw_ecdsa
                raw_signature.write_bytes(
                    der_to_raw_ecdsa(der_signature.read_bytes())
                )
                message_digest.write_bytes(
                    hashlib.sha256(message.read_bytes()).digest()
                )

                run(
                    [
                        str(host["crypto"]),
                        str(public_raw),
                        str(message_digest),
                        str(raw_signature),
                    ],
                    timeout=90,
                )

            for container in (delta, full_v2, full_v3):
                run([str(host["container"]), str(container)])
                run(
                    [str(host["verifier"]), str(container)],
                    timeout=90,
                )

            run(
                [
                    str(host["janpatch"]),
                    str(BASE),
                    str(delta),
                    str(TARGET),
                ]
            )

            print(
                "Phase 14 exact embedded secure delta reconstruction: PASS"
            )

            # The temporary private key must not escape its temp directory.
            if not private_key.is_file():
                fail("temporary signing key unexpectedly disappeared early")

    finally:
        TRUSTED_HEADER.write_bytes(original_trust)
        # Never leave a bootloader binary built against the ephemeral test key.
        subprocess.run(
            ["make", f"TOOLCHAIN={toolchain}", "clean"],
            cwd=BOOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )

    if TRUSTED_HEADER.read_bytes() != original_trust:
        fail("trusted-key source header was not restored")

    no_private_key_in_repo()

    print("Phase 14 private-key non-persistence: PASS")
    print("Secure Delta OTA Phase 14 secure container check: PASS")
    print(
        f"Bootloader signed-key build: {boot_size} bytes / {BOOT_MAX}"
    )
    print(
        f"Application v1/v2/v3: "
        f"{base_size}/{target_size}/{v3_size} bytes"
    )
    print(
        f"Secure delta: patch={patch_size} bytes "
        f"container={delta_size} bytes "
        f"savings={(1.0 - delta_size / target_size) * 100.0:.2f}%"
    )
    print(
        f"Secure full v3: container={full_size} bytes "
        f"(header=140 signature=64)"
    )
    print(
        "Phase 15 boundary: signing-key custody, release publication, "
        "manifest/server integration remain out of Phase 14."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
