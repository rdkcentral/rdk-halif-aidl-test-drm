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
through a decoder. Tests are opt-in because fixtures and licenses are
target/test-run inputs, not repository data.

1. Open a session.
2. Load valid scheme-specific init data containing the test KID.
3. Call `getKeyRequest(session, initData, mime, STREAMING, parameters)`.
4. Acquire a test license response. With `drm.l2.license.mode: "http"`, POST the
   plugin's key request to the configured URL/port. The default `local` mode
   reads a response file, or generates JSON only for ClearKey from a disposable KID/key.
5. Call `provideKeyResponse(session, response)`.
6. Call `queryKeyStatus(session)` and verify a non-empty status result.
7. Create a crypto plugin and bind it with `setMediaDrmSession(session)`.
8. Use `ICryptoPlugin.decrypt(DecryptArgs)` with valid AVBuffer handles to
   decrypt a known AES-CTR test vector and compare it with the known plaintext.
9. Run the AIDL negative cases for empty subsamples, short source/destination
   buffers, invalid handles, no-license after key removal, and use after
   session closure. They assert `ERROR_DRM_ZERO_SUBSAMPLES`,
   `ERROR_DRM_FRAME_TOO_LARGE`, `ERROR_DRM_DECRYPT`, `ERROR_DRM_NO_LICENSE`,
   and `ERROR_DRM_SESSION_NOT_OPENED`, respectively.
10. Existing session/request negatives also assert `BAD_VALUE` for an empty
    response and ClearKey empty init data, and
    `ERROR_DRM_SESSION_NOT_OPENED` for a closed-session key request.

The registered L2 AIDL cases check exact plaintext and returned byte count,
multiple and mixed clear/encrypted subsamples, configured modes/patterns,
buffer bounds, caller ownership/lifetime, and documented negative errors.
The retained `decryptKnownSample_pos` and `removeKeysRejectsDecrypt_neg` cases
continue to cover the legacy `IDrmPlugin.decrypt()` path for ClearKey only.
AVBuffer sample tests are opt-in and require per-scheme ciphertext, plaintext,
KID and IV fixtures; they are API/data-path tests, not decoder playback.

Set YAML `drm.l2.enabled: true` (or `DRM_L2_ENABLE=1`) and provide the vector inputs documented
in [Test Streams and Vector Inputs](drm_TestStreams.md) to execute these cases.
Disabled and unsupported optional cases are explicitly skipped, not passed.

L2 and L3 support configurable HTTP license exchange; see
[HTTP license transport](drm_TestStreams.md#http-license-transport).
The same common cases are registered for ClearKey, Widevine and PlayReady.
Use `drm.schemes.<scheme>` profiles for a one-run matrix with separate service,
UUID, MIME/security level, license and sample inputs; see
[multi-scheme setup](../../README.md#one-run-clearkey-widevine-and-playready-testing).
An absent optional service/UUID or unsupported MIME/security level skips; a
required one fails. Operational errors on advertised capabilities always fail.
Vendor license authentication/wrapping needs an external approved adapter;
the device must already be provisioned.

## L3: sample crypto coverage and end-to-end playback

The opt-in `VTS_L3_Drm` (ClearKey), `VTS_L3_DrmWidevine` and
`VTS_L3_DrmPlayReady` groups share three bounded cases:

| Test | Current validation |
|---|---|
| `decryptSampleWithAvBuffer_pos` | Acquire a key, decrypt a configured sample using `ICryptoPlugin.decrypt(DecryptArgs)` with AVBuffer handles, and compare the bytes with an independent plaintext |
| `removedKeysAndClosedSessionRejectDecrypt_neg` | Confirm decryption fails after key removal and after session closure |
| `secureSessionAndDecoderRequirement_pos` | Optionally check a configured session security level and secure-decoder requirement |

These checks do not parse a media container, process KID rotation, feed a
decoder, or validate playback or protected output. They are useful integration
checks but are not end-to-end L3 playback conformance.
Secure-decoder-only paths skip the non-secure byte-comparison cases; a skip
does not validate decryption into protected buffers.

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

## prerequisites

- Provide each selected scheme's init data and accepted test license flow;
  for AVBuffer comparison, also provide its KID, IV, encrypted sample and plaintext.
- A raw disposable key is needed only for local ClearKey JSON generation.
- Provide the AVBuffer and AVBufferHelper services required by L2 AIDL decrypt
  tests; the tests allocate and release their own buffers.
- Provide an encrypted stream and playback/output validation method for
  end-to-end L3; the current sample test is not a substitute.
- Execute the synchronized component on a DUT, either through the standalone
  UT-Core binary or the opt-in unified integration.
- Preserve exact documented service-specific error assertions when adding
  negative cases; do not treat any failure as an equivalent success.

See the [High Level Test Specification](drm_High-Level_TestSpecification.md)
for the implementation status and L2/L3 requirements.