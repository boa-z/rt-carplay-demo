[English](mbedtls-upgrade-assessment.md) | [简体中文](zh-CN/mbedtls-upgrade-assessment.md)

# mbedTLS 4.x assessment

## Recommendation

Consider 4.1 LTS for a planned security and maintenance migration, not as an established D213 speed improvement. The official 4.1.1 release states support until at least March 2029. No library upgrade is included in the current test image. Keep wired startup repair and validation ahead of migration. The actual embedded source is 2.28.1; its legacy package-selection label does not identify the source version accurately.

## Measured evidence

The official 4.1.1 archive was checked against its published SHA256, extracted independently and probed using the RV64 Linux compiler and QEMU. Its bundled crypto implementation still selects 32-bit MPI limbs by default on RV64. Therefore upgrading alone does not automatically provide the architecture correction applied to the current library. A private-header probe is evaluation evidence only; applications must not depend on that internal API.

| Check | Result | Limit |
| --- | --- | --- |
| Current library, native generic MPI, 32/64-bit full pair generation | 586.8 / 163.9 ms CPU time | One deterministic test run; not a version comparison |
| Current library, RV64 QEMU, 32/64-bit full pair generation | 2166.4 / 816.9 ms CPU time | One emulated run; not D213 wall time |
| User's optimized D213 image | 6933 ms pair-record generation | No matched pre-change board baseline supplied |
| Official 4.1.1 default RV64 MPI probe | 32-bit limbs | Type-selection check, not a speed benchmark |
| 2.28.1 versus 4.1.1 on D213 | NOT_RUN | No defensible version-upgrade speed percentage |

These timings retain two independent RSA-2048 keys and certificate generation. RSA key search varies with randomness. Ratios from one test run must not be interpreted as an expected board acceleration. The supplied board timing separates generation from trust-dialog waiting but does not measure CPU consumed by each substage.

## Migration costs and compatibility

Version 4 splits cryptography into TF-PSA-Crypto, makes PSA the primary crypto API and uses CMake as its supported build system. The application currently uses low-level MPI/RSA, ChaCha20-Poly1305, HKDF and other legacy APIs. Pair-record construction and protocol crypto need adapters to public PSA interfaces. Legacy crypto ALT backends are replaced by PSA drivers, so the existing ArtInChip AES/SHA acceleration needs a deliberate port; losing it can make some operations slower. Entropy initialization, threading, key lifetime, memory allocation and the SCons integration also require work.

AirPlay SRP-6a also uses the legacy MPI API. PSA does not expose general-purpose MPI arithmetic as a direct replacement. This requires a suitable supported SRP implementation or a separately maintained arithmetic backend, with protocol-vector and pairing regression tests; depending on private crypto headers would not provide a stable migration.

The current Lockdown client restricts negotiation to TLS 1.2. Version 4 removes static-RSA and finite-field DHE key exchange in TLS 1.2; RSA signatures with ECDHE remain supported. The supplied logs do not identify the phone's selected cipher suite. An actual negotiated-suite capture and iPhone compatibility test are required before migration. TLS 1.3 improvements cannot be assumed to accelerate a TLS 1.2-only connection.

## Expected performance scope

Faster RSA generation would reduce fresh pairing latency, while cached reconnect already skips generation. Steady CarPlay music/video also depend on USB/Wi-Fi scheduling, buffering, AAC decode, resampling and rendering; a TLS library replacement does not directly fix these paths. The crypto release notes describe an AArch64 Neon ChaCha20 improvement of roughly 2x to 2.7x. That is architecture-specific evidence and does not apply to this D213 RISC-V target. No general percentage for frame rate, music quality or overall firmware CPU use is justified.

Before migration, compare the optimized current library and the latest selected 4.1 maintenance release on the same board with equivalent security checks, entropy policy and acceleration. Measure repeated RSA generation, fresh/cached Lockdown, negotiated TLS suites, short-packet ChaCha20-Poly1305, SHA/HKDF, firmware size, peak heap/stack and simultaneous audio/video. Report median and tail latency, initialization cost and hardware acceptance separately.

## Official references

- [4.1.1 release and official archive](https://github.com/Mbed-TLS/mbedtls/releases/tag/mbedtls-4.1.1)
- [4.0 migration guide](https://github.com/Mbed-TLS/mbedtls/blob/mbedtls-4.0.0/docs/4.0-migration-guide.md)
- [Crypto API migration](https://github.com/Mbed-TLS/TF-PSA-Crypto/blob/tf-psa-crypto-1.1.1/docs/1.0-migration-guide.md)
- [Pinned crypto MPI type selection](https://github.com/Mbed-TLS/TF-PSA-Crypto/blob/tf-psa-crypto-1.1.1/drivers/builtin/include/mbedtls/private/bignum.h)
- [Pinned crypto release notes](https://github.com/Mbed-TLS/TF-PSA-Crypto/blob/tf-psa-crypto-1.1.1/ChangeLog)
