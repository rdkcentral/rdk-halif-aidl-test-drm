# DRM Test Streams and Vector Inputs

## Overview

L1 requires no media or license. L2 uses independently prepared known vectors;
the current L3 decrypt case uses a single demuxed encrypted sample and output
reference. Full L3 playback still requires encrypted streams and an output
oracle. No vectors, production keys, license responses, or media files are
committed here.

## L2 Vector Configuration

The common L2 cases run for ClearKey, Widevine and PlayReady. The legacy
`IDrmPlugin.decrypt()` vector cases remain ClearKey-only; L2 also exercises
`ICryptoPlugin.decrypt(DecryptArgs)` with AVBuffer handles for any scheme with
configured vector inputs. `drm.schemes.<scheme>` enables a one-run matrix. Each enabled profile
has its own factory, optional UUID override, required/optional policy and
`l2`/`l3` inputs. See the [complete setup](../../README.md#one-run-clearkey-widevine-and-playready-testing).

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

The tables below show single-scheme paths. In matrix mode, replace `drm.l2`
and `drm.l3` with `drm.schemes.<scheme>.l2` and `.l3`, and insert the uppercase
scheme after `DRM_` in overrides, e.g. `DRM_WIDEVINE_L2_INIT_DATA`.
Global fixture values/overrides are intentionally not inherited in matrix
mode. Missing profile sections default to disabled. Without a matrix,
`drm.scheme` can explicitly identify a vendor-named factory and `drm.uuid` can
override the canonical scheme UUID. Existing single-scheme inputs still work.

| Environment override | YAML field |
|---|---|
| `DRM_FACTORY_INSTANCE` | `drm.factory_instance` |
| `DRM_L2_ENABLE` | `drm.l2.enabled` |
| `DRM_L2_INIT_DATA` | `drm.l2.content.init_data_file` |
| `DRM_L2_MIME_TYPE` | `drm.l2.content.mime_type` |
| `DRM_L2_SECURITY_LEVEL` | `drm.l2.security_level` |
| `DRM_L2_CIPHERTEXT` | `drm.l2.content.encrypted_sample_file` |
| `DRM_L2_PLAINTEXT` | `drm.l2.content.expected_plaintext_file` |
| `DRM_L2_MODE` | `drm.l2.content.mode` |
| `DRM_L2_PATTERN_ENCRYPT_BLOCKS` | `drm.l2.content.pattern.encrypt_blocks` |
| `DRM_L2_PATTERN_SKIP_BLOCKS` | `drm.l2.content.pattern.skip_blocks` |
| `DRM_L2_KID_HEX` | `drm.l2.content.kid_hex` |
| `DRM_L2_KEY_HEX` | `drm.l2.content.key_hex` |
| `DRM_L2_IV_HEX` | `drm.l2.content.iv_hex` |
| `DRM_L2_LICENSE_RESPONSE` | `drm.l2.content.license_response_file` |
| `DRM_L2_LICENSE_MODE` | `drm.l2.license.mode` |
| `DRM_L2_LICENSE_CONTENT_TYPE` | `drm.l2.license.content_type` |
| `DRM_L2_LICENSE_URL` | `drm.l2.license.url` |
| `DRM_L2_LICENSE_REQUEST_TIMEOUT_SECONDS` | `drm.l2.license.request_timeout_seconds` |

Required inputs (provided through YAML or environment overrides):

| Variable | Required | Meaning |
|---|---|---|
| `DRM_FACTORY_INSTANCE` | No | Registered factory suffix; defaults to `clearkey` |
| `DRM_L2_ENABLE` | Yes, for L2 | YAML `enabled: true` or environment `1` executes L2 bodies |
| `DRM_L2_INIT_DATA` | Yes | Non-empty init data accepted by the selected scheme and configured MIME |
| `DRM_L2_MIME_TYPE` | No | MIME used in the key request; default `video/mp4` |
| `DRM_L2_SECURITY_LEVEL` | No | Requested session level; default `DEFAULT` |
| `DRM_L2_CIPHERTEXT` | Known-vector decrypt | Path to non-empty encrypted vector |
| `DRM_L2_PLAINTEXT` | Known-vector decrypt | Independent reference plaintext with matching length |
| `DRM_L2_KID_HEX` | Known-vector decrypt or generated response | 16-byte disposable KID as 32 hexadecimal characters |
| `DRM_L2_KEY_HEX` | Generated ClearKey response only | Matching disposable AES-128 key as 32 hexadecimal characters |
| `DRM_L2_IV_HEX` | Known-vector decrypt | Matching 16-byte IV as 32 hexadecimal characters |
| `DRM_L2_MODE` | No | `AES_CTR` (default), `AES_CBC`, or `AES_CBC_CTS` for the AIDL vector case |
| `DRM_L2_PATTERN_ENCRYPT_BLOCKS` | No | Non-negative integer; pattern encrypt block count, default `0` |
| `DRM_L2_PATTERN_SKIP_BLOCKS` | No | Non-negative integer; pattern skip block count, default `0` |
| `DRM_L2_LICENSE_RESPONSE` | Local mode except generated ClearKey | Accepted response file; ignored in HTTP mode. Never generate ClearKey JSON for Widevine/PlayReady |
| `DRM_L2_LICENSE_MODE` | No | `local` (default) or `http` |
| `DRM_L2_LICENSE_CONTENT_TYPE` | No | HTTP request MIME; scheme-specific default described below |
| `DRM_L2_LICENSE_URL` | HTTP mode | HTTP(S) endpoint including port; defaults to `http://127.0.0.1:8765/license` |
| `DRM_L2_LICENSE_REQUEST_TIMEOUT_SECONDS` | No | Positive integer for total HTTP timeout and connection timeout; default `10` |

Use the scheme's accepted init-data format and ensure its KID matches the
license response and vector. The AIDL vector test uses the configured mode and
pattern with AVBuffer handles, checks the byte count and exact plaintext, and
tests subsample boundaries. Non-zero patterns must match the supplied vector;
the default is an unpatterned AES-CTR sample. The separate legacy
`IDrmPlugin.decrypt()` case remains ClearKey-only and does not prove AIDL
AVBuffer behavior.

The [configuration template](../../drm_test_configuration/drm_test_config.yaml)
contains the runtime input fields. Its legacy L1 settings and
`required_instances`/`optional_instances` lists remain descriptive; use
`drm.schemes` for the executable L2/L3 matrix.

## L3 Sample Inputs

| Environment override | YAML field |
|---|---|
| `DRM_L3_ENABLE` | `drm.l3.enabled` |
| `DRM_L3_SECURITY_LEVEL` | `drm.l3.security_level` |
| `DRM_L3_INIT_DATA` | `drm.l3.sample.init_data_file` |
| `DRM_L3_ENCRYPTED_SAMPLE` | `drm.l3.sample.encrypted_sample_file` |
| `DRM_L3_EXPECTED_PLAINTEXT` | `drm.l3.sample.expected_plaintext_file` |
| `DRM_L3_LICENSE_RESPONSE` | `drm.l3.sample.license_response_file` |
| `DRM_L3_KID_HEX` | `drm.l3.sample.kid_hex` |
| `DRM_L3_IV_HEX` | `drm.l3.sample.iv_hex` |
| `DRM_L3_MIME_TYPE` | `drm.l3.sample.mime_type` |
| `DRM_L3_CODEC_MIME_TYPE` | `drm.l3.sample.codec_mime_type` |
| `DRM_L3_MODE` | `drm.l3.sample.mode` |
| `DRM_L3_SECURE_SESSION_ENABLE` | `drm.l3.secure_session.enabled` |
| `DRM_L3_REQUIRED_SECURITY_LEVEL` | `drm.l3.secure_session.required_security_level` |
| `DRM_L3_LICENSE_MODE` | `drm.l3.license.mode` |
| `DRM_L3_LICENSE_CONTENT_TYPE` | `drm.l3.license.content_type` |
| `DRM_L3_LICENSE_URL` | `drm.l3.license.url` |
| `DRM_L3_LICENSE_REQUEST_TIMEOUT_SECONDS` | `drm.l3.license.request_timeout_seconds` |

Required inputs (provided through YAML or environment overrides):

| Variable | Required | Meaning |
|---|---|---|
| `DRM_L3_ENABLE` | Yes | YAML `enabled: true` or environment `1` executes L3 bodies |
| `DRM_L3_SECURITY_LEVEL` | No | Session level for sample tests; default `DEFAULT` |
| `DRM_L3_INIT_DATA` | Yes | Path to non-empty scheme-accepted init data |
| `DRM_L3_ENCRYPTED_SAMPLE` | Yes | Path to one non-empty demuxed encrypted sample |
| `DRM_L3_EXPECTED_PLAINTEXT` | Yes | Independent plaintext sample with matching byte length |
| `DRM_L3_LICENSE_RESPONSE` | Local mode only | Path to an accepted response file; ignored in HTTP mode |
| `DRM_L3_KID_HEX` | Yes | 16-byte sample KID as 32 hexadecimal characters |
| `DRM_L3_IV_HEX` | Yes | 16-byte sample IV as 32 hexadecimal characters |
| `DRM_L3_MIME_TYPE` | Yes | MIME type used in the key request |
| `DRM_L3_CODEC_MIME_TYPE` | Vendor L3 cases | Actual codec MIME, e.g. `video/avc` or `audio/mp4a-latm`, used for secure-decoder queries; legacy ClearKey falls back to the key-request MIME |
| `DRM_L3_MODE` | No | `AES_CTR` (default), `AES_CBC`, or `AES_CBC_CTS` |
| `DRM_L3_SECURE_SESSION_ENABLE` | No | YAML secure-session `enabled: true` or environment `1` executes the capability test |
| `DRM_L3_REQUIRED_SECURITY_LEVEL` | For secure-session test | Explicit supported security level to request |
| `DRM_L3_LICENSE_MODE` | No | `local` (default) or `http` |
| `DRM_L3_LICENSE_CONTENT_TYPE` | No | HTTP request MIME; scheme-specific default described below |
| `DRM_L3_LICENSE_URL` | HTTP mode | HTTP(S) endpoint including port; defaults to `http://127.0.0.1:8765/license` |
| `DRM_L3_LICENSE_REQUEST_TIMEOUT_SECONDS` | No | Positive integer for total HTTP timeout and connection timeout; default `10` |

This test checks the AIDL AVBuffer decrypt path only. It does not parse the
container, validate stream metadata, or submit samples to a decoder.
The suite probes the resolved session's secure-decoder requirement before
loading the sample, then checks again after licensing. Protected-only paths
skip non-secure sample comparison rather than trying to map protected output.
The separate secure-session capability test does not prove secure decryption.
L2, L3 and secure-session tests default to disabled. Missing optional values
default to factory `clearkey`, mode `AES_CTR`, or empty fixture inputs. YAML
scalar values must fit within the UT-Core KVP limit of 255 bytes.

## HTTP License Transport

Both levels support `license.mode: "http"`. The test POSTs the plugin's key
request bytes, with the configured `license.content_type`, to the configured URL
and supplies the response bytes unchanged to `provideKeyResponse()`. No file
or generated-response fallback occurs in HTTP mode. Only a non-empty HTTP 2xx
response up to 1 MiB is accepted; the DRM plugin validates the license content.
Timeout, connection errors, redirects and non-2xx responses fail the test.
Proxies/redirects are disabled; HTTPS certificate checks remain enabled.
Default content types are `application/json` (ClearKey),
`application/octet-stream` (Widevine) and `text/xml` (PlayReady).
This is raw transport, not vendor-specific authentication/wrapping. Use a
test adapter if the vendor endpoint requires headers, envelopes or response
extraction. Provision the device externally before running these tests.
Static response files must be valid for the actual session challenge; a
previously captured Widevine/PlayReady response is not generally replayable.

See [HTTP setup and the disposable local server](../../README.md#http-license-exchange-l2-and-l3)
for a configurable-port server and the separate-DUT addressing rules.

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
