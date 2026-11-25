# DRM L3 Low-Level Test Specification

## Overview

The current L3 group contains seven opt-in functional checks covering AIDL
AVBuffer sample decryption, subsamples and patterns, invalid buffer inputs,
missing/removed keys and closed sessions, clear-segment pass-through, and
secure-session/decoder capability reporting.

For licensed sample decryption, the test obtains and provides a license
response, associates the DRM session with the crypto plugin, and submits the
encrypted sample through `ICryptoPlugin.decrypt(DecryptArgs)` using AVBuffer
handles. It validates the returned byte count and compares the output
byte-for-byte with an independent plaintext fixture. Other cases exercise
subsample/pattern handling, documented failures, and clear-only pass-through.
The input is already demultiplexed; the tests do not parse a media container or
submit samples to a decoder.

These are sample-level HAL/crypto checks, not end-to-end playback conformance.
The L3 test-suite label does not prove a specific DRM security level. The
secure-session test checks reported capability only; it does not decrypt into
protected buffers or verify display/output policy. See
[vts_drm_l3_tests.cpp](../../src/vts_drm_l3_tests.cpp) for the implementation.

## Crypto and functional coverage

| Case ID | Registered test name | Functional check | What a pass establishes |
|---:|---|---|---|
| 001 | `decryptSampleWithAvBuffer_pos` | Decrypt one configured sample through AIDL AVBuffers after acquiring and binding a key. | The crypto plugin accepts the configured KID, IV, mode, pattern, subsample, and buffers; reports the expected byte count; returns exactly the expected plaintext; and leaves the caller-owned source readable and unchanged. |
| 002 | `removedKeysAndClosedSessionRejectDecrypt_neg` | Decrypt successfully as a baseline, remove keys and retry, then close the session and retry. | The baseline crypto path works; missing keys return `ERROR_DRM_NO_LICENSE`, and a closed session returns `ERROR_DRM_SESSION_NOT_OPENED`. |
| 003 | `secureSessionAndDecoderRequirement_pos` | Open a session at the configured security level, query the session level, bind crypto, and query the secure-decoder requirement. | The reported session level matches the requested level and the configured codec requires a secure decoder. This validates capability reporting only. |
| 004 | `decryptSubsamplesAndPattern_pos` | Decrypt using adjacent encrypted subsamples or the configured encryption pattern; for unpatterned AES-CTR, also decrypt a sample with a clear tail. | The returned byte count and output match the independent plaintext fixture for each tested subsample arrangement. |
| 005 | `decryptBoundsAndInvalidInputsRejected_neg` | Submit undersized destinations, short sources, invalid handles, and an empty subsample list. | Invalid inputs return the corresponding documented errors, including `ERROR_DRM_FRAME_TOO_LARGE`, `ERROR_DRM_DECRYPT`, or `ERROR_DRM_ZERO_SUBSAMPLES`. |
| 006 | `decryptClearSegmentWithAvBuffer_pos` | Submit deterministic clear-only data with `Mode::UNENCRYPTED` through AVBuffers. | The call reports the full size and preserves the bytes. This is API/buffer pass-through only; no key, ciphertext, or cryptographic transform is involved. |
| 007 | `decryptWithoutKeysRejected_neg` | Bind a session without acquiring a key and attempt to decrypt the configured encrypted sample. | Decryption fails with `ERROR_DRM_NO_LICENSE`. |

For cases 001, 002, 004, and 005, if the license or plugin requires protected
output, the test skips CPU-readable buffer comparison and non-secure decrypt
checks. A skip does not demonstrate protected decryption.

## Configuration and prerequisites

Enable the sample tests with `DRM_L3_ENABLE=1` (or
`drm.l3.enabled: true`) and provide init data, encrypted sample, expected
plaintext, KID, IV, and MIME type. Cases 001, 002, 004, and 005 also require an
accepted license response, supplied locally or through the configured HTTP
adapter; case 007 deliberately runs without a license. Configure matching mode
and pattern values when using patterned encryption. AVBuffer cases also
require the DRM, AVBuffer, and AVBufferHelper services.

Enable case 003 separately with `DRM_L3_SECURE_SESSION_ENABLE=1` (or
`drm.l3.secure_session.enabled: true`) and configure the required security
level and MIME/codec values. If an optional capability is not advertised, the
test may skip; a skip is not a pass.

The full-playback prerequisites below are not part of the current seven test
cases. End-to-end playback would additionally require:

- Container demux and extraction of DRM metadata and encrypted subsamples.
- Decoder routing, including secure-decoder selection where required.
- Protected-buffer handling and output-policy verification.
- An independent audio/video output oracle with defined tolerances and
  timeouts.

## References

- [High-Level Test Specification](drm_High-Level_TestSpecification.md)
- [L2 Low-Level Test Specification](drm_L2_Low-Level_TestSpecification.md)
- [DRM Test Streams](drm_TestStreams.md)
- [Combined test overview](drm_TestSpecification.md)
- [Configuration template](../../drm_test_configuration/drm_test_config.yaml)
