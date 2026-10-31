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
 * @defgroup DRM_HALTEST_L1 DRM HAL Tests L1 File
 * @{
 * @parblock
 *
 * ### L1 Test Cases for DRM HAL :
 *
 * This module tests DRM AIDL discovery, capabilities, plugin and session
 * lifecycle, and documented positive and negative API behavior without
 * requiring a valid license or media playback.
 *
 * **Pre-Conditions:** DRM factory service is available@n
 * **Dependencies:** Generated DRM AIDL headers and UT-Core@n
 *
 * @endparblock
 */

/**
 * @file vts_drm_l1_tests.cpp
 *
 */

#include "vts_drm.h"

#include <com/rdk/hal/drm/BnDrmPluginListener.h>
#include <com/rdk/hal/drm/DrmErrors.h>
#include <com/rdk/hal/drm/DrmMetricGroup.h>
#include <com/rdk/hal/drm/HdcpLevel.h>
#include <com/rdk/hal/drm/HdcpLevels.h>
#include <com/rdk/hal/drm/KeyRequest.h>
#include <com/rdk/hal/drm/KeyStatus.h>
#include <com/rdk/hal/drm/KeyType.h>
#include <com/rdk/hal/drm/NumberOfSessions.h>
#include <com/rdk/hal/drm/SecurityLevel.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <set>
#include <vector>

using com::rdk::hal::drm::BnDrmPluginListener;
using com::rdk::hal::drm::CryptoSchemes;
using com::rdk::hal::drm::DrmErrors;
using com::rdk::hal::drm::EventType;
using com::rdk::hal::drm::HdcpLevel;
using com::rdk::hal::drm::HdcpLevels;
using com::rdk::hal::drm::KeyRequest;
using com::rdk::hal::drm::KeyStatus;
using com::rdk::hal::drm::KeyType;
using com::rdk::hal::drm::NumberOfSessions;
using com::rdk::hal::drm::SecurityLevel;

UT_ADD_TEST_TO_GROUP(VTS_L1_Drm, UT_TESTS_L1)

namespace {

class DrmTestListener final : public BnDrmPluginListener {
public:
    Status onEvent(
            EventType,
            const std::vector<uint8_t>&,
            const std::vector<uint8_t>&) override {
        return Status::ok();
    }

    Status onExpirationUpdate(const std::vector<uint8_t>&, int64_t) override {
        return Status::ok();
    }

    Status onKeysChange(
            const std::vector<uint8_t>&,
            const std::vector<KeyStatus>&,
            bool) override {
        return Status::ok();
    }

    Status onSessionLostState(const std::vector<uint8_t>&) override {
        return Status::ok();
    }
};

class VTS_L1_Drm : public UTCore {
public:
    void SetUp() override {
        if (gDrmFactory == nullptr) {
            UT_FAIL_FATAL("DRM factory service not available");
        }
    }
};

Uuid unsupportedUuid() {
    Uuid uuid;
    uuid.uuid = {
            0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80,
            0x90, 0xa0, 0xb0, 0xc0, 0xd0, 0xe0, 0xf0, 0x01};
    return uuid;
}

bool isResolvedSecurityLevel(SecurityLevel level) {
    return level >= SecurityLevel::SW_SECURE_CRYPTO &&
            level <= SecurityLevel::HW_SECURE_ALL;
}

bool isValidHdcpLevel(HdcpLevel level) {
    return level >= HdcpLevel::HDCP_UNKNOWN && level <= HdcpLevel::HDCP_V2_3;
}

bool hasBinderException(const Status& status, int32_t exceptionCode) {
    return !status.isOk() && status.exceptionCode() == exceptionCode;
}

bool hasServiceSpecificError(const Status& status, DrmErrors error) {
    return !status.isOk() &&
            status.exceptionCode() == android::binder::Status::EX_SERVICE_SPECIFIC &&
            status.serviceSpecificErrorCode() == static_cast<int32_t>(error);
}

}  // namespace

/**
 * @brief Verifies the DRM factory reports the generated AIDL version and hash.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 001@n
 * **Priority:** High@n
 * **Pre-Conditions:** DRM factory service is available@n
 * **Dependencies:** Pinned DRM AIDL headers@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Read the factory interface version|None|Matches IDrmFactory::VERSION|Should Pass|
 * |02|Read the factory interface hash|None|Matches IDrmFactory::HASHVALUE|Should Pass|
 */
UT_ADD_TEST(VTS_L1_Drm, interfaceMetadata_pos) {
    UT_ASSERT_TRUE(gDrmFactory->getInterfaceVersion() == IDrmFactory::VERSION);
    UT_ASSERT_TRUE(gDrmFactory->getInterfaceHash() == IDrmFactory::HASHVALUE);
}

/**
 * @brief Validates the advertised DRM schemes and supported content types.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 002@n
 * **Priority:** High@n
 * **Pre-Conditions:** DRM factory service is available@n
 * **Dependencies:** None@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Retrieve supported crypto schemes|None|Call succeeds and UUID list is non-empty|Should Pass|
 * |02|Check UUID uniqueness|Advertised UUIDs|Every UUID appears once|Should Pass|
 * |03|Validate each content type and security range|mime, minLevel, maxLevel|MIME is non-empty and levels are ordered and resolved|Should Pass|
 */
UT_ADD_TEST(VTS_L1_Drm, supportedCryptoSchemes_pos) {
    CryptoSchemes schemes;
    const Status status = gDrmFactory->getSupportedCryptoSchemes(&schemes);

    UT_ASSERT_TRUE(status.isOk());
    UT_ASSERT_FALSE(schemes.uuids.empty());

    const std::set<Uuid> uniqueUuids(schemes.uuids.begin(), schemes.uuids.end());
    UT_ASSERT_EQUAL(uniqueUuids.size(), schemes.uuids.size());
    for (const auto& contentType : schemes.mimeTypes) {
        UT_ASSERT_TRUE(contentType.mime.size() > 0);
        UT_ASSERT_TRUE(contentType.minLevel <= contentType.maxLevel);
        UT_ASSERT_TRUE(isResolvedSecurityLevel(contentType.minLevel));
        UT_ASSERT_TRUE(isResolvedSecurityLevel(contentType.maxLevel));
    }
}

/**
 * @brief Creates DRM and crypto plugins using an advertised scheme UUID.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 003@n
 * **Priority:** High@n
 * **Pre-Conditions:** DRM factory service is available@n
 * **Dependencies:** UUID returned by getSupportedCryptoSchemes@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Select an advertised crypto scheme UUID|Supported UUID list|A UUID is available|Should Pass|
 * |02|Create the DRM and crypto plugins|UUID, package name, empty init data|Both calls succeed and return non-null plugins|Should Pass|
 */
UT_ADD_TEST(VTS_L1_Drm, createPlugins_pos) {
    Uuid uuid;
    UT_ASSERT_TRUE(vts_drm_getSupportedUuid(&uuid).isOk());

    sp<IDrmPlugin> drmPlugin;
    sp<ICryptoPlugin> cryptoPlugin;
    UT_ASSERT_TRUE(vts_drm_createPlugins(uuid, &drmPlugin, &cryptoPlugin).isOk());
    UT_ASSERT_TRUE(drmPlugin != nullptr);
    UT_ASSERT_TRUE(cryptoPlugin != nullptr);
}

/**
 * @brief Rejects DRM plugin creation for a UUID not advertised by the factory.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 004@n
 * **Priority:** High@n
 * **Pre-Conditions:** DRM factory service is available@n
 * **Dependencies:** None@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Request a DRM plugin for an unsupported UUID|Fixed unsupported UUID|Returns ERROR_DRM_CANNOT_HANDLE and output plugin is null|Should Pass|
 */
UT_ADD_TEST(VTS_L1_Drm, createDrmPluginUnsupportedUuid_neg) {
    sp<IDrmPlugin> plugin;
    const Status status = gDrmFactory->createDrmPlugin(
            unsupportedUuid(), String16("com.rdk.vts.drm"), &plugin);

    UT_ASSERT_TRUE(hasServiceSpecificError(status, DrmErrors::ERROR_DRM_CANNOT_HANDLE));
    UT_ASSERT_TRUE(plugin == nullptr);
}

/**
 * @brief Rejects crypto plugin creation for a UUID not advertised by the factory.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 005@n
 * **Priority:** High@n
 * **Pre-Conditions:** DRM factory service is available@n
 * **Dependencies:** None@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Request a crypto plugin for an unsupported UUID|Fixed unsupported UUID, empty init data|Call fails and output plugin is null|Should Pass|
 */
UT_ADD_TEST(VTS_L1_Drm, createCryptoPluginUnsupportedUuid_neg) {
    sp<ICryptoPlugin> plugin;
    const Status status =
            gDrmFactory->createCryptoPlugin(unsupportedUuid(), {}, &plugin);

    UT_ASSERT_FALSE(status.isOk());
    UT_ASSERT_TRUE(plugin == nullptr);
}

/**
 * @brief Exercises opening, querying, binding, listening, and closing a session.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 006@n
 * **Priority:** High@n
 * **Pre-Conditions:** DRM factory service is available@n
 * **Dependencies:** Advertised DRM scheme and crypto plugin@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Open a session at the default security level|SecurityLevel::DEFAULT|Call succeeds and session ID is non-empty|Should Pass|
 * |02|Query the session security level|Session ID|Call succeeds and returns a resolved level|Should Pass|
 * |03|Register a listener and bind the crypto plugin|Listener, session ID|Both calls succeed|Callback delivery is not tested|
 * |04|Close the session|Session ID|Call succeeds|Should Pass|
 */
UT_ADD_TEST(VTS_L1_Drm, sessionLifecycle_pos) {
    Uuid uuid;
    UT_ASSERT_TRUE(vts_drm_getSupportedUuid(&uuid).isOk());

    sp<IDrmPlugin> drmPlugin;
    sp<ICryptoPlugin> cryptoPlugin;
    UT_ASSERT_TRUE(vts_drm_createPlugins(uuid, &drmPlugin, &cryptoPlugin).isOk());

    std::vector<uint8_t> sessionId;
    UT_ASSERT_TRUE(drmPlugin->openSession(SecurityLevel::DEFAULT, &sessionId).isOk());
    UT_ASSERT_FALSE(sessionId.empty());

    SecurityLevel actualLevel = SecurityLevel::UNKNOWN;
    UT_ASSERT_TRUE(drmPlugin->getSecurityLevel(sessionId, &actualLevel).isOk());
    UT_ASSERT_TRUE(isResolvedSecurityLevel(actualLevel));

    sp<DrmTestListener> listener = sp<DrmTestListener>::make();
    UT_ASSERT_TRUE(drmPlugin->setListener(listener).isOk());
    UT_ASSERT_TRUE(cryptoPlugin->setMediaDrmSession(sessionId).isOk());
    UT_ASSERT_TRUE(drmPlugin->closeSession(sessionId).isOk());
}

/**
 * @brief Confirms closing an already closed session returns the documented error.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 007@n
 * **Priority:** High@n
 * **Pre-Conditions:** DRM factory service is available@n
 * **Dependencies:** A session can be opened@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Open and close a DRM session|Default security level|Both calls succeed|Should Pass|
 * |02|Close the same session again|Closed session ID|Returns ERROR_DRM_SESSION_NOT_OPENED|Should Pass|
 */
UT_ADD_TEST(VTS_L1_Drm, closeSessionTwice_neg) {
    Uuid uuid;
    UT_ASSERT_TRUE(vts_drm_getSupportedUuid(&uuid).isOk());

    sp<IDrmPlugin> drmPlugin;
    UT_ASSERT_TRUE(gDrmFactory->createDrmPlugin(
            uuid, String16("com.rdk.vts.drm"), &drmPlugin).isOk());

    std::vector<uint8_t> sessionId;
    UT_ASSERT_TRUE(drmPlugin->openSession(SecurityLevel::DEFAULT, &sessionId).isOk());
    UT_ASSERT_TRUE(drmPlugin->closeSession(sessionId).isOk());
    const Status status = drmPlugin->closeSession(sessionId);
    UT_ASSERT_TRUE(hasServiceSpecificError(
            status, com::rdk::hal::drm::DrmErrors::ERROR_DRM_SESSION_NOT_OPENED));
}

/**
 * @brief Rejects empty IDs for session lookup, close, crypto binding, and key requests.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 008@n
 * **Priority:** High@n
 * **Pre-Conditions:** DRM factory service is available@n
 * **Dependencies:** DRM and crypto plugins can be created@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Query security level and close with an empty session ID|Empty byte array|Both calls return BAD_VALUE / EX_ILLEGAL_ARGUMENT|Should Pass|
 * |02|Bind the crypto plugin to the empty session ID|Empty byte array|Returns ERROR_DRM_SESSION_NOT_OPENED|Should Pass|
 * |03|Request keys with the empty session ID|Empty byte array, video/mp4, STREAMING|Returns BAD_VALUE / EX_ILLEGAL_ARGUMENT|Should Pass|
 */
UT_ADD_TEST(VTS_L1_Drm, emptySessionOperations_neg) {
    Uuid uuid;
    UT_ASSERT_TRUE(vts_drm_getSupportedUuid(&uuid).isOk());

    sp<IDrmPlugin> drmPlugin;
    sp<ICryptoPlugin> cryptoPlugin;
    UT_ASSERT_TRUE(vts_drm_createPlugins(uuid, &drmPlugin, &cryptoPlugin).isOk());

    const std::vector<uint8_t> emptySession;
    SecurityLevel level = SecurityLevel::UNKNOWN;
    KeyRequest request;

    const Status securityLevelStatus = drmPlugin->getSecurityLevel(emptySession, &level);
    UT_ASSERT_TRUE(hasBinderException(
            securityLevelStatus, android::binder::Status::EX_ILLEGAL_ARGUMENT));

    const Status closeStatus = drmPlugin->closeSession(emptySession);
    UT_ASSERT_TRUE(hasBinderException(
            closeStatus, android::binder::Status::EX_ILLEGAL_ARGUMENT));

    const Status bindStatus = cryptoPlugin->setMediaDrmSession(emptySession);
    UT_ASSERT_TRUE(hasServiceSpecificError(
            bindStatus, com::rdk::hal::drm::DrmErrors::ERROR_DRM_SESSION_NOT_OPENED));

    const Status keyRequestStatus = drmPlugin->getKeyRequest(
            emptySession,
            {},
            String16("video/mp4"),
            KeyType::STREAMING,
            {},
            &request);
    UT_ASSERT_TRUE(hasBinderException(
            keyRequestStatus, android::binder::Status::EX_ILLEGAL_ARGUMENT));
}

/**
 * @brief Checks documented errors for API calls made with a closed session.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 009@n
 * **Priority:** High@n
 * **Pre-Conditions:** DRM factory service is available@n
 * **Dependencies:** A DRM and crypto plugin and one openable session@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Open and close a session, then query its security level and bind crypto|Closed non-empty session ID|Both calls return ERROR_DRM_SESSION_NOT_OPENED|Should Pass|
 * |02|Query key status and remove keys|Closed session ID|Both calls return ERROR_DRM_SESSION_NOT_OPENED|Should Pass|
 * |03|Set cipher, MAC, and playback ID|Closed session ID, valid algorithm names and playback ID|Each call returns ERROR_DRM_SESSION_NOT_OPENED|Should Pass|
 * |04|Call decrypt, encrypt, sign, and verify|Closed session ID and non-empty arguments|Each call returns ERROR_DRM_SESSION_NOT_OPENED|Should Pass|
 */
UT_ADD_TEST(VTS_L1_Drm, closedSessionOperations_neg) {
    Uuid uuid;
    UT_ASSERT_TRUE(vts_drm_getSupportedUuid(&uuid).isOk());

    sp<IDrmPlugin> drmPlugin;
    sp<ICryptoPlugin> cryptoPlugin;
    UT_ASSERT_TRUE(vts_drm_createPlugins(uuid, &drmPlugin, &cryptoPlugin).isOk());

    std::vector<uint8_t> sessionId;
    UT_ASSERT_TRUE(drmPlugin->openSession(SecurityLevel::DEFAULT, &sessionId).isOk());
    UT_ASSERT_TRUE(drmPlugin->closeSession(sessionId).isOk());

    SecurityLevel level = SecurityLevel::UNKNOWN;
    const Status securityLevelStatus = drmPlugin->getSecurityLevel(sessionId, &level);
    UT_ASSERT_TRUE(hasServiceSpecificError(
            securityLevelStatus,
            com::rdk::hal::drm::DrmErrors::ERROR_DRM_SESSION_NOT_OPENED));

    const Status bindStatus = cryptoPlugin->setMediaDrmSession(sessionId);
    UT_ASSERT_TRUE(hasServiceSpecificError(
            bindStatus, com::rdk::hal::drm::DrmErrors::ERROR_DRM_SESSION_NOT_OPENED));

    std::vector<com::rdk::hal::drm::KeyValue> keyStatus;
    const Status queryStatus = drmPlugin->queryKeyStatus(sessionId, &keyStatus);
    UT_ASSERT_TRUE(hasServiceSpecificError(
            queryStatus, DrmErrors::ERROR_DRM_SESSION_NOT_OPENED));

    const Status removeKeysStatus = drmPlugin->removeKeys(sessionId);
    UT_ASSERT_TRUE(hasServiceSpecificError(
            removeKeysStatus, DrmErrors::ERROR_DRM_SESSION_NOT_OPENED));

    const std::vector<uint8_t> keyId(16, 0);
    const std::vector<uint8_t> input{0x01};
    const std::vector<uint8_t> iv(16, 0);
    std::vector<uint8_t> output;

    const Status cipherStatus =
            drmPlugin->setCipherAlgorithm(sessionId, String16("AES/CBC/PKCS5Padding"));
    UT_ASSERT_TRUE(hasServiceSpecificError(
            cipherStatus, DrmErrors::ERROR_DRM_SESSION_NOT_OPENED));

    const Status macStatus = drmPlugin->setMacAlgorithm(sessionId, String16("HmacSHA256"));
    UT_ASSERT_TRUE(hasServiceSpecificError(
            macStatus, DrmErrors::ERROR_DRM_SESSION_NOT_OPENED));

    const Status playbackStatus = drmPlugin->setPlaybackId(sessionId, String16("vts-playback"));
    UT_ASSERT_TRUE(hasServiceSpecificError(
            playbackStatus, DrmErrors::ERROR_DRM_SESSION_NOT_OPENED));

    const Status decryptStatus = drmPlugin->decrypt(sessionId, keyId, input, iv, &output);
    UT_ASSERT_TRUE(hasServiceSpecificError(
            decryptStatus, DrmErrors::ERROR_DRM_SESSION_NOT_OPENED));

    const Status encryptStatus = drmPlugin->encrypt(sessionId, keyId, input, iv, &output);
    UT_ASSERT_TRUE(hasServiceSpecificError(
            encryptStatus, DrmErrors::ERROR_DRM_SESSION_NOT_OPENED));

    const Status signStatus = drmPlugin->sign(sessionId, keyId, input, &output);
    UT_ASSERT_TRUE(hasServiceSpecificError(
            signStatus, DrmErrors::ERROR_DRM_SESSION_NOT_OPENED));

    bool verified = false;
    const Status verifyStatus =
            drmPlugin->verify(sessionId, keyId, input, input, &verified);
    UT_ASSERT_TRUE(hasServiceSpecificError(
            verifyStatus, DrmErrors::ERROR_DRM_SESSION_NOT_OPENED));
}

/**
 * @brief Ensures the reported HDCP levels are valid values from the DRM AIDL enum.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 010@n
 * **Priority:** Medium@n
 * **Pre-Conditions:** DRM factory service is available@n
 * **Dependencies:** Supported crypto scheme@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Query connected and maximum HDCP levels|None|Call succeeds and both enum values are valid|Should Pass|
 */
UT_ADD_TEST(VTS_L1_Drm, hdcpLevels_pos) {
    Uuid uuid;
    UT_ASSERT_TRUE(vts_drm_getSupportedUuid(&uuid).isOk());

    sp<IDrmPlugin> drmPlugin;
    UT_ASSERT_TRUE(gDrmFactory->createDrmPlugin(
            uuid, String16("com.rdk.vts.drm"), &drmPlugin).isOk());

    HdcpLevels levels;
    UT_ASSERT_TRUE(drmPlugin->getHdcpLevels(&levels).isOk());
    UT_ASSERT_TRUE(isValidHdcpLevel(levels.connectedLevel));
    UT_ASSERT_TRUE(isValidHdcpLevel(levels.maxLevel));
}

/**
 * @brief Verifies session counts increase on open and remain within the reported maximum.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 011@n
 * **Priority:** High@n
 * **Pre-Conditions:** DRM factory service is available@n
 * **Dependencies:** At least one DRM session can be opened@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Read current and maximum session counts|None|Counts are non-negative and current does not exceed maximum|Should Pass|
 * |02|Open one session and query counts again|Session ID|Current count increases by one and maximum is unchanged|Should Pass|
 * |03|Close the session|Session ID|Call succeeds|Should Pass|
 */
UT_ADD_TEST(VTS_L1_Drm, numberOfSessions_pos) {
    Uuid uuid;
    UT_ASSERT_TRUE(vts_drm_getSupportedUuid(&uuid).isOk());

    sp<IDrmPlugin> drmPlugin;
    UT_ASSERT_TRUE(gDrmFactory->createDrmPlugin(
            uuid, String16("com.rdk.vts.drm"), &drmPlugin).isOk());

    NumberOfSessions before;
    UT_ASSERT_TRUE(drmPlugin->getNumberOfSessions(&before).isOk());
    UT_ASSERT_TRUE(before.currentSessions >= 0);
    UT_ASSERT_TRUE(before.maxSessions > 0);
    UT_ASSERT_TRUE(before.currentSessions <= before.maxSessions);

    std::vector<uint8_t> sessionId;
    UT_ASSERT_TRUE(drmPlugin->openSession(SecurityLevel::DEFAULT, &sessionId).isOk());

    NumberOfSessions during;
    UT_ASSERT_TRUE(drmPlugin->getNumberOfSessions(&during).isOk());
    UT_ASSERT_EQUAL(during.currentSessions, before.currentSessions + 1);
    UT_ASSERT_EQUAL(during.maxSessions, before.maxSessions);
    UT_ASSERT_TRUE(drmPlugin->closeSession(sessionId).isOk());
}

/**
 * @brief Verifies required DRM string properties are readable and non-empty.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 012@n
 * **Priority:** Medium@n
 * **Pre-Conditions:** DRM factory service is available@n
 * **Dependencies:** Supported crypto scheme@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Read vendor, version, and description properties|Required property names|Each call succeeds and returns a non-empty value|Should Pass|
 */
UT_ADD_TEST(VTS_L1_Drm, requiredProperties_pos) {
    Uuid uuid;
    UT_ASSERT_TRUE(vts_drm_getSupportedUuid(&uuid).isOk());

    sp<IDrmPlugin> drmPlugin;
    UT_ASSERT_TRUE(gDrmFactory->createDrmPlugin(
            uuid, String16("com.rdk.vts.drm"), &drmPlugin).isOk());

    const std::array<const char*, 3> properties = {"vendor", "version", "description"};
    for (const char* property : properties) {
        String16 value;
        UT_ASSERT_TRUE(drmPlugin->getPropertyString(String16(property), &value).isOk());
        UT_ASSERT_TRUE(value.size() > 0);
    }
}

/**
 * @brief Checks that all property accessors reject an empty property name.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 013@n
 * **Priority:** High@n
 * **Pre-Conditions:** DRM factory service is available@n
 * **Dependencies:** Supported crypto scheme@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Call string and byte-array getters with an empty name|Empty string|Both calls return BAD_VALUE / EX_ILLEGAL_ARGUMENT|Should Pass|
 * |02|Call string and byte-array setters with an empty name|Empty string, disposable values|Both calls return BAD_VALUE / EX_ILLEGAL_ARGUMENT|No property is changed|
 */
UT_ADD_TEST(VTS_L1_Drm, invalidPropertyNames_neg) {
    Uuid uuid;
    UT_ASSERT_TRUE(vts_drm_getSupportedUuid(&uuid).isOk());

    sp<IDrmPlugin> drmPlugin;
    UT_ASSERT_TRUE(gDrmFactory->createDrmPlugin(
            uuid, String16("com.rdk.vts.drm"), &drmPlugin).isOk());

    const String16 invalidName;
    String16 stringValue;
    std::vector<uint8_t> byteValue;

    const Status getStringStatus = drmPlugin->getPropertyString(invalidName, &stringValue);
    UT_ASSERT_TRUE(hasBinderException(
            getStringStatus, android::binder::Status::EX_ILLEGAL_ARGUMENT));

    const Status getByteArrayStatus =
            drmPlugin->getPropertyByteArray(invalidName, &byteValue);
    UT_ASSERT_TRUE(hasBinderException(
            getByteArrayStatus, android::binder::Status::EX_ILLEGAL_ARGUMENT));

    const Status setStringStatus =
            drmPlugin->setPropertyString(invalidName, String16("unused"));
    UT_ASSERT_TRUE(hasBinderException(
            setStringStatus, android::binder::Status::EX_ILLEGAL_ARGUMENT));

    const Status setByteArrayStatus =
            drmPlugin->setPropertyByteArray(invalidName, {});
    UT_ASSERT_TRUE(hasBinderException(
            setByteArrayStatus, android::binder::Status::EX_ILLEGAL_ARGUMENT));
}

/**
 * @brief Validates the documented outcomes of querying plugin-specific metrics.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 014@n
 * **Priority:** Medium@n
 * **Pre-Conditions:** DRM factory service is available@n
 * **Dependencies:** Supported crypto scheme@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Call getMetrics on a new DRM plugin|None|Succeeds or returns documented ERROR_DRM_INVALID_STATE|Metric fields are plugin-defined|
 */
UT_ADD_TEST(VTS_L1_Drm, metricsQuery_pos) {
    Uuid uuid;
    UT_ASSERT_TRUE(vts_drm_getSupportedUuid(&uuid).isOk());

    sp<IDrmPlugin> drmPlugin;
    UT_ASSERT_TRUE(gDrmFactory->createDrmPlugin(
            uuid, String16("com.rdk.vts.drm"), &drmPlugin).isOk());

    std::vector<com::rdk::hal::drm::DrmMetricGroup> metrics;
    const Status status = drmPlugin->getMetrics(&metrics);
    UT_ASSERT_TRUE(status.isOk() ||
            hasServiceSpecificError(status, DrmErrors::ERROR_DRM_INVALID_STATE));
}

/**
 * @brief Checks DRM and crypto secure-decoder query APIs return successfully.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 015@n
 * **Priority:** Medium@n
 * **Pre-Conditions:** DRM factory service is available@n
 * **Dependencies:** Supported DRM and crypto plugins@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Query whether DRM requires a secure decoder|video/mp4, DEFAULT security level|Call succeeds and returns a boolean|Result is scheme and level dependent|
 * |02|Query whether crypto requires a secure decoder component|video/mp4|Call succeeds and returns a boolean|Result is scheme dependent|
 */
UT_ADD_TEST(VTS_L1_Drm, secureDecoderQueries_pos) {
    Uuid uuid;
    UT_ASSERT_TRUE(vts_drm_getSupportedUuid(&uuid).isOk());

    sp<IDrmPlugin> drmPlugin;
    sp<ICryptoPlugin> cryptoPlugin;
    UT_ASSERT_TRUE(vts_drm_createPlugins(uuid, &drmPlugin, &cryptoPlugin).isOk());

    bool drmRequiresSecureDecoder = false;
    bool cryptoRequiresSecureDecoder = false;
    UT_ASSERT_TRUE(drmPlugin->requiresSecureDecoder(
            String16("video/mp4"),
            SecurityLevel::DEFAULT,
            &drmRequiresSecureDecoder).isOk());
    UT_ASSERT_TRUE(cryptoPlugin->requiresSecureDecoderComponent(
            String16("video/mp4"),
            &cryptoRequiresSecureDecoder).isOk());
}

/** @} */ // End of DRM_HALTEST_L1
/** @} */ // End of DRM_HALTEST
/** @} */ // End of DRM
/** @} */ // End of HPK
