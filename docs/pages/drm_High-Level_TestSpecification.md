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
validates API contracts, L2 validates key exchange and retained legacy
known-vector decryption without playback, and L3 validates sample-level AIDL
AVBuffer crypto and secure-session capability. L4 and future work cover
playback integration.

### References

- [RDK DRM AIDL 0.23.0 / interface 0.2.0.0](https://github.com/rdkcentral/rdk-halif-aidl/tree/0.23.0/drm/0.2.0.0/com/rdk/hal/drm)
- [Android DRM AIDL VTS](https://android.googlesource.com/platform/hardware/interfaces/+/refs/heads/main/drm/aidl/vts/)
- [DRM API coverage matrix](drm_TestSpecification.md)
- [L2 Low Level Test Specification](drm_L2_Low-Level_TestSpecification.md)
- [L3 Low Level Test Specification](drm_L3_Low-Level_TestSpecification.md)

## Module Description

`IDrmFactory` advertises schemes and creates `IDrmPlugin` and `ICryptoPlugin`.
The DRM plugin manages sessions, license requests/responses, properties, and
key status. L3 tests use the crypto plugin's AIDL AVBuffer decryption path.

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
reference implementation; common license/session procedures are reused for
vendor Widevine and PlayReady implementations, subject to their advertised
capabilities.

Each scheme requires its own configured factory/UUID, supported MIME/security
level, init data, and accepted test-license flow. L3 AVBuffer sample comparison
additionally requires matching encrypted-sample, KID/IV, and plaintext fixtures.
Widevine and PlayReady do not use ClearKey JSON licenses or raw-key fixtures.
The retained legacy `IDrmPlugin.decrypt()` vector tests and empty-init-data
negative case are ClearKey-specific exceptions, not common requirements.

**Phase 1 covers:**

- Discover the factory and create DRM/Crypto plugins.
- Exercise DRM session lifecycle, key requests/responses, and key status.
- Associate the DRM session with Crypto and decrypt a known AES-encrypted
  sample through `ICryptoPlugin.decrypt(DecryptArgs)` using AVBuffer handles
  in L3.
- Compare the decrypted bytes with an independent plaintext reference.
- Run the VTS without requiring OCDM, a player, or full middleware integration.

The current VTS has L1 factory/session coverage, common L2 license/session
coverage for configured ClearKey, Widevine and PlayReady profiles, retained
ClearKey-only legacy `IDrmPlugin.decrypt()` vectors under L2, and sample-level
`ICryptoPlugin.decrypt(DecryptArgs)` AVBuffer cases under L3. A single run can
exercise all enabled profiles using their exact UUIDs, services, and
independent license fixtures.
Optional unadvertised capabilities skip; required capabilities and operational
failures fail. Secure-decoder-only sample cases skip CPU-readable plaintext
comparison and do not establish protected-path conformance.
The L3 AVBuffer cases check known plaintext, byte counts, subsamples,
configured modes/patterns, bounds, handle ownership, and documented negative
errors. They remain sample/API tests, not comprehensive vendor qualification
or protected playback conformance. These tests have not established target
conformance merely by being present or passing against the software reference
service.

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

See [DRM Test Streams](drm_TestStreams.md) for L2 legacy-vector inputs and L3
sample-vector requirements. No production keys or licensed media are packaged
here.

## L2 Test Specification

All cases below are opt-in. They run only when L2 is enabled and do not
submit content to a decoder. “Implemented” describes a registered test case;
it does not imply the test has been run or passed on a target.

| Case ID | Current test case | Current coverage |
|---:|---|---|
| 001 | `fixtureValidation_pos` | Validates that configured init data is readable and non-empty. |
| 002 | `keyRequestAndResponse_pos` | Opens a session, requests/provides a streaming key, binds crypto, and checks key status. |
| 003 | `decryptKnownSample_pos` | ClearKey-only legacy `IDrmPlugin.decrypt()` comparison with known plaintext. |
| 004 | `removeKeysRejectsDecrypt_neg` | ClearKey-only legacy decrypt baseline, then `ERROR_DRM_NO_LICENSE` after key removal. |
| 005 | `emptyLicenseResponseRejected_neg` | Requests a license, then asserts `BAD_VALUE` for an empty response. |
| 006 | `closedSessionRejectsOperations_neg` | Asserts `ERROR_DRM_SESSION_NOT_OPENED` for a key request using a closed session. |
| 007 | `invalidInitDataRejected_neg` | ClearKey-only `BAD_VALUE` rejection for empty init data; not assumed for vendor renewals. |
| 008 | `offlineKeySetCreated_pos` | Checks the supported offline-license flow and non-empty key-set identifier. |
| 009 | `listenerCallbacks_pos` | Checks license-related event, expiration, and key-change callbacks. |
| 010 | `getKeyRequestResourceContention_neg` | Checks the documented resource-contention error for key requests. |
| 011 | `getKeyRequestInvalidStateAndSessionLost_neg` | Checks invalid-state handling and the session-lost callback. |
| 012 | `malformedClearKeyEmptyKeyRejected_neg` | Checks rejection of a malformed license containing an empty key. |
| 013 | `malformedClearKeyLongKeyRejected_neg` | Checks rejection of a malformed license containing an over-length key. |
| 014 | `badLevelNotSupported_neg` | Checks that the profile does not advertise `HW_SECURE_ALL` for `video/mp4`. |
| 015 | `provisioningStatusAndExchange_pos` | Checks session-open behavior at selected levels and provisioning when required and enabled. |

L2 does not exercise `ICryptoPlugin.decrypt(DecryptArgs)` or AVBuffer handles.
Its known-vector cases use the separate legacy `IDrmPlugin.decrypt()` API and
are ClearKey-specific. See the [L2 specification](drm_L2_Low-Level_TestSpecification.md)
for exact behavior and profile-dependent skips. L2 must not submit data to a
decoder or claim playback coverage.

## L3 Test Specification

### Prerequisites

The current sample-level L3 cases require the DRM service and configured
fixtures. AVBuffer cases additionally require AVBuffer allocation/import and
helper services; they use pre-demuxed samples and do not need a media decoder.
Full playback coverage will additionally require validated
AVBuffer ownership, demux/media pipeline and decoders, encrypted streams, and a
defined output oracle. Secure-path claims additionally require protected
buffers, secure decoder support, and target output-policy validation.

### Phase 1 test cases

All seven cases are opt-in. Case 003 additionally requires its secure-session
option and target configuration. These are sample/API checks, not end-to-end
playback conformance.

| Case ID | Current test case | Current coverage |
|---:|---|---|
| 001 | `decryptSampleWithAvBuffer_pos` | Acquires/provides a key, decrypts one configured sample through AIDL AVBuffers, and checks byte count and exact plaintext. |
| 002 | `removedKeysAndClosedSessionRejectDecrypt_neg` | Checks AIDL decrypt failure after key removal and after session close. |
| 003 | `secureSessionAndDecoderRequirement_pos` | Checks the requested session security level and `requiresSecureDecoderComponent` result. |
| 004 | `decryptSubsamplesAndPattern_pos` | Validates multiple subsamples, configured patterns, and clear-tail handling for unpatterned AES-CTR. |
| 005 | `decryptBoundsAndInvalidInputsRejected_neg` | Checks documented errors for short buffers, invalid handles, and empty subsamples. |
| 006 | `decryptClearSegmentWithAvBuffer_pos` | Checks clear-only `UNENCRYPTED` pass-through; it does not perform a cryptographic transform. |
| 007 | `decryptWithoutKeysRejected_neg` | Checks that encrypted AVBuffer decryption without a key returns `ERROR_DRM_NO_LICENSE`. |

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
