# Phase 14 Checklist

## Container
- [x] Preserve Phase-0 120-byte fixed SDOT header.
- [x] Add signed SCX1 extension.
- [x] Canonical total header size = 140 bytes.
- [x] Full and delta image types.
- [x] Exact product/hardware validation.
- [x] Exact target load address validation.
- [x] Overflow-safe total-length validation.
- [x] Payload CRC32.
- [x] Base SHA-256.
- [x] Target SHA-256.
- [x] Signed target CRC32.
- [x] Key ID.

## Crypto
- [x] SHA-256 streaming implementation.
- [x] SHA-256 known-answer host test.
- [x] ECDSA P-256 verification.
- [x] Raw 64-byte r||s wire signature.
- [x] Raw 64-byte X||Y trust anchor.
- [x] Host OpenSSL interoperability.
- [x] Valid-signature acceptance.
- [x] Tampered-signature rejection.
- [x] Tampered-digest rejection.
- [x] Multiple generated signature vectors.
- [x] Provisioned bootloader still fits 24 KiB.

## Trust boundary
- [x] Private key never stored on STM32.
- [x] Private key not packaged in repository/ZIP.
- [x] Bootloader authenticates container.
- [x] Application transports but does not authenticate.
- [x] Re-authenticate envelope during install recovery.
- [x] Unsigned legacy OTA disabled by default.

## Full OTA
- [x] Signed full payload.
- [x] Verify signature before candidate use.
- [x] Verify target SHA-256.
- [x] Copy only to external Reconstructed Image first.
- [x] Reuse backup/install/trial/rollback.

## Delta OTA
- [x] Signed JojoDiff payload.
- [x] Verify signature before base/patch work.
- [x] Exact base version.
- [x] Exact base size.
- [x] Exact base SHA-256.
- [x] Generic Janpatch stream offset for SDOT payload.
- [x] Reconstruct to external Flash.
- [x] Target SHA-256 + signed CRC validation.
- [x] Host test exact embedded Janpatch implementation.

## Version policy
- [x] Target must be newer than active version.
- [x] Application rejects non-new START requests.
- [x] Bootloader enforces monotonic policy again.
- [x] HIL runner includes downgrade rejection.

## Tests/tooling
- [x] `tools/phase14_keytool.py`.
- [x] `tools/phase14_secure_container.py`.
- [x] `secure-ota` PC UART action.
- [x] `scripts/phase14_check.py`.
- [x] `scripts/phase14_hw_test.py`.
- [x] Canonical container parser host test.
- [x] Exact secure-envelope verifier host test.
- [x] Signature/key-ID tamper host tests.
- [x] Signed delta reconstruction host test.
- [x] Physical signed delta HIL path.
- [x] Physical tampered-signature rejection path provided.
- [x] Physical signed full HIL path provided.
- [ ] Physical Phase-14 HIL PASS — pending user board run.

## Explicitly not Phase 14
- [ ] Production HSM/key custody — Phase 15.
- [ ] Release server/publication pipeline — Phase 15.
- [ ] Multi-key rotation/revocation policy.
- [ ] Firmware encryption.
