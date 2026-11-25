/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2025 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * @addtogroup HPK Hardware Porting Kit
 * @{
 */

/**
 * @addtogroup DRM DRM Module
 * @{
 */

/**
 * @addtogroup DRM_HALTEST DRM HAL Tests
 * @{
 */

/**
 * @defgroup DRM_HALTEST_L2_CLEARKEY DRM HAL Tests L2 ClearKey File
 * @{
 * @parblock
 *
 * ### Scheme-independent L2 license/session tests and legacy ClearKey vectors :
 *
 * The common cases exercise license response handling and session/key status
 * for each configured scheme. L2 checks known vectors through both the legacy
 * ClearKey API and the AIDL AVBuffer path; L3 remains a separate sample-level
 * AVBuffer group.
 *
 * **Pre-Conditions:** Enable each scheme's L2 cases and provide scheme-specific fixtures@n
 * **Dependencies:** Selected DRM factory; test licenses or provisioning endpoint, as needed@n
 *
 * @endparblock
 */

/**
 * @file vts_drm_l2_clearkey_tests.cpp
 *
 */

#include "vts_drm.h"
#include "vts_drm_avbuffer.h"
#include "vts_drm_license.h"

#include <com/rdk/hal/drm/DecryptArgs.h>
#include <com/rdk/hal/drm/DrmErrors.h>
#include <com/rdk/hal/drm/BnDrmPluginListener.h>
#include <com/rdk/hal/drm/EventType.h>
#include <com/rdk/hal/drm/IDrmPluginListener.h>
#include <com/rdk/hal/drm/KeyRequest.h>
#include <com/rdk/hal/drm/KeyStatus.h>
#include <com/rdk/hal/drm/KeyStatusType.h>
#include <com/rdk/hal/drm/KeyType.h>
#include <com/rdk/hal/drm/Mode.h>
#include <com/rdk/hal/drm/ProvideProvisionResponseResult.h>
#include <com/rdk/hal/drm/ProvisionRequest.h>
#include <com/rdk/hal/drm/SubSample.h>
#include <utils/Errors.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <climits>
#include <condition_variable>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

using android::binder::Status;
using com::rdk::hal::drm::DecryptArgs;
using com::rdk::hal::drm::DrmErrors;
using com::rdk::hal::drm::EventType;
using com::rdk::hal::drm::IDrmPluginListener;
using com::rdk::hal::drm::KeyStatus;
using com::rdk::hal::drm::KeyStatusType;
using com::rdk::hal::drm::Mode;
using com::rdk::hal::drm::SubSample;
using com::rdk::hal::drm::KeyRequest;
using com::rdk::hal::drm::KeyType;
using vts_drm::SampleBuffers;

namespace {

struct DrmFixture {
    std::vector<uint8_t> initData;
    std::vector<uint8_t> ciphertext;
    std::vector<uint8_t> plaintext;
    std::vector<uint8_t> kid;
    std::vector<uint8_t> key;
    std::vector<uint8_t> iv;
    Mode mode = Mode::AES_CTR;
    int32_t patternEncryptBlocks = 0;
    int32_t patternSkipBlocks = 0;
};

void logSamplePreview(const char* label, const std::vector<uint8_t>& data) {
    constexpr char digits[] = "0123456789abcdef";
    const size_t previewSize = std::min(data.size(), size_t{64});
    std::string hex;
    std::string text;
    for (size_t i = 0; i < previewSize; ++i) {
        const uint8_t byte = data[i];
        hex += digits[byte >> 4];
        hex += digits[byte & 0x0f];
        if (byte == '"' || byte == '\\') {
            text += '\\';
        }
        text += byte >= 0x20 && byte <= 0x7e ? static_cast<char>(byte) : '.';
    }
    UT_LOG_INFO("VTS client %s: %zu bytes, preview %zu bytes%s, hex=%s, ASCII=\"%s\"",
            label, data.size(), previewSize, previewSize < data.size() ? " (truncated)" : "",
            hex.c_str(), text.c_str());
}

bool hasServiceSpecificError(const Status& status, DrmErrors error) {
    return !status.isOk() &&
            status.exceptionCode() == Status::EX_SERVICE_SPECIFIC &&
            status.serviceSpecificErrorCode() == static_cast<int32_t>(error);
}

bool hasServiceSpecificErrorCode(const Status& status, int32_t error) {
    return !status.isOk() &&
            status.exceptionCode() == Status::EX_SERVICE_SPECIFIC &&
            status.serviceSpecificErrorCode() == error;
}

bool parseMode(const char* value, Mode* mode) {
    if (value == nullptr || value[0] == '\0' || std::string(value) == "AES_CTR") {
        *mode = Mode::AES_CTR;
        return true;
    }
    if (std::string(value) == "AES_CBC") {
        *mode = Mode::AES_CBC;
        return true;
    }
    if (std::string(value) == "AES_CBC_CTS") {
        *mode = Mode::AES_CBC_CTS;
        return true;
    }
    UT_LOG_ERROR("DRM_L2_MODE must be AES_CTR, AES_CBC, or AES_CBC_CTS");
    return false;
}

bool parseNonNegativeInt32(const char* variable, int32_t* output) {
    const char* value = vts_drm_config_value(variable);
    if (value == nullptr || value[0] == '\0') {
        UT_LOG_ERROR("%s must be a non-negative integer", variable);
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (errno == ERANGE || end == value || *end != '\0' || parsed < 0 ||
            parsed > INT32_MAX) {
        UT_LOG_ERROR("%s must be a non-negative 32-bit integer", variable);
        return false;
    }
    *output = static_cast<int32_t>(parsed);
    return true;
}

bool readFile(const char* variable, std::vector<uint8_t>* data) {
    const char* path = vts_drm_config_value(variable);
    if (path == nullptr || path[0] == '\0') {
        UT_LOG_ERROR("%s is not set", variable);
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        UT_LOG_ERROR("Unable to read %s: %s", variable, path);
        return false;
    }
    *data = std::vector<uint8_t>(
            std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    if (input.bad() || data->empty()) {
        UT_LOG_ERROR("%s is empty or could not be read completely", variable);
        return false;
    }
    return true;
}

bool decodeHex(const char* variable, std::vector<uint8_t>* data) {
    const char* value = vts_drm_config_value(variable);
    if (value == nullptr || value[0] == '\0' || std::strlen(value) % 2 != 0) {
        UT_LOG_ERROR("%s must contain an even-length hexadecimal value", variable);
        return false;
    }
    data->clear();
    for (size_t i = 0; value[i] != '\0'; i += 2) {
        if (!std::isxdigit(static_cast<unsigned char>(value[i])) ||
                !std::isxdigit(static_cast<unsigned char>(value[i + 1]))) {
            UT_LOG_ERROR("%s contains non-hexadecimal data", variable);
            return false;
        }
        const auto digit = [](char c) -> uint8_t {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return static_cast<uint8_t>(c >= 'a' ? c - 'a' + 10 : c - '0');
        };
        data->push_back(static_cast<uint8_t>((digit(value[i]) << 4) | digit(value[i + 1])));
    }
    return !data->empty();
}

std::string base64Url(const std::vector<uint8_t>& data) {
    static constexpr char alphabet[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string result;
    uint32_t buffer = 0;
    int bits = 0;
    for (uint8_t byte : data) {
        buffer = (buffer << 8) | byte;
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            result.push_back(alphabet[(buffer >> bits) & 0x3f]);
        }
    }
    if (bits != 0) {
        result.push_back(alphabet[(buffer << (6 - bits)) & 0x3f]);
    }
    return result;
}

bool loadFixture(DrmFixture* fixture) {
    return readFile("DRM_L2_INIT_DATA", &fixture->initData);
}

bool loadVector(DrmFixture* fixture) {
    if (!loadFixture(fixture) ||
            !readFile("DRM_L2_CIPHERTEXT", &fixture->ciphertext) ||
            !readFile("DRM_L2_PLAINTEXT", &fixture->plaintext) ||
            !decodeHex("DRM_L2_KID_HEX", &fixture->kid) ||
            !decodeHex("DRM_L2_IV_HEX", &fixture->iv) ||
            !parseMode(vts_drm_config_value("DRM_L2_MODE"), &fixture->mode) ||
            !parseNonNegativeInt32(
                    "DRM_L2_PATTERN_ENCRYPT_BLOCKS", &fixture->patternEncryptBlocks) ||
            !parseNonNegativeInt32(
                    "DRM_L2_PATTERN_SKIP_BLOCKS", &fixture->patternSkipBlocks)) {
        return false;
    }
    if (fixture->kid.size() != 16 || fixture->iv.size() != 16 ||
            fixture->ciphertext.size() != fixture->plaintext.size()) {
        UT_LOG_ERROR("L2 vector requires 16-byte KID/IV and matching ciphertext/plaintext sizes "
                "(kid=%zu, iv=%zu, ciphertext=%zu, plaintext=%zu); regenerate the fixtures "
                "with make_clearkey_fixtures.py instead of editing plain.bin by hand",
                fixture->kid.size(), fixture->iv.size(),
                fixture->ciphertext.size(), fixture->plaintext.size());
        return false;
    }
    UT_LOG_INFO("VTS client fixture: encrypted stream %s clear text (%zu bytes each)",
            fixture->ciphertext == fixture->plaintext ? "is IDENTICAL to" : "differs from",
            fixture->plaintext.size());
    if (fixture->ciphertext.size() >
            static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
        UT_LOG_ERROR("L2 sample exceeds the AVBuffer API size limit");
        return false;
    }
    return true;
}

bool l2VectorConfigured() {
    const char* variables[] = {
        "DRM_L2_CIPHERTEXT", "DRM_L2_PLAINTEXT", "DRM_L2_KID_HEX", "DRM_L2_IV_HEX"};
    size_t configured = 0;
    for (const char* variable : variables) {
        const char* value = vts_drm_config_value(variable);
        configured += value != nullptr && value[0] != '\0';
    }
    return configured != 0;
}

bool loadLicenseResponse(
        const std::vector<uint8_t>& request, std::vector<uint8_t>* response) {
    bool useHttp = false;
    if (!vts_drm_license_uses_http("L2", &useHttp)) {
        return false;
    }
    if (useHttp) {
        return vts_drm_request_http_license("L2", request, response);
    }
    const char* responsePath = vts_drm_config_value("DRM_L2_LICENSE_RESPONSE");
    if (responsePath != nullptr && responsePath[0] != '\0') {
        return readFile("DRM_L2_LICENSE_RESPONSE", response);
    }
    if (vts_drm_active_profile()->scheme != "clearkey") {
        UT_LOG_ERROR("Widevine/PlayReady require a license response file or HTTP license adapter");
        return false;
    }
    DrmFixture fixture;
    if (!decodeHex("DRM_L2_KID_HEX", &fixture.kid) ||
            !decodeHex("DRM_L2_KEY_HEX", &fixture.key)) {
        return false;
    }
    if (fixture.kid.size() != 16 || fixture.key.size() != 16) {
        UT_LOG_ERROR("Generated ClearKey response requires a 16-byte KID and key");
        return false;
    }
    const std::string json = "{\"keys\":[{\"kty\":\"oct\",\"kid\":\"" +
            base64Url(fixture.kid) + "\",\"k\":\"" + base64Url(fixture.key) +
            "\"}],\"type\":\"temporary\"}";
    response->assign(json.begin(), json.end());
    return true;
}

bool loadOfflineLicenseResponse(
        const std::vector<uint8_t>& request, std::vector<uint8_t>* response) {
    bool useHttp = false;
    if (!vts_drm_license_uses_http("L2", &useHttp)) {
        return false;
    }
    if (useHttp) {
        return vts_drm_request_http_license("L2", request, response);
    }
    const char* responsePath =
            vts_drm_config_value("DRM_L2_OFFLINE_LICENSE_RESPONSE");
    if (responsePath != nullptr && responsePath[0] != '\0') {
        return readFile("DRM_L2_OFFLINE_LICENSE_RESPONSE", response);
    }
    if (vts_drm_active_profile()->scheme != "clearkey") {
        UT_LOG_INFO("SKIP: offline license response fixture is not configured");
        return false;
    }
    DrmFixture fixture;
    if (!decodeHex("DRM_L2_KID_HEX", &fixture.kid) ||
            !decodeHex("DRM_L2_KEY_HEX", &fixture.key)) {
        return false;
    }
    if (fixture.kid.size() != 16 || fixture.key.size() != 16) {
        UT_LOG_ERROR("Generated ClearKey offline response requires a 16-byte KID and key");
        return false;
    }
    const std::string json = "{\"keys\":[{\"kty\":\"oct\",\"kid\":\"" +
            base64Url(fixture.kid) + "\",\"k\":\"" + base64Url(fixture.key) +
            "\"}],\"type\":\"persistent-license\"}";
    response->assign(json.begin(), json.end());
    return true;
}

class LicensedSession {
public:
    bool openSession() {
        Uuid uuid;
        if (!vts_drm_getSupportedUuid(&uuid).isOk() ||
                !vts_drm_createPlugins(uuid, &drm, &crypto).isOk()) {
            UT_LOG_ERROR("Unable to create DRM/Crypto plugins");
            return false;
        }
        com::rdk::hal::drm::SecurityLevel level;
        if (!vts_drm_parse_security_level(vts_drm_config_value("DRM_L2_SECURITY_LEVEL"), &level)) {
            return false;
        }
        if (!drm->openSession(level, &session).isOk() || session.empty()) {
            UT_LOG_ERROR("Unable to open DRM session (provision the target before running)");
            return false;
        }
        return true;
    }

    bool open(
            const DrmFixture& fixture,
            const sp<IDrmPluginListener>& listener = nullptr) {
        if (!openSession()) {
            return false;
        }
        if (listener != nullptr && !drm->setListener(listener).isOk()) {
            UT_LOG_ERROR("Unable to register DRM plugin listener");
            return false;
        }
        KeyRequest request;
        if (!drm->getKeyRequest(
                    session,
                    fixture.initData,
                    vts_drm_config_value("DRM_L2_MIME_TYPE"),
                    KeyType::STREAMING,
                    {},
                    &request).isOk() ||
                request.request.empty()) {
            UT_LOG_ERROR("Unable to create a streaming key request");
            return false;
        }
        std::vector<uint8_t> response;
        if (!loadLicenseResponse(request.request, &response)) {
            return false;
        }
        com::rdk::hal::drm::KeySetId keySetId;
        if (!drm->provideKeyResponse(session, response, &keySetId).isOk()) {
            UT_LOG_ERROR("DRM license response was rejected");
            return false;
        }
        if (!crypto->setMediaDrmSession(session).isOk()) {
            UT_LOG_ERROR("Unable to bind DRM session to Crypto");
            return false;
        }
        return true;
    }

    bool close() {
        if (drm != nullptr && !session.empty()) {
            if (!drm->closeSession(session).isOk()) {
                UT_LOG_ERROR("Unable to close DRM session");
                return false;
            }
            session.clear();
        }
        return true;
    }

    ~LicensedSession() {
        close();
    }

    sp<IDrmPlugin> drm;
    sp<ICryptoPlugin> crypto;
    std::vector<uint8_t> session;
};

class DrmTestListener final : public com::rdk::hal::drm::BnDrmPluginListener {
public:
    Status onEvent(
            EventType eventType,
            const std::vector<uint8_t>& sessionId,
            const std::vector<uint8_t>& data) override {
        std::lock_guard<std::mutex> lock(mMutex);
        mEventType = eventType;
        mEventSessionId = sessionId;
        mEventData = data;
        mHasEvent = true;
        mCondition.notify_all();
        return Status::ok();
    }

    Status onExpirationUpdate(
            const std::vector<uint8_t>& sessionId, int64_t expiryTimeInMs) override {
        std::lock_guard<std::mutex> lock(mMutex);
        mExpirationSessionId = sessionId;
        mExpiryTimeInMs = expiryTimeInMs;
        mHasExpirationUpdate = true;
        mCondition.notify_all();
        return Status::ok();
    }

    Status onKeysChange(
            const std::vector<uint8_t>& sessionId,
            const std::vector<KeyStatus>& keyStatusList,
            bool hasNewUsableKey) override {
        std::lock_guard<std::mutex> lock(mMutex);
        mKeysSessionId = sessionId;
        mKeyStatusList = keyStatusList;
        mHasNewUsableKey = hasNewUsableKey;
        mHasKeysChange = true;
        mCondition.notify_all();
        return Status::ok();
    }

    Status onSessionLostState(const std::vector<uint8_t>& sessionId) override {
        std::lock_guard<std::mutex> lock(mMutex);
        mLostSessionId = sessionId;
        mHasSessionLost = true;
        mCondition.notify_all();
        return Status::ok();
    }

    bool waitForLicenseCallbacks() {
        std::unique_lock<std::mutex> lock(mMutex);
        return mCondition.wait_for(lock, std::chrono::seconds(5), [this] {
            return mHasEvent && mHasExpirationUpdate && mHasKeysChange;
        });
    }

    bool licenseCallbacksMatch(
            const std::vector<uint8_t>& sessionId, const std::vector<uint8_t>& keyId) {
        std::lock_guard<std::mutex> lock(mMutex);
        return mEventType == EventType::VENDOR_DEFINED &&
                mEventSessionId == sessionId && mEventData == sessionId &&
                mExpirationSessionId == sessionId && mExpiryTimeInMs == 100 &&
                mKeysSessionId == sessionId && mHasNewUsableKey &&
                std::any_of(mKeyStatusList.begin(), mKeyStatusList.end(),
                        [&keyId](const KeyStatus& status) {
                            return status.keyId == keyId &&
                                    status.type == KeyStatusType::USABLE;
                        });
    }

    bool waitForSessionLost(const std::vector<uint8_t>& sessionId) {
        std::unique_lock<std::mutex> lock(mMutex);
        return mCondition.wait_for(lock, std::chrono::seconds(5), [this, &sessionId] {
            return mHasSessionLost && mLostSessionId == sessionId;
        });
    }

private:
    std::mutex mMutex;
    std::condition_variable mCondition;
    EventType mEventType = EventType::VENDOR_DEFINED;
    std::vector<uint8_t> mEventSessionId;
    std::vector<uint8_t> mEventData;
    std::vector<uint8_t> mExpirationSessionId;
    int64_t mExpiryTimeInMs = 0;
    std::vector<uint8_t> mKeysSessionId;
    std::vector<KeyStatus> mKeyStatusList;
    std::vector<uint8_t> mLostSessionId;
    bool mHasNewUsableKey = false;
    bool mHasEvent = false;
    bool mHasExpirationUpdate = false;
    bool mHasKeysChange = false;
    bool mHasSessionLost = false;
};

}  // namespace

class L2Cases : public DrmSchemeTest {
public:
    explicit L2Cases(const char* scheme) : DrmSchemeTest(scheme, "L2") {}
    void fixtureValidation_pos();
    void keyRequestAndResponse_pos();
    void offlineKeySetCreated_pos();
    void provisioningStatusAndExchange_pos();
    void listenerCallbacks_pos();
    void getKeyRequestResourceContention_neg();
    void getKeyRequestInvalidStateAndSessionLost_neg();
    void malformedClearKeyEmptyKeyRejected_neg();
    void malformedClearKeyLongKeyRejected_neg();
    void badLevelNotSupported_neg();
    void decryptClearSegmentWithAvBuffer_pos();
    void decryptKnownSample_pos();
    void decryptKnownSampleWithAvBuffer_pos();
    void decryptSubsamplesAndPattern_pos();
    void decryptBoundsAndInvalidInputsRejected_neg();
    void decryptWithoutKeysRejected_neg();
    void removedKeysAndClosedSessionRejectAvBufferDecrypt_neg();
    void removeKeysRejectsDecrypt_neg();
    void emptyLicenseResponseRejected_neg();
    void invalidInitDataRejected_neg();
    void closedSessionRejectsOperations_neg();
};

/**
 * @brief Validates that the selected scheme's init-data fixture is readable and non-empty.
 *
 * Disabled and unsupported optional profiles are reported as skipped by SetUp.
 *
 * **Test Group ID:** L2: 02@n
 * **Test Case ID:** 001@n
 * **Priority:** High@n
 * **Pre-Conditions:** Enable the scheme's L2 cases and provide init data@n
 * **Dependencies:** Scheme-specific init-data file@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Load the configured init-data fixture|YAML or scoped environment override|Fixture is readable and non-empty|Should Pass|
 */
void L2Cases::fixtureValidation_pos() {
    DrmFixture fixture;
    UT_ASSERT_TRUE_FATAL(loadFixture(&fixture));
}

/**
 * @brief Exercises the selected scheme's request/response processing and key status.
 *
 * **Test Group ID:** L2: 02@n
 * **Test Case ID:** 002@n
 * **Priority:** High@n
 * **Pre-Conditions:** Enable L2 and provide valid scheme-specific license inputs@n
 * **Dependencies:** Selected DRM factory and an accepted license response@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Open a DRM session and request a streaming key|Configured init data, MIME and security level|Request succeeds and request bytes are non-empty|Should Pass|
 * |02|Acquire and provide the license response|HTTP adapter or local response file; optional ClearKey JSON generation|Response and crypto session binding succeed|Disposable test licenses|
 * |03|Query the session key status|Session ID|Call succeeds and returns at least one status value|Should Pass|
 */
void L2Cases::keyRequestAndResponse_pos() {
    DrmFixture fixture;
    UT_ASSERT_TRUE_FATAL(loadFixture(&fixture));
    LicensedSession licensed;
    UT_ASSERT_TRUE_FATAL(licensed.open(fixture));

    std::vector<com::rdk::hal::drm::KeyValue> status;
    DRM_PLUGIN_QUERY_KEY_STATUS_SUCCESS(licensed.drm, licensed.session, status);
    UT_ASSERT_FALSE(status.empty());
    UT_ASSERT_TRUE(licensed.close());
}

/**
 * @brief Requests an offline key and checks the returned key-set identifier.
 *
 * **Test Group ID:** L2: 02@n
 * **Test Case ID:** 014@n
 * **Priority:** Medium@n
 * **Pre-Conditions:** L2 enabled with an offline-capable license fixture or HTTP adapter@n
 * **Dependencies:** DRM plugin and scheme-specific offline license support@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Request and provide an OFFLINE license|Configured init data and offline license response|AIDL succeeds and returns a non-empty key-set ID|No playback|
 */
void L2Cases::offlineKeySetCreated_pos() {
    DrmFixture fixture;
    UT_ASSERT_TRUE_FATAL(loadFixture(&fixture));
    LicensedSession licensed;
    UT_ASSERT_TRUE_FATAL(licensed.openSession());

    KeyRequest request;
    const Status requestStatus = licensed.drm->getKeyRequest(
            licensed.session,
            fixture.initData,
            vts_drm_config_value("DRM_L2_MIME_TYPE"),
            KeyType::OFFLINE,
            {},
            &request);
    if (hasServiceSpecificError(
                requestStatus, DrmErrors::ERROR_DRM_CANNOT_HANDLE)) {
        UT_LOG_INFO("SKIP: configured DRM scheme does not support offline key requests");
        UT_IGNORE_TEST();
    }
    DRM_STATUS_SUCCESS(requestStatus);
    UT_ASSERT_TRUE_FATAL(!request.request.empty());

    bool useHttp = false;
    UT_ASSERT_TRUE_FATAL(vts_drm_license_uses_http("L2", &useHttp));
    const char* offlineResponsePath =
            vts_drm_config_value("DRM_L2_OFFLINE_LICENSE_RESPONSE");
    const char* configuredKey = vts_drm_config_value("DRM_L2_KEY_HEX");
    if (!useHttp &&
            (offlineResponsePath == nullptr || offlineResponsePath[0] == '\0') &&
            (vts_drm_active_profile()->scheme != "clearkey" ||
             configuredKey == nullptr || configuredKey[0] == '\0')) {
        UT_LOG_INFO("SKIP: no local OFFLINE license response or generated ClearKey key configured");
        UT_IGNORE_TEST();
    }

    std::vector<uint8_t> response;
    if (!loadOfflineLicenseResponse(request.request, &response)) {
        UT_FAIL_FATAL("Unable to load an OFFLINE license response");
    }
    com::rdk::hal::drm::KeySetId keySetId;
    const Status responseStatus = licensed.drm->provideKeyResponse(
            licensed.session, response, &keySetId);
    if (hasServiceSpecificError(
                responseStatus, DrmErrors::ERROR_DRM_CANNOT_HANDLE)) {
        UT_LOG_INFO("SKIP: configured DRM scheme does not support offline license responses");
        UT_IGNORE_TEST();
    }
    DRM_STATUS_SUCCESS(responseStatus);
    UT_ASSERT_TRUE_FATAL(!keySetId.keySetId.empty());
    UT_ASSERT_TRUE(licensed.close());
}

/**
 * @brief Verifies ClearKey event, expiration, and key-change callback payloads.
 *
 * **Test Group ID:** L2: 02@n
 * **Test Case ID:** 015@n
 * **Priority:** Medium@n
 * **Pre-Conditions:** ClearKey L2 enabled with accepted license inputs@n
 * **Dependencies:** ClearKey DRM plugin listener@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Register listener before providing the license|Session ID|Listener registration succeeds|ClearKey-specific callback contract|
 * |02|Wait for key response callbacks|License response|Vendor event, expiration and key-change callbacks arrive|Should Pass|
 * |03|Validate callback session, event data, status and expiry|Configured KID|Payloads match the callback contract|Should Pass|
 */
void L2Cases::listenerCallbacks_pos() {
    if (vts_drm_active_profile()->scheme != "clearkey") {
        UT_IGNORE_TEST();
    }
    DrmFixture fixture;
    UT_ASSERT_TRUE_FATAL(loadFixture(&fixture));
    sp<DrmTestListener> listener = sp<DrmTestListener>::make();
    LicensedSession licensed;
    UT_ASSERT_TRUE_FATAL(licensed.open(fixture, listener));
    UT_ASSERT_TRUE_FATAL(listener->waitForLicenseCallbacks());
    UT_ASSERT_TRUE(listener->licenseCallbacksMatch(licensed.session, fixture.kid));
    UT_ASSERT_TRUE(licensed.close());
}

/**
 * @brief Checks resource-contention errors from the ClearKey test hook.
 *
 * **Test Group ID:** L2: 02@n
 * **Test Case ID:** 016@n
 * **Priority:** Medium@n
 * **Pre-Conditions:** ClearKey L2 enabled with valid init data@n
 * **Dependencies:** RDK ClearKey VTS error-injection property@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Set one-shot resource-contention injection|rdkVtsError=resourceContention|Property is accepted|Reference ClearKey test hook|
 * |02|Request a streaming key|Open session and configured init data|Returns ERROR_DRM_RESOURCE_CONTENTION|Documented error|
 */
void L2Cases::getKeyRequestResourceContention_neg() {
    if (vts_drm_active_profile()->scheme != "clearkey") {
        UT_IGNORE_TEST();
    }
    DrmFixture fixture;
    UT_ASSERT_TRUE_FATAL(loadFixture(&fixture));
    LicensedSession licensed;
    UT_ASSERT_TRUE_FATAL(licensed.openSession());

    const Status hookStatus =
            licensed.drm->setPropertyString("rdkVtsError", "resourceContention");
    if (hasServiceSpecificError(hookStatus, DrmErrors::ERROR_DRM_CANNOT_HANDLE)) {
        UT_LOG_INFO("SKIP: ClearKey implementation does not expose the RDK VTS error hook");
        UT_IGNORE_TEST();
    }
    DRM_STATUS_SUCCESS(hookStatus);

    KeyRequest request;
    const Status status = licensed.drm->getKeyRequest(
            licensed.session,
            fixture.initData,
            vts_drm_config_value("DRM_L2_MIME_TYPE"),
            KeyType::STREAMING,
            {},
            &request);
    UT_ASSERT_TRUE_FATAL(hasServiceSpecificError(
            status, DrmErrors::ERROR_DRM_RESOURCE_CONTENTION));
    UT_ASSERT_TRUE(licensed.close());
}

/**
 * @brief Checks invalid-state reporting and the matching session-lost callback.
 *
 * **Test Group ID:** L2: 02@n
 * **Test Case ID:** 017@n
 * **Priority:** Medium@n
 * **Pre-Conditions:** ClearKey L2 enabled with valid init data@n
 * **Dependencies:** RDK ClearKey VTS error-injection property and listener@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Register listener and set invalid-state injection|rdkVtsError=invalidState|Both calls succeed|Reference ClearKey test hook|
 * |02|Request a streaming key|Open session and configured init data|Returns ERROR_DRM_INVALID_STATE|Documented error|
 * |03|Wait for session-lost notification|Same session ID|onSessionLostState reports that session|Should Pass|
 */
void L2Cases::getKeyRequestInvalidStateAndSessionLost_neg() {
    if (vts_drm_active_profile()->scheme != "clearkey") {
        UT_IGNORE_TEST();
    }
    DrmFixture fixture;
    UT_ASSERT_TRUE_FATAL(loadFixture(&fixture));
    LicensedSession licensed;
    UT_ASSERT_TRUE_FATAL(licensed.openSession());
    sp<DrmTestListener> listener = sp<DrmTestListener>::make();
    DRM_PLUGIN_SET_LISTENER_SUCCESS(licensed.drm, listener);

    const Status hookStatus =
            licensed.drm->setPropertyString("rdkVtsError", "invalidState");
    if (hasServiceSpecificError(hookStatus, DrmErrors::ERROR_DRM_CANNOT_HANDLE)) {
        UT_LOG_INFO("SKIP: ClearKey implementation does not expose the RDK VTS error hook");
        UT_IGNORE_TEST();
    }
    DRM_STATUS_SUCCESS(hookStatus);

    KeyRequest request;
    const Status status = licensed.drm->getKeyRequest(
            licensed.session,
            fixture.initData,
            vts_drm_config_value("DRM_L2_MIME_TYPE"),
            KeyType::STREAMING,
            {},
            &request);
    UT_ASSERT_TRUE_FATAL(hasServiceSpecificError(
            status, DrmErrors::ERROR_DRM_INVALID_STATE));
    UT_ASSERT_TRUE_FATAL(listener->waitForSessionLost(licensed.session));
    UT_ASSERT_TRUE(licensed.close());
}

namespace {

void rejectMalformedClearKeyKey(
        const std::vector<uint8_t>& keyBytes) {
    DrmFixture fixture;
    UT_ASSERT_TRUE_FATAL(loadFixture(&fixture));
    LicensedSession licensed;
    UT_ASSERT_TRUE_FATAL(licensed.openSession());
    KeyRequest request;
    DRM_PLUGIN_GET_KEY_REQUEST_SUCCESS(
            licensed.drm,
            licensed.session,
            fixture.initData,
            vts_drm_config_value("DRM_L2_MIME_TYPE"),
            KeyType::STREAMING,
            {},
            request);
    const std::string responseText =
            "{\"keys\":[{\"kty\":\"oct\",\"kid\":\"" + base64Url(fixture.kid) +
            "\",\"k\":\"" + base64Url(keyBytes) +
            "\"}],\"type\":\"temporary\"}";
    const std::vector<uint8_t> response(responseText.begin(), responseText.end());
    com::rdk::hal::drm::KeySetId keySetId;
    const Status status = licensed.drm->provideKeyResponse(
            licensed.session, response, &keySetId);
    UT_ASSERT_TRUE_FATAL(hasServiceSpecificError(
            status, DrmErrors::ERROR_DRM_LICENSE_PARSE));
    UT_ASSERT_TRUE(licensed.close());
}

}  // namespace

/**
 * @brief Rejects a ClearKey response containing an empty content key.
 *
 * **Test Group ID:** L2: 02@n
 * **Test Case ID:** 018@n
 * **Priority:** Medium@n
 * **Pre-Conditions:** ClearKey L2 enabled with valid init data@n
 * **Dependencies:** ClearKey license parser@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Provide a license with an empty JWK key|Valid 16-byte KID and empty key|Returns ERROR_DRM_LICENSE_PARSE|Rejected before decrypt|
 */
void L2Cases::malformedClearKeyEmptyKeyRejected_neg() {
    rejectMalformedClearKeyKey({});
}

/**
 * @brief Rejects a ClearKey response containing a key longer than AES-128.
 *
 * **Test Group ID:** L2: 02@n
 * **Test Case ID:** 019@n
 * **Priority:** Medium@n
 * **Pre-Conditions:** ClearKey L2 enabled with valid init data@n
 * **Dependencies:** ClearKey license parser@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Provide a license with a 17-byte JWK key|Valid 16-byte KID and 17-byte key|Returns ERROR_DRM_LICENSE_PARSE|Rejected before decrypt|
 */
void L2Cases::malformedClearKeyLongKeyRejected_neg() {
    rejectMalformedClearKeyKey(std::vector<uint8_t>(17, 0x5a));
}

/**
 * @brief Confirms ClearKey does not advertise hardware-secure content support.
 *
 * **Test Group ID:** L2: 02@n
 * **Test Case ID:** 020@n
 * **Priority:** Medium@n
 * **Pre-Conditions:** ClearKey L2 enabled@n
 * **Dependencies:** DRM factory capability query@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Query ClearKey support at HW_SECURE_ALL|video/mp4|Combination is not supported|ClearKey-specific|
 */
void L2Cases::badLevelNotSupported_neg() {
    if (vts_drm_active_profile()->scheme != "clearkey") {
        UT_IGNORE_TEST();
    }
    bool supported = true;
    UT_ASSERT_TRUE(vts_drm_content_supported(
            "video/mp4", com::rdk::hal::drm::SecurityLevel::HW_SECURE_ALL, &supported));
    UT_ASSERT_FALSE(supported);
}

/**
 * @brief Decrypts a fully clear segment through the AIDL AVBuffer path.
 *
 * **Test Group ID:** L2: 02@n
 * **Test Case ID:** 012@n
 * **Priority:** High@n
 * **Pre-Conditions:** L2 enabled and AVBuffer services available@n
 * **Dependencies:** DRM and Crypto plugins@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Submit a clear-only sample using UNENCRYPTED mode|Caller-owned source/destination AVBuffers|AIDL succeeds and reports the full sample size|No license or decoder|
 * |02|Compare destination bytes and clean up handles|Deterministic clear bytes|Output matches input; caller frees both handles|Should Pass|
 */
void L2Cases::decryptClearSegmentWithAvBuffer_pos() {
    Uuid uuid;
    UT_ASSERT_TRUE_FATAL(vts_drm_getSupportedUuid(&uuid).isOk());
    sp<IDrmPlugin> drm;
    sp<ICryptoPlugin> crypto;
    UT_ASSERT_TRUE_FATAL(vts_drm_createPlugins(uuid, &drm, &crypto).isOk());

    com::rdk::hal::drm::SecurityLevel level;
    UT_ASSERT_TRUE_FATAL(vts_drm_parse_security_level(
            vts_drm_config_value("DRM_L2_SECURITY_LEVEL"), &level));
    std::vector<uint8_t> session;
    DRM_PLUGIN_OPEN_SESSION_SUCCESS(drm, level, session);
    DRM_CRYPTO_PLUGIN_SET_MEDIA_DRM_SESSION_SUCCESS(crypto, session);

    std::vector<uint8_t> clearData(1024);
    for (size_t i = 0; i < clearData.size(); ++i) {
        clearData[i] = static_cast<uint8_t>((i * 37U + 11U) & 0xffU);
    }
    SampleBuffers buffers;
    UT_ASSERT_TRUE_FATAL(buffers.open(clearData.size()));
    UT_ASSERT_TRUE_FATAL(buffers.writeSource(clearData));

    SubSample clearSegment;
    clearSegment.numBytesOfClearData = static_cast<int32_t>(clearData.size());
    clearSegment.numBytesOfEncryptedData = 0;
    const std::vector<uint8_t> keyId(16, 0);
    const std::vector<uint8_t> iv(16, 0);
    const DecryptArgs args = buffers.decryptArgs(
            keyId, iv, Mode::UNENCRYPTED, {clearSegment});
    int32_t decryptedBytes = 0;
    logSamplePreview("clear segment before ICryptoPlugin::decrypt", clearData);
    DRM_CRYPTO_PLUGIN_DECRYPT_SUCCESS(crypto, args, decryptedBytes);
    UT_ASSERT_EQUAL_FATAL(decryptedBytes, static_cast<int32_t>(clearData.size()));

    std::vector<uint8_t> decrypted;
    UT_ASSERT_TRUE_FATAL(buffers.readDestination(&decrypted, clearData.size()));
    logSamplePreview("clear segment after ICryptoPlugin::decrypt", decrypted);
    UT_ASSERT_TRUE_FATAL(decrypted == clearData);
    UT_ASSERT_TRUE_FATAL(buffers.cleanup());
    DRM_PLUGIN_CLOSE_SESSION_SUCCESS(drm, session);
}

/**
 * @brief Decrypts a known ClearKey sample and compares it with expected plaintext.
 *
 * **Test Group ID:** L2: 02@n
 * **Test Case ID:** 003@n
 * **Priority:** High@n
 * **Pre-Conditions:** Enable L2 through YAML or environment and provide a valid ClearKey fixture@n
 * **Dependencies:** ClearKey key request/response succeeds@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Acquire the test key and bind the DRM session to crypto|Init data and ClearKey response|Session setup succeeds|Should Pass|
 * |02|Decrypt the sample using the legacy DRM vector API|KID, ciphertext, IV|Decrypt call succeeds|Not the ICryptoPlugin AVBuffer path|
 * |03|Compare decrypted output with the known plaintext|Decrypted bytes, plaintext file|Byte arrays match exactly|Should Pass|
 */
void L2Cases::decryptKnownSample_pos() {
    if (vts_drm_active_profile()->scheme != "clearkey") {
        UT_LOG_INFO("SKIP: legacy DRM vector decryption is ClearKey-specific; use L3 AVBuffer tests");
        UT_IGNORE_TEST();
    }
    DrmFixture fixture;
    UT_ASSERT_TRUE_FATAL(loadVector(&fixture));
    LicensedSession licensed;
    UT_ASSERT_TRUE_FATAL(licensed.open(fixture));

    logSamplePreview("expected plaintext fixture", fixture.plaintext);
    logSamplePreview("ciphertext before IDrmPlugin::decrypt", fixture.ciphertext);
    std::vector<uint8_t> decrypted;
    DRM_PLUGIN_DECRYPT_SUCCESS(
            licensed.drm, licensed.session, fixture.kid, fixture.ciphertext, fixture.iv, decrypted);
    logSamplePreview("returned plaintext after IDrmPlugin::decrypt", decrypted);
    UT_ASSERT_TRUE(decrypted == fixture.plaintext);
    UT_ASSERT_TRUE(licensed.close());
}

/**
 * @brief Decrypts the configured known vector through the AIDL AVBuffer path.
 *
 * **Test Group ID:** L2: 02@n
 * **Test Case ID:** 008@n
 * **Priority:** High@n
 * **Pre-Conditions:** L2 enabled with a known vector and accepted license@n
 * **Dependencies:** DRM, AVBuffer, and AVBufferHelper services@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Decrypt using the configured AIDL mode and pattern|KID, IV, ciphertext, AVBuffer handles|AIDL succeeds and reports the full sample size|No decoder|
 * |02|Compare output and retain caller ownership of buffers|Plaintext fixture and source AVBuffer|Output matches; source remains readable and caller frees handles|Should Pass|
 */
void L2Cases::decryptKnownSampleWithAvBuffer_pos() {
    if (!l2VectorConfigured()) {
        UT_LOG_INFO("SKIP: L2 AVBuffer vector fixtures are not configured");
        UT_IGNORE_TEST();
    }
    DrmFixture fixture;
    UT_ASSERT_TRUE_FATAL(loadVector(&fixture));
    LicensedSession licensed;
    UT_ASSERT_TRUE_FATAL(licensed.open(fixture));
    SampleBuffers buffers;
    UT_ASSERT_TRUE_FATAL(buffers.open(fixture.ciphertext.size()));
    UT_ASSERT_TRUE_FATAL(buffers.writeSource(fixture.ciphertext));

    SubSample sample;
    sample.numBytesOfClearData = 0;
    sample.numBytesOfEncryptedData = static_cast<int32_t>(fixture.ciphertext.size());
    const DecryptArgs args = buffers.decryptArgs(
            fixture.kid, fixture.iv, fixture.mode, {sample},
            fixture.patternEncryptBlocks, fixture.patternSkipBlocks);
    logSamplePreview("expected plaintext fixture", fixture.plaintext);
    logSamplePreview("ciphertext before ICryptoPlugin::decrypt", fixture.ciphertext);
    int32_t decryptedBytes = 0;
    Status status = licensed.crypto->decrypt(args, &decryptedBytes);
    if (hasServiceSpecificError(
                status, DrmErrors::ERROR_DRM_INSUFFICIENT_OUTPUT_PROTECTION)) {
        UT_LOG_INFO("SKIP: configured license requires protected output buffers");
        UT_IGNORE_TEST();
    }
    DRM_STATUS_SUCCESS(status);
    UT_ASSERT_EQUAL(decryptedBytes, static_cast<int32_t>(fixture.plaintext.size()));

    std::vector<uint8_t> decrypted;
    UT_ASSERT_TRUE_FATAL(buffers.readDestination(&decrypted, fixture.plaintext.size()));
    logSamplePreview("returned plaintext after ICryptoPlugin::decrypt", decrypted);
    UT_ASSERT_TRUE_FATAL(decrypted == fixture.plaintext);
    std::vector<uint8_t> retainedSource;
    UT_ASSERT_TRUE_FATAL(buffers.readSource(&retainedSource, fixture.ciphertext.size()));
    UT_ASSERT_TRUE(retainedSource == fixture.ciphertext);
    UT_ASSERT_TRUE_FATAL(buffers.cleanup());
    UT_ASSERT_TRUE(licensed.close());
}

/**
 * @brief Checks AIDL decrypt handling of multiple subsamples and clear ranges.
 *
 * Pattern values are taken from the L2 vector configuration. Mixed clear and
 * encrypted subsamples use a CTR fixture with the default no-pattern setting.
 *
 * **Test Group ID:** L2: 02@n
 * **Test Case ID:** 009@n
 * **Priority:** High@n
 * **Pre-Conditions:** L2 enabled with a known vector and accepted license@n
 * **Dependencies:** DRM, AVBuffer, and AVBufferHelper services@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Decrypt the vector as adjacent encrypted subsamples|AES-CTR vector split into two entries|Full output matches the plaintext fixture|Should Pass|
 * |02|Decrypt with a clear trailing subsample|Plaintext tail and encrypted prefix|Clear bytes are copied and encrypted bytes are decrypted|No decoder|
 * |03|Apply the configured pattern when present|Patterned known vector|AIDL output matches the plaintext fixture|Pattern is fixture-specific|
 */
void L2Cases::decryptSubsamplesAndPattern_pos() {
    if (!l2VectorConfigured()) {
        UT_LOG_INFO("SKIP: L2 AVBuffer vector fixtures are not configured");
        UT_IGNORE_TEST();
    }
    DrmFixture fixture;
    UT_ASSERT_TRUE_FATAL(loadVector(&fixture));
    LicensedSession licensed;
    UT_ASSERT_TRUE_FATAL(licensed.open(fixture));
    SampleBuffers buffers;
    UT_ASSERT_TRUE_FATAL(buffers.open(fixture.ciphertext.size()));

    std::vector<uint8_t> source = fixture.ciphertext;
    std::vector<SubSample> subSamples;
    if (fixture.mode == Mode::AES_CTR && fixture.patternEncryptBlocks == 0 &&
            fixture.patternSkipBlocks == 0 && source.size() > 1) {
        const size_t split = source.size() / 2;
        SubSample first;
        first.numBytesOfClearData = 0;
        first.numBytesOfEncryptedData = static_cast<int32_t>(split);
        SubSample second;
        second.numBytesOfClearData = 0;
        second.numBytesOfEncryptedData = static_cast<int32_t>(source.size() - split);
        subSamples = {first, second};
    } else {
        SubSample sample;
        sample.numBytesOfClearData = 0;
        sample.numBytesOfEncryptedData = static_cast<int32_t>(source.size());
        subSamples = {sample};
    }

    UT_ASSERT_TRUE_FATAL(buffers.writeSource(source));
    logSamplePreview("expected plaintext fixture", fixture.plaintext);
    logSamplePreview("source before ICryptoPlugin::decrypt", source);
    DecryptArgs args = buffers.decryptArgs(
            fixture.kid, fixture.iv, fixture.mode, subSamples,
            fixture.patternEncryptBlocks, fixture.patternSkipBlocks);
    int32_t decryptedBytes = 0;
    Status status = licensed.crypto->decrypt(args, &decryptedBytes);
    if (hasServiceSpecificError(
                status, DrmErrors::ERROR_DRM_INSUFFICIENT_OUTPUT_PROTECTION)) {
        UT_LOG_INFO("SKIP: configured license requires protected output buffers");
        UT_IGNORE_TEST();
    }
    DRM_STATUS_SUCCESS(status);
    UT_ASSERT_EQUAL_FATAL(decryptedBytes, static_cast<int32_t>(fixture.plaintext.size()));
    std::vector<uint8_t> decrypted;
    UT_ASSERT_TRUE_FATAL(buffers.readDestination(&decrypted, fixture.plaintext.size()));
    logSamplePreview("returned plaintext after ICryptoPlugin::decrypt", decrypted);
    UT_ASSERT_TRUE_FATAL(decrypted == fixture.plaintext);

    if (fixture.mode == Mode::AES_CTR && fixture.patternEncryptBlocks == 0 &&
            fixture.patternSkipBlocks == 0 && source.size() > 1) {
        const size_t clearSize = std::max<size_t>(1, source.size() / 4);
        const size_t encryptedSize = source.size() - clearSize;
        std::copy(
                fixture.plaintext.begin() + encryptedSize,
                fixture.plaintext.end(),
                source.begin() + encryptedSize);
        SubSample encrypted;
        encrypted.numBytesOfClearData = 0;
        encrypted.numBytesOfEncryptedData = static_cast<int32_t>(encryptedSize);
        SubSample clear;
        clear.numBytesOfClearData = static_cast<int32_t>(clearSize);
        clear.numBytesOfEncryptedData = 0;
        UT_ASSERT_TRUE_FATAL(buffers.writeSource(source));
        logSamplePreview("mixed clear/encrypted source before ICryptoPlugin::decrypt", source);
        args = buffers.decryptArgs(
                fixture.kid, fixture.iv, fixture.mode, {encrypted, clear});
        status = licensed.crypto->decrypt(args, &decryptedBytes);
        if (hasServiceSpecificError(
                    status, DrmErrors::ERROR_DRM_INSUFFICIENT_OUTPUT_PROTECTION)) {
            UT_LOG_INFO("SKIP: configured license requires protected output buffers");
            UT_IGNORE_TEST();
        }
        DRM_STATUS_SUCCESS(status);
        UT_ASSERT_EQUAL_FATAL(decryptedBytes, static_cast<int32_t>(fixture.plaintext.size()));
        UT_ASSERT_TRUE_FATAL(buffers.readDestination(&decrypted, fixture.plaintext.size()));
        logSamplePreview("returned plaintext after mixed-segment decrypt", decrypted);
        UT_ASSERT_TRUE(decrypted == fixture.plaintext);
    }

    UT_ASSERT_TRUE_FATAL(buffers.cleanup());
    UT_ASSERT_TRUE(licensed.close());
}

/**
 * @brief Checks documented AIDL errors for buffer bounds and invalid inputs.
 *
 * **Test Group ID:** L2: 02@n
 * **Test Case ID:** 010@n
 * **Priority:** High@n
 * **Pre-Conditions:** L2 enabled with a known vector and accepted license@n
 * **Dependencies:** DRM, AVBuffer, and AVBufferHelper services@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Decrypt into an undersized destination buffer|Valid sample and destination one byte too small|Returns ERROR_DRM_FRAME_TOO_LARGE|Documented error|
 * |02|Describe more source bytes than the AVBuffer contains|Full-sample subsample and short source buffer|Returns ERROR_DRM_DECRYPT|Documented error|
 * |03|Retry with no subsamples or an invalid handle|Empty subsample array or unallocated handle|Returns ERROR_DRM_ZERO_SUBSAMPLES or ERROR_DRM_DECRYPT|Documented errors|
 */
void L2Cases::decryptBoundsAndInvalidInputsRejected_neg() {
    if (!l2VectorConfigured()) {
        UT_LOG_INFO("SKIP: L2 AVBuffer vector fixtures are not configured");
        UT_IGNORE_TEST();
    }
    DrmFixture fixture;
    UT_ASSERT_TRUE_FATAL(loadVector(&fixture));
    UT_ASSERT_TRUE_FATAL(fixture.plaintext.size() > 1);
    LicensedSession licensed;
    UT_ASSERT_TRUE_FATAL(licensed.open(fixture));
    SubSample sample;
    sample.numBytesOfClearData = 0;
    sample.numBytesOfEncryptedData = static_cast<int32_t>(fixture.ciphertext.size());
    {
        SampleBuffers buffers;
        UT_ASSERT_TRUE_FATAL(buffers.open(fixture.ciphertext.size()));
        UT_ASSERT_TRUE_FATAL(buffers.writeSource(fixture.ciphertext));
        DecryptArgs args = buffers.decryptArgs(
                fixture.kid, fixture.iv, fixture.mode, {sample},
                fixture.patternEncryptBlocks, fixture.patternSkipBlocks);
        int32_t decryptedBytes = 0;
        args.subSamples.clear();
        DRM_CRYPTO_PLUGIN_DECRYPT_FAILURE(
                licensed.crypto, args, decryptedBytes, DrmErrors::ERROR_DRM_ZERO_SUBSAMPLES);

        args.subSamples = {sample};
        args.sourceBufferHandle = -1;
        DRM_CRYPTO_PLUGIN_DECRYPT_FAILURE(
                licensed.crypto, args, decryptedBytes, DrmErrors::ERROR_DRM_DECRYPT);

        args.sourceBufferHandle = buffers.sourceHandle();
        args.destinationBufferHandle = -1;
        DRM_CRYPTO_PLUGIN_DECRYPT_FAILURE(
                licensed.crypto, args, decryptedBytes, DrmErrors::ERROR_DRM_DECRYPT);
        UT_ASSERT_TRUE_FATAL(buffers.cleanup());
    }

    {
        SampleBuffers smallDestination;
        UT_ASSERT_TRUE_FATAL(smallDestination.open(
                fixture.ciphertext.size(), fixture.plaintext.size() - 1));
        UT_ASSERT_TRUE_FATAL(smallDestination.writeSource(fixture.ciphertext));
        const DecryptArgs args = smallDestination.decryptArgs(
                fixture.kid, fixture.iv, fixture.mode, {sample},
                fixture.patternEncryptBlocks, fixture.patternSkipBlocks);
        int32_t decryptedBytes = 0;
        const Status status = licensed.crypto->decrypt(args, &decryptedBytes);
        if (hasServiceSpecificError(
                    status, DrmErrors::ERROR_DRM_INSUFFICIENT_OUTPUT_PROTECTION)) {
            UT_LOG_INFO("SKIP: configured license requires protected output buffers");
            UT_IGNORE_TEST();
        }
        UT_ASSERT_TRUE_FATAL(hasServiceSpecificError(
                status, DrmErrors::ERROR_DRM_FRAME_TOO_LARGE));
        UT_ASSERT_TRUE_FATAL(smallDestination.cleanup());
    }

    SampleBuffers shortSource;
    UT_ASSERT_TRUE_FATAL(shortSource.open(
            fixture.ciphertext.size() - 1, fixture.plaintext.size()));
    UT_ASSERT_TRUE_FATAL(shortSource.writeSource(
            std::vector<uint8_t>(fixture.ciphertext.begin(), fixture.ciphertext.end() - 1)));
    const DecryptArgs outOfBoundsArgs = shortSource.decryptArgs(
            fixture.kid, fixture.iv, fixture.mode, {sample},
            fixture.patternEncryptBlocks, fixture.patternSkipBlocks);
    int32_t decryptedBytes = 0;
    DRM_CRYPTO_PLUGIN_DECRYPT_FAILURE(
            licensed.crypto, outOfBoundsArgs, decryptedBytes, DrmErrors::ERROR_DRM_DECRYPT);
    UT_ASSERT_TRUE_FATAL(shortSource.cleanup());
    UT_ASSERT_TRUE(licensed.close());
}

/**
 * @brief Rejects encrypted AVBuffer decryption when the session has no keys.
 *
 * **Test Group ID:** L2: 02@n
 * **Test Case ID:** 013@n
 * **Priority:** High@n
 * **Pre-Conditions:** L2 enabled with known-vector fixtures@n
 * **Dependencies:** DRM, Crypto and AVBuffer services@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Open and bind a session without requesting or providing a key|Configured encrypted sample|Session setup succeeds without a license|Should Pass|
 * |02|Attempt AIDL decryption|KID, IV, ciphertext and AVBuffer handles|Returns ERROR_DRM_NO_LICENSE|Documented error|
 */
void L2Cases::decryptWithoutKeysRejected_neg() {
    if (!l2VectorConfigured()) {
        UT_LOG_INFO("SKIP: L2 AVBuffer vector fixtures are not configured");
        UT_IGNORE_TEST();
    }
    DrmFixture fixture;
    UT_ASSERT_TRUE_FATAL(loadVector(&fixture));

    LicensedSession unlicensed;
    UT_ASSERT_TRUE_FATAL(unlicensed.openSession());
    DRM_CRYPTO_PLUGIN_SET_MEDIA_DRM_SESSION_SUCCESS(unlicensed.crypto, unlicensed.session);
    SampleBuffers buffers;
    UT_ASSERT_TRUE_FATAL(buffers.open(fixture.ciphertext.size()));
    UT_ASSERT_TRUE_FATAL(buffers.writeSource(fixture.ciphertext));

    SubSample sample;
    sample.numBytesOfClearData = 0;
    sample.numBytesOfEncryptedData = static_cast<int32_t>(fixture.ciphertext.size());
    const DecryptArgs args = buffers.decryptArgs(
            fixture.kid, fixture.iv, fixture.mode, {sample},
            fixture.patternEncryptBlocks, fixture.patternSkipBlocks);
    int32_t decryptedBytes = 0;
    DRM_CRYPTO_PLUGIN_DECRYPT_FAILURE(
            unlicensed.crypto, args, decryptedBytes, DrmErrors::ERROR_DRM_NO_LICENSE);
    UT_ASSERT_TRUE_FATAL(buffers.cleanup());
    UT_ASSERT_TRUE(unlicensed.close());
}

/**
 * @brief Asserts documented AIDL errors after key removal and session closure.
 *
 * **Test Group ID:** L2: 02@n
 * **Test Case ID:** 011@n
 * **Priority:** High@n
 * **Pre-Conditions:** L2 enabled with a known vector and accepted license@n
 * **Dependencies:** DRM, AVBuffer, and AVBufferHelper services@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Remove keys and attempt AIDL decryption|Active DRM session and valid sample|Returns ERROR_DRM_NO_LICENSE|Documented error|
 * |02|Close the DRM session and retry decryption|Closed session ID and same handles|Returns ERROR_DRM_SESSION_NOT_OPENED|Documented error|
 */
void L2Cases::removedKeysAndClosedSessionRejectAvBufferDecrypt_neg() {
    if (!l2VectorConfigured()) {
        UT_LOG_INFO("SKIP: L2 AVBuffer vector fixtures are not configured");
        UT_IGNORE_TEST();
    }
    DrmFixture fixture;
    UT_ASSERT_TRUE_FATAL(loadVector(&fixture));
    LicensedSession licensed;
    UT_ASSERT_TRUE_FATAL(licensed.open(fixture));
    SampleBuffers buffers;
    UT_ASSERT_TRUE_FATAL(buffers.open(fixture.ciphertext.size()));
    UT_ASSERT_TRUE_FATAL(buffers.writeSource(fixture.ciphertext));

    SubSample sample;
    sample.numBytesOfClearData = 0;
    sample.numBytesOfEncryptedData = static_cast<int32_t>(fixture.ciphertext.size());
    const DecryptArgs args = buffers.decryptArgs(
            fixture.kid, fixture.iv, fixture.mode, {sample},
            fixture.patternEncryptBlocks, fixture.patternSkipBlocks);
    int32_t decryptedBytes = 0;
    Status status = licensed.crypto->decrypt(args, &decryptedBytes);
    if (hasServiceSpecificError(
                status, DrmErrors::ERROR_DRM_INSUFFICIENT_OUTPUT_PROTECTION)) {
        UT_LOG_INFO("SKIP: configured license requires protected output buffers");
        UT_IGNORE_TEST();
    }
    DRM_STATUS_SUCCESS(status);
    UT_ASSERT_EQUAL_FATAL(decryptedBytes, static_cast<int32_t>(fixture.plaintext.size()));
    DRM_PLUGIN_REMOVE_KEYS_SUCCESS(licensed.drm, licensed.session);
    DRM_CRYPTO_PLUGIN_DECRYPT_FAILURE(
            licensed.crypto, args, decryptedBytes, DrmErrors::ERROR_DRM_NO_LICENSE);

    UT_ASSERT_TRUE_FATAL(licensed.close());
    DRM_CRYPTO_PLUGIN_DECRYPT_FAILURE(
            licensed.crypto, args, decryptedBytes, DrmErrors::ERROR_DRM_SESSION_NOT_OPENED);
    UT_ASSERT_TRUE_FATAL(buffers.cleanup());
}

/**
 * @brief Verifies decryption is rejected after keys are removed from the session.
 *
 * **Test Group ID:** L2: 02@n
 * **Test Case ID:** 004@n
 * **Priority:** High@n
 * **Pre-Conditions:** Enable L2 through YAML or environment and provide a valid ClearKey fixture@n
 * **Dependencies:** ClearKey key request/response succeeds@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Acquire a key for the encrypted sample|Init data and ClearKey response|Session setup succeeds|Should Pass|
 * |02|Remove keys from the active session|Session ID|removeKeys succeeds|Should Pass|
 * |03|Retry sample decryption|KID, ciphertext, IV|Returns ERROR_DRM_NO_LICENSE|Documented negative result|
 */
void L2Cases::removeKeysRejectsDecrypt_neg() {
    if (vts_drm_active_profile()->scheme != "clearkey") {
        UT_LOG_INFO("SKIP: legacy DRM vector decryption is ClearKey-specific; use L3 AVBuffer tests");
        UT_IGNORE_TEST();
    }
    DrmFixture fixture;
    UT_ASSERT_TRUE_FATAL(loadVector(&fixture));
    LicensedSession licensed;
    UT_ASSERT_TRUE_FATAL(licensed.open(fixture));
    std::vector<uint8_t> baseline;
    DRM_PLUGIN_DECRYPT_SUCCESS(
            licensed.drm, licensed.session, fixture.kid, fixture.ciphertext, fixture.iv, baseline);
    logSamplePreview("ciphertext before IDrmPlugin::decrypt", fixture.ciphertext);
    logSamplePreview("plaintext after IDrmPlugin::decrypt (keys loaded)", baseline);
    UT_ASSERT_TRUE_FATAL(baseline == fixture.plaintext);
    DRM_PLUGIN_REMOVE_KEYS_SUCCESS(licensed.drm, licensed.session);

    std::vector<uint8_t> decrypted;
    const Status status = licensed.drm->decrypt(
            licensed.session, fixture.kid, fixture.ciphertext, fixture.iv, &decrypted);
    UT_ASSERT_TRUE(hasServiceSpecificError(status, DrmErrors::ERROR_DRM_NO_LICENSE));
    UT_ASSERT_TRUE(licensed.close());
}

/**
 * @brief Confirms an empty license response is rejected for an open session.
 *
 * **Test Group ID:** L2: 02@n
 * **Test Case ID:** 005@n
 * **Priority:** Medium@n
 * **Pre-Conditions:** Enable the scheme's L2 cases and provide valid init data@n
 * **Dependencies:** Selected DRM factory and openable session@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Open a DRM session|Configured security level|Call succeeds|Should Pass|
 * |02|Request a streaming license, then provide an empty response|Valid init data, empty response|Request succeeds; response returns BAD_VALUE|Documented invalid-parameter error|
 * |03|Close the session|Session ID|Call succeeds|Should Pass|
 */
void L2Cases::emptyLicenseResponseRejected_neg() {
    DrmFixture fixture;
    UT_ASSERT_TRUE_FATAL(loadFixture(&fixture));
    LicensedSession licensed;
    UT_ASSERT_TRUE_FATAL(licensed.openSession());
    KeyRequest request;
    DRM_PLUGIN_GET_KEY_REQUEST_SUCCESS(licensed.drm,
            licensed.session, fixture.initData, vts_drm_config_value("DRM_L2_MIME_TYPE"),
            KeyType::STREAMING, {}, request);
    UT_ASSERT_FALSE_FATAL(request.request.empty());
    com::rdk::hal::drm::KeySetId keySetId;
    const Status status = licensed.drm->provideKeyResponse(
            licensed.session, {}, &keySetId);
    UT_ASSERT_TRUE(hasServiceSpecificErrorCode(status, android::BAD_VALUE));
    UT_ASSERT_TRUE(licensed.close());
}

/**
 * @brief Rejects a ClearKey streaming request with empty init data.
 *
 * **Test Group ID:** L2: 02@n
 * **Test Case ID:** 007@n
 * **Priority:** Medium@n
 * **Pre-Conditions:** ClearKey L2 enabled@n
 * **Dependencies:** ClearKey DRM plugin and openable session@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Request a streaming key with empty init data|Open session, empty init data, configured MIME|Returns BAD_VALUE|Documented invalid-parameter error|
 */
void L2Cases::invalidInitDataRejected_neg() {
    LicensedSession licensed;
    UT_ASSERT_TRUE_FATAL(licensed.openSession());
    KeyRequest request;
    const Status status = licensed.drm->getKeyRequest(
            licensed.session, {}, vts_drm_config_value("DRM_L2_MIME_TYPE"),
            KeyType::STREAMING, {}, &request);
    UT_ASSERT_TRUE(hasServiceSpecificErrorCode(status, android::BAD_VALUE));
    UT_ASSERT_TRUE(licensed.close());
}

/**
 * @brief Checks a key request is rejected after its DRM session has been closed.
 *
 * **Test Group ID:** L2: 02@n
 * **Test Case ID:** 006@n
 * **Priority:** Medium@n
 * **Pre-Conditions:** Enable L2 through YAML or environment and provide the configured fixture inputs@n
 * **Dependencies:** Selected DRM factory and openable session@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Open and close a DRM session|Configured security level|Both calls succeed|Should Pass|
 * |02|Request a streaming key using the closed session ID|Session ID, configured init data and MIME|Returns ERROR_DRM_SESSION_NOT_OPENED|Documented negative result|
 */
void L2Cases::closedSessionRejectsOperations_neg() {
    DrmFixture fixture;
    UT_ASSERT_TRUE_FATAL(loadFixture(&fixture));
    LicensedSession licensed;
    UT_ASSERT_TRUE_FATAL(licensed.openSession());
    const std::vector<uint8_t> session = licensed.session;
    UT_ASSERT_TRUE_FATAL(licensed.close());

    KeyRequest request;
    const Status status = licensed.drm->getKeyRequest(
            session, fixture.initData, vts_drm_config_value("DRM_L2_MIME_TYPE"),
            KeyType::STREAMING, {}, &request);
    UT_ASSERT_TRUE(hasServiceSpecificError(
            status, DrmErrors::ERROR_DRM_SESSION_NOT_OPENED));
}

/**
 * @brief Handles provisioning when opening a session reports NOT_PROVISIONED.
 *
 * This opt-in test exchanges opaque provisioning data with an approved adapter
 * for the selected L2 scheme and retries session creation.
 *
 * **Test Group ID:** L2: 02@n
 * **Test Case ID:** 021@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** L2 enabled and provisioning status test enabled@n
 * **Dependencies:** Selected DRM plugin; configured endpoint if an exchange is required@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Check the provisioning enable setting | DRM_PROVISIONING_ENABLE | Unset/0 explicitly skips; enabled value must be 1 | Opt-in only |
 * | 02 | Create a DRM plugin for the selected L2 scheme | Configured UUID | Plugin creation succeeds | Should Pass |
 * | 03 | Attempt session open at the tested levels | HW_SECURE_ALL, SW_SECURE_CRYPTO | Success with a non-empty ID, or documented CANNOT_HANDLE/NOT_PROVISIONED error | Successfully opened sessions are closed immediately |
 * | 04 | Determine whether an exchange is needed | Results of session-open attempts | If no attempt needs provisioning, finish without an exchange | No unsupported capability is reported as provisioned |
 * | 05 | Request provisioning data if required | Configured certificate type and authority | Non-empty request; ERROR_DRM_CANNOT_HANDLE explicitly skips | Other errors fail |
 * | 06 | Validate transport settings and POST the opaque request | Configured URL, content type, positive timeout, request bytes | HTTP helper returns a valid non-empty response | Approved adapter only |
 * | 07 | Submit the provisioning response | Opaque response bytes | Status is OK with EX_NONE | Device credentials may be updated |
 * | 08 | Retry session creation and close the new session | Security level that reported NOT_PROVISIONED | Session opens with a non-empty ID and closes successfully | Destructor closes session on assertion failure |
 */
void L2Cases::provisioningStatusAndExchange_pos() {
    const char* enabled = vts_drm_config_value("DRM_PROVISIONING_ENABLE");
    if (enabled == nullptr || std::string(enabled) == "0") {
        UT_LOG_INFO("SKIP: provisioning exchange is disabled in DRM test configuration");
        UT_IGNORE_TEST();
    }
    UT_ASSERT_TRUE_FATAL(std::string(enabled) == "1");

    Uuid uuid;
    UT_ASSERT_TRUE_FATAL(vts_drm_getSupportedUuid(&uuid).isOk());
    LicensedSession licensed;
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(uuid, DRM_APP_PACKAGE_NAME, licensed.drm);

    const com::rdk::hal::drm::SecurityLevel levels[] = {
        com::rdk::hal::drm::SecurityLevel::HW_SECURE_ALL,
        com::rdk::hal::drm::SecurityLevel::SW_SECURE_CRYPTO};
    bool provisioningRequired = false;
    com::rdk::hal::drm::SecurityLevel retryLevel =
            com::rdk::hal::drm::SecurityLevel::UNKNOWN;
    for (const auto level : levels) {
        licensed.session.clear();
        Status status;
        DRM_PLUGIN_OPEN_SESSION(licensed.drm, level, licensed.session, status);
        if (status.isOk()) {
            UT_ASSERT_FALSE_FATAL(licensed.session.empty());
            UT_ASSERT_TRUE_FATAL(licensed.close());
            continue;
        }
        licensed.session.clear();
        if (hasServiceSpecificError(status, DrmErrors::ERROR_DRM_CANNOT_HANDLE)) {
            continue;
        }
        UT_ASSERT_TRUE_FATAL(
                hasServiceSpecificError(status, DrmErrors::ERROR_DRM_NOT_PROVISIONED));
        provisioningRequired = true;
        retryLevel = level;
        break;
    }
    if (!provisioningRequired) {
        UT_LOG_INFO("Provisioning not required by the selected DRM scheme");
        return;
    }

    com::rdk::hal::drm::ProvisionRequest request;
    const char* certificateType =
            vts_drm_config_value("DRM_PROVISIONING_CERTIFICATE_TYPE");
    const char* certificateAuthority =
            vts_drm_config_value("DRM_PROVISIONING_CERTIFICATE_AUTHORITY");
    UT_ASSERT_TRUE_FATAL(certificateType != nullptr && certificateType[0] != '\0');
    UT_ASSERT_TRUE_FATAL(certificateAuthority != nullptr);
    Status requestStatus;
    DRM_PLUGIN_GET_PROVISION_REQUEST(
            licensed.drm, certificateType, certificateAuthority, request, requestStatus);
    if (hasServiceSpecificError(requestStatus, DrmErrors::ERROR_DRM_CANNOT_HANDLE)) {
        UT_LOG_INFO("SKIP: DRM scheme reports provisioning is unsupported");
        UT_IGNORE_TEST();
    }
    DRM_STATUS_SUCCESS(requestStatus);
    UT_ASSERT_TRUE_FATAL(!request.request.empty());

    const char* url = vts_drm_config_value("DRM_PROVISIONING_URL");
    const char* contentType = vts_drm_config_value("DRM_PROVISIONING_CONTENT_TYPE");
    const char* timeoutText =
            vts_drm_config_value("DRM_PROVISIONING_REQUEST_TIMEOUT_SECONDS");
    UT_ASSERT_TRUE_FATAL(url != nullptr && url[0] != '\0');
    UT_ASSERT_TRUE_FATAL(contentType != nullptr && contentType[0] != '\0');
    UT_ASSERT_TRUE_FATAL(timeoutText != nullptr);
    long timeout = 0;
    const std::string timeoutValue(timeoutText);
    const auto parsed = std::from_chars(
            timeoutValue.data(), timeoutValue.data() + timeoutValue.size(), timeout);
    UT_ASSERT_TRUE_FATAL(parsed.ec == std::errc() &&
            parsed.ptr == timeoutValue.data() + timeoutValue.size() && timeout > 0);

    std::vector<uint8_t> response;
    UT_ASSERT_TRUE_FATAL(vts_drm_request_http_post(
            url, contentType, timeout, request.request, &response));
    com::rdk::hal::drm::ProvideProvisionResponseResult result;
    DRM_PLUGIN_PROVIDE_PROVISION_RESPONSE_SUCCESS(licensed.drm, response, result);

    licensed.session.clear();
    Status retryStatus;
    DRM_PLUGIN_OPEN_SESSION(licensed.drm, retryLevel, licensed.session, retryStatus);
    DRM_STATUS_SUCCESS(retryStatus);
    UT_ASSERT_FALSE_FATAL(licensed.session.empty());
    UT_ASSERT_TRUE_FATAL(licensed.close());
}

#define DRM_L2_SCHEME(Suite, Scheme) \
    class Suite : public L2Cases { \
    public: Suite() : L2Cases(Scheme) {} \
    }; \
    UT_ADD_TEST_TO_GROUP(Suite, UT_TESTS_L2) \
    UT_ADD_TEST(Suite, fixtureValidation_pos) { L2Cases::fixtureValidation_pos(); } \
    UT_ADD_TEST(Suite, keyRequestAndResponse_pos) { L2Cases::keyRequestAndResponse_pos(); } \
    UT_ADD_TEST(Suite, offlineKeySetCreated_pos) { L2Cases::offlineKeySetCreated_pos(); } \
    UT_ADD_TEST(Suite, provisioningStatusAndExchange_pos) { \
        L2Cases::provisioningStatusAndExchange_pos(); \
    } \
    UT_ADD_TEST(Suite, decryptClearSegmentWithAvBuffer_pos) { \
        L2Cases::decryptClearSegmentWithAvBuffer_pos(); \
    } \
    UT_ADD_TEST(Suite, decryptKnownSample_pos) { L2Cases::decryptKnownSample_pos(); } \
    UT_ADD_TEST(Suite, decryptKnownSampleWithAvBuffer_pos) { \
        L2Cases::decryptKnownSampleWithAvBuffer_pos(); \
    } \
    UT_ADD_TEST(Suite, decryptSubsamplesAndPattern_pos) { \
        L2Cases::decryptSubsamplesAndPattern_pos(); \
    } \
    UT_ADD_TEST(Suite, decryptBoundsAndInvalidInputsRejected_neg) { \
        L2Cases::decryptBoundsAndInvalidInputsRejected_neg(); \
    } \
    UT_ADD_TEST(Suite, decryptWithoutKeysRejected_neg) { \
        L2Cases::decryptWithoutKeysRejected_neg(); \
    } \
    UT_ADD_TEST(Suite, removedKeysAndClosedSessionRejectAvBufferDecrypt_neg) { \
        L2Cases::removedKeysAndClosedSessionRejectAvBufferDecrypt_neg(); \
    } \
    UT_ADD_TEST(Suite, removeKeysRejectsDecrypt_neg) { L2Cases::removeKeysRejectsDecrypt_neg(); } \
    UT_ADD_TEST(Suite, emptyLicenseResponseRejected_neg) { L2Cases::emptyLicenseResponseRejected_neg(); } \
    UT_ADD_TEST(Suite, closedSessionRejectsOperations_neg) { L2Cases::closedSessionRejectsOperations_neg(); }

DRM_L2_SCHEME(VTS_L2_Drm, "clearkey")
//DRM_L2_SCHEME(VTS_L2_DrmWidevine, "widevine")
//DRM_L2_SCHEME(VTS_L2_DrmPlayReady, "playready")
#undef DRM_L2_SCHEME

// Empty init data can be valid for renewal in other schemes.
UT_ADD_TEST(VTS_L2_Drm, invalidInitDataRejected_neg) {
    L2Cases::invalidInitDataRejected_neg();
}
UT_ADD_TEST(VTS_L2_Drm, listenerCallbacks_pos) {
    L2Cases::listenerCallbacks_pos();
}
UT_ADD_TEST(VTS_L2_Drm, getKeyRequestResourceContention_neg) {
    L2Cases::getKeyRequestResourceContention_neg();
}
UT_ADD_TEST(VTS_L2_Drm, getKeyRequestInvalidStateAndSessionLost_neg) {
    L2Cases::getKeyRequestInvalidStateAndSessionLost_neg();
}
UT_ADD_TEST(VTS_L2_Drm, malformedClearKeyEmptyKeyRejected_neg) {
    L2Cases::malformedClearKeyEmptyKeyRejected_neg();
}
UT_ADD_TEST(VTS_L2_Drm, malformedClearKeyLongKeyRejected_neg) {
    L2Cases::malformedClearKeyLongKeyRejected_neg();
}
UT_ADD_TEST(VTS_L2_Drm, badLevelNotSupported_neg) {
    L2Cases::badLevelNotSupported_neg();
}

/** @} */ // End of DRM_HALTEST_L2_CLEARKEY
/** @} */ // End of DRM_HALTEST
/** @} */ // End of DRM
/** @} */ // End of HPK
