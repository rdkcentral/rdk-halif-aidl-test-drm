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
 * ### L2 ClearKey Test Cases for DRM HAL :
 *
 * These opt-in tests exercise ClearKey license response handling, key status,
 * and known-vector decryption without decoder playback. Test key and content
 * fixtures are supplied by YAML configuration or environment overrides.
 *
 * **Pre-Conditions:** Enable L2 through YAML or environment and provide ClearKey fixture inputs@n
 * **Dependencies:** ClearKey DRM factory and disposable test vectors@n
 *
 * @endparblock
 */

/**
 * @file vts_drm_l2_clearkey_tests.cpp
 *
 */

#include "vts_drm.h"
#include "vts_drm_license.h"

#include <com/rdk/hal/drm/KeyRequest.h>
#include <com/rdk/hal/drm/KeyStatus.h>
#include <com/rdk/hal/drm/KeyType.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

using com::rdk::hal::drm::KeyRequest;
using com::rdk::hal::drm::KeyType;

UT_ADD_TEST_TO_GROUP(VTS_L2_DrmClearKey, UT_TESTS_L2)

namespace {

struct ClearKeyFixture {
    std::vector<uint8_t> initData;
    std::vector<uint8_t> ciphertext;
    std::vector<uint8_t> plaintext;
    std::vector<uint8_t> kid;
    std::vector<uint8_t> key;
    std::vector<uint8_t> iv;
    std::vector<uint8_t> licenseResponse;
};

bool enabled() {
    const char* value = vts_drm_config_value("DRM_L2_ENABLE");
    return value != nullptr && std::string(value) == "1";
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

bool loadFixture(ClearKeyFixture* fixture) {
    if (!enabled()) {
        UT_LOG_INFO("L2 ClearKey tests disabled; enable drm.l2.enabled or set DRM_L2_ENABLE=1");
        return false;
    }
    return readFile("DRM_L2_INIT_DATA", &fixture->initData) &&
            readFile("DRM_L2_CIPHERTEXT", &fixture->ciphertext) &&
            readFile("DRM_L2_PLAINTEXT", &fixture->plaintext) &&
            decodeHex("DRM_L2_KID_HEX", &fixture->kid) &&
            decodeHex("DRM_L2_KEY_HEX", &fixture->key) &&
            decodeHex("DRM_L2_IV_HEX", &fixture->iv) &&
            fixture->kid.size() == 16 &&
            fixture->key.size() == 16 &&
            fixture->iv.size() == 16 &&
            fixture->ciphertext.size() == fixture->plaintext.size();
}

bool loadLicenseResponse(const ClearKeyFixture& fixture,
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
    const std::string json = "{\"keys\":[{\"kty\":\"oct\",\"kid\":\"" +
            base64Url(fixture.kid) + "\",\"k\":\"" + base64Url(fixture.key) +
            "\"}],\"type\":\"temporary\"}";
    response->assign(json.begin(), json.end());
    return true;
}

class LicensedSession {
public:
    bool open(const ClearKeyFixture& fixture) {
        Uuid uuid;
        if (!vts_drm_getSupportedUuid(&uuid).isOk() ||
                !vts_drm_createPlugins(uuid, &drm, &crypto).isOk()) {
            return false;
        }
        if (!drm->openSession(
                    com::rdk::hal::drm::SecurityLevel::DEFAULT, &session).isOk()) {
            return false;
        }
        KeyRequest request;
        if (!drm->getKeyRequest(
                    session,
                    fixture.initData,
                    String16("video/mp4"),
                    KeyType::STREAMING,
                    {},
                    &request).isOk() ||
                request.request.empty()) {
            return false;
        }
        std::vector<uint8_t> response;
        if (!loadLicenseResponse(fixture, request.request, &response)) {
            return false;
        }
        com::rdk::hal::drm::KeySetId keySetId;
        if (!drm->provideKeyResponse(session, response, &keySetId).isOk()) {
            return false;
        }
        return crypto->setMediaDrmSession(session).isOk();
    }

    ~LicensedSession() {
        if (drm != nullptr && !session.empty()) {
            drm->closeSession(session);
        }
    }

    sp<IDrmPlugin> drm;
    sp<ICryptoPlugin> crypto;
    std::vector<uint8_t> session;
};

}  // namespace

class VTS_L2_DrmClearKey : public UTCore {};

/**
 * @brief Validates the configured ClearKey vector files and cryptographic field sizes.
 *
 * This test is opt-in. It returns without running when L2 is disabled.
 *
 * **Test Group ID:** L2: 02@n
 * **Test Case ID:** 001@n
 * **Priority:** High@n
 * **Pre-Conditions:** Enable L2 through YAML or environment and provide the ClearKey vector inputs@n
 * **Dependencies:** Init data, ciphertext, plaintext, KID, key, and IV files/values@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Load the configured ClearKey fixture|YAML or DRM_L2_* overrides|All required fixture inputs are present and readable|Should Pass|
 * |02|Validate KID, key, and IV sizes|KID, key, IV|Each value is 16 bytes|AES-128 test vector|
 * |03|Validate sample lengths|Ciphertext, plaintext|Both have equal length|Should Pass|
 */
UT_ADD_TEST(VTS_L2_DrmClearKey, fixtureValidation_pos) {
    if (!enabled()) {
        return;
    }
    ClearKeyFixture fixture;
    UT_ASSERT_TRUE(loadFixture(&fixture));
    UT_ASSERT_EQUAL(fixture.kid.size(), static_cast<size_t>(16));
    UT_ASSERT_EQUAL(fixture.key.size(), static_cast<size_t>(16));
    UT_ASSERT_EQUAL(fixture.iv.size(), static_cast<size_t>(16));
}

/**
 * @brief Exercises ClearKey request/response processing and verifies key status.
 *
 * **Test Group ID:** L2: 02@n
 * **Test Case ID:** 002@n
 * **Priority:** High@n
 * **Pre-Conditions:** Enable L2 through YAML or environment and provide valid ClearKey fixture inputs@n
 * **Dependencies:** ClearKey DRM factory and an accepted license response@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Open a DRM session and request a streaming key|Init data, video/mp4, STREAMING|Request succeeds and request bytes are non-empty|Should Pass|
 * |02|Acquire and provide the license response|HTTP endpoint or local generated/file response|Response succeeds and crypto session binding succeeds|Test-only ClearKey material|
 * |03|Query the session key status|Session ID|Call succeeds and returns at least one status value|Should Pass|
 */
UT_ADD_TEST(VTS_L2_DrmClearKey, keyRequestAndResponse_pos) {
    if (!enabled()) {
        return;
    }
    ClearKeyFixture fixture;
    UT_ASSERT_TRUE(loadFixture(&fixture));
    LicensedSession licensed;
    UT_ASSERT_TRUE(licensed.open(fixture));

    std::vector<com::rdk::hal::drm::KeyValue> status;
    UT_ASSERT_TRUE(licensed.drm->queryKeyStatus(licensed.session, &status).isOk());
    UT_ASSERT_FALSE(status.empty());
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
UT_ADD_TEST(VTS_L2_DrmClearKey, decryptKnownSample_pos) {
    if (!enabled()) {
        return;
    }
    ClearKeyFixture fixture;
    UT_ASSERT_TRUE(loadFixture(&fixture));
    LicensedSession licensed;
    UT_ASSERT_TRUE(licensed.open(fixture));

    std::vector<uint8_t> decrypted;
    UT_ASSERT_TRUE(licensed.drm->decrypt(
            licensed.session, fixture.kid, fixture.ciphertext, fixture.iv, &decrypted).isOk());
    UT_ASSERT_TRUE(decrypted == fixture.plaintext);
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
 * |03|Retry sample decryption|KID, ciphertext, IV|Decrypt call fails after key removal|Negative case|
 */
UT_ADD_TEST(VTS_L2_DrmClearKey, removeKeysRejectsDecrypt_neg) {
    if (!enabled()) {
        return;
    }
    ClearKeyFixture fixture;
    UT_ASSERT_TRUE(loadFixture(&fixture));
    LicensedSession licensed;
    UT_ASSERT_TRUE(licensed.open(fixture));
    UT_ASSERT_TRUE(licensed.drm->removeKeys(licensed.session).isOk());

    std::vector<uint8_t> decrypted;
    UT_ASSERT_FALSE(licensed.drm->decrypt(
            licensed.session, fixture.kid, fixture.ciphertext, fixture.iv, &decrypted).isOk());
}

/**
 * @brief Confirms an empty init-data blob is rejected for a streaming key request.
 *
 * **Test Group ID:** L2: 02@n
 * **Test Case ID:** 005@n
 * **Priority:** Medium@n
 * **Pre-Conditions:** Enable L2 through YAML or environment and provide the configured fixture inputs@n
 * **Dependencies:** ClearKey DRM factory and openable session@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Open a DRM session|Default security level|Call succeeds|Should Pass|
 * |02|Request a streaming key with empty init data|Empty init data, video/mp4|Call fails|Invalid input case|
 * |03|Close the session|Session ID|Call succeeds|Should Pass|
 */
UT_ADD_TEST(VTS_L2_DrmClearKey, invalidInitDataRejected_neg) {
    if (!enabled()) {
        return;
    }
    ClearKeyFixture fixture;
    UT_ASSERT_TRUE(loadFixture(&fixture));
    Uuid uuid;
    UT_ASSERT_TRUE(vts_drm_getSupportedUuid(&uuid).isOk());
    sp<IDrmPlugin> drm;
    UT_ASSERT_TRUE(gDrmFactory->createDrmPlugin(
            uuid, String16("com.rdk.vts.drm"), &drm).isOk());
    std::vector<uint8_t> session;
    UT_ASSERT_TRUE(drm->openSession(
            com::rdk::hal::drm::SecurityLevel::DEFAULT, &session).isOk());

    KeyRequest request;
    UT_ASSERT_FALSE(drm->getKeyRequest(
            session, {}, String16("video/mp4"), KeyType::STREAMING, {}, &request).isOk());
    UT_ASSERT_TRUE(drm->closeSession(session).isOk());
}

/**
 * @brief Checks a key request is rejected after its DRM session has been closed.
 *
 * **Test Group ID:** L2: 02@n
 * **Test Case ID:** 006@n
 * **Priority:** Medium@n
 * **Pre-Conditions:** Enable L2 through YAML or environment and provide the configured fixture inputs@n
 * **Dependencies:** ClearKey DRM factory and openable session@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Open and close a DRM session|Default security level|Both calls succeed|Should Pass|
 * |02|Request a streaming key using the closed session ID|Session ID, valid init data, video/mp4|Call fails|Closed-session negative case|
 */
UT_ADD_TEST(VTS_L2_DrmClearKey, closedSessionRejectsOperations_neg) {
    if (!enabled()) {
        return;
    }
    ClearKeyFixture fixture;
    UT_ASSERT_TRUE(loadFixture(&fixture));
    Uuid uuid;
    UT_ASSERT_TRUE(vts_drm_getSupportedUuid(&uuid).isOk());
    sp<IDrmPlugin> drm;
    UT_ASSERT_TRUE(gDrmFactory->createDrmPlugin(
            uuid, String16("com.rdk.vts.drm"), &drm).isOk());
    std::vector<uint8_t> session;
    UT_ASSERT_TRUE(drm->openSession(
            com::rdk::hal::drm::SecurityLevel::DEFAULT, &session).isOk());
    UT_ASSERT_TRUE(drm->closeSession(session).isOk());

    KeyRequest request;
    UT_ASSERT_FALSE(drm->getKeyRequest(
            session, fixture.initData, String16("video/mp4"), KeyType::STREAMING, {}, &request).isOk());
}

/** @} */ // End of DRM_HALTEST_L2_CLEARKEY
/** @} */ // End of DRM_HALTEST
/** @} */ // End of DRM
/** @} */ // End of HPK
