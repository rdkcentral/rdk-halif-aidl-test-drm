# DRM Test Matrix

## Documentation

- [High Level Test Specification](drm_High-Level_TestSpecification.md)
- [L2 Low Level Test Specification](drm_L2_Low-Level_TestSpecification.md)
- [L3 Low Level Test Specification](drm_L3_Low-Level_TestSpecification.md)
- [Test Streams and Vector Inputs](drm_TestStreams.md)

## L2: license/key exchange and crypto data path (no playback)

The detailed procedures are in the
[L2 Low Level Test Specification](drm_L2_Low-Level_TestSpecification.md).
L2 must establish that a DRM scheme can acquire usable keys and decrypt known
test vectors through the RDK AIDL crypto API. It does not play a media stream
through a decoder. ClearKey tests are opt-in because vectors and keys are
target/test-run inputs, not repository data.

1. Open a session.
2. Build a known ClearKey init-data blob containing the test KID.
3. Call `getKeyRequest(session, initData, mime, STREAMING, parameters)`.
4. Provide a test license response. The current ClearKey test generates a
   deterministic JSON response from the disposable KID/key fixture or reads a
   configured response file; it does not yet send an HTTP request.
5. Call `provideKeyResponse(session, response)`.
6. Call `queryKeyStatus(session)` and verify the test key is usable.
7. Create a crypto plugin and bind it with `setMediaDrmSession(session)`.
8. Use `ICryptoPlugin.decrypt(DecryptArgs)` with valid AVBuffer handles to
   decrypt a known AES-CTR test vector and compare it with the known plaintext.
9. Exercise malformed init data, malformed response, unknown KID, no-license,
   and closed-session paths; assert the documented error category. Current
   tests cover empty init data, key removal, and a closed-session request;
   exact error-code assertions remain pending target validation.
10. Remove keys and close the session; verify subsequent decryption fails with
    `ERROR_DRM_NO_LICENSE` or the implementation's documented equivalent.

**Coverage limitation:** `decryptKnownSample_pos` currently calls the legacy
`IDrmPlugin.decrypt()` vector API. It does not call
`ICryptoPlugin.decrypt(DecryptArgs)` and therefore does not prove the RDK AIDL
AVBuffer crypto data path. Treat L2 as partial until the AIDL decrypt,
subsample/pattern, and buffer-lifetime cases are implemented and run.

Set YAML `drm.l2.enabled: true` (or `DRM_L2_ENABLE=1`) and provide the vector inputs documented
in [Test Streams and Vector Inputs](drm_TestStreams.md) to execute these cases.
Disabled cases return early in the current runner; their reported results
must not be interpreted as executed L2 coverage.

HTTP/license-server integration is future work. Vendor-controlled Widevine or
PlayReady endpoints require scheme-specific test credentials, request formats,
and security state and are not exercised by the current ClearKey tests.

## L3: sample crypto coverage and end-to-end playback

The implemented opt-in `VTS_L3_Drm` group has three bounded cases:

| Test | Current validation |
|---|---|
| `decryptSampleWithAvBuffer_pos` | Acquire a key, decrypt a configured sample using `ICryptoPlugin.decrypt(DecryptArgs)` with AVBuffer handles, and compare the bytes with an independent plaintext |
| `removedKeysAndClosedSessionRejectDecrypt_neg` | Confirm decryption fails after key removal and after session closure |
| `secureSessionAndDecoderRequirement_pos` | Optionally check a configured session security level and secure-decoder requirement |

These checks do not parse a media container, process KID rotation, feed a
decoder, or validate playback or protected output. They are useful integration
checks but are not end-to-end L3 playback conformance.

The full playback flow remains planned:

```text
encrypted stream with DRM metadata
  -> detect DRM scheme and extract init data
  -> IDrmPlugin session and license/key exchange
  -> ICryptoPlugin.setMediaDrmSession
  -> AVBuffer decrypt and decoder input
  -> playback/frame and output-policy verification
```

Full L3 will reuse validated crypto handling in actual playback and verify
stream demux/DRM metadata, decoder selection and acceptance, resolution
notification, playback output, cleanup, and protected output where claimed.
Widevine L1 secure-path coverage additionally requires the OEMCrypto/TEE,
protected memory, secure decoder, display policy, and HDCP setup; it is not a
ClearKey substitute.

## Blocking prerequisites

- Pin the `rdk-halif-aidl` revision and export generated DRM headers/libs.
- Confirm the exact Binder service lookup and instance configuration on the
  DUT.
- Add `hfp-drm.yaml` parsing and a DRM test configuration format.
- Provide a ClearKey UUID, KID, encrypted sample, plaintext, and test key.
- Provide AVBuffer allocation/import helpers for L2 AIDL decrypt tests.
- Provide an encrypted stream and playback/output validation method for
  end-to-end L3; the current sample test is not a substitute.
- Execute the synchronized component on a DUT, either through the standalone
  UT-Core binary or the opt-in unified integration.
- Resolve the public API policy for `DrmErrors` versus `Status`; tests should
  assert one documented error channel, not both opportunistically.

See the [High Level Test Specification](drm_High-Level_TestSpecification.md)
for the implementation status and L2/L3 requirements.