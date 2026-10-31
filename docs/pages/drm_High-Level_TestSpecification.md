# DRM High Level Test Specification Document

## Table of Contents

- [Acronyms, Terms and Abbreviations](#acronyms-terms-and-abbreviations)
- [Introduction](#introduction)
- [Module Description](#module-description)
- [Test Streams Specification](#test-streams-specification)
- [L1 Test Specification](#l1-test-specification)
- [L2 Test Specification](#l2-test-specification)
- [L3 Test Specification](#l3-test-specification)
- [Implementation Status](#implementation-status)

## Acronyms, Terms and Abbreviations

- `AIDL` - Android Interface Definition Language
- `API` - Application Programming Interface
- `DRM` - Digital Rights Management
- `DUT` - Device Under Test
- `HAL` - Hardware Abstraction Layer
- `HFP` - HAL Feature Profile
- `KID` - Key Identifier
- `L1/L2/L3` - VTS test levels, not Widevine security levels
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

## Test Streams Specification

See [DRM Test Streams](drm_TestStreams.md) for L2 vector inputs and future L3
stream requirements. No production keys or licensed media are packaged here.

## L1 Test Specification

The `VTS_L1_Drm` group contains 15 cases documented directly in
[vts_drm_l1_tests.cpp](../../src/vts_drm_l1_tests.cpp).

| Test feature | Description | HAL APIs |
|---|---|---|
| Factory and scheme discovery | Check generated metadata, non-empty unique UUIDs, content MIME/security ranges | `getInterfaceVersion`, `getInterfaceHash`, `getSupportedCryptoSchemes` |
| Plugin creation | Create plugins for an advertised UUID; reject an unsupported UUID | `createDrmPlugin`, `createCryptoPlugin` |
| Session lifecycle | Open, query security, register listener, bind crypto, close | `openSession`, `getSecurityLevel`, `setListener`, `setMediaDrmSession`, `closeSession` |
| Invalid session behavior | Assert Binder exceptions or service-specific errors for empty/closed IDs and repeated close | Session, key-status/removal, cipher/MAC, playback-ID, encrypt/decrypt/sign/verify APIs |
| Capabilities and properties | Check HDCP enum values, session counts, required strings, invalid property names, metrics outcomes, secure-decoder query status | Capability/property getters and setters |

L1 does not acquire a valid license or play media. Current tests select the
first UUID advertised by the selected factory; they do not enumerate all
factories or validate results against HFP declarations. Listener registration
does not establish callback delivery. These remain coverage gaps.

## L2 Test Specification

| Test | Description | HAL APIs | Status |
|---|---|---|---|
| Fixture validation | Validate files, field sizes, and equal ciphertext/plaintext lengths | Test helper only | Implemented |
| License request/response | Request a streaming key, provide response, bind session, query status | `openSession`, `getKeyRequest`, `provideKeyResponse`, `setMediaDrmSession`, `queryKeyStatus` | Implemented, local response/file only |
| Known-vector decrypt | Compare decrypted bytes with reference plaintext | `IDrmPlugin.decrypt` | Implemented legacy API |
| Key removal | Reject decrypt after removing the session keys | `removeKeys`, `IDrmPlugin.decrypt` | Implemented |
| Invalid init data / closed session | Reject empty init data and requests using a closed ID | `getKeyRequest`, `closeSession` | Implemented |
| AIDL AVBuffer decrypt | Verify decrypt vectors, subsamples, patterns, handle ownership, and negative cases | `ICryptoPlugin.decrypt(DecryptArgs)` | One L3 sample case implemented; broader L2 matrix planned |

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

| Feature | Required validation | Status |
|---|---|---|
| AVBuffer-backed sample decryption | Compare AIDL decrypt output with an independent plaintext sample | Implemented; does not parse a stream or submit to a decoder |
| Key removal and closed-session failure | Reject decrypt after keys are removed and after session closure | Implemented |
| Secure-session capability | Check configured session security level and secure-decoder requirement | Implemented; does not exercise protected buffers |
| DRM metadata detection | Detect scheme/KID and extract init data from a media stream | Planned |
| End-to-end playback | Submit encrypted media through the target pipeline and validate output | Planned |
| Key rotation and license failures | Validate rotation and scheme-specific failure behavior in playback | Planned |
| Protected playback/output policy | Verify protected-buffer routing and output restrictions | Planned |
| Full playback cleanup | Release decoder, crypto, session, and buffers across playback failures | Planned |

See the [L3 specification](drm_L3_Low-Level_TestSpecification.md).

## Implementation Status

| Level | Executable group | Current limitation |
|---|---|---|
| L1 | `VTS_L1_Drm` | API baseline, not complete Android VTS or HFP parity |
| L2 | `VTS_L2_DrmClearKey` | Six opt-in cases; legacy vector decrypt, no AVBuffer decrypt or HTTP exchange |
| L3 | `VTS_L3_Drm` | Three opt-in partial cases; no demux, decoder playback, key rotation, or protected-output validation |

The profile and [configuration template](../../drm_test_configuration/drm_test_config.yaml)
document target inputs. Current code reads environment variables rather than
parsing these YAML files. Successful compilation does not establish DUT
conformance. Disabled L2 cases currently return early; such a run is not L2
validation.
