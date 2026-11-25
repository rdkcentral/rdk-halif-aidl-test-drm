# Reference ClearKey DRM service

A software ClearKey implementation of the RDK DRM HAL
(`com.rdk.hal.drm.IDrmFactory`, `IDrmPlugin`, `ICryptoPlugin`) for platforms
that have no SoC DRM implementation yet. It lets the DRM VTS L1/L2/L3 tests run
end to end. It is a **test reference, not a secure CDM**: keys live in
process memory, decryption is in software, and there is no secure decode or
output protection.

The design follows Android's ClearKey default service (`DrmFactory`, `DrmPlugin`,
`CryptoPlugin`, `SessionLibrary`, `InitDataParser`, `JsonWebKey`). The AIDL
contract is derived from Android's `drm` AIDL, so the behaviours below match
Android unless noted as an RDK adaptation.

## Layout

| Path | Purpose |
|---|---|
| `core/` | Binder-free logic: base64url, JSON, PSSH/init data, W3C ClearKey license, session table, AES-CTR/CBC, HMAC |
| `service/` | Binder layer: `ClearKeyDrmFactory`, `ClearKeyDrmPlugin`, `ClearKeyCryptoPlugin`, service `main` |
| `tests/` | `clearkey_core_test` (host, no Binder) and `clearkey_binder_test` (in-process, fake AVBuffer helper) |
| `tools/make_clearkey_fixtures.py` | Generates disposable KID/key/IV, PSSH, license, plaintext/ciphertext and an `env.sh` for the VTS |

## Behaviour

| Area | Behaviour |
|---|---|
| Schemes | ClearKey `e2719d58-a985-b3c9-781a-b030af78d30e` and W3C common PSSH `1077efec-c0b2-4d02-ace3-3c1e52e2fb4b` |
| Unsupported UUID | `createDrmPlugin`/`createCryptoPlugin` fail with `ERROR_DRM_CANNOT_HANDLE`, output plugin null |
| Security level | Every `openSession` level resolves to `SW_SECURE_CRYPTO` |
| Init data | `video/mp4`, `audio/mp4`, `cenc`: `pssh` boxes (v1 key IDs, or v0 JSON `{"kids":[...]}`); `video/webm`, `audio/webm`, `webm`: one raw 16 byte KID. Empty is `BAD_VALUE`, malformed is `ERROR_DRM_INIT_DATA`, other MIME is `ERROR_DRM_CANNOT_HANDLE` |
| Key request | `STREAMING` returns `{"kids":["..."],"type":"temporary"}`; `OFFLINE` returns type `persistent-license`; `RELEASE` (scope = key set ID, empty init data) returns `persistent-release-message` |
| Key response | W3C JWK set of 16 byte `oct` keys. Malformed is `ERROR_DRM_LICENSE_PARSE`; a `persistent-license` response returns a key set ID (kept in memory, lost on service restart) and a response to `RELEASE` removes it; other types are `ERROR_DRM_CANNOT_HANDLE`. Fires deterministic vendor-event, expiration, and key-change callbacks to the listener |
| `ICryptoPlugin.decrypt` | `UNENCRYPTED`, `AES_CTR` (counter continues across subsamples, optional `cens` pattern), `AES_CBC` (IV restarts per subsample, optional `cbcs` pattern, trailing partial block clear). `AES_CBC_CTS` and `secure=true` are `ERROR_DRM_CANNOT_HANDLE` |
| Decrypt errors | Unbound/closed session `ERROR_DRM_SESSION_NOT_OPENED`; missing key or after `removeKeys` `ERROR_DRM_NO_LICENSE`; no subsamples `ERROR_DRM_ZERO_SUBSAMPLES`; output too small `ERROR_DRM_FRAME_TOO_LARGE` |
| Buffers | Source/destination `AVBuffer` handles are mapped with `IAVBufferHelper`; the same handle for both decrypts in place. The caller still owns and frees both buffers |
| Legacy crypto (RDK adaptation) | `IDrmPlugin.encrypt/decrypt` use the session key selected by `keyId`: `AES/CTR/NoPadding` (default) or `AES/CBC/NoPadding` via `setCipherAlgorithm`; `sign/verify` use `HmacSHA256`. `signRSA` is `ERROR_DRM_CANNOT_HANDLE`. Android's ClearKey does not implement these generic crypto calls |
| Properties | `vendor`, `version`, `description`, `algorithms`; test-only `rdkVtsError` accepts one-shot `resourceContention`, `invalidState`, and `notProvisioned` values for `getKeyRequest`; unknown is `ERROR_DRM_CANNOT_HANDLE`; empty name is `BAD_VALUE` |
| Provisioning | Not required: `getProvisionRequest` is `ERROR_DRM_CANNOT_HANDLE` |
| Sessions | Process-wide table shared by every plugin instance, maximum 16 (`ERROR_DRM_RESOURCE_BUSY` beyond that) |

`BAD_VALUE` is reported as `EX_SERVICE_SPECIFIC` with `android::BAD_VALUE`
(-22), as required by DRM 0.2.0.0; every other error is an
`EX_SERVICE_SPECIFIC` exception carrying the `DrmErrors` value.

## Build and test

From the component root, the build helper accepts the same version-export
style as the video decoder build scripts:

```sh
export RDK_HALIF_AIDL_VERSION=0.23.0  # default
./build_clearkey.sh Target=linux
```

The ClearKey service and in-process Binder tests target DRM 0.2.0.0 from
release 0.23.0. DRM arguments and parcelable fields use its UTF-8 `std::string`
API directly; the older 0.22.0 `String16` API is no longer supported.

No `RDK_HAL_DIR`, `BINDER_INCLUDE_DIR`, `BINDER_LIB_DIR` or
`AVBUFFERHELPER_LIBRARY` exports are required. The script:

1. Clones/checks out `rdk-halif-aidl` at the requested Git version.
2. Clones/checks out `linux_binder_idl` at the release's `binder_sdk.version`
   pin and builds/installs the target Binder SDK.
3. Selects the DRM snapshot from `versions_released.yaml` and builds/installs
   its generated C++ as `libdrm-v<version>-cpp.so`. Released snapshots do not
   require AIDL code generation or the host AIDL compiler.
4. Builds a link-time AVBuffer helper placeholder (or links a supplied platform
   helper), then builds/tests/installs the ClearKey service.
5. Generates `Hello World` AES-128-CTR fixtures under the target's
   `fixtures/` directory, displaying plaintext, ciphertext hex and an OpenSSL
   round-trip result.
6. Creates `clearkey_drm_linux_vts.tar.gz` (or `clearkey_drm_arm_vts.tar.gz`)
   inside the target build directory after successful compilation and host tests.
   The archive contains the service, Binder/DRM shared libraries and fixtures.
   It never includes `libavbufferhelper.so` or the separately built unified VTS
   executable. The target must supply a compatible platform helper and OpenSSL
   runtime. Previously installed placeholder helpers are removed; the build-only
   copy remains in the CMake build directory.

Dependency checkouts live under `build/clearkey/deps/`; target SDKs and outputs
are isolated under `build/clearkey/linux/` or `build/clearkey/arm/`.
Clean checkouts can switch to the requested version; dirty checkouts are
rejected rather than cleaned. `BINDER_VERSION`, `AIDL_VERSION_MANIFEST` and
`DRM_AIDL_VERSION` are optional overrides. Development refs must contain
generated DRM C++ or you must supply it using the include/source/library
overrides listed by `--help`.

The placeholder helper is built but never installed or packaged. The deployed
service must load the platform's `libavbufferhelper.so`. The Binder test uses its
own fake helper; passing it does not validate real buffer mapping.
To link against a platform implementation during the build:

```sh
export AVBUFFERHELPER_INCLUDE_DIR=/path/to/platform/helper/include
export AVBUFFERHELPER_LIBRARY=/path/to/platform/libavbufferhelper.so
./build_clearkey.sh Target=linux
```

Both `avbufferhelper.h` and the older `IAVBufferHelper.h` header name are
supported. Binder, OpenSSL and the platform helper must match the target
architecture. Building the DRM AIDL interface alone does not need the helper;
it is a dependency of the ClearKey service implementation.

The service is installed to `build/clearkey/linux/usr/bin/clearkey_service`.
For the automatic native build, run it with:

```sh
LD_LIBRARY_PATH="$PWD/build/clearkey/linux/usr/lib/binder:$PWD/build/clearkey/linux/usr/lib/rdk-halif-aidl:$PWD/build/clearkey/linux/usr/lib" \
  ./build/clearkey/linux/usr/bin/clearkey_service clearkey
```

Include the platform helper directory in `LD_LIBRARY_PATH` for service startup.
Advanced users can optionally supply `RDK_HAL_DIR`, the paired
`BINDER_INCLUDE_DIR`/`BINDER_LIB_DIR`, and DRM include/source/library paths
to reuse prebuilt artifacts. Add their library directories to `LD_LIBRARY_PATH`.
Exports inside the build script do not modify the calling shell.

For ARM, source your target SDK environment first or export
`CMAKE_TOOLCHAIN_FILE`, then use `./build_clearkey.sh Target=arm`.
Binder, DRM AIDL and the build-only placeholder are cross-built automatically;
the target SDK must supply OpenSSL. Deployment needs a matching platform helper.
Host Python 3 and the OpenSSL executable generate the fixtures. This builds but does not
execute target tests. Outputs go to `build/clearkey/arm/` by default.

The script resolves the toolchain from the currently sourced SDK and selects
the current `make` executable. When the compiler, SDK, CMake or build-tool
configuration changes (including build directories copied from another host),
it resets only the affected CMake cache and metadata automatically. Dependency
checkouts and installed outputs are preserved; no manual directory cleanup is
required. `Target=linux` in an OE SDK shell uses native host tools instead of
the ARM compiler and sysroot, without changing the calling shell.

To remove a target's build outputs, including caches and installed artifacts:

```sh
./build_clearkey.sh clean              # Linux (default target)
./build_clearkey.sh Target=arm clean   # ARM
```

Cleaning does not configure CMake or require the SDK/OpenSSL. Dependency
checkouts in `build/clearkey/deps/` are preserved. For safety, `clean` supports
only the default target directories, not custom `BUILD_DIR` locations.
Cleaning cannot resolve missing OpenSSL development files.

### Deploy the archive

```sh
tar -xzf build/clearkey/linux/clearkey_drm_linux_vts.tar.gz -C /path/to/deploy
cd /path/to/deploy/clearkey_drm_linux_vts
. fixtures/env.sh
export LD_LIBRARY_PATH="$PWD/usr/lib/binder:$PWD/usr/lib/rdk-halif-aidl:/path/to/platform/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
./usr/bin/clearkey_service clearkey
```

Source `fixtures/env.sh` in the shell that launches VTS too. Its Bash-based
path resolution works after extraction at a different location, including paths
containing spaces. The service manager and platform AVBuffer service must already
be running. The archive name does not imply that a VTS executable is included.

### OpenSSL missing from an ARM SDK

ClearKey needs target OpenSSL development headers and `libcrypto`, not just
the compiler or the host OpenSSL package. Check the sourced SDK:

```sh
ls "$SDKTARGETSYSROOT/usr/include/openssl/ssl.h"
ls "$SDKTARGETSYSROOT/usr/lib/libcrypto.so"
```

If absent, add `TOOLCHAIN_TARGET_TASK:append = " openssl-dev"` to the Yocto SDK
configuration, regenerate the SDK (`bitbake <image> -c populate_sdk`), reinstall
and source it again. A runtime-only `libcrypto.so.*` does not replace the
development headers and linker library.

If a separate compatible ARM OpenSSL installation is already available:

```sh
export OPENSSL_INCLUDE_DIR=/path/to/arm/openssl/include
export OPENSSL_CRYPTO_LIBRARY=/path/to/arm/openssl/lib/libcrypto.so
./build_clearkey.sh Target=arm
```

`OPENSSL_ROOT_DIR` is also accepted as a discovery hint, but explicit
include/library paths are preferable for installations outside an OE sysroot.
Never point these variables at native x86 libraries for an ARM build.

#### Building OpenSSL once without Yocto

If the SDK cannot be regenerated with `openssl-dev`, cross-build a static
OpenSSL with the sourced SDK toolchain. Docker is optional; any shell with the
SDK works. **Build it once** into a directory you keep, then reuse it for every
ClearKey build: `build_clearkey.sh` never builds OpenSSL, it only reads
`OPENSSL_INCLUDE_DIR` and `OPENSSL_CRYPTO_LIBRARY`, and `clean` never removes
your OpenSSL install.

1. Choose the OpenSSL release (match your target's major/minor, e.g. 3.0.x for
   Kirkstone; any 3.x works for static linking) and an install prefix outside
   `build/clearkey/<target>/`:

   ```sh
   export OPENSSL_VER=3.0.22                           # example release
   export OPENSSL_PREFIX="$HOME/openssl-arm"           # any permanent directory
   ```

2. Download the official release (`-f` fails on a wrong version instead of
   saving an error page), verify its published SHA256, then build:

   ```sh
   source /opt/toolchains/<sdk>/environment-setup-<target>
   curl -fLO https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VER}/openssl-${OPENSSL_VER}.tar.gz
   tar xf openssl-${OPENSSL_VER}.tar.gz && cd openssl-${OPENSSL_VER}

   unset CROSS_COMPILE   # the SDK's CC already has the prefix; OpenSSL would double it
   /usr/bin/perl ./Configure linux-armv4 no-shared no-tests -fPIC \
       --prefix="${OPENSSL_PREFIX}" --openssldir="${OPENSSL_PREFIX}/ssl" --libdir=lib
   make -j"$(nproc)" build_libs
   make install_dev
   ```

3. Check the result is 32-bit ARM, not host x86:

   ```sh
   ls "${OPENSSL_PREFIX}/include/openssl/ssl.h"
   file "${OPENSSL_PREFIX}/lib/libcrypto.a"
   ```

4. Reuse it in every ClearKey build (put these two exports in your shell
   profile or a small `env.sh`):

   ```sh
   source /opt/toolchains/<sdk>/environment-setup-<target>
   export OPENSSL_INCLUDE_DIR="${OPENSSL_PREFIX}/include"
   export OPENSSL_CRYPTO_LIBRARY="${OPENSSL_PREFIX}/lib/libcrypto.a"
   export RDK_HALIF_AIDL_VERSION=0.23.0
   ./build_clearkey.sh Target=arm
   ```

Notes:

- Run `Configure` with the host `/usr/bin/perl`: the SDK's Perl lacks modules
  such as `FindBin`.
- `no-shared -fPIC` links OpenSSL into `clearkey_service`, so no OpenSSL shared
  library has to be deployed or matched to the target image.
- Rebuild OpenSSL only when changing the SDK/ABI or OpenSSL release.


Adjust the workspace path for your user. This uses the script's default AIDL
version (0.23.0); export `RDK_HALIF_AIDL_VERSION` inside the container command
to select another release. The service and both test executables were
cross-built and verified as ARM EABI5 hard-float. ARM tests were not executed;
the container has no ARM user-mode emulator. The default AVBuffer helper
remains a link-time placeholder, not a real L3 buffer mapper.

For Binder-free host validation from the component root:

```sh
./build_clearkey.sh --core-only
```

Host tests need only CMake and OpenSSL:

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

The service needs the platform Binder SDK, the generated DRM AIDL C++ sources
(or library) and the AVBuffer helper:

```sh
cmake -S . -B build -DCLEARKEY_BUILD_SERVICE=ON \
  -DBINDER_INCLUDE_DIR=<dir with binder/ utils/ ...> \
  -DBINDER_LIB_DIR=<dir with libbinder libutils libcutils liblog libbase> \
  -DDRM_AIDL_INCLUDE_DIR=<drm/0.2.0.0/include> \
  -DDRM_AIDL_SOURCE_DIR=<drm/0.2.0.0/src/com/rdk/hal/drm> \
  -DAVBUFFERHELPER_INCLUDE_DIR=<dir with IAVBufferHelper.h> \
  -DAVBUFFERHELPER_LIBRARY=<path to libavbufferhelper.so>
cmake --build build
```

`clearkey_binder_test` calls the plugins in-process through the AIDL
interfaces, so it needs no binder driver. It checks the same status codes the
VTS asserts.

## Run with the VTS

For a readable sample instead of the default 4096 random bytes, generate
fixtures with `--text`:

```sh
tools/make_clearkey_fixtures.py /tmp/clearkey_hello --text "Hello World"
. /tmp/clearkey_hello/env.sh
```

The generator displays the original text, the ciphertext in hex, and the
text recovered by an OpenSSL AES-128-CTR round trip. The key and IV are random,
so the ciphertext changes on each run. `plain.bin` contains exactly the UTF-8
text without an added newline; `cipher.bin` contains binary ciphertext.
`--text` and `--size` cannot be combined.

This demonstration uses OpenSSL, not the ClearKey service. Run
`VTS_L2_DrmClearKey.decryptKnownSample_pos` to verify the service's legacy
decryption, or `VTS_L3_Drm.decryptSampleWithAvBuffer_pos` for the AIDL AVBuffer
path; both compare the decrypted output with `plain.bin`.

```sh
tools/make_clearkey_fixtures.py /tmp/clearkey_fixtures
. /tmp/clearkey_fixtures/env.sh      # DRM_L2_* and DRM_L3_*
servicemanager &                      # platform binder service manager
AVBufferService &                     # AVBuffer HAL + helper used by L3
clearkey_service clearkey &           # registers com.rdk.hal.drm.IDrmFactory/clearkey
<unified VTS binary> -g VTS_L1_Drm; ... VTS_L2_DrmClearKey; ... VTS_L3_Drm
```

The instance name is `clearkey` by default; override with the first argument or
`DRM_FACTORY_INSTANCE`, matching the VTS configuration.

## Not covered

Disk-persistent offline licenses and `keySetId` restore, license renewal/expiry,
`setPlaybackId` metrics, secure decode and HDCP policy. Use a vendor CDM for
those.
