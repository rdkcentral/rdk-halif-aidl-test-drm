// SPDX-License-Identifier: Apache-2.0
#ifndef RDK_CLEARKEY_DRM_PLUGIN_H
#define RDK_CLEARKEY_DRM_PLUGIN_H

#include <com/rdk/hal/drm/BnDrmPlugin.h>
#include <com/rdk/hal/drm/IDrmPluginListener.h>

#include <mutex>
#include <string>

namespace rdk::clearkey {

class ClearKeyDrmPlugin : public ::com::rdk::hal::drm::BnDrmPlugin {
public:
    using Status = ::android::binder::Status;
    using Bytes = std::vector<uint8_t>;

    Status closeSession(const Bytes& sessionId) override;
    Status decrypt(const Bytes& sessionId, const Bytes& keyId, const Bytes& input,
            const Bytes& iv, Bytes* _aidl_return) override;
    Status encrypt(const Bytes& sessionId, const Bytes& keyId, const Bytes& input,
            const Bytes& iv, Bytes* _aidl_return) override;
    Status getHdcpLevels(::com::rdk::hal::drm::HdcpLevels* _aidl_return) override;
    Status getKeyRequest(const Bytes& scope, const Bytes& initData, const std::string& mimeType,
            ::com::rdk::hal::drm::KeyType keyType,
            const std::vector<::com::rdk::hal::drm::KeyValue>& optionalParameters,
            ::com::rdk::hal::drm::KeyRequest* _aidl_return) override;
    Status getMetrics(std::vector<::com::rdk::hal::drm::DrmMetricGroup>* _aidl_return) override;
    Status getNumberOfSessions(::com::rdk::hal::drm::NumberOfSessions* _aidl_return) override;
    Status getPropertyByteArray(const std::string& propertyName, Bytes* _aidl_return) override;
    Status getPropertyString(const std::string& propertyName, std::string* _aidl_return) override;
    Status getProvisionRequest(const std::string& certificateType,
            const std::string& certificateAuthority,
            ::com::rdk::hal::drm::ProvisionRequest* _aidl_return) override;
    Status getSecurityLevel(const Bytes& sessionId,
            ::com::rdk::hal::drm::SecurityLevel* _aidl_return) override;
    Status openSession(::com::rdk::hal::drm::SecurityLevel securityLevel,
            Bytes* _aidl_return) override;
    Status provideKeyResponse(const Bytes& scope, const Bytes& response,
            ::com::rdk::hal::drm::KeySetId* _aidl_return) override;
    Status provideProvisionResponse(const Bytes& response,
            ::com::rdk::hal::drm::ProvideProvisionResponseResult* _aidl_return) override;
    Status queryKeyStatus(const Bytes& sessionId,
            std::vector<::com::rdk::hal::drm::KeyValue>* _aidl_return) override;
    Status removeKeys(const Bytes& sessionId) override;
    Status requiresSecureDecoder(const std::string& mime,
            ::com::rdk::hal::drm::SecurityLevel level, bool* _aidl_return) override;
    Status setCipherAlgorithm(const Bytes& sessionId, const std::string& algorithm) override;
    Status setListener(
            const ::android::sp<::com::rdk::hal::drm::IDrmPluginListener>& listener) override;
    Status setMacAlgorithm(const Bytes& sessionId, const std::string& algorithm) override;
    Status setPlaybackId(const Bytes& sessionId, const std::string& playbackId) override;
    Status setPropertyByteArray(const std::string& propertyName, const Bytes& value) override;
    Status setPropertyString(const std::string& propertyName, const std::string& value) override;
    Status sign(const Bytes& sessionId, const Bytes& keyId, const Bytes& message,
            Bytes* _aidl_return) override;
    Status signRSA(const Bytes& sessionId, const std::string& algorithm, const Bytes& message,
            const Bytes& wrappedKey, Bytes* _aidl_return) override;
    Status verify(const Bytes& sessionId, const Bytes& keyId, const Bytes& message,
            const Bytes& signature, bool* _aidl_return) override;

private:
    ::android::sp<::com::rdk::hal::drm::IDrmPluginListener> listener();
    std::string takeTestError();

    std::mutex mMutex;
    ::android::sp<::com::rdk::hal::drm::IDrmPluginListener> mListener;
    std::string mTestError;
};

}  // namespace rdk::clearkey

#endif
