# DRM HAL VTS component

This repository contains the Vendor Test Suite (VTS) for the IDrmFactory AIDL HAL interface. It is designed to validate the functionality of the DRM HAL implementation.

## Test levels

- **L1** checks DRM AIDL discovery, plugin/session lifecycle, capabilities,
  required properties, and documented invalid-input/session errors. It also
  has an opt-in provisioning exchange through a configured HTTP adapter. It
  does not require a media license or playback.
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
[drm_TestSpecification.md](docs/pages/drm_TestSpecification.md).
The target-specific service and disposable L2 vector settings are in
[drm_test_config.yaml](drm_test_configuration/drm_test_config.yaml).

## Reference Documents

|SNo|Document Name|Document Description|Document Link|
|---|-------------|--------------------|-------------|
|1|High Level Test Specification Document|High Level Test Specification Documentation this module|[drm_High-Level_TestSpecification.md](docs/pages/drm_High-Level_TestSpecification.md)|
|2|`L2` L2 Low Level Test Specification and Procedure Document|`L2` Test Specification and Procedure Documentation|[drm_L2_Low-Level_TestSpecification.md](docs/pages/drm_L2_Low-Level_TestSpecification.md)|
|3|`L3` L3 Low Level Test Specification and Procedure Document|`L3` Test Specification and Procedure Documentation|[drm_L3_Low-Level_TestSpecification.md](docs/pages/drm_L3_Low-Level_TestSpecification.md)|