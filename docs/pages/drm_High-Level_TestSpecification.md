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
playback, and full L3 coverage validates encrypted-stream playback. Current L3
coverage is limited to sample-level decryption, failure handling, and secure
session capability checks.

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

| Test | Description | HAL APIs |
|---|---|---|
| Fixture validation | Validate files, field sizes, and equal ciphertext/plaintext lengths | Test helper only |
| License request/response | Request a streaming key, provide response, bind session, query status | `openSession`, `getKeyRequest`, `provideKeyResponse`, `setMediaDrmSession`, `queryKeyStatus` |
| Known-vector decrypt | Compare decrypted bytes with reference plaintext | `IDrmPlugin.decrypt` |
| Key removal | Reject decrypt after removing the session keys | `removeKeys`, `IDrmPlugin.decrypt` |
| Invalid init data / closed session | Reject empty init data and requests using a closed ID | `getKeyRequest`, `closeSession` |
| AIDL AVBuffer decrypt (coverage gap) | Verify decrypt vectors, subsamples, patterns, handle ownership, and negative cases; a sample-level case currently exists in the L3 group | `ICryptoPlugin.decrypt(DecryptArgs)` |

Detailed implemented procedures and remaining gaps are in the
[L2 specification](drm_L2_Low-Level_TestSpecification.md).
L2 must not submit data to a decoder or claim playback coverage.

## L3 Test Specification

### Prerequisites

L3 requires validated AVBuffer allocation/import and ownership, L2 AIDL decrypt
coverage, the target demux/media pipeline and decoders, disposable encrypted
streams, and a defined output oracle. Secure-path claims additionally require
protected buffers, secure decoder support, and target output-policy validation.

### Feature List

| Feature | Required validation |
|---|---|
| AVBuffer-backed sample decryption | Compare AIDL decrypt output with an independent plaintext sample |
| Key removal and closed-session failure | Reject decrypt after keys are removed and after session closure |
| Secure-session capability | Check configured session security level and secure-decoder requirement |
| DRM metadata detection | Detect scheme/KID and extract init data from a media stream |
| End-to-end playback | Submit encrypted media through the target pipeline and validate output |
| Key rotation and license failures | Validate rotation and scheme-specific failure behavior in playback |
| Protected playback/output policy | Verify protected-buffer routing and output restrictions |
| Full playback cleanup | Release decoder, crypto, session, and buffers across playback failures |

See the [L3 specification](drm_L3_Low-Level_TestSpecification.md).
