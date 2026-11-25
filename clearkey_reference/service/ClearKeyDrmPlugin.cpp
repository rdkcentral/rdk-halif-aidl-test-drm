// SPDX-License-Identifier: Apache-2.0
#include "ClearKeyDrmPlugin.h"

#include <com/rdk/hal/drm/HdcpLevel.h>
#include <com/rdk/hal/drm/EventType.h>
#include <com/rdk/hal/drm/KeyRequestType.h>
#include <com/rdk/hal/drm/KeyStatus.h>
#include <com/rdk/hal/drm/KeyType.h>
#include <com/rdk/hal/drm/SecurityLevel.h>

#include <string>
#include <utility>

#include "binder_status.h"
#include "cipher.h"
#include "license.h"
#include "session.h"

using com::rdk::hal::drm::HdcpLevel;
using com::rdk::hal::drm::KeyRequestType;
using com::rdk::hal::drm::KeyStatus;
using com::rdk::hal::drm::KeyStatusType;
using com::rdk::hal::drm::KeyType;
using com::rdk::hal::drm::KeyValue;
using com::rdk::hal::drm::SecurityLevel;

namespace rdk::clearkey {

namespace {

SessionLibrary& sessions() {
    return SessionLibrary::instance();
}

KeyValue keyValue(const char* key, const std::string& value) {
    KeyValue pair;
    pair.key = key;
    pair.value = value;
    return pair;
}

}  // namespace

::android::sp<::com::rdk::hal::drm::IDrmPluginListener> ClearKeyDrmPlugin::listener() {
    std::lock_guard<std::mutex> lock(mMutex);
    return mListener;
}

std::string ClearKeyDrmPlugin::takeTestError() {
    std::lock_guard<std::mutex> lock(mMutex);
    std::string error = std::move(mTestError);
    mTestError.clear();
    return error;
}

::android::binder::Status ClearKeyDrmPlugin::openSession(SecurityLevel, Bytes* _aidl_return) {
    // ClearKey only has a software level, so every request resolves to it.
    return toStatus(sessions().open(_aidl_return));
}

::android::binder::Status ClearKeyDrmPlugin::closeSession(const Bytes& sessionId) {
    if (sessionId.empty()) {
        return toStatus(Err::BadValue);
    }
    return toStatus(sessions().close(sessionId));
}

::android::binder::Status ClearKeyDrmPlugin::getSecurityLevel(
        const Bytes& sessionId, SecurityLevel* _aidl_return) {
    if (sessionId.empty()) {
        return toStatus(Err::BadValue);
    }
    if (!sessions().contains(sessionId)) {
        return toStatus(Err::SessionNotOpened);
    }
    *_aidl_return = SecurityLevel::SW_SECURE_CRYPTO;
    return Status::ok();
}

::android::binder::Status ClearKeyDrmPlugin::getKeyRequest(const Bytes& scope,
        const Bytes& initData, const std::string& mimeType, KeyType keyType,
        const std::vector<KeyValue>&, ::com::rdk::hal::drm::KeyRequest* _aidl_return) {
    if (scope.empty() || mimeType.size() == 0) {
        return toStatus(Err::BadValue);
    }
    if (keyType != KeyType::RELEASE && !sessions().contains(scope)) {
        return toStatus(Err::SessionNotOpened);
    }
    const std::string testError = takeTestError();
    if (testError == "resourceContention") {
        return toStatus(Err::ResourceContention);
    }
    if (testError == "invalidState") {
        const auto target = listener();
        if (target != nullptr) {
            target->onSessionLostState(scope);
        }
        return toStatus(Err::InvalidState);
    }
    if (testError == "notProvisioned") {
        return toStatus(Err::NotProvisioned);
    }
    std::vector<Bytes> keyIds;
    std::string request;
    KeyRequestType requestType = KeyRequestType::INITIAL;
    if (keyType == KeyType::RELEASE) {
        // For RELEASE the scope is the key set ID and init data must be empty.
        if (!initData.empty()) {
            return toStatus(Err::BadValue);
        }
        const Err releaseErr = sessions().offlineKeyIds(scope, &keyIds);
        if (releaseErr != Err::Ok) {
            return toStatus(releaseErr);
        }
        request = buildLicenseReleaseRequest(keyIds);
        requestType = KeyRequestType::RELEASE;
    } else {
        const Err err = parseInitData(initData, mimeType, &keyIds);
        if (err != Err::Ok) {
            return toStatus(err);
        }
        request = buildLicenseRequest(keyIds, keyType == KeyType::OFFLINE);
    }
    _aidl_return->request.assign(request.begin(), request.end());
    _aidl_return->requestType = requestType;
    _aidl_return->defaultUrl = "";
    return Status::ok();
}

::android::binder::Status ClearKeyDrmPlugin::provideKeyResponse(const Bytes& scope,
        const Bytes& response, ::com::rdk::hal::drm::KeySetId* _aidl_return) {
    if (scope.empty() || response.empty()) {
        return toStatus(Err::BadValue);
    }
    _aidl_return->keySetId.clear();
    std::vector<Bytes> releasedIds;
    if (!sessions().contains(scope) &&
            sessions().offlineKeyIds(scope, &releasedIds) == Err::Ok) {
        // The scope is a key set ID: this is the response to a RELEASE request.
        return toStatus(sessions().releaseOfflineKeys(scope));
    }
    if (!sessions().contains(scope)) {
        return toStatus(Err::SessionNotOpened);
    }
    std::vector<KeyEntry> keys;
    bool persistent = false;
    Err err = parseLicenseResponse(response, &keys, &persistent);
    if (err != Err::Ok) {
        return toStatus(err);
    }
    err = sessions().addKeys(scope, keys);
    if (err != Err::Ok) {
        return toStatus(err);
    }
    if (persistent) {
        err = sessions().storeOfflineKeys(keys, &_aidl_return->keySetId);
        if (err != Err::Ok) {
            return toStatus(err);
        }
    }

    const auto target = listener();
    if (target != nullptr) {
        target->onEvent(
                ::com::rdk::hal::drm::EventType::VENDOR_DEFINED, scope, scope);
        target->onExpirationUpdate(scope, 100);
        std::vector<KeyStatus> statuses;
        for (const KeyEntry& entry : keys) {
            KeyStatus status;
            status.keyId = entry.kid;
            status.type = KeyStatusType::USABLE;
            statuses.push_back(std::move(status));
        }
        target->onKeysChange(scope, statuses, true);
    }
    return Status::ok();
}

::android::binder::Status ClearKeyDrmPlugin::queryKeyStatus(
        const Bytes& sessionId, std::vector<KeyValue>* _aidl_return) {
    if (sessionId.empty()) {
        return toStatus(Err::BadValue);
    }
    size_t count = 0;
    const Err err = sessions().keyCount(sessionId, &count);
    if (err != Err::Ok) {
        return toStatus(err);
    }
    _aidl_return->clear();
    _aidl_return->push_back(keyValue("LicenseType", "streaming"));
    _aidl_return->push_back(keyValue("KeyCount", std::to_string(count)));
    return Status::ok();
}

::android::binder::Status ClearKeyDrmPlugin::removeKeys(const Bytes& sessionId) {
    if (sessionId.empty()) {
        return toStatus(Err::BadValue);
    }
    return toStatus(sessions().removeKeys(sessionId));
}

::android::binder::Status ClearKeyDrmPlugin::decrypt(const Bytes& sessionId, const Bytes& keyId,
        const Bytes& input, const Bytes& iv, Bytes* _aidl_return) {
    if (sessionId.empty() || keyId.empty()) {
        return toStatus(Err::BadValue);
    }
    Bytes key;
    std::string algorithm;
    Err err = sessions().findKey(sessionId, keyId, &key);
    if (err == Err::Ok) {
        err = sessions().getCipherAlgorithm(sessionId, &algorithm);
    }
    if (err == Err::Ok) {
        err = aesCrypt(false, algorithm, key, iv, input, _aidl_return);
    }
    return toStatus(err);
}

::android::binder::Status ClearKeyDrmPlugin::encrypt(const Bytes& sessionId, const Bytes& keyId,
        const Bytes& input, const Bytes& iv, Bytes* _aidl_return) {
    if (sessionId.empty() || keyId.empty()) {
        return toStatus(Err::BadValue);
    }
    Bytes key;
    std::string algorithm;
    Err err = sessions().findKey(sessionId, keyId, &key);
    if (err == Err::Ok) {
        err = sessions().getCipherAlgorithm(sessionId, &algorithm);
    }
    if (err == Err::Ok) {
        err = aesCrypt(true, algorithm, key, iv, input, _aidl_return);
    }
    return toStatus(err);
}

::android::binder::Status ClearKeyDrmPlugin::sign(const Bytes& sessionId, const Bytes& keyId,
        const Bytes& message, Bytes* _aidl_return) {
    if (sessionId.empty() || keyId.empty()) {
        return toStatus(Err::BadValue);
    }
    Bytes key;
    Err err = sessions().findKey(sessionId, keyId, &key);
    if (err == Err::Ok) {
        err = hmacSha256(key, message, _aidl_return);
    }
    return toStatus(err);
}

::android::binder::Status ClearKeyDrmPlugin::verify(const Bytes& sessionId, const Bytes& keyId,
        const Bytes& message, const Bytes& signature, bool* _aidl_return) {
    if (sessionId.empty() || keyId.empty()) {
        return toStatus(Err::BadValue);
    }
    Bytes key;
    Bytes mac;
    Err err = sessions().findKey(sessionId, keyId, &key);
    if (err == Err::Ok) {
        err = hmacSha256(key, message, &mac);
    }
    if (err == Err::Ok) {
        *_aidl_return = constantTimeEquals(mac, signature);
    }
    return toStatus(err);
}

::android::binder::Status ClearKeyDrmPlugin::signRSA(const Bytes& sessionId, const std::string&,
        const Bytes&, const Bytes&, Bytes*) {
    if (sessionId.empty()) {
        return toStatus(Err::BadValue);
    }
    return toStatus(sessions().contains(sessionId) ? Err::CannotHandle : Err::SessionNotOpened);
}

::android::binder::Status ClearKeyDrmPlugin::setCipherAlgorithm(
        const Bytes& sessionId, const std::string& algorithm) {
    if (sessionId.empty() || algorithm.size() == 0) {
        return toStatus(Err::BadValue);
    }
    if (!sessions().contains(sessionId)) {
        return toStatus(Err::SessionNotOpened);
    }
    const std::string canonical = canonicalCipherAlgorithm(algorithm);
    if (canonical.empty()) {
        return toStatus(Err::BadValue);
    }
    return toStatus(sessions().setCipherAlgorithm(sessionId, canonical));
}

::android::binder::Status ClearKeyDrmPlugin::setMacAlgorithm(
        const Bytes& sessionId, const std::string& algorithm) {
    if (sessionId.empty() || algorithm.size() == 0) {
        return toStatus(Err::BadValue);
    }
    if (!sessions().contains(sessionId)) {
        return toStatus(Err::SessionNotOpened);
    }
    const std::string canonical = canonicalMacAlgorithm(algorithm);
    if (canonical.empty()) {
        return toStatus(Err::BadValue);
    }
    return toStatus(sessions().setMacAlgorithm(sessionId, canonical));
}

::android::binder::Status ClearKeyDrmPlugin::setPlaybackId(
        const Bytes& sessionId, const std::string& playbackId) {
    if (sessionId.empty()) {
        return toStatus(Err::BadValue);
    }
    return toStatus(sessions().setPlaybackId(sessionId, playbackId));
}

::android::binder::Status ClearKeyDrmPlugin::getHdcpLevels(
        ::com::rdk::hal::drm::HdcpLevels* _aidl_return) {
    _aidl_return->connectedLevel = HdcpLevel::HDCP_NONE;
    _aidl_return->maxLevel = HdcpLevel::HDCP_NO_OUTPUT;
    return Status::ok();
}

::android::binder::Status ClearKeyDrmPlugin::getNumberOfSessions(
        ::com::rdk::hal::drm::NumberOfSessions* _aidl_return) {
    _aidl_return->currentSessions = static_cast<int32_t>(sessions().count());
    _aidl_return->maxSessions = static_cast<int32_t>(SessionLibrary::kMaxSessions);
    return Status::ok();
}

::android::binder::Status ClearKeyDrmPlugin::getMetrics(
        std::vector<::com::rdk::hal::drm::DrmMetricGroup>* _aidl_return) {
    _aidl_return->clear();
    return Status::ok();
}

::android::binder::Status ClearKeyDrmPlugin::getPropertyString(
        const std::string& propertyName, std::string* _aidl_return) {
    if (propertyName.size() == 0) {
        return toStatus(Err::BadValue);
    }
    const std::string& name = propertyName;
    if (name == "vendor") {
        *_aidl_return = "RDK";
    } else if (name == "version") {
        *_aidl_return = "1.0";
    } else if (name == "description") {
        *_aidl_return = "RDK reference ClearKey CDM";
    } else if (name == "algorithms") {
        *_aidl_return = "AES/CTR/NoPadding,AES/CBC/NoPadding,HmacSHA256";
    } else {
        return toStatus(Err::CannotHandle);
    }
    return Status::ok();
}

::android::binder::Status ClearKeyDrmPlugin::getPropertyByteArray(
        const std::string& propertyName, Bytes*) {
    if (propertyName.size() == 0) {
        return toStatus(Err::BadValue);
    }
    return toStatus(Err::CannotHandle);
}

::android::binder::Status ClearKeyDrmPlugin::setPropertyString(
        const std::string& propertyName, const std::string& value) {
    if (propertyName.size() == 0) {
        return toStatus(Err::BadValue);
    }
    if (propertyName != "rdkVtsError") {
        return toStatus(Err::CannotHandle);
    }
    if (value != "resourceContention" && value != "invalidState" &&
            value != "notProvisioned" && !value.empty()) {
        return toStatus(Err::BadValue);
    }
    std::lock_guard<std::mutex> lock(mMutex);
    mTestError = value;
    return Status::ok();
}

::android::binder::Status ClearKeyDrmPlugin::setPropertyByteArray(
        const std::string& propertyName, const Bytes&) {
    if (propertyName.size() == 0) {
        return toStatus(Err::BadValue);
    }
    return toStatus(Err::CannotHandle);
}

::android::binder::Status ClearKeyDrmPlugin::getProvisionRequest(
        const std::string&, const std::string&, ::com::rdk::hal::drm::ProvisionRequest*) {
    // ClearKey does not require provisioning.
    return toStatus(Err::CannotHandle);
}

::android::binder::Status ClearKeyDrmPlugin::provideProvisionResponse(
        const Bytes& response, ::com::rdk::hal::drm::ProvideProvisionResponseResult*) {
    return toStatus(response.empty() ? Err::BadValue : Err::CannotHandle);
}

::android::binder::Status ClearKeyDrmPlugin::requiresSecureDecoder(
        const std::string&, SecurityLevel, bool* _aidl_return) {
    *_aidl_return = false;
    return Status::ok();
}

::android::binder::Status ClearKeyDrmPlugin::setListener(
        const ::android::sp<::com::rdk::hal::drm::IDrmPluginListener>& listener) {
    std::lock_guard<std::mutex> lock(mMutex);
    mListener = listener;
    return Status::ok();
}

}  // namespace rdk::clearkey
