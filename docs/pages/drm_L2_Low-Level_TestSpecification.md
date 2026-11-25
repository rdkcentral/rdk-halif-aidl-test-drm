# DRM L2 Low-Level Test Specification

## Overview

This document describes the functional checks registered in the current L2
test group. L2 is a test-suite category; it is not itself a DRM security-level
claim. The cases exercise session and license handling, the legacy DRM-plugin
decrypt API for known vectors, and documented error behavior.

The known-vector cases supply configured encrypted samples, KID, and IV,
then compare `IDrmPlugin.decrypt()` output byte-for-byte with an independently
prepared plaintext fixture. L2 does not exercise AIDL AVBuffer decryption;
that coverage is in the L3 group. These are HAL/API checks: they do not submit
decrypted media to a decoder or prove playback, protected output, or end-to-end
content protection.

The cases listed below are the current source registrations. Some require
optional fixtures, a supported capability, or an explicit test setting and
therefore may skip. Several later cases check behaviors supported only by
particular plugin profiles; they are not universal requirements for every DRM
implementation. A skip is not a pass. See
[vts_drm_l2_tests.cpp](../../src/vts_drm_l2_tests.cpp) for the implementation.

## What crypto functionality is checked

1. **Session-to-crypto binding:** obtain a streaming key response, associate
   the DRM session with the crypto plugin, and confirm the key is usable.
2. **Legacy decryption correctness:** decrypt known ClearKey data with the
   configured KID and IV and compare the output with independent plaintext.
3. **Negative behavior:** verify legacy decryption is rejected after key
   removal and that invalid sessions, init data, and license responses return
   their expected errors.

Cases 003 and 004 exercise the legacy decrypt API and may skip when the
selected plugin profile does not support that path. They do not exercise or
replace the AIDL AVBuffer checks in L3.

## Registered L2 test cases

The source registers the tests in Test Case ID order. The test name remains
the stable way to identify a case in console output.

| Case ID | Registered test name | Functional check | Expected behavior |
|---:|---|---|---|
| 001 | `fixtureValidation_pos` | Validate the configured init-data fixture. | The file is readable and non-empty. This checks fixture availability, not license validity. |
| 002 | `keyRequestAndResponse_pos` | Open a session, request and provide a streaming license, bind crypto, and query key status. | Calls succeed and the request and key-status results are non-empty. |
| 003 | `decryptKnownSample_pos` | Decrypt a known sample through the legacy `IDrmPlugin.decrypt()` API. | Decryption succeeds and output exactly matches the expected plaintext. This does not test the AVBuffer path. |
| 004 | `removeKeysRejectsDecrypt_neg` | Establish a successful legacy decrypt baseline, remove session keys, then decrypt again. | The retry fails with `ERROR_DRM_NO_LICENSE`. |
| 005 | `emptyLicenseResponseRejected_neg` | Provide an empty license response after generating a valid request. | `provideKeyResponse` fails with `BAD_VALUE`. |
| 006 | `closedSessionRejectsOperations_neg` | Request a key using a session that has already been closed. | The request fails with `ERROR_DRM_SESSION_NOT_OPENED`. |
| 007 | `invalidInitDataRejected_neg` | Request a streaming key with empty init data. | The request fails with `BAD_VALUE`. This is a profile-specific negative case, not a universal rule for every DRM plugin. |
| 008 | `offlineKeySetCreated_pos` | Request and provide an offline license. | A supported offline flow returns a non-empty key-set identifier. Unsupported or unconfigured offline flows may skip. |
| 009 | `listenerCallbacks_pos` | Register a listener and check license-related event, expiration, and key-change callbacks. | Callback session and payload values match the configured test fixture. |
| 010 | `getKeyRequestResourceContention_neg` | Inject resource contention into a key request. | The request fails with `ERROR_DRM_RESOURCE_CONTENTION`. |
| 011 | `getKeyRequestInvalidStateAndSessionLost_neg` | Inject invalid state during a key request and observe the session-lost callback. | The request fails with `ERROR_DRM_INVALID_STATE`, and the callback identifies the affected session. |
| 012 | `malformedClearKeyEmptyKeyRejected_neg` | Provide a malformed license containing an empty content key. | The response is rejected with `ERROR_DRM_LICENSE_PARSE`. |
| 013 | `malformedClearKeyLongKeyRejected_neg` | Provide a malformed license containing an over-length content key. | The response is rejected with `ERROR_DRM_LICENSE_PARSE`. |
| 014 | `badLevelNotSupported_neg` | Query support for `video/mp4` at `HW_SECURE_ALL`. | The profile-specific check confirms this security-level/MIME combination is not advertised; it is not a general requirement for every plugin. |
| 015 | `provisioningStatusAndExchange_pos` | Check session-open behavior at `HW_SECURE_ALL` and `SW_SECURE_CRYPTO`, and perform provisioning exchange when required and enabled. | The plugin returns documented status; when provisioning is required, the response is accepted and a session can then be opened. |

## Inputs and result interpretation

The YAML fields and environment-variable overrides for L2 fixtures, license
transport, KID, IV, ciphertext, and plaintext are documented in
[DRM Test Streams](drm_TestStreams.md). License responses may come from the
configured local response or HTTP adapter; the test does not require an
external license service when using a local fixture.

Passing these cases demonstrates the specific HAL operations and sample
decryption behavior described above. It does not validate container parsing,
decoder integration, real-time playback, secure-memory routing, or output
protection policy.

## References

- [High-Level Test Specification](drm_High-Level_TestSpecification.md)
- [L3 Low-Level Test Specification](drm_L3_Low-Level_TestSpecification.md)
- [Combined test overview](drm_TestSpecification.md)
- [Configuration template](../../drm_test_configuration/drm_test_config.yaml)
