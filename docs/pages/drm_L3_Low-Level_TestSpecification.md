# DRM L3 Low Level Test Specification and Procedure Documentation

## Table of Contents

- [Overview](#overview)
- [Acronyms, Terms and Abbreviations](#acronyms-terms-and-abbreviations)
- [References](#references)
- [Test Configuration](#test-configuration)
- [Prerequisites](#prerequisites)
- [L3 Tests](#l3-tests)

## Overview

The DRM L3 group implements an opt-in AVBuffer-backed sample decrypt test, a
key-removal/session-close negative test, and a secure-session capability test.
They are not end-to-end encrypted playback tests. The sample test accepts a
pre-demuxed encrypted sample and compares the decrypted bytes against an
independent plaintext fixture; it does not parse a container, submit data to a
decoder, or validate audio/video output.

Full stream playback, key rotation, and protected-output policy tests remain
planned until the target-specific demux, license, decoder, secure-buffer, and
output-oracle integrations described below are available. L2 vector success
must not be reported as L3 playback success.

The L3 test bodies return early unless `DRM_L3_ENABLE=1`; the secure-session
case additionally requires `DRM_L3_SECURE_SESSION_ENABLE=1`. A disabled run is
not L3 validation.

## Acronyms, Terms and Abbreviations

- `DRM` - Digital Rights Management
- `KID` - Key Identifier
- `PTS` - Presentation Time Stamp
- `TEE` - Trusted Execution Environment
- `HDCP` - High-bandwidth Digital Content Protection
- `EOS` - End of Stream

## References

- [High Level Test Specification](drm_High-Level_TestSpecification.md)
- [L2 Low Level Test Specification](drm_L2_Low-Level_TestSpecification.md)
- [DRM Test Streams](drm_TestStreams.md)
- [Coverage matrix](drm_TestSpecification.md)

## Test Configuration

Before implementation, define a per-target playback configuration containing:

| Input | Requirement |
|---|---|
| Stream | Authorized encrypted media, scheme UUID, container/codecs, duration, and expected DRM metadata/KIDs |
| License | Disposable credentials and response handler/endpoint; expected request/response validation |
| Crypto | Supported modes/patterns, AVBuffer allocation/import, protected-memory flag, and ownership rules |
| Decoder | Audio/video decoder IDs, secure capability, routing, and output mode |
| Output oracle | Reference frames/audio or target-approved observation method, explicit tolerances, duration and timeout limits |
| Negative expectations | Exact error category, callback, and output restrictions for each failure |

The current implemented sample and security-level inputs are listed in the
[configuration template](../../drm_test_configuration/drm_test_config.yaml).
The template is documentation; tests consume the corresponding environment
variables directly.

## Full Playback Prerequisites

- Passing relevant AVBuffer and decoder tests on the selected DUT.
- Verified L2 `ICryptoPlugin.decrypt(DecryptArgs)` path and ownership contract.
- Media demux/parser that exposes stream DRM metadata and encrypted subsamples.
- A controlled license service or accepted scheme-specific test response.
- Defined output measurements and timeouts; no pass based solely on API success.
- For protected playback, real protected buffers/decoder/output-policy evidence.

## L3 Tests

### Test 1: AVBuffer-Backed Sample Decryption (Implemented)

| Title | Details |
|---|---|
| Function Name | `decryptSampleWithAvBuffer_pos` |
| Description | Acquire a key and compare AIDL AVBuffer decryption output with a known plaintext sample. |
| Test Group | L3 : 03 |
| Test Case ID | 001 |
| Priority | High |

**Pre-Conditions:** `DRM_L3_ENABLE=1` and all `l3.sample` fixture variables supplied.

**Dependencies:** DRM, AVBuffer, and AVBufferHelper services.

**User Interaction:** None once target output validation is defined.

**Test Procedure:**

| Variation / Steps | Description | Test Data | Expected Result | Notes |
|---|---|---|---|---|
| 01 | Open session and complete license exchange | Init data and configured license response | Request is created and response accepted | No network exchange is performed |
| 02 | Submit the encrypted sample through AIDL AVBuffer decryption | KID, IV, mode, subsample, source and destination handles | Decrypt succeeds and reports the expected byte count | Non-secure test buffers |
| 03 | Compare the output against the expected plaintext | Independent plaintext fixture | Bytes match exactly | Does not validate playback |
| 04 | Release DRM and AVBuffer resources | Session and allocated handles | Resources are released | Cleanup also runs on failure |

### Test 2: Removed-Key and Closed-Session Rejection (Implemented)

| Title | Details |
|---|---|
| Function Name | `removedKeysAndClosedSessionRejectDecrypt_neg` |
| Description | Verify decryption is rejected after keys are removed and after the session closes. |
| Test Group | L3 : 03 |
| Test Case ID | 002 |
| Priority | High |

**Pre-Conditions:** `DRM_L3_ENABLE=1` and valid sample/license fixtures supplied.

**Dependencies:** DRM, AVBuffer, and AVBufferHelper services.

**User Interaction:** None.

**Test Procedure:**

| Variation / Steps | Description | Test Data | Expected Result | Notes |
|---|---|---|---|---|
| 01 | Acquire a key and prepare an encrypted sample | Valid sample and license response | Session and buffers are ready | Should Pass |
| 02 | Remove keys and retry decryption | Active session and sample | Returns `ERROR_DRM_NO_LICENSE` | Negative case |
| 03 | Close the DRM session and retry decryption | Closed session and sample | Returns `ERROR_DRM_SESSION_NOT_OPENED` | Stale-key use is rejected |
| 04 | Release resources | Session and allocated handles | Resources are released | Should Pass |

Key rotation, invalid-license responses, and playback output restrictions are
not covered by this case.

### Test 3: Secure-Session Capability Admission (Implemented)

| Title | Details |
|---|---|
| Function Name | `secureSessionAndDecoderRequirement_pos` |
| Description | Check that the configured DRM security level opens and the plugin requires a secure decoder for the configured MIME type. |
| Test Group | L3 : 03 |
| Test Case ID | 003 |
| Priority | High |

**Pre-Conditions:** `DRM_L3_ENABLE=1`, `DRM_L3_SECURE_SESSION_ENABLE=1`, and a required security level are configured.

**Dependencies:** DRM factory with the configured security level.

**User Interaction:** Target-specific output-policy setup may be required.

**Test Procedure:**

| Variation / Steps | Description | Test Data | Expected Result | Notes |
|---|---|---|---|---|
| 01 | Open a session at the configured security level | `DRM_L3_REQUIRED_SECURITY_LEVEL` | Session reports the requested level | Target-specific setting |
| 02 | Query secure decoder requirement | `DRM_L3_MIME_TYPE` | Plugin reports that a secure decoder is required | Capability check only |
| 03 | Close the DRM session | Session ID | Session closes successfully | Should Pass |

This case does not create protected AVBuffers, route samples to a secure
decoder, or validate output policy. Those remain target-specific work.
