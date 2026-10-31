# DRM Test Streams and Vector Inputs

## Overview

L1 requires no media or license. L2 uses independently prepared known vectors;
the current L3 decrypt case uses a single demuxed encrypted sample and output
reference. Full L3 playback still requires encrypted streams and an output
oracle. No vectors, production keys, license responses, or media files are
committed here.

## L2 Vector Configuration

The current fixture loader reads these environment variables, not YAML:

| Variable | Required | Meaning |
|---|---|---|
| `DRM_FACTORY_INSTANCE` | No | Registered factory suffix; defaults to `clearkey` |
| `DRM_L2_ENABLE` | Yes, for L2 | Must be `1` to execute L2 bodies |
| `DRM_L2_INIT_DATA` | Yes | Path to non-empty init data accepted by the selected scheme for `video/mp4` |
| `DRM_L2_CIPHERTEXT` | Yes | Path to non-empty encrypted vector |
| `DRM_L2_PLAINTEXT` | Yes | Path to independently known non-empty reference plaintext with matching length |
| `DRM_L2_KID_HEX` | Yes | 16-byte disposable KID, encoded as 32 hexadecimal characters |
| `DRM_L2_KEY_HEX` | Yes | Matching 16-byte disposable test key, encoded as 32 hexadecimal characters |
| `DRM_L2_IV_HEX` | Yes | Matching 16-byte IV, encoded as 32 hexadecimal characters |
| `DRM_L2_LICENSE_RESPONSE` | No | Path to a non-empty scheme-accepted response file; otherwise generate ClearKey JSON from KID/key |

Use the scheme's accepted init-data format and ensure its KID matches the
license response and vector. The current decrypt test calls legacy
`IDrmPlugin.decrypt`; it does not configure a cipher algorithm or exercise the
AVBuffer API. Record any target algorithm prerequisite rather than assuming
this vector proves CTR/CBC mode coverage.

The [configuration template](../../drm_test_configuration/drm_test_config.yaml)
records proposed target settings. Its URL, timeout, profile, and instance lists
are not consumed by current C++ tests.

## L3 Sample Inputs

The current opt-in L3 sample test reads these environment variables directly:

| Variable | Required | Meaning |
|---|---|---|
| `DRM_L3_ENABLE` | Yes | Must be `1` to execute L3 test bodies |
| `DRM_L3_INIT_DATA` | Yes | Path to non-empty scheme-accepted init data |
| `DRM_L3_ENCRYPTED_SAMPLE` | Yes | Path to one non-empty demuxed encrypted sample |
| `DRM_L3_EXPECTED_PLAINTEXT` | Yes | Independent plaintext sample with matching byte length |
| `DRM_L3_LICENSE_RESPONSE` | Yes | Path to a response accepted by the selected scheme |
| `DRM_L3_KID_HEX` | Yes | 16-byte sample KID as 32 hexadecimal characters |
| `DRM_L3_IV_HEX` | Yes | 16-byte sample IV as 32 hexadecimal characters |
| `DRM_L3_MIME_TYPE` | Yes | MIME type used in the key request |
| `DRM_L3_MODE` | No | `AES_CTR` (default), `AES_CBC`, or `AES_CBC_CTS` |
| `DRM_L3_SECURE_SESSION_ENABLE` | No | Must be `1` to execute the secure-session capability test |
| `DRM_L3_REQUIRED_SECURITY_LEVEL` | For secure-session test | Explicit supported security level to request |

This test checks the AIDL AVBuffer decrypt path only. It does not parse the
container, validate stream metadata, or submit samples to a decoder.

## Full L3 Stream Requirements

Before end-to-end playback tests can be added, record:

- Authorized content source, fixture identity/checksum, duration and container.
- Audio/video codecs, sample metadata/PTS, scheme UUID, init data and KID changes.
- Encryption mode, subsamples/pattern, expected security and protected-output requirements.
- Disposable license setup, expected rotation/error events, and response behavior.
- Independent output reference or approved protected-output observation method,
  numeric tolerances and timeouts.

At least a baseline encrypted stream, rotation stream, license-failure
variation, and a protected-output stream where supported are needed for the
[remaining L3 procedures](drm_L3_Low-Level_TestSpecification.md). These media
playback fixtures are not currently packaged.

## Test Material Handling

Keep disposable fixtures outside version control. Do not copy production keys,
device certificates, vendor license credentials, or protected plaintext into
the component or logs. ClearKey fixture success does not establish Widevine,
PlayReady, or a hardware-protected playback path.
