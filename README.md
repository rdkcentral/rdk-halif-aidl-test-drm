# DRM VTS component

This component adds opt-in DRM coverage to the unified UT-Core VTS binary.
Enable it with `ENABLE_DRM_TEST=1` when building the unified project; the
configured DRM factory is selected with `DRM_FACTORY_INSTANCE` and defaults to
`clearkey`.

## Test levels

- **L1** checks DRM AIDL discovery, plugin/session lifecycle, capabilities,
  required properties, and documented invalid-input/session errors. It does
  not require a valid license or media playback.
- **L2** runs opt-in ClearKey key-exchange and byte-vector tests with
  `DRM_L2_ENABLE=1`. The current decrypt vector uses the legacy
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

For L2, supply disposable target test vectors using the environment variables
listed in `drm_test_configuration/drm_test_config.yaml`. Do not commit test
keys, license responses, or encrypted media.

For L3, set `DRM_L3_ENABLE=1` and provide the sample, license response, KID, IV,
and MIME type variables listed under `l3` in the same configuration file. The
sample must be a single demuxed encrypted sample with a matching expected
plaintext file and a license response accepted by the selected DRM scheme.
Set `DRM_L3_SECURE_SESSION_ENABLE=1` and provide
`DRM_L3_REQUIRED_SECURITY_LEVEL` to run the secure-session capability case.
L3 success does not imply decoder playback or protected-output conformance.
L3 cases return early when disabled; an unconfigured run is not L3 validation.

The component uses the DRM headers and library generated from the unified
checkout's `rdk-halif-aidl` tree. It adapts applicable Android DRM VTS cases
to the RDK AIDL API; Android-only APIs that are absent from the RDK contract
are not represented as tests.
