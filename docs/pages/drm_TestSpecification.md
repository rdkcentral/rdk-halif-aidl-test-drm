# DRM Test Matrix

## Documentation

- [High-Level Test Specification](drm_High-Level_TestSpecification.md)
- [L2 Low-Level Test Specification](drm_L2_Low-Level_TestSpecification.md)
- [L3 Low-Level Test Specification](drm_L3_Low-Level_TestSpecification.md)
- [Test Streams and Vector Inputs](drm_TestStreams.md)

## L2: license/key exchange and legacy sample crypto

L2 checks DRM session and license operations, the legacy
`IDrmPlugin.decrypt()` known-vector path, and documented errors. The test opens
a session, requests and provides a license response, binds the session to the
crypto plugin, and checks key status. The ClearKey-specific vector cases
compare decrypted output byte-for-byte with independently prepared plaintext.
L2 does not exercise AIDL AVBuffer decryption; those cases are in L3.

Passing L2 cases does not establish decoder integration or playback.

L2 tests are opt-in. Set `drm.l2.enabled: true` or `DRM_L2_ENABLE=1` and
provide the configured input fixtures. Local response files and the optional
HTTP license adapter are described in
[Test Streams and Vector Inputs](drm_TestStreams.md). Optional capabilities
and missing optional fixtures may cause a skip; a skip is not a pass.

See the [L2 Low-Level Test Specification](drm_L2_Low-Level_TestSpecification.md)
for the registered case-by-case behavior and expected results.

## L3: AVBuffer sample crypto and secure-session capability checks

The current L3 group contains seven functional checks:

| Case ID | Test | What it verifies |
|---:|---|---|
| 001 | Decrypt a configured sample using AIDL AVBuffers. | The crypto plugin returns the expected byte count and exact plaintext while preserving the caller-owned source buffer. |
| 002 | Retry AVBuffer decryption after removing keys and closing the session. | The valid baseline works; the invalid states return `ERROR_DRM_NO_LICENSE` and `ERROR_DRM_SESSION_NOT_OPENED`. |
| 003 | Check the configured session security level and secure-decoder requirement. | The session reports the requested level and the plugin reports that the configured codec requires a secure decoder. This is capability reporting only. |
| 004 | Exercise encrypted subsamples, optional encryption patterns, and clear-tail subsamples for unpatterned AES-CTR. | Every successful output matches the independent plaintext fixture. |
| 005 | Submit undersized buffers, short source data, invalid handles, and no subsamples. | The plugin returns documented bounds and invalid-input errors. |
| 006 | Submit deterministic clear-only data in `UNENCRYPTED` mode through AVBuffers. | The output matches the input; this is pass-through only and performs no cryptographic transform. |
| 007 | Attempt encrypted AVBuffer decryption without acquiring a key. | Decryption fails with `ERROR_DRM_NO_LICENSE`. |

These tests do not parse a media container, process key rotation, send data to
a decoder, or validate playback or protected output. If only protected output
is allowed, CPU-readable sample checks may skip; that skip does not demonstrate
protected decryption.

Full playback coverage remains separate and requires:

```text
encrypted stream and DRM metadata
  -> demux and extract init data/subsamples
  -> DRM session and license/key exchange
  -> bind session to crypto plugin
  -> AVBuffer decryption and decoder input
  -> playback and output-policy verification
```

End-to-end validation must verify demux, decoder selection and acceptance,
playback output, cleanup, and protected output when claimed. A passing sample
decrypt case is not a substitute.

See the [L3 Low-Level Test Specification](drm_L3_Low-Level_TestSpecification.md)
for prerequisites, configuration, and expected results.
