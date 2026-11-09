# DRM VTS component

This component adds opt-in DRM coverage to the unified UT-Core VTS binary.
Enable it with `ENABLE_DRM_TEST=1` when building the unified project; the
configured DRM factory is selected with `drm.factory_instance` in YAML (or the
`DRM_FACTORY_INSTANCE` environment override) and defaults to `clearkey`.

## Test levels

- **L1** checks DRM AIDL discovery, plugin/session lifecycle, capabilities,
  required properties, and documented invalid-input/session errors. It does
  not require a valid license or media playback.
- **L2** runs common opt-in license/session tests and, when vector fixtures are
  configured, known-vector decryption through
  `ICryptoPlugin.decrypt(DecryptArgs)` with AVBuffer handles for ClearKey,
  Widevine and PlayReady. The retained ClearKey-only vector also exercises the
  legacy `IDrmPlugin.decrypt()` API.
- **L3** has opt-in AVBuffer-backed sample decryption, key-removal/session-close
  rejection, and secure-session capability checks. These are not end-to-end
  playback tests. Full encrypted-stream playback, key rotation, and
  protected-output policy validation still require target demux, decoder, and
  output-oracle integration.

The test contract and L1/L2/L3 boundaries are in
[`docs/pages/drm_TestSpecification.md`](docs/pages/drm_TestSpecification.md).
The target-specific service and disposable L2 vector settings are in
[`drm_test_configuration/drm_test_config.yaml`](drm_test_configuration/drm_test_config.yaml).

## Build and run

Build the unified VTS binary with DRM enabled:

```sh
ENABLE_DRM_TEST=1 ./build.sh Target=linux
```

Run the binary on a DUT where the configured DRM factory is registered:

```sh
DRM_FACTORY_INSTANCE=clearkey ./run.sh
```

The tests load
[`drm_test_configuration/drm_test_config.yaml`](drm_test_configuration/drm_test_config.yaml)
at DRM initialization using the same UT-Core KVP parser as the decoder/sink
modules. Set `drm.l2.enabled: true` and fill in `drm.l2.content` for L2.
Set `drm.l3.enabled: true` and fill in `drm.l3.sample` for L3. The
sample must be a single demuxed encrypted sample with a matching expected
plaintext file and a license response accepted by the selected DRM scheme.
Set `drm.l3.secure_session.enabled: true` and provide
`drm.l3.secure_session.required_security_level` to run the secure-session capability case.
L3 success does not imply decoder playback or protected-output conformance.
Disabled or unsupported optional cases are reported as skipped, not passed.
An unconfigured run is not L2/L3 validation.

All existing `DRM_L2_*`/`DRM_L3_*` environment variables still override their
YAML fields, including empty values. No manual exports are needed for YAML-only
runs. Relative fixture paths resolve from the process working directory, as
in the other VTS modules.

Use `DRM_TEST_CONFIG=/path/to/drm_test_config.yaml ./run.sh` to select a different
configuration file. Missing/unreadable or invalid configuration fails DRM
initialization; enabled tests fail on missing/invalid fixture inputs.
Configuration is loaded once per run, not watched for live changes. Editing
YAML or fixture files requires no recompilation; restart the test process.
The initial C++ change requires rebuilding with `ENABLE_DRM_TEST=1`, but adds
no new YAML dependency. See the
[YAML/environment mapping](docs/pages/drm_TestStreams.md).
Do not commit test keys, license responses, or encrypted media.

### One-run ClearKey, Widevine and PlayReady testing

Define `drm.schemes.clearkey`, `drm.schemes.widevine` and/or
`drm.schemes.playready` to run the same L2/L3 procedures for multiple schemes.
Each profile accepts `enabled`, `required` (default `true`), `factory_instance`,
an optional canonical `uuid` override, and its own complete `l2`/`l3` sections.
For example:

```yaml
drm:
  schemes:
    clearkey:
      enabled: true
      required: true
      factory_instance: "clearkey"
      l2:
        enabled: true
        content:
          init_data_file: "/fixtures/clearkey/init.bin"
          mime_type: "video/mp4"
          license_response_file: "/fixtures/clearkey/license.json"
          # Supply ciphertext/plaintext/KID/IV for AIDL AVBuffer decryption.
    widevine:
      enabled: true
      required: true
      factory_instance: "widevine"
      l2:
        enabled: true
        security_level: "SW_SECURE_CRYPTO"
        license:
          mode: "http"
          url: "http://127.0.0.1:8766/license"
          content_type: "application/octet-stream"
        content:
          mime_type: "video/mp4"
          init_data_file: "/fixtures/widevine/init.bin"
      l3:
        enabled: true
        security_level: "SW_SECURE_CRYPTO"
        license:
          mode: "http"
          url: "http://127.0.0.1:8766/license"
        sample:
          mime_type: "video/mp4"
          codec_mime_type: "video/avc"
          mode: "AES_CTR"
          init_data_file: "/fixtures/widevine/init.bin"
          encrypted_sample_file: "/fixtures/widevine/cipher.bin"
          expected_plaintext_file: "/fixtures/widevine/plain.bin"
          kid_hex: "<32 hexadecimal characters>"
          iv_hex: "<32 hexadecimal characters>"
    playready:
      enabled: true
      required: false
      factory_instance: "playready"
      l2:
        enabled: true
        content:
          mime_type: "video/mp4"
          init_data_file: "/fixtures/playready/init.bin"
          license_response_file: "/fixtures/playready/license.bin"
```

Replace the example paths, security levels and license adapters with the
vendor-approved test setup. Enable and populate `l3` separately for ClearKey
and PlayReady as needed; missing `l2`/`l3` sections default to disabled.
Only ClearKey local JSON generation needs a disposable raw key. AIDL AVBuffer
vector cases need known ciphertext/plaintext/KID/IV inputs per scheme; they
skip when those vector fields are absent. The ClearKey legacy-vector cases
continue to require the same vector inputs.

The registered groups are `VTS_L2_DrmClearKey`, `VTS_L2_DrmWidevine`,
`VTS_L2_DrmPlayReady`, and `VTS_L3_Drm` (ClearKey), `VTS_L3_DrmWidevine`,
`VTS_L3_DrmPlayReady`. Each uses its configured UUID, never the first
advertised UUID. Missing optional services, unadvertised UUIDs and unsupported
MIME/security-level combinations skip; required ones fail. Once a capability
is advertised, plugin/session/license/decrypt failures fail even for optional
profiles. Secure-decoder-only sample cases skip CPU-readable comparison,
including before loading plaintext fixtures; that skip is **not** secure-path
validation. Enable the separate secure-session capability case where applicable.

In matrix mode, global `drm.factory_instance`, `drm.l2`/`drm.l3` and their
environment overrides do not supply profile inputs. Use profile-scoped
overrides, e.g. `DRM_WIDEVINE_L2_LICENSE_URL` or
`DRM_PLAYREADY_L3_MIME_TYPE`. This prevents ClearKey fixtures leaking into
vendor tests. Without `drm.schemes`, the existing single-scheme configuration
and overrides remain supported; `drm.scheme` selects the scheme when its
factory suffix differs from the scheme name. L1 still checks the first
available configured factory, not every profile. Initialization requires at
least one available factory and rejects any missing required factory.

The device must already be provisioned. Static response files work only when
accepted for the actual session challenge; otherwise use a live test license
adapter. Vendor authentication, SOAP/JSON wrapping, provisioning transport and
certification are not implemented by these generic HAL cases.
For vendor L3 tests, set `sample.codec_mime_type` to the actual elementary
codec (e.g. `video/avc`), separately from the key-request/container
`sample.mime_type` (e.g. `video/mp4`). Secure-decoder capability queries require
the codec MIME. Existing ClearKey configurations retain their MIME fallback.

### HTTP license exchange (L2 and L3)

Set `license.mode: "http"` under `drm.l2` and/or `drm.l3`:

```yaml
license:
  mode: "http"
  url: "http://127.0.0.1:8765/license"
  request_timeout_seconds: 10
```

The port is part of the URL; replace `8765` as needed. The test POSTs the
plugin's actual `getKeyRequest()` bytes using `license.content_type`, then passes the
HTTP response bytes to `provideKeyResponse()`. HTTP errors, timeouts, empty
responses, and responses over 1 MiB fail the test, without local fallback.
Redirects and proxies are disabled; HTTPS uses libcurl's normal certificate
verification. The default content type is `application/json` for ClearKey,
`application/octet-stream` for Widevine and `text/xml` for PlayReady. Override
it to match the test endpoint. This raw transport does not implement vendor
authentication or request wrapping; use a vendor-approved adapter when needed.

The default `local` mode preserves L2 generated/file responses and L3 file
responses. Response-file settings are ignored in HTTP mode.

Start the disposable local server using an existing generated license fixture:

```sh
python3 tools/clearkey_license_server.py \
  --license /tmp/clearkey_fixtures/license.json --port 8765
```

The server validates requested KIDs and returns only matching disposable keys.
It binds to `127.0.0.1` by default. For a separate DUT, explicitly set
`--bind <server-interface-address>` and use that reachable address in the YAML
URL; DUT `127.0.0.1` refers to the DUT, not your host. This server is test-only,
without authentication or TLS: use an isolated test network and never
production keys. Stop it with Ctrl+C. Restart it after changing its fixture.

Rebuild the VTS binary once to include HTTP support. The unified Makefile also
needs the DRM curl-linking change: it reuses UT-Control's bundled curl/OpenSSL
archives, avoiding additional runtime libraries. Custom SDKs may override
`DRM_CURL_DIR`/`DRM_CURL_LIBS`; a system-libcurl build must provide libcurl on
the DUT. Subsequent URL/port/timeout changes require only restarting the tests.

### Configuration parser regression tests

With host UT-Core/UT-Control dependencies built, run from this component:

```sh
make -f tests/Makefile test UT_CORE_DIR=/path/to/ut-core
```

`UT_CONTROL_LIB_DIR` and `GTEST_INCLUDE_DIR` can override the existing dependency
locations. These tests use the real UT-Core KVP parser without Binder services
and check every input mapping, overrides, defaults, reloads, and invalid input.
The same target also runs loopback HTTP client/server tests using Python 3's
standard library; `CURL_DIR`/`CURL_LIBS` can select existing host curl dependencies.

With the pinned DRM AIDL/Binder SDK and Google Test libraries available, also run:

```sh
make -f tests/Makefile test-schemes UT_CORE_DIR=/path/to/ut-core
```

This uses an in-process fake factory (no Binder device or vendor CDM) to check
exact UUID selection, missing UUIDs, MIME/security ranges, query errors and null
plugins. SDK/library paths are overridable in the test Makefile. These host
checks do not validate Widevine/PlayReady implementations on a target.

### Reference ClearKey service

Platforms without a SoC DRM implementation can run the L1/L2/L3 cases against
the software ClearKey service in [`reference_clearkey/`](reference_clearkey/README.md).
It implements `IDrmFactory`, `IDrmPlugin` and `ICryptoPlugin` following
Android's ClearKey service, and `tools/make_clearkey_fixtures.py` generates
matching disposable fixtures and the `DRM_L2_*`/`DRM_L3_*` environment. It is
not a secure CDM and is not compiled into the unified VTS.

Build it separately from this component directory:

```sh
export RDK_HALIF_AIDL_VERSION=0.23.0
./build_clearkey.sh Target=linux
```

The reference service and DRM VTS target DRM snapshot 0.2.0.0 from AIDL release
0.23.0, using its UTF-8 `std::string` API directly. The 0.22.0 string API is no
longer supported. Build the VTS against the same snapshot as the target service.

The script automatically clones the selected AIDL release and pinned Binder
repository, builds the Binder SDK and DRM AIDL library, and builds a link-time
AVBuffer helper placeholder. No SDK path exports are needed. For actual L3
buffer decryption, override `AVBUFFERHELPER_LIBRARY` with the functional platform
library (and `AVBUFFERHELPER_INCLUDE_DIR` if needed). Use
`./build_clearkey.sh --core-only` for host tests without Binder, or
`./build_clearkey.sh --help` for SDK paths and ARM cross-build options.
`Target=arm` also needs target OpenSSL development files; if your SDK lacks
them, build OpenSSL once by following
[Building OpenSSL once without Yocto](reference_clearkey/README.md#building-openssl-once-without-yocto)
and reuse it through `OPENSSL_INCLUDE_DIR` and `OPENSSL_CRYPTO_LIBRARY`.
See the [ClearKey build instructions](reference_clearkey/README.md#build-and-test)
for details.

The component uses the DRM headers and library generated from the unified
checkout's `rdk-halif-aidl` tree. It adapts applicable Android DRM VTS cases
to the RDK AIDL API; Android-only APIs that are absent from the RDK contract
are not represented as tests.
