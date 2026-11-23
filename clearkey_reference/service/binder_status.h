// SPDX-License-Identifier: Apache-2.0
#ifndef RDK_CLEARKEY_BINDER_STATUS_H
#define RDK_CLEARKEY_BINDER_STATUS_H

#include <binder/Status.h>
#include <com/rdk/hal/drm/DrmErrors.h>
#include <utils/Errors.h>

#include "error.h"

namespace rdk::clearkey {

// DRM 0.2.0.0 reports BAD_VALUE and DrmErrors via EX_SERVICE_SPECIFIC.
inline ::android::binder::Status toStatus(Err err) {
    using ::android::binder::Status;
    using com::rdk::hal::drm::DrmErrors;
    const auto specific = [](DrmErrors e) {
        return Status::fromServiceSpecificError(static_cast<int32_t>(e));
    };
    switch (err) {
        case Err::Ok:
            return Status::ok();
        case Err::BadValue:
            return Status::fromServiceSpecificError(::android::BAD_VALUE);
        case Err::SessionNotOpened:
            return specific(DrmErrors::ERROR_DRM_SESSION_NOT_OPENED);
        case Err::NoLicense:
            return specific(DrmErrors::ERROR_DRM_NO_LICENSE);
        case Err::CannotHandle:
            return specific(DrmErrors::ERROR_DRM_CANNOT_HANDLE);
        case Err::Decrypt:
            return specific(DrmErrors::ERROR_DRM_DECRYPT);
        case Err::FrameTooLarge:
            return specific(DrmErrors::ERROR_DRM_FRAME_TOO_LARGE);
        case Err::ZeroSubSamples:
            return specific(DrmErrors::ERROR_DRM_ZERO_SUBSAMPLES);
        case Err::LicenseParse:
            return specific(DrmErrors::ERROR_DRM_LICENSE_PARSE);
        case Err::InitData:
            return specific(DrmErrors::ERROR_DRM_INIT_DATA);
        case Err::ResourceBusy:
            return specific(DrmErrors::ERROR_DRM_RESOURCE_BUSY);
        case Err::ResourceContention:
            return specific(DrmErrors::ERROR_DRM_RESOURCE_CONTENTION);
        case Err::InvalidState:
            return specific(DrmErrors::ERROR_DRM_INVALID_STATE);
        case Err::NotProvisioned:
            return specific(DrmErrors::ERROR_DRM_NOT_PROVISIONED);
    }
    return specific(DrmErrors::ERROR_DRM_UNKNOWN);
}

}  // namespace rdk::clearkey

#endif
