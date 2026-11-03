# DRM Test Streams and Vector Inputs

## Overview

L1 requires no media or license. L2 uses independently prepared known vectors;
the current L3 decrypt case uses a single demuxed encrypted sample and output
reference. Full L3 playback still requires encrypted streams and an output
oracle. No vectors, production keys, license responses, or media files are
committed here.

## L2 Vector Configuration

The tests load [drm_test_config.yaml](../../drm_test_configuration/drm_test_config.yaml)
at DRM initialization using UT-Core KVP, as in the decoder/sink modules.
Edit the YAML values and rerun; no rebuild is needed for configuration changes.
The default path is `./drm_test_configuration/drm_test_config.yaml`;
`DRM_TEST_CONFIG` can select another file. The configuration must remain present
even when using environment-only fixture overrides. Invalid/unreadable YAML
fails DRM initialization.

Each environment variable below overrides its corresponding YAML value when
set, including an empty value. Relative fixture paths are relative to the
process working directory. YAML enable fields use `true`/`false`; environment
enable overrides retain the existing `1` convention.

| Environment override | YAML field |
|---|---|
| `DRM_FACTORY_INSTANCE` | `drm.factory_instance` |
| `DRM_L2_ENABLE` | `drm.l2.enabled` |
| `DRM_L2_INIT_DATA` | `drm.l2.content.init_data_file` |
| `DRM_L2_CIPHERTEXT` | `drm.l2.content.encrypted_sample_file` |
| `DRM_L2_PLAINTEXT` | `drm.l2.content.expected_plaintext_file` |
| `DRM_L2_KID_HEX` | `drm.l2.content.kid_hex` |
| `DRM_L2_KEY_HEX` | `drm.l2.content.key_hex` |
| `DRM_L2_IV_HEX` | `drm.l2.content.iv_hex` |
| `DRM_L2_LICENSE_RESPONSE` | `drm.l2.content.license_response_file` |

Required inputs (provided through YAML or environment overrides):

| Variable | Required | Meaning |
|---|---|---|
| `DRM_FACTORY_INSTANCE` | No | Registered factory suffix; defaults to `clearkey` |
| `DRM_L2_ENABLE` | Yes, for L2 | YAML `enabled: true` or environment `1` executes L2 bodies |
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
contains the runtime input fields. Its license URL/timeout, L1 settings,
`l2.content.mime_type`, and instance lists remain descriptive and are not
consumed by current C++ tests. L2 uses `video/mp4` and no HTTP exchange.

## L3 Sample Inputs

| Environment override | YAML field |
|---|---|
| `DRM_L3_ENABLE` | `drm.l3.enabled` |
| `DRM_L3_INIT_DATA` | `drm.l3.sample.init_data_file` |
| `DRM_L3_ENCRYPTED_SAMPLE` | `drm.l3.sample.encrypted_sample_file` |
| `DRM_L3_EXPECTED_PLAINTEXT` | `drm.l3.sample.expected_plaintext_file` |
| `DRM_L3_LICENSE_RESPONSE` | `drm.l3.sample.license_response_file` |
| `DRM_L3_KID_HEX` | `drm.l3.sample.kid_hex` |
| `DRM_L3_IV_HEX` | `drm.l3.sample.iv_hex` |
| `DRM_L3_MIME_TYPE` | `drm.l3.sample.mime_type` |
| `DRM_L3_MODE` | `drm.l3.sample.mode` |
| `DRM_L3_SECURE_SESSION_ENABLE` | `drm.l3.secure_session.enabled` |
| `DRM_L3_REQUIRED_SECURITY_LEVEL` | `drm.l3.secure_session.required_security_level` |

Required inputs (provided through YAML or environment overrides):

| Variable | Required | Meaning |
|---|---|---|
| `DRM_L3_ENABLE` | Yes | YAML `enabled: true` or environment `1` executes L3 bodies |
| `DRM_L3_INIT_DATA` | Yes | Path to non-empty scheme-accepted init data |
| `DRM_L3_ENCRYPTED_SAMPLE` | Yes | Path to one non-empty demuxed encrypted sample |
| `DRM_L3_EXPECTED_PLAINTEXT` | Yes | Independent plaintext sample with matching byte length |
| `DRM_L3_LICENSE_RESPONSE` | Yes | Path to a response accepted by the selected scheme |
| `DRM_L3_KID_HEX` | Yes | 16-byte sample KID as 32 hexadecimal characters |
| `DRM_L3_IV_HEX` | Yes | 16-byte sample IV as 32 hexadecimal characters |
| `DRM_L3_MIME_TYPE` | Yes | MIME type used in the key request |
| `DRM_L3_MODE` | No | `AES_CTR` (default), `AES_CBC`, or `AES_CBC_CTS` |
| `DRM_L3_SECURE_SESSION_ENABLE` | No | YAML secure-session `enabled: true` or environment `1` executes the capability test |
| `DRM_L3_REQUIRED_SECURITY_LEVEL` | For secure-session test | Explicit supported security level to request |

This test checks the AIDL AVBuffer decrypt path only. It does not parse the
container, validate stream metadata, or submit samples to a decoder.
L2, L3 and secure-session tests default to disabled. Missing optional values
default to factory `clearkey`, mode `AES_CTR`, or empty fixture inputs. YAML
scalar values must fit within the UT-Core KVP limit of 255 bytes.

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
