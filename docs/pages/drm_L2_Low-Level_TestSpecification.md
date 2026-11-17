# DRM L2 Low Level Test Specification and Procedure Documentation

## Table of Contents

- [Overview](#overview)
- [Acronyms, Terms and Abbreviations](#acronyms-terms-and-abbreviations)
- [Definitions](#definitions)
- [References](#references)
- [Level 2 Test Procedure](#level-2-test-procedure)
- [AIDL Data-Path Procedures](#aidl-data-path-procedures)

## Overview

This document describes the validate init-data availability, key exchange/status, empty-response rejection
and closed-session rejection. Legacy vector decryption/key removal and
empty-init-data rejection remain ClearKey-only. AIDL AVBuffer cases are
registered for all schemes; sample-vector cases skip when that profile has no
vector fixtures. They check `ICryptoPlugin.decrypt(DecryptArgs)` without
submitting data to a decoder. The three groups share 13 registrations; the two
legacy crypto cases skip for vendor profiles. ClearKey adds seven
ClearKey-specific cases for callbacks, test-hook errors, malformed keys, and
security-level support.

### Acronyms, Terms and Abbreviations

- `DRM` - Digital Rights Management
- `KID` - Key Identifier
- `IV` - Initialization Vector
- `DUT` - Device Under Test

### Definitions

- `ut-core` - RDK testing framework wrapping the test runner.
- `DrmFixture` - Scheme-specific init data and optional known-vector inputs.
  Raw keys are required only for generated ClearKey JSON.
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
| Function Name | `VTS_L2_DrmClearKey.keyRequestAndResponse_pos` |
| Description | Acquire a test key and query session key status. |
| Test Group | L2 : 02 |
| Test Case ID | 001 |
| Priority | High |

**Pre-Conditions:** L2 enabled; scheme-specific init data and selected factory.

**Dependencies:** Supported scheme, provisioned device, openable session and
accepted scheme-specific response or live license adapter.

**User Interaction:** None.

**Test Procedure:**

| Variation / Steps | Description | Test Data | Expected Result | Notes |
|---|---|---|---|---|
| 01 | Create DRM/crypto plugins and open a session | Exact configured UUID, configured security level | Calls succeed | Same common case in all three groups |
| 02 | Generate a streaming request | Session ID, scheme init data, configured MIME, STREAMING | Request succeeds and contains bytes | Plugin-generated challenge |
| 03 | Acquire and provide the license response | HTTP POST or response file; optional ClearKey-only JSON generation | `provideKeyResponse` succeeds | No raw Widevine/PlayReady key required |
| 04 | Bind crypto and query key status | Session ID | Binding succeeds; status query succeeds and is non-empty | Does not validate vendor-specific status values |

### Test 2

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
| 01 | Acquire test key and bind session | Fixture | Setup succeeds | Same setup as Test 1 |
| 02 | Decrypt sample | Session, KID, ciphertext, IV | `IDrmPlugin.decrypt` succeeds | Legacy vector API |
| 03 | Compare full output | Decrypted bytes, plaintext | Exact byte-for-byte equality | No decoder or playback |

This test does not call `setCipherAlgorithm` or prove AVBuffer crypto routing.
Do not treat it as `ICryptoPlugin.decrypt(DecryptArgs)` conformance.

### Test 3

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
| 03 | Attempt decrypt | Same KID/ciphertext/IV | Returns `ERROR_DRM_NO_LICENSE` | Documented error |

### Test 4

| Title | Details |
|---|---|
| Function Name | `VTS_L2_DrmClearKey.invalidInitDataRejected_neg` |
| Description | Reject a streaming key request with empty init data. |
| Test Group | L2 : 02 |
| Test Case ID | 007 |
| Priority | Medium |

**Pre-Conditions:** ClearKey L2 enabled. This case is not generalized to vendor
schemes, where empty init data can be valid for renewal.

**Dependencies:** Selected DRM factory and an openable session.

**User Interaction:** None.

**Test Procedure:**

| Variation / Steps | Description | Test Data | Expected Result | Notes |
|---|---|---|---|---|
| 01 | Create DRM plugin and open session | Advertised UUID, DEFAULT | Calls succeed | No license acquired |
| 02 | Request streaming key | Session, empty init data, `video/mp4` | Returns `BAD_VALUE` | Documented invalid-parameter error |
| 03 | Close session | Session ID | Close succeeds | Cleanup |

### Test 5

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
| 02 | Request streaming key | Closed ID, scheme init data, configured MIME | Returns `ERROR_DRM_SESSION_NOT_OPENED` | Shared by all schemes |

### Test 6: Empty License Response

`emptyLicenseResponseRejected_neg` (Test Case ID 005) is shared by all three groups. Open a
session at the configured level, successfully request a license using valid
scheme init data/MIME, then call `provideKeyResponse` with an empty response.
The call must return `BAD_VALUE`; close the session. No valid license response,
raw key or encrypted sample is required for this negative case.

### Test 7: Init-Data Fixture Validation

`fixtureValidation_pos` (Test Case ID 001) is shared by all three groups. The configured
init-data file must be readable and non-empty. Its scheme-specific validity
is checked by `getKeyRequest` in the operational cases. The legacy ClearKey
vector cases separately validate 16-byte KID/IV and matching non-empty
ciphertext/plaintext sizes.

## AIDL Data-Path Procedures

The following cases are registered for ClearKey, Widevine and PlayReady. They
skip when the scheme profile has no L2 ciphertext, plaintext, KID and IV
fixtures. A partially configured vector is an error. The fixture's mode
defaults to `AES_CTR`; `pattern.encrypt_blocks` and `pattern.skip_blocks`
default to zero. A non-zero pattern must match the supplied ciphertext and
plaintext. These tests use non-secure AVBuffers only; decrypt operations skip
when the plugin reports that the license requires protected output.

| Function Name | Test Case ID | Procedure | Required assertion |
|---|---:|---|---|
| `decryptClearSegmentWithAvBuffer_pos` | 012 | Open a session, bind crypto, and decrypt generated clear bytes using one clear subsample and `UNENCRYPTED` mode | AIDL succeeds; returned byte count and output match input; no license or decoder |
| `decryptKnownSampleWithAvBuffer_pos` | 008 | Provide the test key, place ciphertext in a caller-owned source AVBuffer, decrypt to a distinct destination using configured mode/pattern and one encrypted subsample, compare all output bytes, then read and free the source/destination handles | AIDL succeeds; returned byte count equals plaintext size; output equals the independent plaintext; source remains caller-owned |
| `decryptSubsamplesAndPattern_pos` | 009 | Decrypt the known vector using two adjacent encrypted subsamples. For an AES-CTR vector with no pattern, also split the vector into an encrypted prefix and clear trailing subsample, with the clear source bytes copied from the plaintext fixture | Both outputs match the complete plaintext and byte count; configured pattern vector is checked with its configured pattern |
| `decryptBoundsAndInvalidInputsRejected_neg` | 010 | Test an undersized destination, a source buffer shorter than the declared subsamples, empty subsamples, and unallocated source/destination handles | Returns `ERROR_DRM_FRAME_TOO_LARGE`, `ERROR_DRM_DECRYPT`, and `ERROR_DRM_ZERO_SUBSAMPLES` for the corresponding invalid input |
| `decryptWithoutKeysRejected_neg` | 013 | Open a fresh session without providing a key, then attempt encrypted AIDL decrypt using the configured vector | Returns `ERROR_DRM_NO_LICENSE` |
| `removedKeysAndClosedSessionRejectAvBufferDecrypt_neg` | 011 | Verify a valid decrypt baseline, remove session keys and retry, close the session and retry again | Returns `ERROR_DRM_NO_LICENSE` after key removal and `ERROR_DRM_SESSION_NOT_OPENED` after closure |
| `offlineKeySetCreated_pos` | 014 | Request an OFFLINE key and provide the configured offline response when supported | Successful response returns a non-empty key-set ID; unsupported schemes or missing optional response are skipped |

`decryptKnownSampleWithAvBuffer_pos` explicitly releases both AVBuffer handles
after the synchronous AIDL call. The test does not transfer handle ownership
to the DRM plugin or pass decrypted data to a decoder. The multi-subsample and
clear-range variation uses AES-CTR so splitting does not change the configured
fixture's cipher state. Other modes and non-zero patterns are still checked
through the full-sample, configured-vector case.

Existing negative cases assert their documented status values: empty
`provideKeyResponse` and ClearKey empty-init-data `getKeyRequest` return
`BAD_VALUE`; a closed-session key request returns
`ERROR_DRM_SESSION_NOT_OPENED`; and legacy decrypt after `removeKeys` returns
`ERROR_DRM_NO_LICENSE`. A fresh-session AIDL decrypt with no key also returns
`ERROR_DRM_NO_LICENSE`. Empty subsamples return
`ERROR_DRM_ZERO_SUBSAMPLES`, an undersized output buffer returns
`ERROR_DRM_FRAME_TOO_LARGE`, and an invalid AVBuffer/source range returns
`ERROR_DRM_DECRYPT`. Vendor empty-init-data requests are not treated as
invalid because empty init data can be valid for renewal.

The following additional cases are ClearKey-specific:

| Function Name | Procedure | Required assertion |
|---|---|---|
| `listenerCallbacks_pos` | Register a listener before providing a valid key response | Vendor event, expiration, and key-change callbacks carry the expected session/KID payloads |
| `getKeyRequestResourceContention_neg` | Inject a one-shot resource-contention error with `rdkVtsError` | Returns `ERROR_DRM_RESOURCE_CONTENTION` |
| `getKeyRequestInvalidStateAndSessionLost_neg` | Inject invalid state on a streaming key request | Returns `ERROR_DRM_INVALID_STATE` and emits `onSessionLostState` for the same session |
| `malformedClearKeyEmptyKeyRejected_neg`, `malformedClearKeyLongKeyRejected_neg` | Provide JWK responses with an empty or 17-byte AES key | Returns `ERROR_DRM_LICENSE_PARSE` before decrypt |
| `badLevelNotSupported_neg` | Query ClearKey support for `HW_SECURE_ALL` | The factory does not advertise support |

`rdkVtsError` is a test-only property implemented by the local reference
ClearKey service; other services may skip those injection cases. The RDK AIDL
0.2.0.0 contract exposes an OFFLINE request and key-set ID result but no
offline-license enumeration, restoration, state-query, or removal methods.
Therefore `offlineKeySetCreated_pos` is not full Android offline lifecycle
coverage. The reference ClearKey service is streaming-only and skips the
unsupported offline case.

The common HAL workflow does not replace vendor qualification or protected-path
tests. The L3 AVBuffer cases remain separate. No stream playback is part of
these procedures.
