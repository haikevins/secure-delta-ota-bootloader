#!/usr/bin/env python3
from __future__ import annotations
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "node-stm32f103/application"

def fail(msg: str) -> None:
    print(f"Phase 5 check: FAIL: {msg}")
    raise SystemExit(1)

def run(cmd, cwd=ROOT, echo=True):
    r = subprocess.run(cmd, cwd=cwd, text=True,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       env=os.environ.copy())
    if echo:
        print(r.stdout, end="")
    if r.returncode != 0:
        fail(f"command returned {r.returncode}: {' '.join(cmd)}")
    return r.stdout

def toolchain() -> str:
    requested = os.environ.get("TOOLCHAIN", "").strip()
    if requested:
        return requested
    if shutil.which("arm-none-eabi-gcc"):
        return "gcc"
    if shutil.which("clang") and shutil.which("ld.lld") and shutil.which("llvm-objcopy"):
        return "clang"
    fail("no ARM toolchain")
    return ""

def symbols(elf: Path):
    readelf = shutil.which("arm-none-eabi-readelf") or shutil.which("llvm-readelf") or shutil.which("readelf")
    out = run([readelf, "-sW", str(elf)], echo=False)
    result = {}
    pattern = re.compile(r"^\s*\d+:\s+([0-9a-fA-F]+)\s+\d+\s+\S+\s+\S+\s+\S+\s+\S+\s+(.+)$")
    for line in out.splitlines():
        m = pattern.match(line)
        if m:
            result[m.group(2).strip()] = int(m.group(1), 16)
    return result

def main() -> None:
    required = [
        "node-stm32f103/application/drivers/uart.c",
        "node-stm32f103/application/protocol/cobs.c",
        "node-stm32f103/application/protocol/ota_packet.c",
        "node-stm32f103/application/protocol/ota_parser.c",
        "node-stm32f103/application/protocol/ota_response.c",
        "node-stm32f103/application/src/ota_agent.c",
        "node-stm32f103/application/src/ota_receiver.c",
        "tools/ota_uart_protocol.py",
        "tools/uart_ota_sender.py",
        "tests/unit/test_phase5_uart_protocol.c",
        "tests/unit/test_phase5_uart_protocol.py",
        "docs/phase-5-uart-pc-protocol.md",
        "docs/phase-5-checklist.md",
        "PHASE5_REPORT.md",
    ]
    missing = [p for p in required if not (ROOT / p).is_file()]
    if missing:
        fail("missing: " + ", ".join(missing))

    cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
    host = ROOT / "build-host/phase5_uart_protocol"
    host.parent.mkdir(parents=True, exist_ok=True)
    run([
        cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
        "-Ishared/include", "-Inode-stm32f103/application/include",
        "shared/src/crc32.c",
        "node-stm32f103/application/protocol/cobs.c",
        "node-stm32f103/application/protocol/ota_packet.c",
        "node-stm32f103/application/protocol/ota_parser.c",
        "node-stm32f103/application/protocol/ota_response.c",
        "tests/unit/test_phase5_uart_protocol.c",
        "-o", str(host)
    ])
    run([str(host)])
    run(["python3", "tests/unit/test_phase5_uart_protocol.py"])
    run(["python3", "-m", "py_compile",
         "tools/ota_uart_protocol.py", "tools/uart_ota_sender.py",
         "scripts/phase5_hw_test.py"])

    tc = toolchain()
    run(["make", f"TOOLCHAIN={tc}", "clean"], cwd=APP)
    run(["make", f"TOOLCHAIN={tc}", "all"], cwd=APP)

    binary = APP / "out/application.bin"
    if binary.stat().st_size > 38 * 1024:
        fail("application exceeds 38 KiB")

    table = symbols(APP / "out/application.elf")
    for name in [
        "USART1_IRQHandler", "Uart_Init", "Cobs_Encode", "Cobs_Decode",
        "OtaPacket_Serialize", "OtaPacket_Deserialize",
        "OtaParser_PushByte", "OtaResponse_BuildAck",
        "OtaReceiver_ProcessPacket", "OtaAgent_Init", "OtaAgent_Process",
        "ExternalFlashStorage_EraseRange",
    ]:
        if name not in table:
            fail(f"missing symbol {name}")

    run(["python3", "tools/merge_images.py", "--output", "dist/secure-delta-ota-phase5.bin", "--label", "Phase 5"])
    if not (ROOT / "dist/secure-delta-ota-phase5.bin").is_file():
        fail("Phase 5 combined image missing")

    print("Secure Delta OTA Phase 5 UART/PC protocol check: PASS")
    print("USART1 PA9/PA10, 115200 8-N-1, COBS+0x00, CRC32")
    print(f"Application size: {binary.stat().st_size} bytes / 38 KiB")
    print("Hardware: make phase5-hw-test PORT=/dev/ttyUSB0")

if __name__ == "__main__":
    main()
