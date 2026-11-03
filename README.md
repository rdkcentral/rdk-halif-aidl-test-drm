# DRM VTS component

This component adds opt-in DRM coverage to the unified UT-Core VTS binary.
Enable it with `ENABLE_DRM_TEST=1` when building the unified project; the
configured DRM factory is selected with `drm.factory_instance` in YAML (or the
`DRM_FACTORY_INSTANCE` environment override) and defaults to `clearkey`.

## Test levels

- **L1** checks DRM AIDL discovery, plugin/session lifecycle, capabilities,
  required properties, and documented invalid-input/session errors. It does
  not require a valid license or media playback.
- **L2** runs opt-in ClearKey key-exchange and byte-vector tests with
  `drm.l2.enabled: true` (or `DRM_L2_ENABLE=1`). The current decrypt vector uses the legacy
  `IDrmPlugin.decrypt()` API; it does not yet validate
  `ICryptoPlugin.decrypt(DecryptArgs)` with AVBuffer handles.
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
L3 cases return early when disabled; an unconfigured run is not L3 validation.

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

### Configuration parser regression tests

With host UT-Core/UT-Control dependencies built, run from this component:

```sh
make -f tests/Makefile test UT_CORE_DIR=/path/to/ut-core
```

`UT_CONTROL_LIB_DIR` and `GTEST_INCLUDE_DIR` can override the existing dependency
locations. These tests use the real UT-Core KVP parser without Binder services
and check every input mapping, overrides, defaults, reloads, and invalid input.

### Reference ClearKey service

Platforms without a SoC DRM implementation can run the L1/L2/L3 cases against
the software ClearKey service in [`reference_clearkey/`](reference_clearkey/README.md).
It implements `IDrmFactory`, `IDrmPlugin` and `ICryptoPlugin` following
Android's ClearKey service, and `tools/make_clearkey_fixtures.py` generates
matching disposable fixtures and the `DRM_L2_*`/`DRM_L3_*` environment. It is
not a secure CDM and is not compiled into the unified VTS.

Build it separately from this component directory:

```sh
export RDK_HALIF_AIDL_VERSION=0.22.0
./build_clearkey.sh Target=linux
```

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
