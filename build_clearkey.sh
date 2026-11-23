#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

TOP="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

usage()
{
    cat <<EOF
Usage: $0 [Target=linux|arm] [clean|--core-only|--help]

Build the reference ClearKey service and tests (not the unified VTS).
--core-only builds/tests the Binder-free core with CMake and OpenSSL only.
clean removes the selected target's default build outputs, preserving deps/.
Target defaults to linux; use Target=arm clean for ARM outputs.

Environment overrides:
  RDK_HALIF_AIDL_VERSION     Git tag/branch/commit (default: 0.23.0)
  RDK_HAL_DIR               Existing matching checkout; otherwise cloned locally
  AIDL_VERSION_MANIFEST     Manifest path (default: versions_released.yaml)
  DRM_AIDL_VERSION          DRM snapshot version; defaults to the manifest entry
  BINDER_VERSION            Binder revision; defaults to binder_sdk.version
  LINUX_BINDER_DIR          Optional existing matching Binder checkout
  BINDER_INCLUDE_DIR        Directory containing binder/, utils/, etc.
  BINDER_LIB_DIR            Directory containing libbinder, libutils, etc.
  DRM_AIDL_INCLUDE_DIR       Generated DRM include root
  DRM_AIDL_SOURCE_DIR        Generated DRM .cpp directory
  DRM_AIDL_LIBRARY           Prebuilt DRM library instead of generated sources
  AVBUFFERHELPER_INCLUDE_DIR Directory containing IAVBufferHelper.h
  AVBUFFERHELPER_LIBRARY     Optional platform library; otherwise build placeholder
  BUILD_DIR                 Output directory (default: build/clearkey/<target>)
  CMAKE_BIN                 CMake executable (default: cmake)
  CMAKE_TOOLCHAIN_FILE       Optional CMake cross-toolchain file
  OPENSSL_ROOT_DIR          Optional target OpenSSL installation prefix
  OPENSSL_INCLUDE_DIR       Optional directory containing openssl/ headers
  OPENSSL_CRYPTO_LIBRARY    Optional target libcrypto.so or libcrypto.a path
  JOBS                      Parallel build jobs (default: nproc)

For ARM, source the toolchain environment (CC/CXX) or set CMAKE_TOOLCHAIN_FILE.
Binder and DRM AIDL are built automatically. Tests run only on Linux.
The default helper is a link-time placeholder, NOT an L3 buffer mapper.
Exports passed to this script are respected; executing it does not change
the calling shell's environment.
EOF
}

fail()
{
    echo "ERROR: $*" >&2
    exit 1
}

TARGET_ARCH="${TARGET_ARCH:-linux}"
BUILD_SERVICE=ON
CLEAN=false
for arg in "$@"; do
    case "${arg}" in
        Target=linux|Target=arm) TARGET_ARCH="${arg#Target=}" ;;
        --core-only) BUILD_SERVICE=OFF ;;
        clean) CLEAN=true ;;
        help|-h|--help) usage; exit 0 ;;
        *) fail "Unknown argument: ${arg}. Use --help." ;;
    esac
done
[[ "${TARGET_ARCH}" == linux || "${TARGET_ARCH}" == arm ]] ||
    fail "TARGET_ARCH must be linux or arm"
if [[ "${CLEAN}" == true ]]; then
    [[ "${BUILD_SERVICE}" == ON ]] || fail "clean cannot be combined with --core-only"
    clean_dir="$(realpath -m "${TOP}/build/clearkey/${TARGET_ARCH}")"
    [[ "${clean_dir}" == "${TOP}/build/clearkey/${TARGET_ARCH}" ]] ||
        fail "Refusing to clean a redirected build directory: ${clean_dir}"
    [[ -z "${BUILD_DIR:-}" || "$(realpath -m "${BUILD_DIR}")" == "${clean_dir}" ]] ||
        fail "clean supports only the default target build directory; unset BUILD_DIR"
    if [[ -e "${clean_dir}" ]]; then
        echo "Removing target build outputs: ${clean_dir}"
        rm -rf -- "${clean_dir}"
    fi
    echo "Cleaned ${TARGET_ARCH} outputs; dependency checkouts preserved"
    exit 0
fi
if [[ "${TARGET_ARCH}" == arm && -z "${CMAKE_TOOLCHAIN_FILE:-}" ]]; then
    [[ -n "${CC:-}" && -n "${CXX:-}" ]] ||
        fail "Target=arm requires CC/CXX from a sourced toolchain or CMAKE_TOOLCHAIN_FILE"
fi

if [[ "${TARGET_ARCH}" == linux &&
      ( -n "${OECORE_NATIVE_SYSROOT:-}" || -n "${OECORE_TARGET_SYSROOT:-}" ) ]]; then
    echo "Target=linux: using native host tools instead of the sourced ARM SDK"
    export PATH="/usr/local/bin:/usr/bin:/bin"
    unset CC CXX CPP LD AR AS NM STRIP RANLIB CFLAGS CXXFLAGS CPPFLAGS LDFLAGS
    unset CMAKE_TOOLCHAIN_FILE CMAKE_PREFIX_PATH CMAKE_FIND_ROOT_PATH
    unset OECORE_NATIVE_SYSROOT OECORE_TARGET_SYSROOT SDKTARGETSYSROOT
    unset PKG_CONFIG_SYSROOT_DIR PKG_CONFIG_PATH PKG_CONFIG_LIBDIR CONFIG_SITE
    unset CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH LIBRARY_PATH LD_LIBRARY_PATH
fi

export RDK_HALIF_AIDL_VERSION="${RDK_HALIF_AIDL_VERSION:-0.23.0}"
BUILD_DIR="${BUILD_DIR:-${TOP}/build/clearkey/${TARGET_ARCH}}"
BUILD_DIR="$(realpath -m "${BUILD_DIR}")"
CMAKE_BIN="${CMAKE_BIN:-cmake}"
JOBS="${JOBS:-$(nproc)}"
[[ "${JOBS}" =~ ^[1-9][0-9]*$ ]] || fail "JOBS must be a positive integer"
CMAKE_BIN="$(command -v "${CMAKE_BIN}")" || fail "CMake not found"
MAKE_BIN="$(command -v make)" || fail "GNU Make not found; install make"
MAKE_BIN="$(realpath "${MAKE_BIN}")"
CTEST_BIN="$(dirname "${CMAKE_BIN}")/ctest"
[[ -x "${CTEST_BIN}" ]] || fail "ctest not found alongside ${CMAKE_BIN}"

toolchain_args=()
if [[ "${TARGET_ARCH}" == arm && -z "${CMAKE_TOOLCHAIN_FILE:-}" && -n "${OECORE_NATIVE_SYSROOT:-}" ]]; then
    for candidate in "${OECORE_NATIVE_SYSROOT}"/usr/share/cmake*/OEToolchainConfig.cmake; do
        if [[ -f "${candidate}" ]]; then
            export CMAKE_TOOLCHAIN_FILE="${candidate}"
            break
        fi
    done
    [[ -n "${CMAKE_TOOLCHAIN_FILE:-}" ]] ||
        fail "Cannot find OEToolchainConfig.cmake in the sourced SDK; export CMAKE_TOOLCHAIN_FILE"
fi
if [[ -n "${CMAKE_TOOLCHAIN_FILE:-}" ]]; then
    [[ -f "${CMAKE_TOOLCHAIN_FILE}" ]] || fail "Toolchain file not found: ${CMAKE_TOOLCHAIN_FILE}"
    export CMAKE_TOOLCHAIN_FILE="$(realpath "${CMAKE_TOOLCHAIN_FILE}")"
    toolchain_args+=("-DCMAKE_TOOLCHAIN_FILE=$(realpath "${CMAKE_TOOLCHAIN_FILE}")")
fi

configure()
{
    local source="$1" directory="$2"
    shift 2
    local signature cached cached_make cached_toolchain cached_compiler
    signature="$(
        {
            printf '%s\n' "${source}" "${TARGET_ARCH}" "${CMAKE_BIN}" "${MAKE_BIN}" \
                "${CC:-}" "${CXX:-}" "${CFLAGS:-}" "${CXXFLAGS:-}" "${LDFLAGS:-}" \
                "${CMAKE_TOOLCHAIN_FILE:-}" "${OECORE_NATIVE_SYSROOT:-}" \
                "${OECORE_TARGET_SYSROOT:-}" "${PATH}"
            "${CMAKE_BIN}" --version
            "${MAKE_BIN}" --version
            if [[ -n "${CMAKE_TOOLCHAIN_FILE:-}" ]]; then
                sha256sum "${CMAKE_TOOLCHAIN_FILE}"
            fi
        } | sha256sum | cut -d ' ' -f 1
    )"
    if [[ -f "${directory}/CMakeCache.txt" ]]; then
        cached="$(awk -F= '$1 == "CLEARKEY_BUILD_ENV_SIGNATURE:STRING" {print $2}' "${directory}/CMakeCache.txt")"
        cached_make="$(awk -F= '$1 ~ /^CMAKE_MAKE_PROGRAM:/ {print $2}' "${directory}/CMakeCache.txt")"
        cached_toolchain="$(awk -F= '$1 ~ /^CMAKE_TOOLCHAIN_FILE:/ {print $2}' "${directory}/CMakeCache.txt")"
        for compiler in C CXX; do
            cached_compiler="$(awk -F= -v key="CMAKE_${compiler}_COMPILER:" \
                'index($1, key) == 1 {print $2}' "${directory}/CMakeCache.txt")"
            if [[ -n "${cached_compiler}" && ! -x "${cached_compiler}" ]]; then
                cached=""
            fi
        done
        if [[ "${cached}" != "${signature}" || "${cached_make}" != "${MAKE_BIN}" ||
              "${cached_toolchain}" != "${CMAKE_TOOLCHAIN_FILE:-}" ]]; then
            echo "Resetting stale CMake configuration: ${directory}"
            # Only CMake metadata is removed; sources, SDKs and installed outputs remain.
            rm -f "${directory}/CMakeCache.txt"
            rm -rf "${directory}/CMakeFiles"
        fi
    fi
    "${CMAKE_BIN}" -S "${source}" -B "${directory}" -G "Unix Makefiles" \
        "-DCMAKE_MAKE_PROGRAM=${MAKE_BIN}" \
        "-DCLEARKEY_BUILD_ENV_SIGNATURE=${signature}" "$@"
}

ensure_checkout()
{
    local directory="$1" url="$2" ref="$3"
    if [[ ! -e "${directory}" ]]; then
        mkdir -p "$(dirname "${directory}")"
        git clone "${url}" "${directory}"
    fi
    git -C "${directory}" rev-parse --is-inside-work-tree >/dev/null ||
        fail "Not a Git checkout: ${directory}"
    if ! git -C "${directory}" rev-parse --verify "${ref}^{commit}" >/dev/null 2>&1; then
        git -C "${directory}" fetch origin "${ref}"
        ref=FETCH_HEAD
    fi
    local expected current
    expected="$(git -C "${directory}" rev-parse "${ref}^{commit}")"
    current="$(git -C "${directory}" rev-parse HEAD)"
    if [[ "${expected}" != "${current}" ]]; then
        [[ -z "$(git -C "${directory}" status --porcelain)" ]] ||
            fail "Cannot switch dirty dependency checkout: ${directory}"
        git -C "${directory}" checkout --detach "${expected}"
    fi
}

cmake_args=(
    "-DCLEARKEY_BUILD_SERVICE=${BUILD_SERVICE}"
    "-DCMAKE_INSTALL_PREFIX=${BUILD_DIR}/usr"
)
cmake_args+=("${toolchain_args[@]}")
for variable in OPENSSL_ROOT_DIR OPENSSL_INCLUDE_DIR OPENSSL_CRYPTO_LIBRARY; do
    case "${variable}" in
        OPENSSL_ROOT_DIR) value="${OPENSSL_ROOT_DIR:-}" ;;
        OPENSSL_INCLUDE_DIR) value="${OPENSSL_INCLUDE_DIR:-}" ;;
        OPENSSL_CRYPTO_LIBRARY) value="${OPENSSL_CRYPTO_LIBRARY:-}" ;;
    esac
    if [[ -n "${value}" ]]; then
        [[ -e "${value}" ]] || fail "${variable} does not exist: ${value}"
        cmake_args+=("-D${variable}=${value}")
    fi
done

if [[ "${BUILD_SERVICE}" == ON ]]; then
    [[ -z "${BINDER_INCLUDE_DIR:-}" && -z "${BINDER_LIB_DIR:-}" ||
       -n "${BINDER_INCLUDE_DIR:-}" && -n "${BINDER_LIB_DIR:-}" ]] ||
        fail "Set both BINDER_INCLUDE_DIR and BINDER_LIB_DIR, or neither"

    if [[ -z "${DRM_AIDL_INCLUDE_DIR:-}" ||
          ( -z "${DRM_AIDL_SOURCE_DIR:-}" && -z "${DRM_AIDL_LIBRARY:-}" ) ||
          -z "${BINDER_INCLUDE_DIR:-}" || -z "${AVBUFFERHELPER_INCLUDE_DIR:-}" ]]; then
        RDK_HAL_DIR="${RDK_HAL_DIR:-${TOP}/build/clearkey/deps/rdk-halif-aidl}"
        ensure_checkout "${RDK_HAL_DIR}" https://github.com/rdkcentral/rdk-halif-aidl.git "${RDK_HALIF_AIDL_VERSION}"
        manifest="${AIDL_VERSION_MANIFEST:-versions_released.yaml}"
        [[ "${manifest}" == /* ]] || manifest="${RDK_HAL_DIR}/${manifest}"

        if [[ -z "${BINDER_INCLUDE_DIR:-}" && -z "${BINDER_LIB_DIR:-}" ]]; then
            if [[ -z "${BINDER_VERSION:-}" ]]; then
                [[ -f "${RDK_HAL_DIR}/binder_sdk.version" ]] || fail "Missing binder_sdk.version"
                BINDER_VERSION="$(awk '!/^[[:space:]]*(#|$)/ {gsub(/[[:space:]]/, ""); print; exit}' "${RDK_HAL_DIR}/binder_sdk.version")"
            fi
            [[ -n "${BINDER_VERSION}" ]] || fail "Binder version pin is empty"
            LINUX_BINDER_DIR="${LINUX_BINDER_DIR:-${TOP}/build/clearkey/deps/linux_binder_idl}"
            ensure_checkout "${LINUX_BINDER_DIR}" https://github.com/rdkcentral/linux_binder_idl.git "${BINDER_VERSION}"
            BINDER_INCLUDE_DIR="${BUILD_DIR}/usr/include/binder_sdk"
            BINDER_LIB_DIR="${BUILD_DIR}/usr/lib/binder"
            # Binder suppresses install rules under OE; keep the compiler/sysroot
            # flags but request standalone SDK installation, not recipe packaging.
            (
                unset OECORE_NATIVE_SYSROOT OECORE_TARGET_SYSROOT
                configure "${LINUX_BINDER_DIR}" "${BUILD_DIR}/binder" \
                "${toolchain_args[@]}" -DBUILD_ENV_HOST=ON -DBUILD_HOST_AIDL=OFF \
                "-DCMAKE_INSTALL_PREFIX=${BUILD_DIR}/usr" \
                "-DCMAKE_INSTALL_LIBDIR=${BINDER_LIB_DIR}" \
                "-DCMAKE_INSTALL_INCDIR=${BINDER_INCLUDE_DIR}"
            )
            "${CMAKE_BIN}" --build "${BUILD_DIR}/binder" --parallel "${JOBS}"
            "${CMAKE_BIN}" --install "${BUILD_DIR}/binder"
        fi

        if [[ -z "${DRM_AIDL_INCLUDE_DIR:-}" ||
              ( -z "${DRM_AIDL_SOURCE_DIR:-}" && -z "${DRM_AIDL_LIBRARY:-}" ) ]]; then
            if [[ -z "${DRM_AIDL_VERSION:-}" ]]; then
                [[ -f "${manifest}" ]] || fail "AIDL manifest not found: ${manifest}"
                DRM_AIDL_VERSION="$(awk '$1 == "drm:" {gsub(/["\047]/, "", $2); print $2; exit}' "${manifest}")"
                [[ -n "${DRM_AIDL_VERSION}" ]] ||
                    fail "No DRM version in ${manifest}; export DRM_AIDL_VERSION"
            fi
            DRM_AIDL_INCLUDE_DIR="${DRM_AIDL_INCLUDE_DIR:-${RDK_HAL_DIR}/drm/${DRM_AIDL_VERSION}/include}"
            if [[ -z "${DRM_AIDL_LIBRARY:-}" && -z "${DRM_AIDL_SOURCE_DIR:-}" ]]; then
                # Released snapshots compile committed C++; no host AIDL generator is needed.
                sdk="${BUILD_DIR}/aidl-sdk"
                mkdir -p "${sdk}/include" "${sdk}/lib"
                ln -sfn "$(realpath "${BINDER_INCLUDE_DIR}")" "${sdk}/include/binder_sdk"
                ln -sfn "$(realpath "${BINDER_LIB_DIR}")" "${sdk}/lib/binder"
                aidl_build_dir="${BUILD_DIR}/drm-aidl/${DRM_AIDL_VERSION}"
                configure "${RDK_HAL_DIR}/drm/${DRM_AIDL_VERSION}" "${aidl_build_dir}" \
                    "${toolchain_args[@]}" "-DBINDER_SDK_DIR=${sdk}" \
                    "-DCMAKE_INSTALL_PREFIX=${BUILD_DIR}/usr" \
                    -DHALIF_INSTALL_LIBDIR=lib/rdk-halif-aidl
                "${CMAKE_BIN}" --build "${aidl_build_dir}" --parallel "${JOBS}"
                "${CMAKE_BIN}" --install "${aidl_build_dir}"
                DRM_AIDL_LIBRARY="${BUILD_DIR}/usr/lib/rdk-halif-aidl/libdrm-v${DRM_AIDL_VERSION}-cpp.so"
            fi
        fi
        if [[ -z "${AVBUFFERHELPER_INCLUDE_DIR:-}" ]]; then
            [[ -f "${manifest}" ]] || fail "AIDL manifest not found: ${manifest}"
            avbuffer_version="$(awk '$1 == "avbuffer:" {gsub(/["\047]/, "", $2); print $2; exit}' "${manifest}")"
            [[ -n "${avbuffer_version}" ]] || fail "No AVBuffer version in ${manifest}"
            AVBUFFERHELPER_INCLUDE_DIR="${RDK_HAL_DIR}/avbuffer/${avbuffer_version}"
        fi
    fi

    if [[ -n "${AVBUFFERHELPER_LIBRARY:-}" ]]; then
        [[ -f "${AVBUFFERHELPER_LIBRARY}" ]] || fail "AVBUFFERHELPER_LIBRARY not found: ${AVBUFFERHELPER_LIBRARY}"
        cmake_args+=("-DCLEARKEY_BUILD_HELPER_SKELETON=OFF")
        helper_install_lib_dir="$(dirname "${AVBUFFERHELPER_LIBRARY}")"
    else
        echo "WARNING: Building a link-time AVBuffer helper placeholder. Supply the platform library for L3." >&2
        cmake_args+=("-DCLEARKEY_BUILD_HELPER_SKELETON=ON")
        AVBUFFERHELPER_LIBRARY="${BUILD_DIR}/libavbufferhelper.so"
        helper_install_lib_dir="${BUILD_DIR}/usr/lib"
    fi
    [[ -f "${BINDER_INCLUDE_DIR:-}/binder/IBinder.h" ]] ||
        fail "Export BINDER_INCLUDE_DIR to the Binder SDK header root"
    [[ -d "${BINDER_LIB_DIR:-}" ]] || fail "Export BINDER_LIB_DIR to the Binder SDK library directory"
    [[ -f "${DRM_AIDL_INCLUDE_DIR}/com/rdk/hal/drm/IDrmFactory.h" ]] ||
        fail "Generated DRM headers not found: ${DRM_AIDL_INCLUDE_DIR}"
    [[ -z "${DRM_AIDL_LIBRARY:-}" || -z "${DRM_AIDL_SOURCE_DIR:-}" ]] ||
        fail "Set DRM_AIDL_LIBRARY or DRM_AIDL_SOURCE_DIR, not both"
    if [[ -n "${DRM_AIDL_LIBRARY:-}" ]]; then
        [[ -f "${DRM_AIDL_LIBRARY}" ]] || fail "DRM_AIDL_LIBRARY not found: ${DRM_AIDL_LIBRARY}"
        cmake_args+=("-DDRM_AIDL_LIBRARY=${DRM_AIDL_LIBRARY}" "-DDRM_AIDL_SOURCE_DIR=")
    else
        [[ -f "${DRM_AIDL_SOURCE_DIR}/IDrmFactory.cpp" ]] ||
            fail "Generated DRM sources not found: ${DRM_AIDL_SOURCE_DIR}; use a released snapshot or prebuilt library"
        cmake_args+=("-DDRM_AIDL_SOURCE_DIR=${DRM_AIDL_SOURCE_DIR}" "-DDRM_AIDL_LIBRARY=")
    fi
    cmake_args+=(
        "-DBINDER_INCLUDE_DIR=${BINDER_INCLUDE_DIR}"
        "-DBINDER_LIB_DIR=${BINDER_LIB_DIR}"
        "-DDRM_AIDL_INCLUDE_DIR=${DRM_AIDL_INCLUDE_DIR}"
        "-DAVBUFFERHELPER_INCLUDE_DIR=${AVBUFFERHELPER_INCLUDE_DIR}"
        "-DAVBUFFERHELPER_LIBRARY=${AVBUFFERHELPER_LIBRARY}"
    )
fi

echo "ClearKey: AIDL=${RDK_HALIF_AIDL_VERSION}, target=${TARGET_ARCH}, service=${BUILD_SERVICE}"
configure "${TOP}/clearkey_reference" "${BUILD_DIR}" "${cmake_args[@]}"
"${CMAKE_BIN}" --build "${BUILD_DIR}" --parallel "${JOBS}"
if [[ "${TARGET_ARCH}" == linux ]]; then
    if [[ "${BUILD_SERVICE}" == ON ]]; then
        runtime_lib_dirs="${BINDER_LIB_DIR}:$(dirname "${AVBUFFERHELPER_LIBRARY}")"
        if [[ -n "${DRM_AIDL_LIBRARY:-}" ]]; then
            runtime_lib_dirs+=":$(dirname "${DRM_AIDL_LIBRARY}")"
        fi
        export LD_LIBRARY_PATH="${runtime_lib_dirs}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
    fi
    (cd "${BUILD_DIR}" && "${CTEST_BIN}" --output-on-failure)
fi
if [[ "${BUILD_SERVICE}" == ON ]]; then
    "${CMAKE_BIN}" --install "${BUILD_DIR}"
    echo "ClearKey service: ${BUILD_DIR}/usr/bin/clearkey_service"
    echo "At runtime add these directories to LD_LIBRARY_PATH:"
    echo "  ${BINDER_LIB_DIR}:${helper_install_lib_dir}"
    if [[ -n "${DRM_AIDL_LIBRARY:-}" ]]; then
        echo "  $(dirname "${DRM_AIDL_LIBRARY}")"
    fi
else
    echo "ClearKey core built: ${BUILD_DIR}"
fi
