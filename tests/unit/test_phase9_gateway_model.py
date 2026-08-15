#!/usr/bin/env python3
"""Model the Phase-9 ESP32 gateway's resume and final-state policy."""

MAX_PAYLOAD = 256
CHECKPOINT = 4096

def sequence_for_offset(offset: int) -> int:
    assert offset % MAX_PAYLOAD == 0
    return (offset // MAX_PAYLOAD) & 0xFFFF

def test_persistent_resume() -> None:
    image_size = 10184
    runtime_received = 4608

    # STM32 Phase 7 persists only complete 4 KiB boundaries.
    recovered_offset = runtime_received - (runtime_received % CHECKPOINT)
    assert recovered_offset == 4096
    assert sequence_for_offset(recovered_offset) == 16

    # Gateway retransmits from the authoritative STM32 ACK offset.
    sent = recovered_offset
    seq = sequence_for_offset(recovered_offset)
    while sent < image_size:
        chunk = min(MAX_PAYLOAD, image_size - sent)
        sent += chunk
        seq = (seq + 1) & 0xFFFF

    assert sent == image_size

def test_install_ack_loss_is_not_failure() -> None:
    # INSTALL resets the target. The gateway waits for final QUERY state even
    # if the transport-level ACK was missed.
    install_ack_seen = False
    observations = [
        None,                         # bootloader owns MCU
        None,
        (2, 10),                      # app v2, TRIAL_BOOT
        None,                         # candidate confirmation reset
        (2, 0),                       # app v2, IDLE
    ]

    success = False
    saw_trial = False
    for item in observations:
        if item is None:
            continue
        app_version, state = item
        if app_version == 2 and state == 10:
            saw_trial = True
        if app_version == 2 and state == 0:
            success = True
            break

    assert not install_ack_seen
    assert saw_trial
    assert success

def test_rollback_is_detected() -> None:
    observations = [
        (3, 10),   # bad candidate trial
        None,
        None,
        (2, 0),    # old confirmed image comes back
    ]

    saw_trial = False
    rolled_back = False
    for item in observations:
        if item is None:
            continue
        app_version, state = item
        if app_version == 3 and state == 10:
            saw_trial = True
        elif saw_trial and app_version != 3 and state == 0:
            rolled_back = True

    assert rolled_back

def main() -> int:
    test_persistent_resume()
    test_install_ack_loss_is_not_failure()
    test_rollback_is_detected()
    print("Phase 9 ESP32 gateway resume/final-state model: PASS")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
