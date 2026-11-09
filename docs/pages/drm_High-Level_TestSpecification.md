# DRM High Level Test Specification Document

## Table of Contents

- [Acronyms, Terms and Abbreviations](#acronyms-terms-and-abbreviations)
- [Introduction](#introduction)
- [Module Description](#module-description)
- [Delivery Phases](#delivery-phases)
- [Test Streams Specification](#test-streams-specification)
- [L2 Test Specification](#l2-test-specification)
- [L3 Test Specification](#l3-test-specification)

## Acronyms, Terms and Abbreviations

- `AIDL` - Android Interface Definition Language
- `API` - Application Programming Interface
- `DRM` - Digital Rights Management
- `DUT` - Device Under Test
- `HAL` - Hardware Abstraction Layer
- `HFP` - HAL Feature Profile
- `KID` - Key Identifier
- `L2/L3` - VTS test levels, not Widevine security levels
- `VTS` - Vendor Test Suite

## Introduction

This document defines the scope and level boundaries for DRM testing: L1
validates API contracts, L2 validates key exchange and known-vector crypto
through AIDL AVBuffers and the retained legacy API without playback, and L3/L4
covers higher-level crypto and playback integration. Current
L3 coverage is limited to sample-level AVBuffer decryption, failure handling,
and secure-session capability checks.

### References

- [RDK DRM AIDL 0.23.0 / interface 0.2.0.0](https://github.com/rdkcentral/rdk-halif-aidl/tree/0.23.0/drm/0.2.0.0/com/rdk/hal/drm)
- [Android DRM AIDL VTS](https://android.googlesource.com/platform/hardware/interfaces/+/refs/heads/main/drm/aidl/vts/)
- [DRM API coverage matrix](drm_TestSpecification.md)
- [L2 Low Level Test Specification](drm_L2_Low-Level_TestSpecification.md)
- [L3 Low Level Test Specification](drm_L3_Low-Level_TestSpecification.md)

## Module Description

`IDrmFactory` advertises schemes and creates `IDrmPlugin` and `ICryptoPlugin`.
The DRM plugin manages sessions, license requests/responses, properties, and
key status. The crypto plugin binds a DRM session and supplies the AIDL
AVBuffer decryption path.

The test client, not the HAL, owns license-server transport. Tests can POST
the plugin's challenge to a configured test adapter or load an accepted
response file. ClearKey additionally supports disposable local JSON generation.
Vendor provisioning and license authentication/wrapping remain external.

Tests adapt applicable Android VTS behaviour to RDK's Binder backend and API.

## Delivery Phases

The delivery phases describe project integration milestones; they are
independent of the VTS L1/L2/L3 test-level names. In particular, an L3 test
group does not by itself mean that end-to-end playback has been implemented.

### Phase 1: DRM/Crypto functional bring-up and VTS

**Goal:** Prove that the DRM and Crypto HALs work independently of middleware,
applications, or a complete audio/video pipeline for each DRM scheme under
test: ClearKey, Widevine and PlayReady. Phase 1 is a scheme-independent HAL
test solution, not a ClearKey-only solution. ClearKey is the initial software
reference implementation; the same common license/session and AVBuffer
test procedures are reused for vendor Widevine and PlayReady implementations,
subject to their advertised capabilities.

Each scheme requires its own configured factory/UUID, supported MIME/security
level, init data, and accepted test-license flow. AVBuffer sample comparison
additionally requires matching encrypted-sample, KID/IV and plaintext fixtures.
Widevine and PlayReady do not use ClearKey JSON licenses or raw-key fixtures.
The retained legacy `IDrmPlugin.decrypt()` vector tests and empty-init-data
negative case are ClearKey-specific exceptions, not common requirements.

**Phase 1 covers:**

- Discover the factory and create DRM/Crypto plugins.
- Exercise DRM session lifecycle, key requests/responses, and key status.
- Associate the DRM session with Crypto and decrypt a known AES-encrypted
  sample through `ICryptoPlugin.decrypt(DecryptArgs)` using AVBuffer handles.
- Compare the decrypted bytes with an independent plaintext reference.
- Run the VTS without requiring OCDM, a player, or full middleware integration.

The current VTS has L1 factory/session coverage, common L2 license/session
coverage for configured ClearKey, Widevine and PlayReady profiles, retained
ClearKey-only legacy `IDrmPlugin.decrypt()` vectors, opt-in L2 known-vector
decryption through `ICryptoPlugin.decrypt(DecryptArgs)` with AVBuffer handles,
and separate sample-level AVBuffer cases under L3. A single run can exercise
all enabled profiles using their exact UUIDs, services and independent license
fixtures.
Optional unadvertised capabilities skip; required capabilities and operational
failures fail. Secure-decoder-only sample cases skip CPU-readable plaintext
comparison and do not establish protected-path conformance.
The L2 AIDL cases check known plaintext, byte counts, subsamples, configured
modes/patterns, bounds, handle ownership and documented negative errors. They
remain sample/API tests, not comprehensive vendor qualification or protected
playback conformance. These tests have not established target conformance
merely by being present or passing against the software reference service.

**Phase 1 exit criterion:** For each required DRM scheme on the target, VTS
can independently open a DRM session, acquire/provide a key and bind the
session to Crypto. Where non-secure sample decryption is supported, it must
also decrypt a known AES sample through AVBuffer and verify exact plaintext
output without decoder playback. Record results separately for ClearKey,
Widevine and PlayReady; a ClearKey pass does not validate the other schemes.
For protected-only paths, a skipped plaintext comparison is an explicit
coverage gap, not a decrypt pass or proof of the complete Phase 1 decrypt
criterion; protected decryption needs a target-approved validation method.

### Phase 2: DRM integration with the AV pipeline

**Goal:** Connect the Phase 1 DRM/Crypto path to media processing and verify
encrypted content can be consumed by the target AV pipeline.

**Phase 2 covers:**

- Integrate DRM/Crypto, AVBuffer, demux, and audio/video decoder components in
  a small controlled pipeline or test application.
- First verify a non-secure/non-SVP encrypted sample can be decrypted and
  decoded; then add SVP/protected-buffer handling where supported.
- Validate playback output and resource cleanup, then exercise multiple DRM
  and decoder sessions and measure load/performance against agreed targets.

Encrypted-stream demux, decoder playback, SVP/protected output, concurrent
stream, and performance validation are not provided by the current sample-level
VTS tests. They remain Phase 2 work and require target pipeline components,
test streams, and an agreed output/performance oracle. Middleware/OCDM may be
integrated where needed, but is not a prerequisite for the standalone Phase 1
VTS foundation.

## Test Streams Specification

See [DRM Test Streams](drm_TestStreams.md) for L2 vector inputs and future L3
stream requirements. No production keys or licensed media are packaged here.

## L2 Test Specification

All cases below are opt-in. They run only when L2 is enabled and do not
submit content to a decoder. “Implemented” describes a registered test case;
it does not imply the test has been run or passed on a target.

| Current test case | Current coverage |
|-------------------|------------------|
| `keyRequestAndResponse_pos` | Opens a session, requests/provides a streaming key, binds the crypto session, and checks that key status is non-empty |
| `decryptKnownSample_pos` | ClearKey-only legacy `IDrmPlugin.decrypt()` comparison with known plaintext |
| `removeKeysRejectsDecrypt_neg` | ClearKey-only legacy decrypt baseline, then `ERROR_DRM_NO_LICENSE` after key removal |
| `emptyLicenseResponseRejected_neg` | Common case: successfully request a license, then assert `BAD_VALUE` for an empty response |
| `invalidInitDataRejected_neg` | ClearKey-only `BAD_VALUE` rejection for empty init data; not assumed for vendor renewals |
| `closedSessionRejectsOperations_neg` | Asserts `ERROR_DRM_SESSION_NOT_OPENED` for a key request using a closed session |
| `decryptKnownSampleWithAvBuffer_pos` | Compares `ICryptoPlugin.decrypt(DecryptArgs)` output and byte count with the configured vector; the caller retains and releases AVBuffer handles |
| `decryptSubsamplesAndPattern_pos` | Exercises multiple and clear/encrypted subsamples for an unpatterned AES-CTR vector; validates configured mode and pattern values against the matching full-sample vector |
| `decryptBoundsAndInvalidInputsRejected_neg` | Asserts documented errors for short source/destination bounds, invalid AVBuffer handles, and empty subsamples |
| `removedKeysAndClosedSessionRejectAvBufferDecrypt_neg` | Asserts `ERROR_DRM_NO_LICENSE` after key removal and `ERROR_DRM_SESSION_NOT_OPENED` after session closure |

### L2 AIDL decrypt coverage

L2 adds known-vector decryption through `ICryptoPlugin.decrypt(DecryptArgs)`
with AVBuffer handles, alongside the retained legacy `IDrmPlugin.decrypt()`
case. The AIDL cases cover exact plaintext and byte-count checks, mixed
clear/encrypted subsamples, supported pattern handling, invalid bounds and
subsample inputs, and buffer-handle ownership and lifetime. Negative cases
assert the documented AIDL error codes. Cases are registered per scheme where
that scheme provides the configured vector and license fixtures.

An AVBuffer sample-decrypt case also exists in the L3 group. It remains a
separate higher-level sample/API check; it does not replace the L2 AIDL
coverage. Detailed procedures and any target-dependent limitations are in the
[L2 specification](drm_L2_Low-Level_TestSpecification.md).
L2 must not submit data to a decoder or claim playback coverage.

## L3 Test Specification

### Prerequisites

The current sample-level L3 cases require the DRM service and configured
fixtures. `decryptSampleWithAvBuffer_pos` and
`removedKeysAndClosedSessionRejectDecrypt_neg` additionally require AVBuffer
allocation/import and helper services; they use a pre-demuxed sample and do not
need a media decoder. Full playback coverage will additionally require validated
AVBuffer ownership, demux/media pipeline and decoders, encrypted streams, and a
defined output oracle. Secure-path claims additionally require protected
buffers, secure decoder support, and target output-policy validation.

### Phase 1 test cases

All three cases are opt-in. The secure-session case additionally requires its
secure-session option and target configuration. These tests are sample/API
checks, not end-to-end playback conformance.

| Current test case | Current coverage |
|---|---|
| `decryptSampleWithAvBuffer_pos` | Acquires/provides a key, decrypts one configured sample through `ICryptoPlugin.decrypt(DecryptArgs)` using AVBuffer handles, checks byte count and exact plaintext, and cleans up |
| `removedKeysAndClosedSessionRejectDecrypt_neg` | Checks AIDL decrypt failure after key removal and after session close, with expected service-specific errors |
| `secureSessionAndDecoderRequirement_pos` | Checks the requested session security level and `requiresSecureDecoderComponent` result|

### Phase 2 L3/L4 playback coverage

These are requirements for future integration tests, not current registered
test cases:

| Planned feature | Required validation |
|---|---|
| DRM metadata detection | Detect scheme/KID and extract init data from a media stream |
| End-to-end playback | Submit encrypted media through the target pipeline and validate decoded output |
| Key rotation and playback license failures | Validate rotation and scheme-specific failure behavior during playback |
| Protected playback/output policy | Verify protected-buffer routing and output restrictions |
| Playback cleanup | Release demux, decoder, crypto, session, and buffers across playback success and failure |
| Multi-session and performance | Exercise concurrent encrypted streams/decoders and measure agreed load, latency, and drop targets |

See the [L3 specification](drm_L3_Low-Level_TestSpecification.md).
