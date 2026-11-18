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
 * @addtogroup DRM DRM Module
 * @{
 * @addtogroup DRM_HALTEST DRM HAL Tests
 * @{
 * @defgroup DRM_HALTEST_L1 DRM HAL Tests L1 File
 * @{
 *
 * @file vts_drm_l1_tests.cpp
 * @brief Per-API tests for DRM 0.2.0.0 from rdk-halif-aidl 0.23.0.
 *
 * Each negative case groups the documented invalid-input and closed-session
 * variations for one API. License-dependent success paths remain in L2/L3.
 * The existing optional provisioning exchange is retained.
 * Session-dependent cases repeat at each unique level in the advertised
 * inclusive security ranges, closing each session before the next level.
 */

#include "vts_drm.h"
#include "vts_drm_license.h"
#include <utils/Errors.h>

#include <com/rdk/hal/drm/BnDrmPluginListener.h>
#include <com/rdk/hal/drm/DrmMetricGroup.h>
#include <com/rdk/hal/drm/HdcpLevel.h>
#include <com/rdk/hal/drm/HdcpLevels.h>
#include <com/rdk/hal/drm/KeyRequest.h>
#include <com/rdk/hal/drm/KeySetId.h>
#include <com/rdk/hal/drm/KeyStatus.h>
#include <com/rdk/hal/drm/KeyType.h>
#include <com/rdk/hal/drm/KeyValue.h>
#include <com/rdk/hal/drm/NumberOfSessions.h>
#include <com/rdk/hal/drm/ProvideProvisionResponseResult.h>
#include <com/rdk/hal/drm/ProvisionRequest.h>
#include <com/rdk/hal/drm/SecurityLevel.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using com::rdk::hal::drm::BnDrmPluginListener;
using com::rdk::hal::drm::CryptoSchemes;
using com::rdk::hal::drm::DrmErrors;
using com::rdk::hal::drm::EventType;
using com::rdk::hal::drm::HdcpLevel;
using com::rdk::hal::drm::HdcpLevels;
using com::rdk::hal::drm::KeyRequest;
using com::rdk::hal::drm::KeySetId;
using com::rdk::hal::drm::KeyStatus;
using com::rdk::hal::drm::KeyType;
using com::rdk::hal::drm::KeyValue;
using com::rdk::hal::drm::NumberOfSessions;
using com::rdk::hal::drm::SecurityLevel;

UT_ADD_TEST_TO_GROUP(VTS_L1_Drm, UT_TESTS_L1)

namespace {

using Bytes = std::vector<uint8_t>;

bool hasServiceSpecificError(const Status& status, int32_t error) {
    return !status.isOk() &&
            status.exceptionCode() == Status::EX_SERVICE_SPECIFIC &&
            status.serviceSpecificErrorCode() == error;
}

bool hasServiceSpecificError(const Status& status, DrmErrors error) {
    return hasServiceSpecificError(status, static_cast<int32_t>(error));
}

class DrmTestListener final : public BnDrmPluginListener {
public:
    Status onEvent(EventType, const Bytes&, const Bytes&) override {
        return Status::ok();
    }

    Status onExpirationUpdate(const Bytes&, int64_t) override {
        return Status::ok();
    }

    Status onKeysChange(const Bytes&, const std::vector<KeyStatus>&, bool) override {
        return Status::ok();
    }

    Status onSessionLostState(const Bytes&) override {
        return Status::ok();
    }
};

class VTS_L1_Drm : public UTCore {
public:
    void SetUp() override {
        UT_ASSERT_TRUE_FATAL(gDrmFactory != nullptr) << "DRM factory service not available";
    }

    void TearDown() override {
        if (mSessionOpen) {
            const Status status = closeTestSession();
            UT_ASSERT_TRUE(status.isOk())
                    << "Failed to clean up DRM session: " << status.toString8().c_str();
        }
    }

protected:
    Status closeTestSession() {
        const Status status = mPlugin->closeSession(mSession);
        UT_LOG_INFO("closeSession: %s", status.toString8().c_str());
        if (status.isOk()) {
            mSessionOpen = false;
        }
        return status;
    }

    bool loadKeyRequestFixture() {
        const char* path = vts_drm_config_value("DRM_L1_INIT_DATA");
        const char* mime = vts_drm_config_value("DRM_L1_MIME_TYPE");
        if (path == nullptr || path[0] == '\0' || mime == nullptr || mime[0] == '\0') {
            UT_LOG_ERROR("DRM L1 key-request init data and MIME must be configured");
            return false;
        }
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            UT_LOG_ERROR("Unable to read DRM L1 init data: %s", path);
            return false;
        }
        mInitData.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        if (input.bad() || mInitData.empty()) {
            UT_LOG_ERROR("DRM L1 init data is empty or could not be read completely: %s", path);
            return false;
        }
        mMime = mime;
        return true;
    }

    sp<IDrmPlugin> mPlugin;
    sp<ICryptoPlugin> mCrypto;
    Bytes mSession;
    bool mSessionOpen = false;
    Bytes mInitData;
    std::string mMime;
};

std::array<Uuid, 3> unsupportedUuids() {
    std::array<Uuid, 3> uuids{};
    uuids[1].uuid = {
            0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80,
            0x90, 0xa0, 0xb0, 0xc0, 0xd0, 0xe0, 0xf0, 0x01};
    uuids[2].uuid.fill(0xff);
    return uuids;
}

bool isResolvedSecurityLevel(SecurityLevel level) {
    return level >= SecurityLevel::SW_SECURE_CRYPTO &&
            level <= SecurityLevel::HW_SECURE_ALL;
}

bool isValidHdcpLevel(HdcpLevel level) {
    return level >= HdcpLevel::HDCP_UNKNOWN && level <= HdcpLevel::HDCP_V2_3;
}

bool containsAlgorithm(const std::string& algorithms, const char* expected) {
    std::istringstream input(algorithms);
    std::string algorithm;
    while (std::getline(input, algorithm, ',')) {
        const auto first = algorithm.find_first_not_of(" \t\r\n");
        if (first != std::string::npos &&
                algorithm.substr(first, algorithm.find_last_not_of(" \t\r\n") - first + 1) ==
                        expected) {
            return true;
        }
    }
    return false;
}

}  // namespace

#define DRM_REQUIRE_ALGORITHM(algorithm) \
    do { \
        std::string algorithms; \
        const Status status = mPlugin->getPropertyString("algorithms", &algorithms); \
        if (hasServiceSpecificError(status, DrmErrors::ERROR_DRM_CANNOT_HANDLE)) { \
            UT_LOG_INFO("SKIP: provider does not advertise operator algorithms"); \
            UT_IGNORE_TEST(); \
        } \
        DRM_STATUS_SUCCESS(status); \
        if (!containsAlgorithm(algorithms, algorithm)) { \
            UT_LOG_INFO("SKIP: provider does not advertise %s", algorithm); \
            UT_IGNORE_TEST(); \
        } \
    } while (0)

#define DRM_REQUIRE_KEY_REQUEST_FIXTURE() \
    do { \
        const char* path = vts_drm_config_value("DRM_L1_INIT_DATA"); \
        if (path == nullptr || path[0] == '\0') { \
            UT_LOG_INFO("SKIP: configure drm.l1.init_data_file for key-request parameter tests"); \
            UT_IGNORE_TEST(); \
        } \
        UT_ASSERT_TRUE_FATAL(loadKeyRequestFixture()); \
    } while (0)

/**
 * @brief Checks generated factory interface version and hash.
 *
 * This test verifies that the registered factory matches the pinned DRM AIDL metadata.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 001@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** Configured DRM factory is registered@n
 * **Dependencies:** DRM 0.2.0.0 generated headers and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Read the factory interface version | IDrmFactory::VERSION | Reported version matches the generated version | Should Pass |
 * | 02 | Read the factory interface hash | IDrmFactory::HASHVALUE | Reported hash matches the generated hash | Should Pass |
 */
UT_ADD_TEST(VTS_L1_Drm, interfaceMetadata_pos) {
    UT_ASSERT_TRUE(gDrmFactory->getInterfaceVersion() == IDrmFactory::VERSION);
    UT_ASSERT_TRUE(gDrmFactory->getInterfaceHash() == IDrmFactory::HASHVALUE);
}

/**
 * @brief Validates advertised UUIDs, content types, and security ranges against the HFP.
 *
 * This test compares the advertised UUIDs and MIME types with the HFP, checks
 * scheme uniqueness, and validates every advertised content-type security range.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 002@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** Configured DRM factory is registered@n
 * **Dependencies:** DRM 0.2.0.0 generated headers and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Read UUID and container MIME expectations from the HFP | drm.SupportedCryptoSchemes | HFP values are present and valid | Should Pass |
 * | 02 | Retrieve supported crypto schemes from the HAL | OUT: CryptoSchemes | Status is OK with EX_NONE; UUID list is non-empty | Should Pass |
 * | 03 | Compare advertised UUIDs with the HFP UUID list | CryptoSchemes.uuids | UUID sets match and contain no duplicates | Should Pass |
 * | 04 | Compare advertised MIME types with the HFP container MIME list | CryptoSchemes.mimeTypes, supportedContainerMimeTypes | MIME sets match | Should Pass |
 * | 05 | Validate each content-type security range | mime, minLevel, maxLevel | MIME is non-empty; levels are resolved and minLevel does not exceed maxLevel | Should Pass |
 */
UT_ADD_TEST(VTS_L1_Drm, getSupportedCryptoSchemes_pos) {
    const DrmHfpCryptoSchemes& hfp = gDrmHfpCryptoSchemes;
    std::set<Uuid> expectedUuids;
    for (const auto& uuidBytes : hfp.uuids) {
        Uuid uuid;
        uuid.uuid = uuidBytes;
        expectedUuids.insert(uuid);
    }
    const std::set<std::string> expectedMimeTypes(
            hfp.supportedContainerMimeTypes.begin(),
            hfp.supportedContainerMimeTypes.end());

    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    const std::set<Uuid> uniqueUuids(schemes.uuids.begin(), schemes.uuids.end());
    UT_ASSERT_EQUAL(uniqueUuids.size(), schemes.uuids.size());
    UT_ASSERT_EQUAL(uniqueUuids.size(), expectedUuids.size())
            << "HAL UUID count does not match HFP";
    for (const auto& uuid : expectedUuids) {
        UT_ASSERT_TRUE(uniqueUuids.find(uuid) != uniqueUuids.end())
                << "HAL did not advertise a UUID listed in the HFP";
    }

    std::set<std::string> advertisedMimeTypes;
    for (const auto& contentType : schemes.mimeTypes) {
        UT_ASSERT_FALSE(contentType.mime.empty());
        UT_ASSERT_TRUE(contentType.minLevel <= contentType.maxLevel);
        UT_ASSERT_TRUE(isResolvedSecurityLevel(contentType.minLevel));
        UT_ASSERT_TRUE(isResolvedSecurityLevel(contentType.maxLevel));
        advertisedMimeTypes.insert(contentType.mime);
    }
    UT_ASSERT_EQUAL(advertisedMimeTypes.size(), expectedMimeTypes.size())
            << "HAL MIME type count does not match HFP";
    for (const auto& mime : expectedMimeTypes) {
        UT_ASSERT_TRUE(advertisedMimeTypes.find(mime) != advertisedMimeTypes.end())
                << "HAL did not advertise MIME type listed in the HFP: " << mime;
    }
}

/**
 * @brief Creates a DRM plugin for each advertised UUID.
 *
 * This test verifies plugin creation independently of crypto-plugin creation.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 003@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** Configured DRM factory is registered@n
 * **Dependencies:** Advertised DRM UUIDs and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Retrieve supported scheme UUIDs | OUT: CryptoSchemes | Status is OK; UUID list is non-empty | Should Pass |
 * | 02 | Create a DRM plugin for each UUID and log status/output | IN: advertised UUID, DRM_APP_PACKAGE_NAME; OUT: plugin | Status is OK with EX_NONE; plugin is non-null | Should Pass |
 * | 03 | Release each local plugin handle | Returned plugin | Handle leaves scope before the next iteration | No session is opened |
 */
UT_ADD_TEST(VTS_L1_Drm, createDrmPlugin_pos) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    for (const auto& uuid : schemes.uuids) {
        sp<IDrmPlugin> plugin;
        DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(uuid, DRM_APP_PACKAGE_NAME, plugin);
    }
}

/**
 * @brief Groups zero, unadvertised, and all-ones UUID variations.
 *
 * This test checks the documented DRM creation error without assuming package-name restrictions.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 004@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** Configured DRM factory is registered@n
 * **Dependencies:** Supported-scheme query and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Retrieve advertised UUIDs | OUT: CryptoSchemes | Status is OK with EX_NONE | Should Pass |
 * | 02 | Verify each negative-test UUID is absent from the advertised list | Zero, fixed unadvertised, and all-ones UUIDs | Every candidate is unadvertised | Fatal if a candidate is advertised |
 * | 03 | Request a DRM plugin for each candidate and log status/output | IN: candidate UUID, DRM_APP_PACKAGE_NAME; OUT: plugin | EX_SERVICE_SPECIFIC with ERROR_DRM_CANNOT_HANDLE; plugin is null | Expected API rejection |
 */
UT_ADD_TEST(VTS_L1_Drm, createDrmPlugin_neg) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    for (const auto& uuid : unsupportedUuids()) {
        UT_ASSERT_TRUE_FATAL(
                std::find(schemes.uuids.begin(), schemes.uuids.end(), uuid) == schemes.uuids.end())
                << "Negative-test UUID must not be advertised";
        UT_LOG_INFO("Unsupported DRM UUID: %s", uuid.toString().c_str());
        sp<IDrmPlugin> plugin;
        DRM_FACTORY_CREATE_DRM_PLUGIN_FAILURE(uuid, DRM_APP_PACKAGE_NAME, plugin);
    }
}

/**
 * @brief Creates crypto plugins with empty initialization data and a live session.
 *
 * This test covers both initialization forms for every advertised scheme.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 005@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** Configured factory is registered and sessions can be opened@n
 * **Dependencies:** Advertised UUIDs, DRM/crypto plugins, and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Retrieve advertised UUIDs | OUT: CryptoSchemes | Status is OK; UUID list is non-empty | Should Pass |
 * | 02 | Create a DRM plugin and open a session for each UUID | Advertised UUID, every unique advertised level | Non-null DRM plugin and non-empty session ID | Should Pass |
 * | 03 | Create a crypto plugin using the session ID | IN: same UUID, live session ID; OUT: plugin | Status is OK with EX_NONE; plugin is non-null | Should Pass |
 * | 04 | Release the crypto plugin, close the session, and release the DRM plugin | Live plugin handles and session ID | Close succeeds; teardown also handles assertion failures | Cleanup |
 * | 05 | Create a fresh DRM plugin without opening a session | Same UUID | Status is OK with EX_NONE; plugin is non-null | Should Pass |
 * | 06 | Create a crypto plugin with zero-length initialization data | IN: same UUID, empty byte array; OUT: plugin | Status is OK with EX_NONE; plugin is non-null | No session ID is available |
 * | 07 | Release the plugin handles before the next UUID | Returned plugins | Handles are released | Cleanup |
 */
UT_ADD_TEST(VTS_L1_Drm, createCryptoPlugin_pos) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    std::set<SecurityLevel> levels;
    UT_ASSERT_TRUE_FATAL(vts_drm_collect_security_levels(schemes, levels));
    for (const auto& uuid : schemes.uuids) {
        mPlugin.clear();
        DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(uuid, DRM_APP_PACKAGE_NAME, mPlugin);
        for (const auto requested : levels) {
            mSession.clear();
            DRM_PLUGIN_OPEN_SESSION_SUCCESS(mPlugin, requested, mSession);
            mSessionOpen = true;
            UT_ASSERT_FALSE(mSession.empty());
            sp<ICryptoPlugin> plugin;
            DRM_FACTORY_CREATE_CRYPTO_PLUGIN_SUCCESS(uuid, mSession, plugin);
            plugin.clear();
            DRM_PLUGIN_CLOSE_SESSION_SUCCESS(mPlugin, mSession);
            mSessionOpen = false;
        }
        mPlugin.clear();

        DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(uuid, DRM_APP_PACKAGE_NAME, mPlugin);
        sp<ICryptoPlugin> plugin;
        DRM_FACTORY_CREATE_CRYPTO_PLUGIN_SUCCESS(uuid, {}, plugin);
        plugin.clear();
        mPlugin.clear();
    }
}

/**
 * @brief Groups zero, unadvertised, and all-ones UUID variations.
 *
 * This test validates nullable crypto-plugin failure without inventing a service error code.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 006@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** Configured DRM factory is registered@n
 * **Dependencies:** Supported-scheme query and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Retrieve advertised UUIDs | OUT: CryptoSchemes | Status is OK with EX_NONE | Should Pass |
 * | 02 | Verify each candidate is not advertised | Zero, fixed unadvertised, and all-ones UUIDs | Every candidate is unadvertised | Fatal if a candidate is advertised |
 * | 03 | Request a crypto plugin and log status/output for each candidate | IN: candidate UUID, empty init data; OUT: plugin | Plugin is null; status is OK or EX_SERVICE_SPECIFIC | No exact service error is specified; transport errors fail |
 */
UT_ADD_TEST(VTS_L1_Drm, createCryptoPlugin_neg) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    for (const auto& uuid : unsupportedUuids()) {
        UT_ASSERT_TRUE_FATAL(
                std::find(schemes.uuids.begin(), schemes.uuids.end(), uuid) == schemes.uuids.end());
        sp<ICryptoPlugin> plugin;
        UT_LOG_INFO("Unsupported DRM UUID: %s", uuid.toString().c_str());
        DRM_FACTORY_CREATE_CRYPTO_PLUGIN_FAILURE(uuid, {}, plugin);
    }
}

/**
 * @brief Opens sessions at every level in the advertised security ranges.
 *
 * This test checks session IDs and the resolved security level for each requested level.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 007@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** Configured factory is registered and the DRM scheme is ready@n
 * **Dependencies:** DRM plugin, advertised security ranges, and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Query supported schemes and create a DRM plugin for the first advertised UUID | OUT: CryptoSchemes; IN: advertised UUID | UUID list is non-empty and plugin creation succeeds | Should Pass |
 * | 02 | Collect unique requested security levels | Every level from each minLevel through maxLevel | Overlapping ranges are deduplicated; missing or invalid ranges fail | No synthetic DEFAULT request |
 * | 03 | Open a session for each level and log the result | IN: requested level; OUT: session ID | Status is OK; session ID is non-empty | Should Pass |
 * | 04 | Query and validate the session security level | Live session ID | EX_NONE and resolved level no higher than requested | Should Pass |
 * | 05 | Close the session before testing the next level | Live session ID | Close succeeds | Teardown closes a session if an assertion fails |
 */
UT_ADD_TEST(VTS_L1_Drm, openSession_pos) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);

    std::set<SecurityLevel> levels;
    UT_ASSERT_TRUE_FATAL(vts_drm_collect_security_levels(schemes, levels));
    for (const auto requested : levels) {
        mSession.clear();
        DRM_PLUGIN_OPEN_SESSION_SUCCESS(mPlugin, requested, mSession);
        mSessionOpen = true;
        UT_ASSERT_FALSE(mSession.empty());

        DRM_PLUGIN_CLOSE_SESSION_SUCCESS(mPlugin, mSession);
        mSessionOpen = false;
    }
}

/**
 * @brief Closes sessions opened at every level in the advertised security ranges.
 *
 * This test verifies successful closure independently of the negative close variations.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 008@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** Configured DRM scheme is ready to open a session@n
 * **Dependencies:** DRM plugin and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Query supported schemes and create a DRM plugin | First advertised UUID | UUID list is non-empty and plugin creation succeeds | Should Pass |
 * | 02 | Collect unique supported security levels | Every level from each minLevel through maxLevel | Level list is non-empty; missing or invalid ranges fail | Overlapping ranges are deduplicated |
 * | 03 | Open a session for each level | IN: advertised level; OUT: session ID | Status is OK and session ID is non-empty | One session at a time |
 * | 04 | Close each session before the next level and log status | IN: live session ID | Status is OK with EX_NONE | Teardown closes a session if an assertion fails |
 */
UT_ADD_TEST(VTS_L1_Drm, closeSession_pos) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);
    std::set<SecurityLevel> levels;
    UT_ASSERT_TRUE_FATAL(vts_drm_collect_security_levels(schemes, levels));
    for (const auto requested : levels) {
        mSession.clear();
        DRM_PLUGIN_OPEN_SESSION_SUCCESS(mPlugin, requested, mSession);
        mSessionOpen = true;
        UT_ASSERT_FALSE(mSession.empty());
        DRM_PLUGIN_CLOSE_SESSION_SUCCESS(mPlugin, mSession);
        mSessionOpen = false;
    }
}

/**
 * @brief Groups empty-session and already-closed-session variations.
 *
 * This test distinguishes invalid session data from a session that was successfully closed.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 009@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** Configured DRM scheme can open and close a session@n
 * **Dependencies:** DRM plugin and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Create a DRM plugin and open a session | every unique advertised level | Setup succeeds | Should Pass |
 * | 02 | Call closeSession with an empty ID | Empty byte array | EX_SERVICE_SPECIFIC with BAD_VALUE | Expected API rejection |
 * | 03 | Close the real session | Live session ID | Status is OK with EX_NONE | Prepare closed-session variation |
 * | 04 | Close the same session again | Closed non-empty ID | EX_SERVICE_SPECIFIC with ERROR_DRM_SESSION_NOT_OPENED | Expected API rejection |
 */
UT_ADD_TEST(VTS_L1_Drm, closeSession_neg) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);
    std::set<SecurityLevel> levels;
    UT_ASSERT_TRUE_FATAL(vts_drm_collect_security_levels(schemes, levels));
    for (const auto requested : levels) {
        mSession.clear();
        DRM_PLUGIN_OPEN_SESSION_SUCCESS(mPlugin, requested, mSession);
        mSessionOpen = true;
        UT_ASSERT_FALSE(mSession.empty());
        DRM_CALL_FAILURE(mPlugin->closeSession({}), android::BAD_VALUE);
        DRM_PLUGIN_CLOSE_SESSION_SUCCESS(mPlugin, mSession);
        mSessionOpen = false;
        DRM_CALL_FAILURE(mPlugin->closeSession(mSession), DrmErrors::ERROR_DRM_SESSION_NOT_OPENED);
    }
}

/**
 * @brief Gets the resolved security level of an open session.
 *
 * This test checks that a successful query returns an actual, resolved security level.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 010@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** Configured DRM scheme is ready to open a session@n
 * **Dependencies:** DRM plugin and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Create a DRM plugin and open a session | every unique advertised level | Non-null plugin and non-empty session ID | Should Pass |
 * | 02 | Query the security level and log status | IN: live session ID; OUT: level initially UNKNOWN | Status is OK with EX_NONE | Should Pass |
 * | 03 | Validate the returned level | SecurityLevel output | Level is between SW_SECURE_CRYPTO and HW_SECURE_ALL | Should Pass |
 * | 04 | Close the session | Live session ID | Cleanup succeeds | Per-level close; teardown on failure |
 */
UT_ADD_TEST(VTS_L1_Drm, getSecurityLevel_pos) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);
    std::set<SecurityLevel> levels;
    UT_ASSERT_TRUE_FATAL(vts_drm_collect_security_levels(schemes, levels));
    for (const auto requested : levels) {
        mSession.clear();
        DRM_PLUGIN_OPEN_SESSION_SUCCESS(mPlugin, requested, mSession);
        mSessionOpen = true;
        UT_ASSERT_FALSE(mSession.empty());
        SecurityLevel level = SecurityLevel::UNKNOWN;
        DRM_PLUGIN_GET_SECURITY_LEVEL_SUCCESS(mPlugin, mSession, level);
        UT_ASSERT_TRUE(isResolvedSecurityLevel(level));
        DRM_PLUGIN_CLOSE_SESSION_SUCCESS(mPlugin, mSession);
        mSessionOpen = false;
    }
}

/**
 * @brief Groups empty and closed IDs without inspecting failed-call outputs.
 *
 * This test checks only the documented status when the queried session is invalid.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 011@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** Configured DRM scheme can open and close a session@n
 * **Dependencies:** DRM plugin and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Create a DRM plugin and open a session | every unique advertised level | Setup succeeds | Should Pass |
 * | 02 | Query security level with an empty ID | Empty byte array | EX_SERVICE_SPECIFIC with BAD_VALUE | Ignore output on failure |
 * | 03 | Close the real session | Live session ID | Status is OK with EX_NONE | Prepare closed-session variation |
 * | 04 | Query security level using the closed ID | Closed non-empty ID | EX_SERVICE_SPECIFIC with ERROR_DRM_SESSION_NOT_OPENED | Ignore output on failure |
 */
UT_ADD_TEST(VTS_L1_Drm, getSecurityLevel_neg) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);
    std::set<SecurityLevel> levels;
    UT_ASSERT_TRUE_FATAL(vts_drm_collect_security_levels(schemes, levels));
    for (const auto requested : levels) {
        mSession.clear();
        DRM_PLUGIN_OPEN_SESSION_SUCCESS(mPlugin, requested, mSession);
        mSessionOpen = true;
        UT_ASSERT_FALSE(mSession.empty());
        SecurityLevel level = SecurityLevel::UNKNOWN;
        DRM_CALL_FAILURE(mPlugin->getSecurityLevel({}, &level), android::BAD_VALUE);
        DRM_PLUGIN_CLOSE_SESSION_SUCCESS(mPlugin, mSession);
        mSessionOpen = false;
        DRM_CALL_FAILURE(mPlugin->getSecurityLevel(mSession, &level),
                DrmErrors::ERROR_DRM_SESSION_NOT_OPENED);
    }
}

/**
 * @brief Queries connected and maximum HDCP levels.
 *
 * This test validates enum values without making output-protection enforcement claims.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 012@n
 * **Priority:** Medium@n
 * @n
 * **Pre-Conditions:** Configured DRM factory is registered@n
 * **Dependencies:** DRM plugin, HdcpLevels parcelable, and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Create a DRM plugin | Advertised UUID | Plugin creation succeeds | Should Pass |
 * | 02 | Query HDCP levels and log status | OUT: HdcpLevels | Status is OK with EX_NONE | Should Pass |
 * | 03 | Validate connected and maximum levels | connectedLevel, maxLevel | Both values are within the defined HdcpLevel range | No HDCP policy enforcement is tested |
 */
UT_ADD_TEST(VTS_L1_Drm, getHdcpLevels_pos) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);
    HdcpLevels levels;
    DRM_PLUGIN_GET_HDCP_LEVELS_SUCCESS(mPlugin, levels);
    UT_ASSERT_TRUE(isValidHdcpLevel(levels.connectedLevel));
    UT_ASSERT_TRUE(isValidHdcpLevel(levels.maxLevel));
}

/**
 * @brief Queries session counts before opening, during, and after closing.
 *
 * This test verifies that opening and closing one session changes the reported count correctly.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 013@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** DRM scheme is ready; no concurrent session-count changes by other clients@n
 * **Dependencies:** DRM plugin, NumberOfSessions parcelable, and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Create a DRM plugin and read baseline counts | OUT: before | EX_NONE; current count is non-negative and no greater than positive maximum | Should Pass |
 * | 02 | Open one session | every unique advertised level | Status is OK; session ID is non-empty | Should Pass |
 * | 03 | Read counts while the session is open | OUT: during | Current count increases by one; maximum is unchanged | Should Pass |
 * | 04 | Close the session | Live session ID | Status is OK with EX_NONE | Should Pass |
 * | 05 | Read counts after closing | OUT: after | Current count matches the baseline | Should Pass |
 */
UT_ADD_TEST(VTS_L1_Drm, getNumberOfSessions_pos) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);
    std::set<SecurityLevel> levels;
    UT_ASSERT_TRUE_FATAL(vts_drm_collect_security_levels(schemes, levels));
    for (const auto requested : levels) {
        NumberOfSessions before;
        DRM_PLUGIN_GET_NUMBER_OF_SESSIONS_SUCCESS(mPlugin, before);
        UT_ASSERT_TRUE(before.currentSessions >= 0);
        UT_ASSERT_TRUE(before.maxSessions > 0);
        UT_ASSERT_TRUE(before.currentSessions <= before.maxSessions);
        mSession.clear();
        DRM_PLUGIN_OPEN_SESSION_SUCCESS(mPlugin, requested, mSession);
        mSessionOpen = true;
        UT_ASSERT_FALSE_FATAL(mSession.empty());
        NumberOfSessions during;
        DRM_PLUGIN_GET_NUMBER_OF_SESSIONS_SUCCESS(mPlugin, during);
        UT_ASSERT_EQUAL(during.currentSessions, before.currentSessions + 1);
        UT_ASSERT_EQUAL(during.maxSessions, before.maxSessions);
        DRM_PLUGIN_CLOSE_SESSION_SUCCESS(mPlugin, mSession);
        mSessionOpen = false;
        NumberOfSessions after;
        DRM_PLUGIN_GET_NUMBER_OF_SESSIONS_SUCCESS(mPlugin, after);
        UT_ASSERT_EQUAL(after.currentSessions, before.currentSessions);
    }
}

/**
 * @brief Reads metrics when available.
 *
 * This test reports unavailable metrics as skipped rather than treating an error as success.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 014@n
 * **Priority:** Medium@n
 * @n
 * **Pre-Conditions:** Configured DRM factory is registered@n
 * **Dependencies:** DRM plugin, DrmMetricGroup parcelable, and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Create a DRM plugin | Advertised UUID | Plugin creation succeeds | Should Pass |
 * | 02 | Request metrics and log status | OUT: metric groups | A status is returned | Metric contents are provider-defined |
 * | 03 | Handle documented unavailable metrics | EX_SERVICE_SPECIFIC with ERROR_DRM_INVALID_STATE | Test is explicitly skipped | No failed-call output is inspected |
 * | 04 | Otherwise verify successful retrieval | Returned status | Status is OK with EX_NONE | Any other error fails |
 */
UT_ADD_TEST(VTS_L1_Drm, getMetrics_pos) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);
    std::vector<com::rdk::hal::drm::DrmMetricGroup> metrics;
    const Status status = mPlugin->getMetrics(&metrics);
    UT_LOG_INFO("getMetrics: %s", status.toString8().c_str());
    if (hasServiceSpecificError(status, DrmErrors::ERROR_DRM_INVALID_STATE)) {
        UT_LOG_INFO("SKIP: provider reports metrics are unavailable");
        UT_IGNORE_TEST();
    }
    DRM_STATUS_SUCCESS(status);
}

/**
 * @brief Reads each required string property.
 *
 * This test verifies that the provider exposes non-empty identification properties.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 015@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** Configured DRM factory is registered@n
 * **Dependencies:** DRM plugin and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Create a DRM plugin | Advertised UUID | Plugin creation succeeds | Should Pass |
 * | 02 | Read each required string property and log the property name/status | vendor, version, description | Status is OK with EX_NONE for each getter | Should Pass |
 * | 03 | Validate each returned value | OUT: property string | Value is non-empty | Should Pass |
 */
UT_ADD_TEST(VTS_L1_Drm, getPropertyString_pos) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);
    for (const char* property : {"vendor", "version", "description"}) {
        UT_LOG_INFO("Reading string property: %s", property);
        std::string value;
        DRM_PLUGIN_GET_PROPERTY_STRING_SUCCESS(mPlugin, property, value);
        UT_ASSERT_FALSE(value.empty()) << property;
    }
}

/**
 * @brief Groups empty and unsupported property-name variations.
 *
 * This test distinguishes an empty property name from an unsupported named property.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 016@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** Configured DRM factory is registered@n
 * **Dependencies:** DRM plugin and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Create a DRM plugin | Advertised UUID | Plugin creation succeeds | Should Pass |
 * | 02 | Read a string property with an empty name | Empty string | EX_SERVICE_SPECIFIC with BAD_VALUE | Ignore output on failure |
 * | 03 | Read an unsupported string property | rdk.vts.unsupported.property | EX_SERVICE_SPECIFIC with ERROR_DRM_CANNOT_HANDLE | Ignore output on failure |
 */
UT_ADD_TEST(VTS_L1_Drm, getPropertyString_neg) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);
    std::string value;
    DRM_CALL_FAILURE(mPlugin->getPropertyString("", &value), android::BAD_VALUE);
    DRM_CALL_FAILURE(mPlugin->getPropertyString("rdk.vts.unsupported.property", &value),
            DrmErrors::ERROR_DRM_CANNOT_HANDLE);
}

/**
 * @brief Reads the provisioned device identifier when supported.
 *
 * This test checks the byte-array property without logging the identifier itself.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 017@n
 * **Priority:** Medium@n
 * @n
 * **Pre-Conditions:** Configured DRM factory is registered; device is provisioned where required@n
 * **Dependencies:** DRM plugin, deviceUniqueId property support, and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Create a DRM plugin | Advertised UUID | Plugin creation succeeds | Should Pass |
 * | 02 | Read the device identifier and log status | IN: deviceUniqueId; OUT: bytes | A status is returned | Identifier contents are not logged |
 * | 03 | Handle an unsupported property | EX_SERVICE_SPECIFIC with ERROR_DRM_CANNOT_HANDLE | Test is explicitly skipped | Optional capability |
 * | 04 | Otherwise validate status and returned bytes | Returned status and byte array | Status is OK with EX_NONE; value is non-empty | Should Pass |
 */
UT_ADD_TEST(VTS_L1_Drm, getPropertyByteArray_pos) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);
    Bytes value;
    const Status status = mPlugin->getPropertyByteArray("deviceUniqueId", &value);
    UT_LOG_INFO("getPropertyByteArray(deviceUniqueId): %s", status.toString8().c_str());
    if (hasServiceSpecificError(status, DrmErrors::ERROR_DRM_CANNOT_HANDLE)) {
        UT_LOG_INFO("SKIP: provider does not support deviceUniqueId");
        UT_IGNORE_TEST();
    }
    DRM_STATUS_SUCCESS(status);
    UT_ASSERT_FALSE(value.empty());
}

/**
 * @brief Groups empty and unsupported byte-array property names.
 *
 * This test checks documented errors without inspecting failed-call property values.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 018@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** Configured DRM factory is registered@n
 * **Dependencies:** DRM plugin and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Create a DRM plugin | Advertised UUID | Plugin creation succeeds | Should Pass |
 * | 02 | Read a byte-array property with an empty name | Empty string | EX_SERVICE_SPECIFIC with BAD_VALUE | Ignore output on failure |
 * | 03 | Read an unsupported byte-array property | rdk.vts.unsupported.property | EX_SERVICE_SPECIFIC with ERROR_DRM_CANNOT_HANDLE | Ignore output on failure |
 */
UT_ADD_TEST(VTS_L1_Drm, getPropertyByteArray_neg) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);
    Bytes value;
    DRM_CALL_FAILURE(mPlugin->getPropertyByteArray("", &value), android::BAD_VALUE);
    DRM_CALL_FAILURE(mPlugin->getPropertyByteArray("rdk.vts.unsupported.property", &value),
            DrmErrors::ERROR_DRM_CANNOT_HANDLE);
}

/**
 * @brief Groups empty and unsupported string-property writes.
 *
 * This test rejects invalid property names without changing a provider-specific writable property.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 019@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** Configured DRM factory is registered@n
 * **Dependencies:** DRM plugin and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Create a DRM plugin | Advertised UUID | Plugin creation succeeds | Should Pass |
 * | 02 | Set a string property using an empty name | Name: empty; value: vts | EX_SERVICE_SPECIFIC with BAD_VALUE | Expected API rejection |
 * | 03 | Set an unsupported string property | Name: rdk.vts.unsupported.property; value: vts | EX_SERVICE_SPECIFIC with ERROR_DRM_CANNOT_HANDLE | No known property is modified |
 */
UT_ADD_TEST(VTS_L1_Drm, setPropertyString_neg) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);
    DRM_CALL_FAILURE(mPlugin->setPropertyString("", "vts"), android::BAD_VALUE);
    DRM_CALL_FAILURE(mPlugin->setPropertyString("rdk.vts.unsupported.property", "vts"),
            DrmErrors::ERROR_DRM_CANNOT_HANDLE);
}

/**
 * @brief Groups empty and unsupported byte-array property writes.
 *
 * This test validates property names; it does not assume that empty property values are invalid.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 020@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** Configured DRM factory is registered@n
 * **Dependencies:** DRM plugin and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Create a DRM plugin | Advertised UUID | Plugin creation succeeds | Should Pass |
 * | 02 | Set a byte-array property using an empty name | Name: empty; value: one byte 0x01 | EX_SERVICE_SPECIFIC with BAD_VALUE | Expected API rejection |
 * | 03 | Set an unsupported byte-array property | Name: rdk.vts.unsupported.property; value: one byte 0x01 | EX_SERVICE_SPECIFIC with ERROR_DRM_CANNOT_HANDLE | No known property is modified |
 */
UT_ADD_TEST(VTS_L1_Drm, setPropertyByteArray_neg) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);
    const Bytes value{0x01};
    DRM_CALL_FAILURE(mPlugin->setPropertyByteArray("", value), android::BAD_VALUE);
    DRM_CALL_FAILURE(mPlugin->setPropertyByteArray("rdk.vts.unsupported.property", value),
            DrmErrors::ERROR_DRM_CANNOT_HANDLE);
}

/**
 * @brief Queries license status for an open session, without requiring keys.
 *
 * This test checks the query API before a license has been installed.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 021@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** Configured DRM scheme is ready to open a session@n
 * **Dependencies:** DRM plugin, KeyValue parcelable, and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Create a DRM plugin and open an unlicensed session | every unique advertised level | Non-null plugin and non-empty session ID | Should Pass |
 * | 02 | Query key status and log status | IN: live session ID; OUT: KeyValue list | Status is OK with EX_NONE | Empty list is allowed before licensing |
 * | 03 | Close the session | Live session ID | Cleanup succeeds | Per-level close; teardown on failure |
 */
UT_ADD_TEST(VTS_L1_Drm, queryKeyStatus_pos) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);
    std::set<SecurityLevel> levels;
    UT_ASSERT_TRUE_FATAL(vts_drm_collect_security_levels(schemes, levels));
    for (const auto requested : levels) {
        mSession.clear();
        DRM_PLUGIN_OPEN_SESSION_SUCCESS(mPlugin, requested, mSession);
        mSessionOpen = true;
        UT_ASSERT_FALSE_FATAL(mSession.empty());
        std::vector<KeyValue> status;
        DRM_PLUGIN_QUERY_KEY_STATUS_SUCCESS(mPlugin, mSession, status);
        DRM_PLUGIN_CLOSE_SESSION_SUCCESS(mPlugin, mSession);
        mSessionOpen = false;
    }
}

/**
 * @brief Groups empty and closed license-status scopes.
 *
 * This test verifies status errors independently of license contents.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 022@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** Configured DRM scheme can open and close a session@n
 * **Dependencies:** DRM plugin and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Create a DRM plugin and open a session | every unique advertised level | Setup succeeds | Should Pass |
 * | 02 | Query key status using an empty session ID | Empty byte array | EX_SERVICE_SPECIFIC with BAD_VALUE | Ignore output on failure |
 * | 03 | Close the real session | Live session ID | Status is OK with EX_NONE | Prepare closed-session variation |
 * | 04 | Query key status using the closed ID | Closed non-empty ID | EX_SERVICE_SPECIFIC with ERROR_DRM_SESSION_NOT_OPENED | Ignore output on failure |
 */
UT_ADD_TEST(VTS_L1_Drm, queryKeyStatus_neg) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);
    std::set<SecurityLevel> levels;
    UT_ASSERT_TRUE_FATAL(vts_drm_collect_security_levels(schemes, levels));
    for (const auto requested : levels) {
        mSession.clear();
        DRM_PLUGIN_OPEN_SESSION_SUCCESS(mPlugin, requested, mSession);
        mSessionOpen = true;
        UT_ASSERT_FALSE_FATAL(mSession.empty());
        std::vector<KeyValue> status;
        DRM_CALL_FAILURE(mPlugin->queryKeyStatus({}, &status), android::BAD_VALUE);
        DRM_PLUGIN_CLOSE_SESSION_SUCCESS(mPlugin, mSession);
        mSessionOpen = false;
        DRM_CALL_FAILURE(mPlugin->queryKeyStatus(mSession, &status),
                DrmErrors::ERROR_DRM_SESSION_NOT_OPENED);
    }
}

/**
 * @brief Removes keys from an open session without loading a license.
 *
 * This test exercises key removal on a valid unlicensed session.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 023@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** Configured DRM scheme is ready to open a session@n
 * **Dependencies:** DRM plugin and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Create a DRM plugin and open an unlicensed session | every unique advertised level | Setup succeeds | Should Pass |
 * | 02 | Remove keys and log status | IN: live session ID | Status is OK with EX_NONE | No license is loaded |
 * | 03 | Close the session | Live session ID | Cleanup succeeds | Per-level close; teardown on failure |
 */
UT_ADD_TEST(VTS_L1_Drm, removeKeys_pos) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);
    std::set<SecurityLevel> levels;
    UT_ASSERT_TRUE_FATAL(vts_drm_collect_security_levels(schemes, levels));
    for (const auto requested : levels) {
        mSession.clear();
        DRM_PLUGIN_OPEN_SESSION_SUCCESS(mPlugin, requested, mSession);
        mSessionOpen = true;
        UT_ASSERT_FALSE_FATAL(mSession.empty());
        DRM_PLUGIN_REMOVE_KEYS_SUCCESS(mPlugin, mSession);
        DRM_PLUGIN_CLOSE_SESSION_SUCCESS(mPlugin, mSession);
        mSessionOpen = false;
    }
}

/**
 * @brief Groups empty and closed key-removal session IDs.
 *
 * This test verifies that key removal rejects invalid session references.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 024@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** Configured DRM scheme can open and close a session@n
 * **Dependencies:** DRM plugin and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Create a DRM plugin and open a session | every unique advertised level | Setup succeeds | Should Pass |
 * | 02 | Remove keys using an empty ID | Empty byte array | EX_SERVICE_SPECIFIC with BAD_VALUE | Expected API rejection |
 * | 03 | Close the real session | Live session ID | Status is OK with EX_NONE | Prepare closed-session variation |
 * | 04 | Remove keys using the closed ID | Closed non-empty ID | EX_SERVICE_SPECIFIC with ERROR_DRM_SESSION_NOT_OPENED | Expected API rejection |
 */
UT_ADD_TEST(VTS_L1_Drm, removeKeys_neg) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);
    std::set<SecurityLevel> levels;
    UT_ASSERT_TRUE_FATAL(vts_drm_collect_security_levels(schemes, levels));
    for (const auto requested : levels) {
        mSession.clear();
        DRM_PLUGIN_OPEN_SESSION_SUCCESS(mPlugin, requested, mSession);
        mSessionOpen = true;
        UT_ASSERT_FALSE_FATAL(mSession.empty());
        DRM_CALL_FAILURE(mPlugin->removeKeys({}), android::BAD_VALUE);
        DRM_PLUGIN_CLOSE_SESSION_SUCCESS(mPlugin, mSession);
        mSessionOpen = false;
        DRM_CALL_FAILURE(mPlugin->removeKeys(mSession), DrmErrors::ERROR_DRM_SESSION_NOT_OPENED);
    }
}

/**
 * @brief Groups invalid key-request scopes, MIME types, and key-type enum values.
 *
 * This test first validates provider-specific init data, then changes one field at a time
 * so an init-data error cannot mask the parameter under test.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 025@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** DRM scheme is ready; provider-valid L1 init data and MIME are configured@n
 * **Dependencies:** DRM plugin, drm.l1.init_data_file, drm.l1.mime_type, and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Create a DRM plugin and open a session | every unique advertised level | Setup succeeds | Should Pass |
 * | 02 | Load configured init data and MIME | L1 file path and MIME | Non-empty readable data is loaded; absent file setting explicitly skips | Configured invalid input fails; teardown closes the session |
 * | 03 | Generate a baseline streaming request | Live ID, valid init data/MIME, STREAMING, empty optional parameters | EX_NONE and non-empty request | No license exchange |
 * | 04 | Request keys using an empty scope | Empty scope; other baseline inputs unchanged | EX_SERVICE_SPECIFIC with BAD_VALUE | Ignore output on failure |
 * | 05 | Request keys using an empty MIME | Empty MIME; other baseline inputs unchanged | EX_SERVICE_SPECIFIC with BAD_VALUE | Expected API rejection |
 * | 06 | Request keys using an unsupported MIME | video/rdk-vts-unsupported | EX_SERVICE_SPECIFIC with BAD_VALUE or ERROR_DRM_CANNOT_HANDLE | AIDL does not disambiguate these mappings |
 * | 07 | Request keys using below/above-range enum values | KeyType -1 and RELEASE + 1 | EX_SERVICE_SPECIFIC with BAD_VALUE for each value | Expected API rejection |
 * | 08 | Close the session and request keys using its ID | Closed ID; remaining baseline inputs unchanged | Close succeeds; request returns EX_SERVICE_SPECIFIC with ERROR_DRM_SESSION_NOT_OPENED | Expected API rejection |
 */
UT_ADD_TEST(VTS_L1_Drm, getKeyRequest_neg) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);
    std::set<SecurityLevel> levels;
    UT_ASSERT_TRUE_FATAL(vts_drm_collect_security_levels(schemes, levels));
    for (const auto requested : levels) {
        mSession.clear();
        DRM_PLUGIN_OPEN_SESSION_SUCCESS(mPlugin, requested, mSession);
        mSessionOpen = true;
        UT_ASSERT_FALSE_FATAL(mSession.empty());
        DRM_REQUIRE_KEY_REQUEST_FIXTURE();
        KeyRequest request;
        DRM_PLUGIN_GET_KEY_REQUEST_SUCCESS(
                mPlugin, mSession, mInitData, mMime, KeyType::STREAMING, {}, request);
        UT_ASSERT_FALSE_FATAL(request.request.empty());
        DRM_CALL_FAILURE(mPlugin->getKeyRequest(
                {}, mInitData, mMime, KeyType::STREAMING, {}, &request), android::BAD_VALUE);
        DRM_CALL_FAILURE(mPlugin->getKeyRequest(
                mSession, mInitData, "", KeyType::STREAMING, {}, &request), android::BAD_VALUE);
        const Status badMimeStatus = mPlugin->getKeyRequest(
                mSession, mInitData, "video/rdk-vts-unsupported", KeyType::STREAMING, {}, &request);
        UT_LOG_INFO("getKeyRequest(unsupported MIME): %s", badMimeStatus.toString8().c_str());
        UT_ASSERT_TRUE(hasServiceSpecificError(badMimeStatus, android::BAD_VALUE) ||
                hasServiceSpecificError(badMimeStatus, DrmErrors::ERROR_DRM_CANNOT_HANDLE));
        for (const auto type : {static_cast<KeyType>(-1),
                static_cast<KeyType>(static_cast<int32_t>(KeyType::RELEASE) + 1)}) {
            UT_LOG_INFO("Invalid key type: %d", static_cast<int32_t>(type));
            DRM_CALL_FAILURE(mPlugin->getKeyRequest(mSession, mInitData, mMime, type, {}, &request),
                    android::BAD_VALUE);
        }
        DRM_PLUGIN_CLOSE_SESSION_SUCCESS(mPlugin, mSession);
        mSessionOpen = false;
        DRM_CALL_FAILURE(mPlugin->getKeyRequest(
                mSession, mInitData, mMime, KeyType::STREAMING, {}, &request),
                DrmErrors::ERROR_DRM_SESSION_NOT_OPENED);
    }
}

/**
 * @brief Generates a key request without acquiring or installing a license.
 *
 * This test verifies that configured scheme-specific init data produces an opaque request.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 048@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** DRM scheme is ready; provider-valid L1 init data and MIME are configured@n
 * **Dependencies:** DRM plugin, drm.l1.init_data_file, drm.l1.mime_type, and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Load configured init data and MIME | L1 file path and MIME | Non-empty readable data is loaded; absent file setting explicitly skips | Configured unreadable/empty data fails |
 * | 02 | Create a DRM plugin and open a session | every unique advertised level | Setup succeeds | Should Pass |
 * | 03 | Generate a streaming request and log status | Live ID, init data, MIME, STREAMING, empty optional parameters | Status is OK with EX_NONE; request is non-empty | No license is acquired or installed |
 * | 04 | Close the session | Live session ID | Cleanup succeeds | Per-level close; teardown on failure |
 */
UT_ADD_TEST(VTS_L1_Drm, getKeyRequest_pos) {
    DRM_REQUIRE_KEY_REQUEST_FIXTURE();
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);
    std::set<SecurityLevel> levels;
    UT_ASSERT_TRUE_FATAL(vts_drm_collect_security_levels(schemes, levels));
    for (const auto requested : levels) {
        mSession.clear();
        DRM_PLUGIN_OPEN_SESSION_SUCCESS(mPlugin, requested, mSession);
        mSessionOpen = true;
        UT_ASSERT_FALSE_FATAL(mSession.empty());
        KeyRequest request;
        DRM_PLUGIN_GET_KEY_REQUEST_SUCCESS(
                mPlugin, mSession, mInitData, mMime, KeyType::STREAMING, {}, request);
        UT_ASSERT_FALSE(request.request.empty());
        DRM_PLUGIN_CLOSE_SESSION_SUCCESS(mPlugin, mSession);
        mSessionOpen = false;
    }
}

/**
 * @brief Groups empty response and empty scope for license-response handling.
 *
 * This test checks invalid response parameters without supplying a license fixture.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 026@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** Configured DRM scheme is ready to open a session@n
 * **Dependencies:** DRM plugin, KeySetId parcelable, and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Create a DRM plugin and open a session | every unique advertised level | Setup succeeds | Should Pass |
 * | 02 | Provide an empty response for the live session | Live scope, empty response | EX_SERVICE_SPECIFIC with BAD_VALUE | Ignore returned key-set ID on failure |
 * | 03 | Provide data with an empty scope | Empty scope, response byte 0x01 | EX_SERVICE_SPECIFIC with BAD_VALUE | Not an accepted license-response fixture |
 * | 04 | Close the session | Live session ID | Cleanup succeeds | Per-level close; teardown on failure |
 */
UT_ADD_TEST(VTS_L1_Drm, provideKeyResponse_neg) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);
    std::set<SecurityLevel> levels;
    UT_ASSERT_TRUE_FATAL(vts_drm_collect_security_levels(schemes, levels));
    for (const auto requested : levels) {
        mSession.clear();
        DRM_PLUGIN_OPEN_SESSION_SUCCESS(mPlugin, requested, mSession);
        mSessionOpen = true;
        UT_ASSERT_FALSE_FATAL(mSession.empty());
        KeySetId keySetId;
        DRM_CALL_FAILURE(mPlugin->provideKeyResponse(mSession, {}, &keySetId), android::BAD_VALUE);
        DRM_CALL_FAILURE(mPlugin->provideKeyResponse({}, {0x01}, &keySetId), android::BAD_VALUE);
        DRM_PLUGIN_CLOSE_SESSION_SUCCESS(mPlugin, mSession);
        mSessionOpen = false;
    }
}

/**
 * @brief Requests provisioning data without performing a provisioning exchange.
 *
 * This test retrieves an opaque request or explicitly skips schemes that need no provisioning.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 027@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** Configured DRM factory is registered@n
 * **Dependencies:** DRM plugin, ProvisionRequest parcelable, and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Create a DRM plugin | Advertised UUID | Plugin creation succeeds | Should Pass |
 * | 02 | Request provisioning data and log status | Certificate type X.509, empty authority | A status is returned | No HTTP exchange is performed |
 * | 03 | Handle a scheme that does not require provisioning | EX_SERVICE_SPECIFIC with ERROR_DRM_CANNOT_HANDLE | Test is explicitly skipped | Optional capability |
 * | 04 | Otherwise validate status and request data | Returned status and ProvisionRequest | Status is OK with EX_NONE; request is non-empty | Any other error fails |
 */
UT_ADD_TEST(VTS_L1_Drm, getProvisionRequest_pos) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);
    com::rdk::hal::drm::ProvisionRequest request;
    const Status status = mPlugin->getProvisionRequest("X.509", "", &request);
    UT_LOG_INFO("getProvisionRequest: %s", status.toString8().c_str());
    if (hasServiceSpecificError(status, DrmErrors::ERROR_DRM_CANNOT_HANDLE)) {
        UT_LOG_INFO("SKIP: provider does not require provisioning");
        UT_IGNORE_TEST();
    }
    DRM_STATUS_SUCCESS(status);
    UT_ASSERT_FALSE(request.request.empty());
}

/**
 * @brief Rejects an empty provisioning response without provisioning the device.
 *
 * This test checks the documented invalid-parameter error using an empty response.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 028@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** Configured DRM factory is registered@n
 * **Dependencies:** DRM plugin, ProvideProvisionResponseResult parcelable, and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Create a DRM plugin | Advertised UUID | Plugin creation succeeds | Should Pass |
 * | 02 | Submit an empty provisioning response and log status | Empty byte array | EX_SERVICE_SPECIFIC with BAD_VALUE | Ignore output on failure; no valid credentials are submitted |
 */
UT_ADD_TEST(VTS_L1_Drm, provideProvisionResponse_neg) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);
    com::rdk::hal::drm::ProvideProvisionResponseResult result;
    DRM_CALL_FAILURE(mPlugin->provideProvisionResponse({}, &result), android::BAD_VALUE);
}

/**
 * @brief Registers a valid client listener.
 *
 * This test checks listener registration only, not asynchronous callback delivery.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 029@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** Configured DRM factory is registered@n
 * **Dependencies:** DRM plugin, DrmTestListener implementation, and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Create a DRM plugin | Advertised UUID | Plugin creation succeeds | Should Pass |
 * | 02 | Create a client listener | DrmTestListener instance | Non-null listener is available | Test preparation |
 * | 03 | Register the listener and log status | IN: listener | Status is OK with EX_NONE | Callback delivery is not tested |
 */
UT_ADD_TEST(VTS_L1_Drm, setListener_pos) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);
    const sp<DrmTestListener> listener = sp<DrmTestListener>::make();
    DRM_PLUGIN_SET_LISTENER_SUCCESS(mPlugin, listener);
}

/**
 * @brief Associates a playback identifier with an open session.
 *
 * This test exercises playback-metric association without starting playback.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 030@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** Configured DRM scheme is ready to open a session@n
 * **Dependencies:** DRM plugin and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Create a DRM plugin and open a session | every unique advertised level | Setup succeeds | Should Pass |
 * | 02 | Set the playback identifier and log status | Live ID, rdk-vts-playback | Status is OK with EX_NONE | No media playback |
 * | 03 | Close the session | Live session ID | Cleanup succeeds | Per-level close; teardown on failure |
 */
UT_ADD_TEST(VTS_L1_Drm, setPlaybackId_pos) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);
    std::set<SecurityLevel> levels;
    UT_ASSERT_TRUE_FATAL(vts_drm_collect_security_levels(schemes, levels));
    for (const auto requested : levels) {
        mSession.clear();
        DRM_PLUGIN_OPEN_SESSION_SUCCESS(mPlugin, requested, mSession);
        mSessionOpen = true;
        UT_ASSERT_FALSE_FATAL(mSession.empty());
        DRM_PLUGIN_SET_PLAYBACK_ID_SUCCESS(mPlugin, mSession, "rdk-vts-playback");
        DRM_PLUGIN_CLOSE_SESSION_SUCCESS(mPlugin, mSession);
        mSessionOpen = false;
    }
}

/**
 * @brief Groups empty and closed playback-session IDs.
 *
 * This test uses setPlaybackId's specific session-not-found error for both variations.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 031@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** Configured DRM scheme can open and close a session@n
 * **Dependencies:** DRM plugin and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Create a DRM plugin and open a session | every unique advertised level | Setup succeeds | Should Pass |
 * | 02 | Set playback ID using an empty session ID | Empty ID, rdk-vts-playback | EX_SERVICE_SPECIFIC with ERROR_DRM_SESSION_NOT_OPENED | Error follows this method's AIDL |
 * | 03 | Close the real session | Live session ID | Status is OK with EX_NONE | Prepare closed-session variation |
 * | 04 | Set playback ID using the closed ID | Closed ID, rdk-vts-playback | EX_SERVICE_SPECIFIC with ERROR_DRM_SESSION_NOT_OPENED | Expected API rejection |
 */
UT_ADD_TEST(VTS_L1_Drm, setPlaybackId_neg) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);
    std::set<SecurityLevel> levels;
    UT_ASSERT_TRUE_FATAL(vts_drm_collect_security_levels(schemes, levels));
    for (const auto requested : levels) {
        mSession.clear();
        DRM_PLUGIN_OPEN_SESSION_SUCCESS(mPlugin, requested, mSession);
        mSessionOpen = true;
        UT_ASSERT_FALSE_FATAL(mSession.empty());
        DRM_CALL_FAILURE(mPlugin->setPlaybackId({}, "rdk-vts-playback"),
                DrmErrors::ERROR_DRM_SESSION_NOT_OPENED);
        DRM_PLUGIN_CLOSE_SESSION_SUCCESS(mPlugin, mSession);
        mSessionOpen = false;
        DRM_CALL_FAILURE(mPlugin->setPlaybackId(mSession, "rdk-vts-playback"),
                DrmErrors::ERROR_DRM_SESSION_NOT_OPENED);
    }
}

/**
 * @brief Sets an advertised cipher algorithm without loading keys.
 *
 * This test requires provider advertisement before checking a valid algorithm assignment.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 032@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** DRM scheme is ready and advertises the tested cipher algorithm@n
 * **Dependencies:** DRM plugin, optional algorithms property, and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Create a DRM plugin | Advertised UUID | Plugin creation succeeds | Should Pass |
 * | 02 | Read the algorithms property and find the tested cipher | AES/CBC/NoPadding | Algorithm is advertised; unsupported property or absent algorithm explicitly skips | Other property errors fail |
 * | 03 | Open a session | every unique advertised level | Non-empty session ID | Should Pass |
 * | 04 | Set the advertised cipher and log status | Live ID, AES/CBC/NoPadding | Status is OK with EX_NONE | No key is required |
 * | 05 | Close the session | Live session ID | Cleanup succeeds | Per-level close; teardown on failure |
 */
UT_ADD_TEST(VTS_L1_Drm, setCipherAlgorithm_pos) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);
    DRM_REQUIRE_ALGORITHM("AES/CBC/NoPadding");
    std::set<SecurityLevel> levels;
    UT_ASSERT_TRUE_FATAL(vts_drm_collect_security_levels(schemes, levels));
    for (const auto requested : levels) {
        mSession.clear();
        DRM_PLUGIN_OPEN_SESSION_SUCCESS(mPlugin, requested, mSession);
        mSessionOpen = true;
        UT_ASSERT_FALSE_FATAL(mSession.empty());
        DRM_PLUGIN_SET_CIPHER_ALGORITHM_SUCCESS(mPlugin, mSession, "AES/CBC/NoPadding");
        DRM_PLUGIN_CLOSE_SESSION_SUCCESS(mPlugin, mSession);
        mSessionOpen = false;
    }
}

/**
 * @brief Groups empty session, empty/unsupported algorithm, and closed session.
 *
 * This test checks invalid cipher configuration without loading operator keys.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 033@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** Configured DRM scheme can open and close a session@n
 * **Dependencies:** DRM plugin and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Create a DRM plugin and open a session | every unique advertised level | Setup succeeds | Should Pass |
 * | 02 | Set a cipher using an empty session ID | Empty ID, AES/CBC/NoPadding | EX_SERVICE_SPECIFIC with BAD_VALUE | Expected API rejection |
 * | 03 | Set empty and unsupported algorithms on the live session | Empty string, rdk-vts-invalid-cipher | EX_SERVICE_SPECIFIC with BAD_VALUE for each value | Algorithm value is logged |
 * | 04 | Close the real session | Live session ID | Status is OK with EX_NONE | Prepare closed-session variation |
 * | 05 | Set a cipher using the closed ID | Closed ID, AES/CBC/NoPadding | EX_SERVICE_SPECIFIC with ERROR_DRM_SESSION_NOT_OPENED | Expected API rejection |
 */
UT_ADD_TEST(VTS_L1_Drm, setCipherAlgorithm_neg) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);
    std::set<SecurityLevel> levels;
    UT_ASSERT_TRUE_FATAL(vts_drm_collect_security_levels(schemes, levels));
    for (const auto requested : levels) {
        mSession.clear();
        DRM_PLUGIN_OPEN_SESSION_SUCCESS(mPlugin, requested, mSession);
        mSessionOpen = true;
        UT_ASSERT_FALSE_FATAL(mSession.empty());
        DRM_CALL_FAILURE(mPlugin->setCipherAlgorithm({}, "AES/CBC/NoPadding"), android::BAD_VALUE);
        for (const char* algorithm : {"", "rdk-vts-invalid-cipher"}) {
            UT_LOG_INFO("Invalid cipher algorithm: '%s'", algorithm);
            DRM_CALL_FAILURE(mPlugin->setCipherAlgorithm(mSession, algorithm), android::BAD_VALUE);
        }
        DRM_PLUGIN_CLOSE_SESSION_SUCCESS(mPlugin, mSession);
        mSessionOpen = false;
        DRM_CALL_FAILURE(mPlugin->setCipherAlgorithm(mSession, "AES/CBC/NoPadding"),
                DrmErrors::ERROR_DRM_SESSION_NOT_OPENED);
    }
}

/**
 * @brief Sets an advertised MAC algorithm without loading keys.
 *
 * This test requires provider advertisement before checking a valid MAC assignment.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 034@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** DRM scheme is ready and advertises the tested MAC algorithm@n
 * **Dependencies:** DRM plugin, optional algorithms property, and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Create a DRM plugin | Advertised UUID | Plugin creation succeeds | Should Pass |
 * | 02 | Read the algorithms property and find the tested MAC | HmacSHA256 | Algorithm is advertised; unsupported property or absent algorithm explicitly skips | Other property errors fail |
 * | 03 | Open a session | every unique advertised level | Non-empty session ID | Should Pass |
 * | 04 | Set the advertised MAC and log status | Live ID, HmacSHA256 | Status is OK with EX_NONE | No key is required |
 * | 05 | Close the session | Live session ID | Cleanup succeeds | Per-level close; teardown on failure |
 */
UT_ADD_TEST(VTS_L1_Drm, setMacAlgorithm_pos) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);
    DRM_REQUIRE_ALGORITHM("HmacSHA256");
    std::set<SecurityLevel> levels;
    UT_ASSERT_TRUE_FATAL(vts_drm_collect_security_levels(schemes, levels));
    for (const auto requested : levels) {
        mSession.clear();
        DRM_PLUGIN_OPEN_SESSION_SUCCESS(mPlugin, requested, mSession);
        mSessionOpen = true;
        UT_ASSERT_FALSE_FATAL(mSession.empty());
        DRM_PLUGIN_SET_MAC_ALGORITHM_SUCCESS(mPlugin, mSession, "HmacSHA256");
        DRM_PLUGIN_CLOSE_SESSION_SUCCESS(mPlugin, mSession);
        mSessionOpen = false;
    }
}

/**
 * @brief Groups empty session, empty/unsupported algorithm, and closed session.
 *
 * This test checks invalid MAC configuration without loading operator keys.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 035@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** Configured DRM scheme can open and close a session@n
 * **Dependencies:** DRM plugin and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Create a DRM plugin and open a session | every unique advertised level | Setup succeeds | Should Pass |
 * | 02 | Set a MAC using an empty session ID | Empty ID, HmacSHA256 | EX_SERVICE_SPECIFIC with BAD_VALUE | Expected API rejection |
 * | 03 | Set empty and unsupported algorithms on the live session | Empty string, rdk-vts-invalid-mac | EX_SERVICE_SPECIFIC with BAD_VALUE for each value | Algorithm value is logged |
 * | 04 | Close the real session | Live session ID | Status is OK with EX_NONE | Prepare closed-session variation |
 * | 05 | Set a MAC using the closed ID | Closed ID, HmacSHA256 | EX_SERVICE_SPECIFIC with ERROR_DRM_SESSION_NOT_OPENED | Expected API rejection |
 */
UT_ADD_TEST(VTS_L1_Drm, setMacAlgorithm_neg) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);
    std::set<SecurityLevel> levels;
    UT_ASSERT_TRUE_FATAL(vts_drm_collect_security_levels(schemes, levels));
    for (const auto requested : levels) {
        mSession.clear();
        DRM_PLUGIN_OPEN_SESSION_SUCCESS(mPlugin, requested, mSession);
        mSessionOpen = true;
        UT_ASSERT_FALSE_FATAL(mSession.empty());
        DRM_CALL_FAILURE(mPlugin->setMacAlgorithm({}, "HmacSHA256"), android::BAD_VALUE);
        for (const char* algorithm : {"", "rdk-vts-invalid-mac"}) {
            UT_LOG_INFO("Invalid MAC algorithm: '%s'", algorithm);
            DRM_CALL_FAILURE(mPlugin->setMacAlgorithm(mSession, algorithm), android::BAD_VALUE);
        }
        DRM_PLUGIN_CLOSE_SESSION_SUCCESS(mPlugin, mSession);
        mSessionOpen = false;
        DRM_CALL_FAILURE(mPlugin->setMacAlgorithm(mSession, "HmacSHA256"),
                DrmErrors::ERROR_DRM_SESSION_NOT_OPENED);
    }
}

/**
 * @brief Groups empty and closed sessions for legacy decryption.
 *
 * This test validates session errors only; licensed key/IV and successful decryption checks remain in L2.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 036@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** Configured DRM scheme can open and close a session@n
 * **Dependencies:** DRM plugin and UT-Core; no license required@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Create a DRM plugin and open a session | every unique advertised level | Setup succeeds | Should Pass |
 * | 02 | Prepare non-empty crypto arguments | 16-byte zero key ID, input, and IV | Argument buffers are available | No license/key is installed |
 * | 03 | Decrypt using an empty session ID | Empty ID and prepared buffers | EX_SERVICE_SPECIFIC with BAD_VALUE | Ignore output on failure |
 * | 04 | Close the session and decrypt using its ID | Closed ID and prepared buffers | Close succeeds; decrypt returns EX_SERVICE_SPECIFIC with ERROR_DRM_SESSION_NOT_OPENED | Ignore output on failure |
 */
UT_ADD_TEST(VTS_L1_Drm, decrypt_neg) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);
    std::set<SecurityLevel> levels;
    UT_ASSERT_TRUE_FATAL(vts_drm_collect_security_levels(schemes, levels));
    for (const auto requested : levels) {
        mSession.clear();
        DRM_PLUGIN_OPEN_SESSION_SUCCESS(mPlugin, requested, mSession);
        mSessionOpen = true;
        UT_ASSERT_FALSE_FATAL(mSession.empty());
        const Bytes keyId(16, 0);
        const Bytes input(16, 0);
        const Bytes iv(16, 0);
        Bytes output;
        DRM_CALL_FAILURE(mPlugin->decrypt({}, keyId, input, iv, &output), android::BAD_VALUE);
        DRM_PLUGIN_CLOSE_SESSION_SUCCESS(mPlugin, mSession);
        mSessionOpen = false;
        DRM_CALL_FAILURE(mPlugin->decrypt(mSession, keyId, input, iv, &output),
                DrmErrors::ERROR_DRM_SESSION_NOT_OPENED);
    }
}

/**
 * @brief Groups empty and closed sessions for legacy encryption.
 *
 * This test verifies session rejection independently of licensed encryption behavior.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 037@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** Configured DRM scheme can open and close a session@n
 * **Dependencies:** DRM plugin and UT-Core; no license required@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Create a DRM plugin and open a session | every unique advertised level | Setup succeeds | Should Pass |
 * | 02 | Prepare non-empty crypto arguments | 16-byte zero key ID, input, and IV | Argument buffers are available | No license/key is installed |
 * | 03 | Encrypt using an empty session ID | Empty ID and prepared buffers | EX_SERVICE_SPECIFIC with BAD_VALUE | Ignore output on failure |
 * | 04 | Close the session and encrypt using its ID | Closed ID and prepared buffers | Close succeeds; encrypt returns EX_SERVICE_SPECIFIC with ERROR_DRM_SESSION_NOT_OPENED | Ignore output on failure |
 */
UT_ADD_TEST(VTS_L1_Drm, encrypt_neg) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);
    std::set<SecurityLevel> levels;
    UT_ASSERT_TRUE_FATAL(vts_drm_collect_security_levels(schemes, levels));
    for (const auto requested : levels) {
        mSession.clear();
        DRM_PLUGIN_OPEN_SESSION_SUCCESS(mPlugin, requested, mSession);
        mSessionOpen = true;
        UT_ASSERT_FALSE_FATAL(mSession.empty());
        const Bytes keyId(16, 0);
        const Bytes input(16, 0);
        const Bytes iv(16, 0);
        Bytes output;
        DRM_CALL_FAILURE(mPlugin->encrypt({}, keyId, input, iv, &output), android::BAD_VALUE);
        DRM_PLUGIN_CLOSE_SESSION_SUCCESS(mPlugin, mSession);
        mSessionOpen = false;
        DRM_CALL_FAILURE(mPlugin->encrypt(mSession, keyId, input, iv, &output),
                DrmErrors::ERROR_DRM_SESSION_NOT_OPENED);
    }
}

/**
 * @brief Groups empty and closed signing-session IDs.
 *
 * This test validates session errors without assuming that an empty message is invalid.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 038@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** Configured DRM scheme can open and close a session@n
 * **Dependencies:** DRM plugin and UT-Core; no operator key required@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Create a DRM plugin and open a session | every unique advertised level | Setup succeeds | Should Pass |
 * | 02 | Prepare signing inputs | 16-byte zero key ID and message byte 0x01 | Input buffers are available | No signing key is loaded |
 * | 03 | Sign using an empty session ID | Empty ID and prepared inputs | EX_SERVICE_SPECIFIC with BAD_VALUE | Ignore signature output on failure |
 * | 04 | Close the session and sign using its ID | Closed ID and prepared inputs | Close succeeds; sign returns EX_SERVICE_SPECIFIC with ERROR_DRM_SESSION_NOT_OPENED | Ignore signature output on failure |
 */
UT_ADD_TEST(VTS_L1_Drm, sign_neg) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);
    std::set<SecurityLevel> levels;
    UT_ASSERT_TRUE_FATAL(vts_drm_collect_security_levels(schemes, levels));
    for (const auto requested : levels) {
        mSession.clear();
        DRM_PLUGIN_OPEN_SESSION_SUCCESS(mPlugin, requested, mSession);
        mSessionOpen = true;
        UT_ASSERT_FALSE_FATAL(mSession.empty());
        const Bytes keyId(16, 0);
        const Bytes message{0x01};
        Bytes signature;
        DRM_CALL_FAILURE(mPlugin->sign({}, keyId, message, &signature), android::BAD_VALUE);
        DRM_PLUGIN_CLOSE_SESSION_SUCCESS(mPlugin, mSession);
        mSessionOpen = false;
        DRM_CALL_FAILURE(mPlugin->sign(mSession, keyId, message, &signature),
                DrmErrors::ERROR_DRM_SESSION_NOT_OPENED);
    }
}

/**
 * @brief Groups empty and closed verification-session IDs.
 *
 * This test checks invalid-session handling without claiming signature correctness.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 039@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** Configured DRM scheme can open and close a session@n
 * **Dependencies:** DRM plugin and UT-Core; no operator key required@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Create a DRM plugin and open a session | every unique advertised level | Setup succeeds | Should Pass |
 * | 02 | Prepare verification inputs | 16-byte zero key ID, message byte 0x01, 32-byte zero signature | Input buffers are available | Not a valid-signature fixture |
 * | 03 | Verify using an empty session ID | Empty ID and prepared inputs | EX_SERVICE_SPECIFIC with BAD_VALUE | Ignore boolean output on failure |
 * | 04 | Close the session and verify using its ID | Closed ID and prepared inputs | Close succeeds; verify returns EX_SERVICE_SPECIFIC with ERROR_DRM_SESSION_NOT_OPENED | Ignore boolean output on failure |
 */
UT_ADD_TEST(VTS_L1_Drm, verify_neg) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);
    std::set<SecurityLevel> levels;
    UT_ASSERT_TRUE_FATAL(vts_drm_collect_security_levels(schemes, levels));
    for (const auto requested : levels) {
        mSession.clear();
        DRM_PLUGIN_OPEN_SESSION_SUCCESS(mPlugin, requested, mSession);
        mSessionOpen = true;
        UT_ASSERT_FALSE_FATAL(mSession.empty());
        const Bytes keyId(16, 0);
        const Bytes message{0x01};
        const Bytes signature(32, 0);
        bool verified = false;
        DRM_CALL_FAILURE(mPlugin->verify({}, keyId, message, signature, &verified), android::BAD_VALUE);
        DRM_PLUGIN_CLOSE_SESSION_SUCCESS(mPlugin, mSession);
        mSessionOpen = false;
        DRM_CALL_FAILURE(mPlugin->verify(mSession, keyId, message, signature, &verified),
                DrmErrors::ERROR_DRM_SESSION_NOT_OPENED);
    }
}

/**
 * @brief Rejects an empty RSA session ID without requiring a provisioned key.
 *
 * This test checks invalid parameters only; successful RSA and isolated wrapped-key tests need provider fixtures.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 040@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** Configured DRM factory is registered@n
 * **Dependencies:** DRM plugin and UT-Core; no provisioning exchange required@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Create a DRM plugin | Advertised UUID | Plugin creation succeeds | Should Pass |
 * | 02 | Call signRSA with an empty session and placeholder key data | Empty ID, PKCS1-BlockType1, message/key byte 0x01 | EX_SERVICE_SPECIFIC with BAD_VALUE | Placeholder is not a provisioned wrapped key; ignore signature output |
 */
UT_ADD_TEST(VTS_L1_Drm, signRSA_neg) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);
    Bytes signature;
    DRM_CALL_FAILURE(mPlugin->signRSA({}, "PKCS1-BlockType1", {0x01}, {0x01}, &signature),
            android::BAD_VALUE);
}

/**
 * @brief Queries secure-decoder requirements for advertised MIME/security ranges.
 *
 * This test validates successful queries without imposing a provider-independent boolean result.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 041@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** Configured DRM factory is registered@n
 * **Dependencies:** DRM plugin, advertised content types, and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Create a DRM plugin and retrieve supported schemes | OUT: CryptoSchemes | Both calls succeed | Should Pass |
 * | 02 | Check for advertised MIME/security combinations | mimeTypes list | Continue when non-empty; otherwise explicitly skip | No synthetic content type is substituted |
 * | 03 | Query each MIME at its minimum and maximum security levels | MIME, minLevel, maxLevel | Status is OK with EX_NONE for each query | Boolean is provider-dependent; MIME/level are logged |
 */
UT_ADD_TEST(VTS_L1_Drm, requiresSecureDecoder_pos) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);
    if (schemes.mimeTypes.empty()) {
        UT_LOG_INFO("SKIP: no MIME/security-level combinations are advertised");
        UT_IGNORE_TEST();
    }
    for (const auto& content : schemes.mimeTypes) {
        for (const auto level : {content.minLevel, content.maxLevel}) {
            UT_LOG_INFO("requiresSecureDecoder(mime:%s, level:%d)",
                    content.mime.c_str(), static_cast<int32_t>(level));
            bool required = false;
            DRM_PLUGIN_REQUIRES_SECURE_DECODER_SUCCESS(mPlugin, content.mime, level, required);
        }
    }
}

/**
 * @brief Binds crypto to an open DRM session.
 *
 * This test verifies session association without acquiring content keys.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 042@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** Configured DRM scheme is ready to open a session@n
 * **Dependencies:** DRM/crypto plugins for the same UUID and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Create DRM and crypto plugins | Same advertised UUID, empty crypto init data | Both plugins are non-null | Should Pass |
 * | 02 | Open a DRM session | every unique advertised level | Non-empty session ID | Should Pass |
 * | 03 | Bind crypto to the DRM session and log status | IN: live session ID | Status is OK with EX_NONE | No license is required |
 * | 04 | Close the DRM session | Live session ID | Cleanup succeeds | Per-level close; teardown on failure |
 */
UT_ADD_TEST(VTS_L1_Drm, setMediaDrmSession_pos) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);
    DRM_FACTORY_CREATE_CRYPTO_PLUGIN_SUCCESS(schemes.uuids.front(), {}, mCrypto);
    std::set<SecurityLevel> levels;
    UT_ASSERT_TRUE_FATAL(vts_drm_collect_security_levels(schemes, levels));
    for (const auto requested : levels) {
        mSession.clear();
        DRM_PLUGIN_OPEN_SESSION_SUCCESS(mPlugin, requested, mSession);
        mSessionOpen = true;
        UT_ASSERT_FALSE_FATAL(mSession.empty());
        DRM_CRYPTO_PLUGIN_SET_MEDIA_DRM_SESSION_SUCCESS(mCrypto, mSession);
        DRM_PLUGIN_CLOSE_SESSION_SUCCESS(mPlugin, mSession);
        mSessionOpen = false;
    }
}

/**
 * @brief Groups empty and closed media-DRM session IDs.
 *
 * This test verifies the session-not-opened error for invalid crypto-session association.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 043@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** Configured DRM scheme can open and close a session@n
 * **Dependencies:** DRM/crypto plugins for the same UUID and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Create DRM/crypto plugins and open a session | Same UUID, every unique advertised level | Setup succeeds | Should Pass |
 * | 02 | Associate crypto with an empty session ID | Empty byte array | EX_SERVICE_SPECIFIC with ERROR_DRM_SESSION_NOT_OPENED | Expected API rejection |
 * | 03 | Close the real DRM session | Live session ID | Status is OK with EX_NONE | Prepare closed-session variation |
 * | 04 | Associate crypto with the closed ID | Closed non-empty ID | EX_SERVICE_SPECIFIC with ERROR_DRM_SESSION_NOT_OPENED | Expected API rejection |
 */
UT_ADD_TEST(VTS_L1_Drm, setMediaDrmSession_neg) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);
    DRM_FACTORY_CREATE_CRYPTO_PLUGIN_SUCCESS(schemes.uuids.front(), {}, mCrypto);
    std::set<SecurityLevel> levels;
    UT_ASSERT_TRUE_FATAL(vts_drm_collect_security_levels(schemes, levels));
    for (const auto requested : levels) {
        mSession.clear();
        DRM_PLUGIN_OPEN_SESSION_SUCCESS(mPlugin, requested, mSession);
        mSessionOpen = true;
        UT_ASSERT_FALSE_FATAL(mSession.empty());
        DRM_CALL_FAILURE(mCrypto->setMediaDrmSession({}), DrmErrors::ERROR_DRM_SESSION_NOT_OPENED);
        DRM_PLUGIN_CLOSE_SESSION_SUCCESS(mPlugin, mSession);
        mSessionOpen = false;
        DRM_CALL_FAILURE(mCrypto->setMediaDrmSession(mSession), DrmErrors::ERROR_DRM_SESSION_NOT_OPENED);
    }
}

/**
 * @brief Notifies crypto of valid display resolutions.
 *
 * This test covers common positive resolutions; the AIDL does not define invalid-dimension errors.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 044@n
 * **Priority:** Medium@n
 * @n
 * **Pre-Conditions:** Configured DRM factory is registered@n
 * **Dependencies:** DRM/crypto plugin creation and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Create DRM and crypto plugins | Advertised UUID, empty crypto init data | Both plugins are non-null | Should Pass |
 * | 02 | Notify each resolution and log dimensions/status | 1280x720, 1920x1080, 3840x2160 | Status is OK with EX_NONE for every notification | No media playback or resolution output is asserted |
 */
UT_ADD_TEST(VTS_L1_Drm, notifyResolution_pos) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);
    DRM_FACTORY_CREATE_CRYPTO_PLUGIN_SUCCESS(schemes.uuids.front(), {}, mCrypto);
    for (const auto& resolution :
            {std::array<int32_t, 2>{1280, 720}, {1920, 1080}, {3840, 2160}}) {
        UT_LOG_INFO("notifyResolution(width:%d, height:%d)", resolution[0], resolution[1]);
        DRM_CRYPTO_PLUGIN_NOTIFY_RESOLUTION_SUCCESS(mCrypto, resolution[0], resolution[1]);
    }
}

/**
 * @brief Queries video and audio MIME types listed by ICryptoPlugin.
 *
 * This test checks query success while leaving secure-decoder policy to the provider.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 045@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** Configured DRM factory is registered@n
 * **Dependencies:** DRM/crypto plugin creation and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Create DRM and crypto plugins | Advertised UUID, empty crypto init data | Both plugins are non-null | Should Pass |
 * | 02 | Query listed video MIME types and log each result status | video/avc, video/hevc, video/x-vnd.on2.vp9, video/av1, video/mp4v-es | Status is OK with EX_NONE for each MIME | Boolean is provider-dependent |
 * | 03 | Query listed audio MIME types and log each result status | audio/mp4a-latm, audio/ac3, audio/eac3, audio/vnd.dts, audio/opus | Status is OK with EX_NONE for each MIME | Boolean is provider-dependent |
 */
UT_ADD_TEST(VTS_L1_Drm, requiresSecureDecoderComponent_pos) {
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);
    DRM_FACTORY_CREATE_CRYPTO_PLUGIN_SUCCESS(schemes.uuids.front(), {}, mCrypto);
    for (const char* mime : {"video/avc", "video/hevc", "video/x-vnd.on2.vp9", "video/av1",
            "video/mp4v-es", "audio/mp4a-latm", "audio/ac3", "audio/eac3",
            "audio/vnd.dts", "audio/opus"}) {
        UT_LOG_INFO("requiresSecureDecoderComponent(mime:%s)", mime);
        bool required = false;
        DRM_CRYPTO_PLUGIN_REQUIRES_SECURE_DECODER_COMPONENT_SUCCESS(mCrypto, mime, required);
    }
}

/**
 * @brief Confirms an unsupported MIME type is not advertised.
 *
 * This test checks the capability helper rather than submitting a key request.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 046@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** Configured DRM factory is registered@n
 * **Dependencies:** Supported-scheme query, DRM capability helper, and UT-Core@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Initialize the capability result | supported = true | A false result must be written by the lookup | Test preparation |
 * | 02 | Query support for an unsupported MIME/security pair | video/rdk-vts-unsupported, SW_SECURE_CRYPTO | Capability query completes successfully | Query errors fail rather than mean unsupported |
 * | 03 | Check the capability result | OUT: supported | supported is false | Should Pass |
 */
UT_ADD_TEST(VTS_L1_Drm, badMimeNotSupported_neg) {
    bool supported = true;
    UT_ASSERT_TRUE_FATAL(vts_drm_content_supported(
            "video/rdk-vts-unsupported", SecurityLevel::SW_SECURE_CRYPTO, &supported));
    UT_ASSERT_FALSE(supported);
}

/**
 * @brief Handles provisioning when opening a session reports NOT_PROVISIONED.
 *
 * This opt-in test exchanges opaque provisioning data with an approved adapter and retries session creation.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 047@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** Provisioning test is enabled with an approved HTTP adapter@n
 * **Dependencies:** DRM plugin and configured provisioning endpoint@n
 * **User Interaction:** None@n
 * @n
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- | -------------- | ----- |
 * | 01 | Check the provisioning enable setting | DRM_PROVISIONING_ENABLE | Unset/0 explicitly skips; enabled value must be 1 | Opt-in only |
 * | 02 | Create a DRM plugin | Advertised UUID | Plugin creation succeeds | Should Pass |
 * | 03 | Attempt session open at the tested levels | HW_SECURE_ALL, SW_SECURE_CRYPTO | Success with a non-empty ID, or documented CANNOT_HANDLE/NOT_PROVISIONED error | Successfully opened sessions are closed immediately |
 * | 04 | Determine whether an exchange is needed | Results of session-open attempts | If no attempt needs provisioning, finish without an exchange | No unsupported capability is reported as provisioned |
 * | 05 | Request provisioning data if required | Configured certificate type and authority | Non-empty request; ERROR_DRM_CANNOT_HANDLE explicitly skips | Other errors fail |
 * | 06 | Validate transport settings and POST the opaque request | Configured URL, content type, positive timeout, request bytes | HTTP helper returns a valid non-empty response | Approved adapter only |
 * | 07 | Submit the provisioning response | Opaque response bytes | Status is OK with EX_NONE | Device credentials may be updated |
 * | 08 | Retry session creation and close the new session | Security level that reported NOT_PROVISIONED | Session opens with a non-empty ID and closes successfully | Teardown handles assertion-failure cleanup |
 */
UT_ADD_TEST(VTS_L1_Drm, provisioningStatusAndExchange_pos) {
    const char* enabled = vts_drm_config_value("DRM_PROVISIONING_ENABLE");
    if (enabled == nullptr || std::string(enabled) == "0") {
        UT_LOG_INFO("SKIP: provisioning exchange is disabled in DRM test configuration");
        UT_IGNORE_TEST();
    }
    UT_ASSERT_TRUE_FATAL(std::string(enabled) == "1");
    CryptoSchemes schemes;
    DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes);
    UT_ASSERT_FALSE_FATAL(schemes.uuids.empty());
    DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(
            schemes.uuids.front(), DRM_APP_PACKAGE_NAME, mPlugin);

    const SecurityLevel levels[] = {
        SecurityLevel::HW_SECURE_ALL, SecurityLevel::SW_SECURE_CRYPTO};
    bool provisioningRequired = false;
    SecurityLevel retryLevel = SecurityLevel::UNKNOWN;
    for (SecurityLevel level : levels) {
        mSession.clear();
        Status status;
        DRM_PLUGIN_OPEN_SESSION(mPlugin, level, mSession, status);
        if (status.isOk()) {
            mSessionOpen = true;
            UT_ASSERT_FALSE_FATAL(mSession.empty());
            DRM_PLUGIN_CLOSE_SESSION_SUCCESS(mPlugin, mSession);
            mSessionOpen = false;
            continue;
        }
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
        UT_LOG_INFO("Provisioning not required by the tested DRM scheme");
        return;
    }

    com::rdk::hal::drm::ProvisionRequest request;
    const char* certificateType =
            vts_drm_config_value("DRM_PROVISIONING_CERTIFICATE_TYPE");
    const char* certificateAuthority =
            vts_drm_config_value("DRM_PROVISIONING_CERTIFICATE_AUTHORITY");
    UT_ASSERT_TRUE_FATAL(certificateType != nullptr && certificateType[0] != '\0');
    UT_ASSERT_TRUE_FATAL(certificateAuthority != nullptr);
    const Status requestStatus = mPlugin->getProvisionRequest(
            certificateType, certificateAuthority, &request);
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

    Bytes response;
    UT_ASSERT_TRUE_FATAL(vts_drm_request_http_post(
            url, contentType, timeout, request.request, &response));
    com::rdk::hal::drm::ProvideProvisionResponseResult result;
    DRM_PLUGIN_PROVIDE_PROVISION_RESPONSE_SUCCESS(mPlugin, response, result);
    mSession.clear();
    DRM_PLUGIN_OPEN_SESSION_SUCCESS(mPlugin, retryLevel, mSession);
    mSessionOpen = true;
    UT_ASSERT_FALSE_FATAL(mSession.empty());
    DRM_PLUGIN_CLOSE_SESSION_SUCCESS(mPlugin, mSession);
    mSessionOpen = false;
}

#undef DRM_REQUIRE_ALGORITHM
#undef DRM_REQUIRE_KEY_REQUEST_FIXTURE

/** @} */ // End of DRM_HALTEST_L1
/** @} */ // End of DRM_HALTEST
/** @} */ // End of DRM
/** @} */ // End of HPK
