// SPDX-License-Identifier: Apache-2.0
#ifndef RDK_CLEARKEY_DRM_FACTORY_H
#define RDK_CLEARKEY_DRM_FACTORY_H

#include <com/rdk/hal/drm/BnDrmFactory.h>

#include <string>

namespace rdk::clearkey {

class ClearKeyDrmFactory : public ::com::rdk::hal::drm::BnDrmFactory {
public:
    using Status = ::android::binder::Status;

    // Service manager name for the given instance, e.g. "clearkey".
    static std::string serviceName(const std::string& instance);

    Status createDrmPlugin(const ::com::rdk::hal::drm::Uuid& uuid,
            const std::string& appPackageName,
            ::android::sp<::com::rdk::hal::drm::IDrmPlugin>* _aidl_return) override;
    Status createCryptoPlugin(const ::com::rdk::hal::drm::Uuid& uuid,
            const std::vector<uint8_t>& initData,
            ::android::sp<::com::rdk::hal::drm::ICryptoPlugin>* _aidl_return) override;
    Status getSupportedCryptoSchemes(::com::rdk::hal::drm::CryptoSchemes* _aidl_return) override;
};

}  // namespace rdk::clearkey

#endif
