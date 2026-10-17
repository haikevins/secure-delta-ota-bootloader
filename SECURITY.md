# Security Policy

Secure Delta OTA Bootloader is a security-sensitive embedded firmware project. Responsible disclosure is preferred for any issue that could weaken firmware authenticity, rollback protection, update integrity, secret handling, boot decisions, or recovery behavior.

## Supported versions

Until the first stable release is published, security fixes are maintained on the current `main` branch.

After `v1.0.0`, the latest stable release and current `main` branch are the supported lines unless a release note states otherwise. Older releases should be treated as unsupported.

## Reporting a vulnerability

Do not disclose exploitable security details in a public issue.

Use GitHub's private vulnerability reporting flow from the repository **Security** tab when it is available. If private reporting is unavailable, open a minimal public issue asking for a private contact channel without including exploit steps, secrets, proof-of-concept payloads, or sensitive logs.

A useful report should include:

- the affected component and revision;
- the security property that can be violated;
- the required attacker capabilities or preconditions;
- a minimal reproduction description that is safe to share privately;
- the observed and expected behavior;
- any proposed mitigation, if known.

## Security scope

Examples of security-relevant areas include:

- SDOT container parsing and signature verification;
- ECDSA P-256 trust-anchor and key-ID handling;
- SHA-256 integrity checks;
- anti-downgrade and exact-base validation;
- boot metadata, trial boot, confirmation, rollback, and recovery;
- external SPI NOR staging and resume state;
- UART OTA framing and gateway-to-node trust boundaries;
- HTTPS/MQTTS artifact acquisition and release metadata handling;
- release tooling and signing-key handling.

## Secrets and signing keys

Private signing keys and production credentials must never be committed to the repository or embedded in test fixtures.

The repository is designed to keep the trust anchor unprovisioned by default. Test material must not be represented as production credentials. Release signing should use external secret storage or CI secrets and should avoid printing sensitive key material to logs.

## Security model

The documented security assumptions, trust boundaries, threats, and non-goals are maintained in [`docs/threat-model.md`](docs/threat-model.md). Reports outside that model are still welcome when they reveal a practical security impact.
