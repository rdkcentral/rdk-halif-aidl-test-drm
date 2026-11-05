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

This document defines DRM test scope using the audio-decoder documentation
layout. The DRM-specific level boundaries are retained: L1 validates API
contracts, L2 validates key exchange and legacy known-vector crypto without
playback, and L3 covers higher-level crypto and playback integration. Current
L3 coverage is limited to sample-level AVBuffer decryption, failure handling,
and secure-session capability checks; encrypted-stream playback is not yet
implemented.

### References

- [RDK DRM AIDL 0.22.0 / interface 0.1.0.0](https://github.com/rdkcentral/rdk-halif-aidl/tree/0.22.0/drm/0.1.0.0/com/rdk/hal/drm)
- [Android DRM AIDL VTS](https://android.googlesource.com/platform/hardware/interfaces/+/refs/heads/main/drm/aidl/vts/)
- [DRM API coverage matrix](drm_TestSpecification.md)
- [L2 Low Level Test Specification](drm_L2_Low-Level_TestSpecification.md)
- [L3 Low Level Test Specification](drm_L3_Low-Level_TestSpecification.md)

## Module Description

`IDrmFactory` advertises schemes and creates `IDrmPlugin` and `ICryptoPlugin`.
The DRM plugin manages sessions, license requests/responses, properties, and
key status. The crypto plugin binds a DRM session and supplies the AIDL
AVBuffer decryption path.

The test client, not the HAL, owns any license-server transport. The current
ClearKey tests generate a response from disposable fixture material or read a
response file; they do not perform HTTP exchange or vendor provisioning.

Tests adapt applicable Android VTS behavior to RDK's Binder backend and API.
Android-only offline-license/secure-stop APIs absent from the pinned RDK
interface cannot be covered without an interface extension.

## Delivery Phases

The delivery phases describe project integration milestones; they are
independent of the VTS L1/L2/L3 test-level names. In particular, an L3 test
group does not by itself mean that end-to-end playback has been implemented.

### Phase 1: DRM/Crypto functional bring-up and VTS

**Goal:** Prove that the DRM and Crypto HALs work independently of middleware,
applications, or a complete audio/video pipeline. ClearKey is the initial
reference implementation.

Phase 1 covers:

- Discover the factory and create DRM/Crypto plugins.
- Exercise DRM session lifecycle, key requests/responses, and key status.
- Associate the DRM session with Crypto and decrypt a known AES-encrypted
  sample through `ICryptoPlugin.decrypt(DecryptArgs)` using AVBuffer handles.
- Compare the decrypted bytes with an independent plaintext reference.
- Run the VTS without requiring OCDM, a player, or full middleware integration.

The current VTS has L1 factory/session coverage, L2 key-exchange and legacy
`IDrmPlugin.decrypt()` vector coverage, and an opt-in AVBuffer sample-decrypt
case under the L3 group. The AVBuffer case is a useful Phase 1 functional
check, but comprehensive AIDL crypto-path coverage (including buffer
ownership/lifetime, subsamples, patterns, and negative cases) remains
incomplete. These tests have not established target conformance merely by
being present or passing against the software reference service.

**Phase 1 exit criterion:** On the target, VTS can independently open a DRM
session, acquire/provide a key, bind the session to Crypto, decrypt a known
AES sample through AVBuffer, and verify exact plaintext output without decoder
playback.

### Phase 2: DRM integration with the AV pipeline

**Goal:** Connect the Phase 1 DRM/Crypto path to media processing and verify
encrypted content can be consumed by the target AV pipeline.

Phase 2 covers:

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

All six cases below are opt-in. They run only when L2 is enabled and do not
submit content to a decoder. “Implemented” describes a registered test case;
it does not imply the test has been run or passed on a target.

| Current test case | Current coverage |
|---|---|
| `fixtureValidation_pos` | Checks fixture readability, non-empty data, 16-byte KID/key/IV, and equal ciphertext/plaintext lengths; no HAL call |
| `keyRequestAndResponse_pos` | Opens a session, requests/provides a streaming key, binds the crypto session, and checks that key status is non-empty |
| `decryptKnownSample_pos` | Uses legacy `IDrmPlugin.decrypt()` and compares output with known plaintext |
| `removeKeysRejectsDecrypt_neg` | Checks that legacy decrypt fails after session keys are removed |
| `invalidInitDataRejected_neg` | Checks that a streaming key request with empty init data fails |
| `closedSessionRejectsOperations_neg` | Checks that a streaming key request using a closed session fails |

### L2 coverage gaps

The following are not currently implemented as L2 test cases:

- Known-vector decryption through `ICryptoPlugin.decrypt(DecryptArgs)` with
  AVBuffer handles.
- AIDL decrypt coverage for subsamples, patterns, bounds, handle ownership and
  lifetime, and related negative cases.
- Exact documented error-code assertions for the L2 negative cases.

An AVBuffer sample-decrypt case exists in the L3 group, described below. It
does not make the L2 AIDL decrypt coverage complete.

Detailed implemented procedures and remaining gaps are in the
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
