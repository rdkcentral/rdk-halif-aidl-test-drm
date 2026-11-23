// SPDX-License-Identifier: Apache-2.0
#include "ClearKeyDrmFactory.h"

#include <com/rdk/hal/drm/CryptoSchemes.h>
#include <com/rdk/hal/drm/SecurityLevel.h>
#include <com/rdk/hal/drm/SupportedContentType.h>

#include "ClearKeyCryptoPlugin.h"
#include "ClearKeyDrmPlugin.h"
#include "binder_status.h"
#include "license.h"
#include "session.h"

using com::rdk::hal::drm::SecurityLevel;

namespace rdk::clearkey {

std::string ClearKeyDrmFactory::serviceName(const std::string& instance) {
    return "com.rdk.hal.drm.IDrmFactory/" + instance;
}

::android::binder::Status ClearKeyDrmFactory::createDrmPlugin(
        const ::com::rdk::hal::drm::Uuid& uuid, const std::string&,
        ::android::sp<::com::rdk::hal::drm::IDrmPlugin>* _aidl_return) {
    *_aidl_return = nullptr;
    if (!isSupportedUuid(uuid.uuid)) {
        return toStatus(Err::CannotHandle);
    }
    *_aidl_return = ::android::sp<ClearKeyDrmPlugin>::make();
    return Status::ok();
}

::android::binder::Status ClearKeyDrmFactory::createCryptoPlugin(
        const ::com::rdk::hal::drm::Uuid& uuid, const std::vector<uint8_t>& initData,
        ::android::sp<::com::rdk::hal::drm::ICryptoPlugin>* _aidl_return) {
    *_aidl_return = nullptr;
    if (!isSupportedUuid(uuid.uuid)) {
        return toStatus(Err::CannotHandle);
    }
    // Non-empty init data is the MediaDrm session ID to bind to.
    if (!initData.empty() && !SessionLibrary::instance().contains(initData)) {
        return toStatus(Err::SessionNotOpened);
    }
    *_aidl_return = ::android::sp<ClearKeyCryptoPlugin>::make(initData);
    return Status::ok();
}

::android::binder::Status ClearKeyDrmFactory::getSupportedCryptoSchemes(
        ::com::rdk::hal::drm::CryptoSchemes* _aidl_return) {
    _aidl_return->uuids.clear();
    for (const auto& id : {kClearKeyUuid, kCommonPsshUuid}) {
        ::com::rdk::hal::drm::Uuid uuid;
        uuid.uuid = id;
        _aidl_return->uuids.push_back(uuid);
    }
    _aidl_return->mimeTypes.clear();
    for (const char* mime : {"video/mp4", "audio/mp4", "video/webm", "audio/webm"}) {
        ::com::rdk::hal::drm::SupportedContentType type;
        type.mime = mime;
        type.minLevel = SecurityLevel::SW_SECURE_CRYPTO;
        type.maxLevel = SecurityLevel::SW_SECURE_CRYPTO;
        _aidl_return->mimeTypes.push_back(type);
    }
    return Status::ok();
}

}  // namespace rdk::clearkey
