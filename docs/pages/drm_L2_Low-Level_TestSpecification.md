# DRM L2 Low Level Test Specification and Procedure Documentation

## Table of Contents

- [Overview](#overview)
- [Acronyms, Terms and Abbreviations](#acronyms-terms-and-abbreviations)
- [Definitions](#definitions)
- [References](#references)
- [Level 2 Test Procedure](#level-2-test-procedure)
- [Pending AIDL Data-Path Coverage](#pending-aidl-data-path-coverage)

## Overview

This document describes the six implemented `VTS_L2_DrmClearKey` tests in
[vts_drm_l2_clearkey_tests.cpp](../../src/vts_drm_l2_clearkey_tests.cpp).
All require `DRM_L2_ENABLE=1`. When disabled, the current cases return early;
their reported result is not evidence that a key exchange or decrypt ran.

### Acronyms, Terms and Abbreviations

- `DRM` - Digital Rights Management
- `KID` - Key Identifier
- `IV` - Initialization Vector
- `DUT` - Device Under Test

### Definitions

- `ut-core` - RDK testing framework wrapping the test runner.
- `ClearKeyFixture` - Files and disposable KID/key/IV values supplied through
  environment variables.
- `LicensedSession` - Test helper that creates plugins, opens a session,
  requests/provides a key response, binds crypto, and attempts session closure
  on destruction.

### References

- [High Level Test Specification](drm_High-Level_TestSpecification.md)
- [Test Streams and Vector Inputs](drm_TestStreams.md)
- [Configuration template](../../drm_test_configuration/drm_test_config.yaml)

## Level 2 Test Procedure

### Test 1

| Title | Details |
|---|---|
| Function Name | `VTS_L2_DrmClearKey.fixtureValidation_pos` |
| Description | Validate configured vector inputs before exercising DRM. |
| Test Group | L2 : 02 |
| Test Case ID | 001 |
| Priority | High |

**Pre-Conditions:** Set `DRM_L2_ENABLE=1` and all required fixture variables.

**Dependencies:** Readable init-data, ciphertext, and plaintext files; hexadecimal KID/key/IV.

**User Interaction:** None.

**Test Procedure:**

| Variation / Steps | Description | Test Data | Expected Result | Notes |
|---|---|---|---|---|
| 01 | Load fixture files | `DRM_L2_INIT_DATA`, `DRM_L2_CIPHERTEXT`, `DRM_L2_PLAINTEXT` | Files are readable and non-empty | No HAL call |
| 02 | Decode and validate hexadecimal values | `DRM_L2_KID_HEX`, `DRM_L2_KEY_HEX`, `DRM_L2_IV_HEX` | Each decodes to 16 bytes | Disposable inputs only |
| 03 | Check sample lengths | Ciphertext and plaintext | Byte lengths match | Checked by fixture loader |

### Test 2

| Title | Details |
|---|---|
| Function Name | `VTS_L2_DrmClearKey.keyRequestAndResponse_pos` |
| Description | Acquire a test key and query session key status. |
| Test Group | L2 : 02 |
| Test Case ID | 002 |
| Priority | High |

**Pre-Conditions:** L2 enabled; valid fixture and selected ClearKey factory.

**Dependencies:** Supported scheme, openable session, and accepted ClearKey response.

**User Interaction:** None.

**Test Procedure:**

| Variation / Steps | Description | Test Data | Expected Result | Notes |
|---|---|---|---|---|
| 01 | Create DRM/crypto plugins and open a session | Advertised UUID, DEFAULT security level | Calls succeed | `LicensedSession` helper |
| 02 | Generate a streaming request | Session ID, init data, `video/mp4`, STREAMING | Request succeeds and contains bytes | No HTTP transport |
| 03 | Provide generated JSON or configured response file | KID/key or `DRM_L2_LICENSE_RESPONSE` | `provideKeyResponse` succeeds | File is optional |
| 04 | Bind crypto and query key status | Session ID | Binding succeeds; status query succeeds and is non-empty | Does not validate vendor-specific status values |

### Test 3

| Title | Details |
|---|---|
| Function Name | `VTS_L2_DrmClearKey.decryptKnownSample_pos` |
| Description | Compare known-vector decrypt output with independent plaintext. |
| Test Group | L2 : 02 |
| Test Case ID | 003 |
| Priority | High |

**Pre-Conditions:** L2 enabled; valid fixture; matching disposable key response.

**Dependencies:** `LicensedSession` setup succeeds.

**User Interaction:** None.

**Test Procedure:**

| Variation / Steps | Description | Test Data | Expected Result | Notes |
|---|---|---|---|---|
| 01 | Acquire test key and bind session | Fixture | Setup succeeds | Same setup as Test 2 |
| 02 | Decrypt sample | Session, KID, ciphertext, IV | `IDrmPlugin.decrypt` succeeds | Legacy vector API |
| 03 | Compare full output | Decrypted bytes, plaintext | Exact byte-for-byte equality | No decoder or playback |

This test does not call `setCipherAlgorithm` or prove AVBuffer crypto routing.
Do not treat it as `ICryptoPlugin.decrypt(DecryptArgs)` conformance.

### Test 4

| Title | Details |
|---|---|
| Function Name | `VTS_L2_DrmClearKey.removeKeysRejectsDecrypt_neg` |
| Description | Reject decrypt after removing the acquired keys. |
| Test Group | L2 : 02 |
| Test Case ID | 004 |
| Priority | High |

**Pre-Conditions:** L2 enabled; valid fixture.

**Dependencies:** `LicensedSession` setup succeeds.

**User Interaction:** None.

**Test Procedure:**

| Variation / Steps | Description | Test Data | Expected Result | Notes |
|---|---|---|---|---|
| 01 | Acquire key and bind session | Fixture | Setup succeeds | No playback |
| 02 | Remove session keys | Session ID | `removeKeys` succeeds | Session remains open |
| 03 | Attempt decrypt | Same KID/ciphertext/IV | Call fails | Exact error code is not yet asserted |

### Test 5

| Title | Details |
|---|---|
| Function Name | `VTS_L2_DrmClearKey.invalidInitDataRejected_neg` |
| Description | Reject a streaming key request with empty init data. |
| Test Group | L2 : 02 |
| Test Case ID | 005 |
| Priority | Medium |

**Pre-Conditions:** L2 enabled; fixture loader succeeds.

**Dependencies:** Selected DRM factory and an openable session.

**User Interaction:** None.

**Test Procedure:**

| Variation / Steps | Description | Test Data | Expected Result | Notes |
|---|---|---|---|---|
| 01 | Create DRM plugin and open session | Advertised UUID, DEFAULT | Calls succeed | No license acquired |
| 02 | Request streaming key | Session, empty init data, `video/mp4` | Call fails | Exact error code is not yet asserted |
| 03 | Close session | Session ID | Close succeeds | Cleanup |

### Test 6

| Title | Details |
|---|---|
| Function Name | `VTS_L2_DrmClearKey.closedSessionRejectsOperations_neg` |
| Description | Reject key requests after session closure. |
| Test Group | L2 : 02 |
| Test Case ID | 006 |
| Priority | Medium |

**Pre-Conditions:** L2 enabled; fixture loader succeeds.

**Dependencies:** Selected DRM factory and an openable session.

**User Interaction:** None.

**Test Procedure:**

| Variation / Steps | Description | Test Data | Expected Result | Notes |
|---|---|---|---|---|
| 01 | Open and close a session | DEFAULT security level | Both calls succeed | Keep closed ID for next step |
| 02 | Request streaming key | Closed ID, valid init data, `video/mp4` | Call fails | Exact error code is not yet asserted |

## Pending AIDL Data-Path Coverage

The following are requirements, not registered tests:

- Known-vector decrypt using real `ICryptoPlugin.decrypt(DecryptArgs)` AVBuffer
  handles with an agreed allocation/import and ownership contract.
- Correct byte counts, clear/encrypted subsamples, supported modes/patterns,
  buffer bounds, reuse, and release.
- Unknown KID, missing license, malformed response, and invalid-buffer errors
  with exact documented error assertions.
- Key status semantics beyond a non-empty result, response/request correlation,
  callback delivery, and optional vendor license-server transport.

L2 remains partial until the AIDL crypto path is implemented and verified on a
DUT. No stream playback is part of these procedures.
