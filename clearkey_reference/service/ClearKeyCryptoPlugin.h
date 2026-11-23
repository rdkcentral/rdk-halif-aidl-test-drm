// SPDX-License-Identifier: Apache-2.0
#ifndef RDK_CLEARKEY_CRYPTO_PLUGIN_H
#define RDK_CLEARKEY_CRYPTO_PLUGIN_H

#include <com/rdk/hal/drm/BnCryptoPlugin.h>

#include <mutex>
#include <string>

namespace rdk::clearkey {

class ClearKeyCryptoPlugin : public ::com::rdk::hal::drm::BnCryptoPlugin {
public:
    using Status = ::android::binder::Status;
    using Bytes = std::vector<uint8_t>;

    // A non-empty sessionId binds the plugin to a MediaDrm session at creation.
    explicit ClearKeyCryptoPlugin(const Bytes& sessionId = Bytes());

    Status decrypt(const ::com::rdk::hal::drm::DecryptArgs& args, int32_t* _aidl_return) override;
    Status notifyResolution(int32_t width, int32_t height) override;
    Status requiresSecureDecoderComponent(
            const std::string& mime, bool* _aidl_return) override;
    Status setMediaDrmSession(const Bytes& sessionId) override;

private:
    std::mutex mMutex;
    Bytes mSessionId;
};

}  // namespace rdk::clearkey

#endif
