// SPDX-License-Identifier: Apache-2.0
// In-process check of the Binder facing classes. The factory and plugins are
// called through their AIDL interfaces without IPC, using a fake AVBuffer helper,
// and assert the status codes the DRM VTS (L1/L2/L3) expects.
#include <IAVBufferHelper.h>

#include <com/rdk/hal/drm/BnDrmPluginListener.h>
#include <com/rdk/hal/drm/DecryptArgs.h>
#include <com/rdk/hal/drm/DrmErrors.h>
#include <com/rdk/hal/drm/KeyStatus.h>
#include <com/rdk/hal/drm/KeyType.h>
#include <com/rdk/hal/drm/ProvisionRequest.h>
#include <com/rdk/hal/drm/SecurityLevel.h>

#include <cstdio>
#include <cstdlib>
#include <map>
#include <utils/Errors.h>

#include "ClearKeyDrmFactory.h"
#include "base64.h"

using android::sp;
using android::binder::Status;
using com::rdk::hal::drm::DrmErrors;
using com::rdk::hal::drm::ICryptoPlugin;
using com::rdk::hal::drm::IDrmPlugin;
using com::rdk::hal::drm::KeyType;
using com::rdk::hal::drm::SecurityLevel;
using com::rdk::hal::drm::Uuid;
using Bytes = std::vector<uint8_t>;

namespace {

int gFailures = 0;

#define CHECK(condition)                                                      \
    do {                                                                      \
        if (!(condition)) {                                                   \
            std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++gFailures;                                                      \
        }                                                                     \
    } while (0)

Bytes hex(const std::string& text) {
    Bytes out;
    for (size_t i = 0; i + 1 < text.size(); i += 2) {
        out.push_back(static_cast<uint8_t>(std::stoi(text.substr(i, 2), nullptr, 16)));
    }
    return out;
}

bool isSpecific(const Status& s, DrmErrors e) {
    return !s.isOk() && s.exceptionCode() == Status::EX_SERVICE_SPECIFIC &&
            s.serviceSpecificErrorCode() == static_cast<int32_t>(e);
}

bool isBadValue(const Status& s) {
    return !s.isOk() && s.exceptionCode() == Status::EX_SERVICE_SPECIFIC &&
            s.serviceSpecificErrorCode() == android::BAD_VALUE;
}

class FakeBufferHelper : public IAVBufferHelper {
public:
    uint64_t allocate(const Bytes& content, size_t capacity) {
        Bytes data(capacity, 0);
        std::copy(content.begin(), content.end(), data.begin());
        mBuffers[++mNext] = std::move(data);
        return mNext;
    }
    const Bytes& contents(uint64_t handle) { return mBuffers[handle]; }
    int mapped = 0;

    void* mapHandle(uint64_t handle, uint32_t* size) override {
        const auto it = mBuffers.find(handle);
        if (it == mBuffers.end()) return nullptr;
        *size = static_cast<uint32_t>(it->second.size());
        ++mapped;
        return it->second.data();
    }
    bool unmapHandle(uint64_t handle) override {
        --mapped;
        return mBuffers.count(handle) != 0;
    }
    uint32_t getAllocationSize(uint64_t handle) override {
        return static_cast<uint32_t>(mBuffers[handle].size());
    }

private:
    std::map<uint64_t, Bytes> mBuffers;
    uint64_t mNext = 0;
};

FakeBufferHelper gHelper;

class Listener : public com::rdk::hal::drm::BnDrmPluginListener {
public:
    Status onEvent(
            com::rdk::hal::drm::EventType event,
            const Bytes& session,
            const Bytes& data) override {
        eventType = event;
        eventSession = session;
        eventData = data;
        return Status::ok();
    }
    Status onExpirationUpdate(const Bytes& session, int64_t expiry) override {
        expirationSession = session;
        expiryTimeInMs = expiry;
        return Status::ok();
    }
    Status onKeysChange(const Bytes&, const std::vector<com::rdk::hal::drm::KeyStatus>& list,
            bool newUsable) override {
        keysChanged = list.size();
        sawNewUsable = newUsable;
        return Status::ok();
    }
    Status onSessionLostState(const Bytes& session) override {
        lostSession = session;
        return Status::ok();
    }

    size_t keysChanged = 0;
    bool sawNewUsable = false;
    com::rdk::hal::drm::EventType eventType =
            com::rdk::hal::drm::EventType::VENDOR_DEFINED;
    Bytes eventSession;
    Bytes eventData;
    Bytes expirationSession;
    int64_t expiryTimeInMs = 0;
    Bytes lostSession;
};

Bytes psshV1(const Uuid& system, const Bytes& kid) {
    Bytes box = {0, 0, 0, 52, 'p', 's', 's', 'h', 1, 0, 0, 0};
    box.insert(box.end(), system.uuid.begin(), system.uuid.end());
    box.insert(box.end(), {0, 0, 0, 1});
    box.insert(box.end(), kid.begin(), kid.end());
    box.insert(box.end(), {0, 0, 0, 0});
    return box;
}

std::string licenseJson(const Bytes& kid, const Bytes& key) {
    return "{\"keys\":[{\"kty\":\"oct\",\"kid\":\"" +
            rdk::clearkey::base64UrlEncode(kid.data(), kid.size()) + "\",\"k\":\"" +
            rdk::clearkey::base64UrlEncode(key.data(), key.size()) + "\"}],\"type\":\"temporary\"}";
}

std::vector<com::rdk::hal::drm::SubSample> subSamples(
        std::initializer_list<std::pair<int32_t, int32_t>> lengths) {
    std::vector<com::rdk::hal::drm::SubSample> out;
    for (const auto& length : lengths) {
        com::rdk::hal::drm::SubSample s;
        s.numBytesOfClearData = length.first;
        s.numBytesOfEncryptedData = length.second;
        out.push_back(s);
    }
    return out;
}

Uuid unsupportedUuid() {
    Uuid uuid;
    uuid.uuid = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80,
            0x90, 0xa0, 0xb0, 0xc0, 0xd0, 0xe0, 0xf0, 0x01};
    return uuid;
}

}  // namespace

IAVBufferHelper* getAVBufferHelperInstance() {
    return &gHelper;
}

int main() {
    const Bytes kid = hex("00112233445566778899aabbccddeeff");
    const Bytes key = hex("000102030405060708090a0b0c0d0e0f");
    const Bytes iv = hex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff");
    const Bytes plain = hex(
            "030a11181f262d343b424950575e656c737a81888f969da4abb2b9c0c7ced5dce3eaf1f8"
            "ff060d141b222930373e454c535a61686f767d848b9299a0a7aeb5bcc3cad1d8dfe6");
    const Bytes sample = hex(
            "030a11181f262d343b422ff790b6513e423216d95191aeb2061f0b4110ced5dce3eaf14f"
            "613aa0b0b651925eab21af9e22c83714349de07f0a19c0ef35fbc4cc1bac8be460be");

    sp<rdk::clearkey::ClearKeyDrmFactory> factory =
            sp<rdk::clearkey::ClearKeyDrmFactory>::make();

    com::rdk::hal::drm::CryptoSchemes schemes;
    CHECK(factory->getSupportedCryptoSchemes(&schemes).isOk());
    CHECK(!schemes.uuids.empty() && !schemes.mimeTypes.empty());
    const Uuid uuid = schemes.uuids.front();

    // L1 negative factory paths.
    sp<IDrmPlugin> drm;
    sp<ICryptoPlugin> crypto;
    CHECK(isSpecific(factory->createDrmPlugin(unsupportedUuid(), "app", &drm),
            DrmErrors::ERROR_DRM_CANNOT_HANDLE) && drm == nullptr);
    CHECK(!factory->createCryptoPlugin(unsupportedUuid(), {}, &crypto).isOk() && crypto == nullptr);
    CHECK(isSpecific(factory->createCryptoPlugin(uuid, Bytes(16, 1), &crypto),
            DrmErrors::ERROR_DRM_SESSION_NOT_OPENED));

    CHECK(factory->createDrmPlugin(uuid, "app", &drm).isOk() && drm != nullptr);
    CHECK(factory->createCryptoPlugin(uuid, {}, &crypto).isOk() && crypto != nullptr);

    // L1 properties, capabilities, session lifecycle.
    std::string property;
    for (const char* name : {"vendor", "version", "description"}) {
        CHECK(drm->getPropertyString(name, &property).isOk() && property.size() > 0);
    }
    CHECK(isBadValue(drm->getPropertyString(std::string(), &property)));
    CHECK(isSpecific(drm->getPropertyString("nope", &property),
            DrmErrors::ERROR_DRM_CANNOT_HANDLE));
    com::rdk::hal::drm::NumberOfSessions sessions;
    CHECK(drm->getNumberOfSessions(&sessions).isOk() && sessions.maxSessions > 0);

    Bytes session;
    CHECK(drm->openSession(SecurityLevel::DEFAULT, &session).isOk() && !session.empty());
    SecurityLevel level = SecurityLevel::UNKNOWN;
    CHECK(drm->getSecurityLevel(session, &level).isOk() && level == SecurityLevel::SW_SECURE_CRYPTO);
    com::rdk::hal::drm::NumberOfSessions during;
    CHECK(drm->getNumberOfSessions(&during).isOk() &&
            during.currentSessions == sessions.currentSessions + 1);

    const Bytes none;
    com::rdk::hal::drm::KeyRequest request;
    CHECK(isBadValue(drm->getSecurityLevel(none, &level)));
    CHECK(isBadValue(drm->closeSession(none)));
    CHECK(isSpecific(crypto->setMediaDrmSession(none), DrmErrors::ERROR_DRM_SESSION_NOT_OPENED));
    CHECK(isBadValue(drm->getKeyRequest(none, {}, "video/mp4",
            KeyType::STREAMING, {}, &request)));

    // L2 key exchange.
    CHECK(isBadValue(drm->getKeyRequest(session, {}, "video/mp4",
            KeyType::STREAMING, {}, &request)));
    CHECK(isSpecific(drm->getKeyRequest(session, Bytes{1, 2, 3}, "video/mp4",
            KeyType::STREAMING, {}, &request), DrmErrors::ERROR_DRM_INIT_DATA));
    CHECK(drm->getKeyRequest(session, psshV1(uuid, kid), "video/mp4",
            KeyType::STREAMING, {}, &request).isOk() && !request.request.empty());
    CHECK(drm->getKeyRequest(session, psshV1(uuid, kid), "video/mp4",
            KeyType::OFFLINE, {}, &request).isOk());
    CHECK(std::string(request.request.begin(), request.request.end()).find(
            "persistent-license") != std::string::npos);
    com::rdk::hal::drm::ProvisionRequest provisionRequest;
    CHECK(isSpecific(drm->getProvisionRequest("X.509", "", &provisionRequest),
            DrmErrors::ERROR_DRM_CANNOT_HANDLE));

    sp<Listener> listener = sp<Listener>::make();
    CHECK(drm->setListener(listener).isOk());
    CHECK(crypto->setMediaDrmSession(session).isOk());

    // Test-only RDK ClearKey error injection and session-lost callback.
    CHECK(drm->setPropertyString("rdkVtsError", "resourceContention").isOk());
    CHECK(isSpecific(drm->getKeyRequest(session, psshV1(uuid, kid), "video/mp4",
            KeyType::STREAMING, {}, &request), DrmErrors::ERROR_DRM_RESOURCE_CONTENTION));
    CHECK(drm->setPropertyString("rdkVtsError", "invalidState").isOk());
    CHECK(isSpecific(drm->getKeyRequest(session, psshV1(uuid, kid), "video/mp4",
            KeyType::STREAMING, {}, &request), DrmErrors::ERROR_DRM_INVALID_STATE));
    CHECK(listener->lostSession == session);
    CHECK(drm->setPropertyString("rdkVtsError", "notProvisioned").isOk());
    CHECK(isSpecific(drm->getKeyRequest(session, psshV1(uuid, kid), "video/mp4",
            KeyType::STREAMING, {}, &request), DrmErrors::ERROR_DRM_NOT_PROVISIONED));

    // No license yet.
    Bytes out;
    CHECK(isSpecific(drm->decrypt(session, kid, Bytes(16), iv, &out),
            DrmErrors::ERROR_DRM_NO_LICENSE));

    const std::string bad = "garbage";
    com::rdk::hal::drm::KeySetId keySetId;
    CHECK(isSpecific(drm->provideKeyResponse(session, Bytes(bad.begin(), bad.end()), &keySetId),
            DrmErrors::ERROR_DRM_LICENSE_PARSE));
    const std::string emptyKey = licenseJson(kid, {});
    CHECK(isSpecific(drm->provideKeyResponse(
            session, Bytes(emptyKey.begin(), emptyKey.end()), &keySetId),
            DrmErrors::ERROR_DRM_LICENSE_PARSE));
    const std::string longKey = licenseJson(kid, Bytes(17, 0x5a));
    CHECK(isSpecific(drm->provideKeyResponse(
            session, Bytes(longKey.begin(), longKey.end()), &keySetId),
            DrmErrors::ERROR_DRM_LICENSE_PARSE));
    const std::string license = licenseJson(kid, key);
    CHECK(drm->provideKeyResponse(session, Bytes(license.begin(), license.end()), &keySetId).isOk());
    CHECK(listener->keysChanged == 1 && listener->sawNewUsable);
    CHECK(listener->eventType == com::rdk::hal::drm::EventType::VENDOR_DEFINED);
    CHECK(listener->eventSession == session && listener->eventData == session);
    CHECK(listener->expirationSession == session && listener->expiryTimeInMs == 100);
    CHECK(keySetId.keySetId.empty());

    // OFFLINE license: persistent response returns a key set ID, RELEASE removes it.
    std::string offline = licenseJson(kid, key);
    offline.replace(offline.find("temporary"), 9, "persistent-license");
    com::rdk::hal::drm::KeySetId offlineSet;
    CHECK(drm->provideKeyResponse(session, Bytes(offline.begin(), offline.end()), &offlineSet).isOk());
    CHECK(!offlineSet.keySetId.empty());
    CHECK(drm->getKeyRequest(offlineSet.keySetId, {}, "video/mp4",
            KeyType::RELEASE, {}, &request).isOk());
    CHECK(request.requestType == com::rdk::hal::drm::KeyRequestType::RELEASE);
    CHECK(isBadValue(drm->getKeyRequest(offlineSet.keySetId, psshV1(uuid, kid), "video/mp4",
            KeyType::RELEASE, {}, &request)));
    const std::string releaseResponse = "{\"kids\":[],\"type\":\"license-release\"}";
    com::rdk::hal::drm::KeySetId releaseSet;
    CHECK(drm->provideKeyResponse(offlineSet.keySetId,
            Bytes(releaseResponse.begin(), releaseResponse.end()), &releaseSet).isOk());
    CHECK(isSpecific(drm->getKeyRequest(offlineSet.keySetId, {}, "video/mp4",
            KeyType::RELEASE, {}, &request), DrmErrors::ERROR_DRM_LICENSE_RELEASE));

    std::vector<com::rdk::hal::drm::KeyValue> keyStatus;
    CHECK(drm->queryKeyStatus(session, &keyStatus).isOk() && !keyStatus.empty());

    // Legacy IDrmPlugin crypto: AES-CTR default, CBC and HMAC on request.
    CHECK(drm->decrypt(session, kid, hex(
            "65add6f02b741c7cac1397576448c8c1c1fb56883808a1090f1fca7ba952ca36319b63ae"
            "835de689e0a3a26978ac103d2382073253c925c3dcff9ab8e8ce8226a8cb73f0f270"),
            iv, &out).isOk() && out == plain);
    CHECK(drm->setCipherAlgorithm(session, "aes/cbc/nopadding").isOk());
    CHECK(drm->encrypt(session, kid, Bytes(plain.begin(), plain.begin() + 48), iv, &out).isOk() &&
            out == hex("29c99ea986fa95edf1f01a6a9ba4b1f9990e43bb4c151b5afb7146d2756cde07"
                       "f0a1d6943fe3ae7c0c1e6b5e9aed7cd0"));
    CHECK(isBadValue(drm->setCipherAlgorithm(session, "AES/ECB/NoPadding")));
    Bytes mac;
    bool verified = false;
    CHECK(drm->sign(session, kid, Bytes{'m', 'e', 's', 's', 'a', 'g', 'e'}, &mac).isOk());
    CHECK(mac == hex("a3e24a124c1900e52c406c42351252b0d07b751bbaa9a44ce6690158ff6f032b"));
    CHECK(drm->verify(session, kid, Bytes{'m', 'e', 's', 's', 'a', 'g', 'e'}, mac, &verified).isOk() &&
            verified);

    // L3 style decrypt through AVBuffer handles (separate and in-place).
    com::rdk::hal::drm::DecryptArgs args;
    args.keyId = kid;
    args.iv = iv;
    args.mode = com::rdk::hal::drm::Mode::AES_CTR;
    args.subSamples = subSamples({{10, 20}, {5, 35}});
    args.sourceBufferHandle = static_cast<int64_t>(gHelper.allocate(sample, 128));
    args.destinationBufferHandle = static_cast<int64_t>(gHelper.allocate({}, 128));
    int32_t decrypted = 0;
    CHECK(crypto->decrypt(args, &decrypted).isOk() && decrypted == 70);
    CHECK(Bytes(gHelper.contents(static_cast<uint64_t>(args.destinationBufferHandle)).begin(),
            gHelper.contents(static_cast<uint64_t>(args.destinationBufferHandle)).begin() + 70) == plain);
    CHECK(gHelper.mapped == 0);

    args.destinationBufferHandle = args.sourceBufferHandle;
    CHECK(crypto->decrypt(args, &decrypted).isOk() && decrypted == 70);
    CHECK(Bytes(gHelper.contents(static_cast<uint64_t>(args.sourceBufferHandle)).begin(),
            gHelper.contents(static_cast<uint64_t>(args.sourceBufferHandle)).begin() + 70) == plain);
    CHECK(gHelper.mapped == 0);

    args.destinationBufferHandle = static_cast<int64_t>(gHelper.allocate({}, 16));
    CHECK(isSpecific(crypto->decrypt(args, &decrypted), DrmErrors::ERROR_DRM_FRAME_TOO_LARGE));
    args.destinationBufferHandle = static_cast<int64_t>(gHelper.allocate({}, 128));
    args.subSamples.clear();
    CHECK(isSpecific(crypto->decrypt(args, &decrypted), DrmErrors::ERROR_DRM_ZERO_SUBSAMPLES));
    args.subSamples = subSamples({{10, 20}, {5, 35}});
    args.secure = true;
    CHECK(isSpecific(crypto->decrypt(args, &decrypted), DrmErrors::ERROR_DRM_CANNOT_HANDLE));
    args.secure = false;
    args.keyId = Bytes(16, 0x77);
    CHECK(isSpecific(crypto->decrypt(args, &decrypted), DrmErrors::ERROR_DRM_NO_LICENSE));
    args.keyId = kid;

    // Key removal then session closure.
    CHECK(drm->removeKeys(session).isOk());
    CHECK(isSpecific(crypto->decrypt(args, &decrypted), DrmErrors::ERROR_DRM_NO_LICENSE));
    CHECK(drm->closeSession(session).isOk());
    CHECK(isSpecific(crypto->decrypt(args, &decrypted), DrmErrors::ERROR_DRM_SESSION_NOT_OPENED));
    CHECK(isSpecific(drm->closeSession(session), DrmErrors::ERROR_DRM_SESSION_NOT_OPENED));
    CHECK(isSpecific(drm->getSecurityLevel(session, &level), DrmErrors::ERROR_DRM_SESSION_NOT_OPENED));
    CHECK(isSpecific(drm->queryKeyStatus(session, &keyStatus), DrmErrors::ERROR_DRM_SESSION_NOT_OPENED));
    CHECK(isSpecific(drm->decrypt(session, kid, Bytes(16), iv, &out),
            DrmErrors::ERROR_DRM_SESSION_NOT_OPENED));
    CHECK(isSpecific(drm->getKeyRequest(session, psshV1(uuid, kid), "video/mp4",
            KeyType::STREAMING, {}, &request), DrmErrors::ERROR_DRM_SESSION_NOT_OPENED));

    if (gFailures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", gFailures);
        return EXIT_FAILURE;
    }
    std::puts("clearkey binder layer: all checks passed");
    return EXIT_SUCCESS;
}
