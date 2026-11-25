// SPDX-License-Identifier: Apache-2.0
#ifndef RDK_CLEARKEY_BINDER_STATUS_H
#define RDK_CLEARKEY_BINDER_STATUS_H

#include <binder/Status.h>
#include <com/rdk/hal/drm/DrmErrors.h>
#include <utils/Errors.h>

#include <cstdint>

#include "error.h"

namespace rdk::clearkey {

using com::rdk::hal::drm::DrmErrors;

static_assert(static_cast<int32_t>(Err::BadValue) == ::android::BAD_VALUE);
static_assert(static_cast<int32_t>(Err::Unknown) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_UNKNOWN));
static_assert(static_cast<int32_t>(Err::NoLicense) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_NO_LICENSE));
static_assert(static_cast<int32_t>(Err::LicenseExpired) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_LICENSE_EXPIRED));
static_assert(static_cast<int32_t>(Err::SessionNotOpened) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_SESSION_NOT_OPENED));
static_assert(static_cast<int32_t>(Err::DecryptUnitNotInitialized) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_DECRYPT_UNIT_NOT_INITIALIZED));
static_assert(static_cast<int32_t>(Err::Decrypt) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_DECRYPT));
static_assert(static_cast<int32_t>(Err::CannotHandle) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_CANNOT_HANDLE));
static_assert(static_cast<int32_t>(Err::TamperDetected) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_TAMPER_DETECTED));
static_assert(static_cast<int32_t>(Err::NotProvisioned) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_NOT_PROVISIONED));
static_assert(static_cast<int32_t>(Err::DeviceRevoked) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_DEVICE_REVOKED));
static_assert(static_cast<int32_t>(Err::ResourceBusy) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_RESOURCE_BUSY));
static_assert(static_cast<int32_t>(Err::InsufficientOutputProtection) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_INSUFFICIENT_OUTPUT_PROTECTION));
static_assert(static_cast<int32_t>(Err::InsufficientSecurity) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_INSUFFICIENT_SECURITY));
static_assert(static_cast<int32_t>(Err::FrameTooLarge) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_FRAME_TOO_LARGE));
static_assert(static_cast<int32_t>(Err::ResourceContention) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_RESOURCE_CONTENTION));
static_assert(static_cast<int32_t>(Err::SessionLostState) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_SESSION_LOST_STATE));
static_assert(static_cast<int32_t>(Err::InvalidState) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_INVALID_STATE));
static_assert(static_cast<int32_t>(Err::CertificateMalformed) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_CERTIFICATE_MALFORMED));
static_assert(static_cast<int32_t>(Err::CertificateMissing) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_CERTIFICATE_MISSING));
static_assert(static_cast<int32_t>(Err::CryptoLibrary) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_CRYPTO_LIBRARY));
static_assert(static_cast<int32_t>(Err::GenericOem) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_GENERIC_OEM));
static_assert(static_cast<int32_t>(Err::GenericPlugin) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_GENERIC_PLUGIN));
static_assert(static_cast<int32_t>(Err::InitData) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_INIT_DATA));
static_assert(static_cast<int32_t>(Err::KeyNotLoaded) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_KEY_NOT_LOADED));
static_assert(static_cast<int32_t>(Err::LicenseParse) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_LICENSE_PARSE));
static_assert(static_cast<int32_t>(Err::LicensePolicy) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_LICENSE_POLICY));
static_assert(static_cast<int32_t>(Err::LicenseRelease) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_LICENSE_RELEASE));
static_assert(static_cast<int32_t>(Err::LicenseRequestRejected) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_LICENSE_REQUEST_REJECTED));
static_assert(static_cast<int32_t>(Err::LicenseRestore) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_LICENSE_RESTORE));
static_assert(static_cast<int32_t>(Err::LicenseState) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_LICENSE_STATE));
static_assert(static_cast<int32_t>(Err::MediaFramework) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_MEDIA_FRAMEWORK));
static_assert(static_cast<int32_t>(Err::ProvisioningCertificate) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_PROVISIONING_CERTIFICATE));
static_assert(static_cast<int32_t>(Err::ProvisioningConfig) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_PROVISIONING_CONFIG));
static_assert(static_cast<int32_t>(Err::ProvisioningParse) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_PROVISIONING_PARSE));
static_assert(static_cast<int32_t>(Err::ProvisioningRequestRejected) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_PROVISIONING_REQUEST_REJECTED));
static_assert(static_cast<int32_t>(Err::ProvisioningRetry) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_PROVISIONING_RETRY));
static_assert(static_cast<int32_t>(Err::SecureStopRelease) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_SECURE_STOP_RELEASE));
static_assert(static_cast<int32_t>(Err::StorageRead) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_STORAGE_READ));
static_assert(static_cast<int32_t>(Err::StorageWrite) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_STORAGE_WRITE));
static_assert(static_cast<int32_t>(Err::ZeroSubSamples) ==
              static_cast<int32_t>(DrmErrors::ERROR_DRM_ZERO_SUBSAMPLES));

// DRM 0.2.0.0 reports BAD_VALUE and DrmErrors via EX_SERVICE_SPECIFIC. Err
// carries the wire values, so the mapping is the identity.
inline ::android::binder::Status toStatus(Err err) {
    using ::android::binder::Status;
    if (err == Err::Ok) {
        return Status::ok();
    }
    return Status::fromServiceSpecificError(static_cast<int32_t>(err));
}

}  // namespace rdk::clearkey

#endif
