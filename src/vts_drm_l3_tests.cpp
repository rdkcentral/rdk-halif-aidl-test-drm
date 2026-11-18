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
 * @defgroup DRM_HALTEST_L3 DRM HAL Tests L3 File
 * @{
 * @parblock
 *
 * ### L3 Test Cases for DRM HAL :
 *
 * These opt-in tests exercise AVBuffer-backed decryption and secure-session
 * admission. They do not claim end-to-end playback or protected-output
 * validation.
 *
 * **Pre-Conditions:** Enable L3 through YAML or environment and provide the documented sample
 * and license fixtures@n
 * **Dependencies:** DRM, AVBuffer, and AVBufferHelper services@n
 *
 * @endparblock
 */

/**
 * @file vts_drm_l3_tests.cpp
 *
 */

#include "vts_drm.h"
#include "vts_drm_license.h"
#include "vts_drm_avbuffer.h"

#include <com/rdk/hal/drm/DecryptArgs.h>
#include <com/rdk/hal/drm/DrmErrors.h>
#include <com/rdk/hal/drm/KeyRequest.h>
#include <com/rdk/hal/drm/KeyType.h>
#include <com/rdk/hal/drm/Mode.h>
#include <com/rdk/hal/drm/SecurityLevel.h>
#include <com/rdk/hal/drm/SubSample.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

using android::sp;
using android::binder::Status;
using com::rdk::hal::drm::DecryptArgs;
using com::rdk::hal::drm::DrmErrors;
using com::rdk::hal::drm::KeyRequest;
using com::rdk::hal::drm::KeyType;
using com::rdk::hal::drm::Mode;
using com::rdk::hal::drm::SecurityLevel;
using com::rdk::hal::drm::SubSample;
using vts_drm::SampleBuffers;

namespace {

struct SampleFixture {
    std::vector<uint8_t> initData;
    std::vector<uint8_t> ciphertext;
    std::vector<uint8_t> plaintext;
    std::vector<uint8_t> keyId;
    std::vector<uint8_t> iv;
    std::vector<uint8_t> licenseResponse;
    std::string mimeType;
    Mode mode = Mode::AES_CTR;
};

DecryptArgs sampleDecryptArgs(const SampleFixture& fixture, const SampleBuffers& buffers) {
    SubSample sample;
    sample.numBytesOfClearData = 0;
    sample.numBytesOfEncryptedData = static_cast<int32_t>(fixture.ciphertext.size());
    return buffers.decryptArgs(fixture.keyId, fixture.iv, fixture.mode, {sample});
}

bool hasServiceSpecificError(const Status& status, DrmErrors error) {
    return !status.isOk() &&
            status.exceptionCode() == Status::EX_SERVICE_SPECIFIC &&
            status.serviceSpecificErrorCode() == static_cast<int32_t>(error);
}

bool enabled() {
    const char* value = vts_drm_config_value("DRM_L3_ENABLE");
    if (value == nullptr || std::string(value) != "1") {
        UT_LOG_INFO("L3 DRM tests disabled; enable drm.l3.enabled or set DRM_L3_ENABLE=1");
        return false;
    }
    return true;
}

bool secureSessionTestEnabled() {
    const char* value = vts_drm_config_value("DRM_L3_SECURE_SESSION_ENABLE");
    if (value == nullptr || std::string(value) != "1") {
        UT_LOG_INFO(
                "Secure-session test disabled; enable drm.l3.secure_session.enabled "
                "or set DRM_L3_SECURE_SESSION_ENABLE=1");
        return false;
    }
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

    const auto digit = [](char c) -> uint8_t {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return static_cast<uint8_t>(c >= 'a' ? c - 'a' + 10 : c - '0');
    };
    data->clear();
    for (size_t i = 0; value[i] != '\0'; i += 2) {
        if (!std::isxdigit(static_cast<unsigned char>(value[i])) ||
                !std::isxdigit(static_cast<unsigned char>(value[i + 1]))) {
            UT_LOG_ERROR("%s contains non-hexadecimal data", variable);
            return false;
        }
        data->push_back(static_cast<uint8_t>((digit(value[i]) << 4) | digit(value[i + 1])));
    }
    return !data->empty();
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
    UT_LOG_ERROR("DRM_L3_MODE must be AES_CTR, AES_CBC, or AES_CBC_CTS");
    return false;
}

bool loadFixture(SampleFixture* fixture) {
    const char* mimeType = vts_drm_config_value("DRM_L3_MIME_TYPE");
    if (mimeType == nullptr || mimeType[0] == '\0') {
        UT_LOG_ERROR("DRM_L3_MIME_TYPE is not set");
        return false;
    }
    bool useHttp = false;
    if (!vts_drm_license_uses_http("L3", &useHttp)) {
        return false;
    }
    if (!useHttp && !readFile("DRM_L3_LICENSE_RESPONSE", &fixture->licenseResponse)) {
        return false;
    }
    fixture->mimeType = mimeType;

    if (!readFile("DRM_L3_INIT_DATA", &fixture->initData) ||
            !readFile("DRM_L3_ENCRYPTED_SAMPLE", &fixture->ciphertext) ||
            !readFile("DRM_L3_EXPECTED_PLAINTEXT", &fixture->plaintext) ||
            !decodeHex("DRM_L3_KID_HEX", &fixture->keyId) ||
            !decodeHex("DRM_L3_IV_HEX", &fixture->iv) ||
            !parseMode(vts_drm_config_value("DRM_L3_MODE"), &fixture->mode)) {
        return false;
    }

    if (fixture->keyId.size() != 16 || fixture->iv.size() != 16) {
        UT_LOG_ERROR("DRM_L3_KID_HEX and DRM_L3_IV_HEX must each decode to 16 bytes");
        return false;
    }
    if (fixture->ciphertext.size() != fixture->plaintext.size()) {
        UT_LOG_ERROR("Encrypted and expected plaintext sample sizes differ");
        return false;
    }
    if (fixture->ciphertext.size() >
            static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
        UT_LOG_ERROR("DRM L3 sample exceeds the AVBuffer API size limit");
        return false;
    }
    return true;
}

class LicensedSession {
public:
    bool open(const SampleFixture& fixture) {
        Uuid uuid;
        if (!vts_drm_getSupportedUuid(&uuid).isOk() ||
                !vts_drm_createPlugins(uuid, &drm, &crypto).isOk()) {
            UT_LOG_ERROR("Unable to create DRM and crypto plugins");
            return false;
        }
        if (drm == nullptr || crypto == nullptr) {
            UT_LOG_ERROR("DRM factory returned a null plugin");
            return false;
        }

        SecurityLevel level;
        if (!vts_drm_parse_security_level(
                    vts_drm_config_value("DRM_L3_SECURITY_LEVEL"), &level)) {
            return false;
        }
        Status status = drm->openSession(level, &session);
        if (!status.isOk() || session.empty()) {
            UT_LOG_ERROR("Unable to open DRM session");
            return false;
        }

        KeyRequest request;
        status = drm->getKeyRequest(
                session,
                fixture.initData,
                fixture.mimeType,
                KeyType::STREAMING,
                {},
                &request);
        if (!status.isOk() || request.request.empty()) {
            UT_LOG_ERROR("Unable to create a streaming key request");
            return false;
        }

        com::rdk::hal::drm::KeySetId keySetId;
        bool useHttp = false;
        if (!vts_drm_license_uses_http("L3", &useHttp)) {
            return false;
        }
        std::vector<uint8_t> response = fixture.licenseResponse;
        if (useHttp && !vts_drm_request_http_license("L3", request.request, &response)) {
            return false;
        }
        status = drm->provideKeyResponse(session, response, &keySetId);
        if (!status.isOk()) {
            UT_LOG_ERROR("DRM license response was rejected");
            return false;
        }
        status = crypto->setMediaDrmSession(session);
        if (!status.isOk()) {
            UT_LOG_ERROR("Unable to bind the DRM session to the crypto plugin");
            return false;
        }
        return true;
    }

    bool close() {
        if (drm == nullptr || session.empty()) {
            return true;
        }
        const Status status = drm->closeSession(session);
        if (!status.isOk()) {
            UT_LOG_ERROR("Unable to close DRM session");
            return false;
        }
        session.clear();
        return true;
    }

    ~LicensedSession() {
        if (!close()) {
            UT_LOG_ERROR("DRM session cleanup failed");
        }
    }

    sp<IDrmPlugin> drm;
    sp<ICryptoPlugin> crypto;
    std::vector<uint8_t> session;
};


class OpenDrmSession {
public:
    bool open(const sp<IDrmPlugin>& plugin, SecurityLevel level) {
        mDrm = plugin;
        const Status status = mDrm->openSession(level, &mSession);
        if (!status.isOk() || mSession.empty()) {
            UT_LOG_ERROR("Unable to open DRM session at the requested security level");
            return false;
        }
        return true;
    }

    const std::vector<uint8_t>& id() const {
        return mSession;
    }

    bool close() {
        if (mDrm == nullptr || mSession.empty()) {
            return true;
        }
        const Status status = mDrm->closeSession(mSession);
        if (!status.isOk()) {
            UT_LOG_ERROR("Unable to close DRM session");
            return false;
        }
        mSession.clear();
        return true;
    }

    ~OpenDrmSession() {
        if (!close()) {
            UT_LOG_ERROR("DRM session cleanup failed");
        }
    }

private:
    sp<IDrmPlugin> mDrm;
    std::vector<uint8_t> mSession;
};

const char* codecMimeType() {
    const char* codec = vts_drm_config_value("DRM_L3_CODEC_MIME_TYPE");
    if (codec != nullptr && codec[0] != '\0') {
        return codec;
    }
    if (vts_drm_active_profile()->scheme == "clearkey") {
        return vts_drm_config_value("DRM_L3_MIME_TYPE");
    }
    UT_LOG_ERROR("Vendor L3 tests require sample.codec_mime_type (for example video/avc), not a container MIME");
    return nullptr;
}

bool requiresProtectedOutput(const LicensedSession& licensed, bool* required) {
    const char* mime = codecMimeType();
    if (mime == nullptr) {
        return false;
    }
    SecurityLevel level = SecurityLevel::UNKNOWN;
    if (!licensed.drm->getSecurityLevel(licensed.session, &level).isOk() ||
            level < SecurityLevel::SW_SECURE_CRYPTO || level > SecurityLevel::HW_SECURE_ALL ||
            !licensed.drm->requiresSecureDecoder(mime, level, required).isOk()) {
        UT_LOG_ERROR("Unable to query session secure-decoder requirement");
        return false;
    }
    bool cryptoRequired = false;
    if (!licensed.crypto->requiresSecureDecoderComponent(mime, &cryptoRequired).isOk()) {
        UT_LOG_ERROR("Unable to query Crypto secure-decoder requirement");
        return false;
    }
    *required = *required || cryptoRequired;
    return true;
}

bool probeProtectedOutput(bool* required) {
    Uuid uuid;
    LicensedSession probe;
    SecurityLevel level;
    if (!vts_drm_getSupportedUuid(&uuid).isOk() ||
            !vts_drm_createPlugins(uuid, &probe.drm, &probe.crypto).isOk() ||
            !vts_drm_parse_security_level(
                    vts_drm_config_value("DRM_L3_SECURITY_LEVEL"), &level)) {
        UT_LOG_ERROR("Unable to prepare DRM output-capability probe");
        return false;
    }
    if (!probe.drm->openSession(level, &probe.session).isOk() || probe.session.empty() ||
            !probe.crypto->setMediaDrmSession(probe.session).isOk()) {
        UT_LOG_ERROR("Unable to open/bind DRM output-capability probe session");
        return false;
    }
    const bool success = requiresProtectedOutput(probe, required);
    const bool closed = probe.close();
    return success && closed;
}

}  // namespace

class L3Cases : public DrmSchemeTest {
public:
    explicit L3Cases(const char* scheme) : DrmSchemeTest(scheme, "L3") {}
    void decryptSampleWithAvBuffer_pos();
    void removedKeysAndClosedSessionRejectDecrypt_neg();
    void secureSessionAndDecoderRequirement_pos();
};

/**
 * @brief Decrypts a configured encrypted sample through the AVBuffer AIDL path.
 *
 * **Test Group ID:** L3: 03@n
 * **Test Case ID:** 001@n
 * **Priority:** High@n
 * **Pre-Conditions:** L3 enabled through YAML or environment and valid sample/license fixtures@n
 * **Dependencies:** DRM, AVBuffer, and AVBufferHelper services@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Acquire a streaming key and bind the DRM session|Init data and license response|Key response is accepted and session is bound|Should Pass|
 * |02|Decrypt the sample using source and destination AVBuffers|KID, IV, mode, subsample|AIDL decrypt succeeds and reports the sample byte count|Should Pass|
 * |03|Compare output with the independent plaintext fixture|Decrypted and expected bytes|All bytes match exactly|Does not validate decoder playback|
 * |04|Release DRM and AVBuffer resources|Session and allocated handles|All resources are released|Should Pass|
 */
void L3Cases::decryptSampleWithAvBuffer_pos() {
    if (!enabled()) {
        return;
    }
    bool protectedOutput = false;
    UT_ASSERT_TRUE_FATAL(probeProtectedOutput(&protectedOutput));
    if (protectedOutput) {
        UT_LOG_INFO("SKIP: secure decoder required; CPU-readable plaintext comparison is not applicable");
        UT_IGNORE_TEST();
    }
    SampleFixture fixture;
    UT_ASSERT_TRUE_FATAL(loadFixture(&fixture));

    LicensedSession licensed;
    UT_ASSERT_TRUE_FATAL(licensed.open(fixture));
    UT_ASSERT_TRUE_FATAL(requiresProtectedOutput(licensed, &protectedOutput));
    if (protectedOutput) {
        UT_ASSERT_TRUE_FATAL(licensed.close());
        UT_LOG_INFO("SKIP: secure decoder required; CPU-readable plaintext comparison is not applicable");
        UT_IGNORE_TEST();
    }
    SampleBuffers buffers;
    UT_ASSERT_TRUE_FATAL(buffers.open(fixture.ciphertext.size()));
    UT_ASSERT_TRUE_FATAL(buffers.writeSource(fixture.ciphertext));

    int32_t decryptedBytes = 0;
    DRM_CRYPTO_PLUGIN_DECRYPT_SUCCESS(
            licensed.crypto, sampleDecryptArgs(fixture, buffers), decryptedBytes);
    UT_ASSERT_EQUAL(decryptedBytes, static_cast<int32_t>(fixture.plaintext.size()));

    std::vector<uint8_t> decrypted;
    UT_ASSERT_TRUE_FATAL(buffers.readDestination(&decrypted, fixture.plaintext.size()));
    UT_ASSERT_TRUE(decrypted == fixture.plaintext);
    UT_ASSERT_TRUE(buffers.cleanup());
    UT_ASSERT_TRUE(licensed.close());
}

/**
 * @brief Confirms removed keys and closed sessions cannot decrypt the sample.
 *
 * **Test Group ID:** L3: 03@n
 * **Test Case ID:** 002@n
 * **Priority:** High@n
 * **Pre-Conditions:** L3 enabled through YAML or environment and valid sample/license fixtures@n
 * **Dependencies:** DRM, AVBuffer, and AVBufferHelper services@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Acquire a streaming key and prepare an AVBuffer sample|Valid fixture|Session and buffers are ready|Should Pass|
 * |02|Remove session keys and retry decrypt|Active session and sample|Returns ERROR_DRM_NO_LICENSE|Negative case|
 * |03|Close the DRM session and retry decrypt|Closed session and sample|Returns ERROR_DRM_SESSION_NOT_OPENED|Stale-key use is rejected|
 * |04|Release resources|Session and allocated handles|All resources are released|Should Pass|
 */
void L3Cases::removedKeysAndClosedSessionRejectDecrypt_neg() {
    if (!enabled()) {
        return;
    }
    bool protectedOutput = false;
    UT_ASSERT_TRUE_FATAL(probeProtectedOutput(&protectedOutput));
    if (protectedOutput) {
        UT_LOG_INFO("SKIP: secure decoder required; non-secure AVBuffer negative tests are not applicable");
        UT_IGNORE_TEST();
    }
    SampleFixture fixture;
    UT_ASSERT_TRUE_FATAL(loadFixture(&fixture));

    LicensedSession licensed;
    UT_ASSERT_TRUE_FATAL(licensed.open(fixture));
    UT_ASSERT_TRUE_FATAL(requiresProtectedOutput(licensed, &protectedOutput));
    if (protectedOutput) {
        UT_ASSERT_TRUE_FATAL(licensed.close());
        UT_LOG_INFO("SKIP: secure decoder required; non-secure AVBuffer negative tests are not applicable");
        UT_IGNORE_TEST();
    }
    SampleBuffers buffers;
    UT_ASSERT_TRUE_FATAL(buffers.open(fixture.ciphertext.size()));
    UT_ASSERT_TRUE_FATAL(buffers.writeSource(fixture.ciphertext));
    const DecryptArgs args = sampleDecryptArgs(fixture, buffers);

    int32_t decryptedBytes = 0;
    DRM_CRYPTO_PLUGIN_DECRYPT_SUCCESS(licensed.crypto, args, decryptedBytes);
    UT_ASSERT_EQUAL_FATAL(decryptedBytes, static_cast<int32_t>(fixture.plaintext.size()));
    std::vector<uint8_t> baseline;
    UT_ASSERT_TRUE_FATAL(buffers.readDestination(&baseline, fixture.plaintext.size()));
    UT_ASSERT_TRUE_FATAL(baseline == fixture.plaintext);
    DRM_PLUGIN_REMOVE_KEYS_SUCCESS(licensed.drm, licensed.session);
    Status status = licensed.crypto->decrypt(args, &decryptedBytes);
    UT_ASSERT_TRUE(hasServiceSpecificError(status, DrmErrors::ERROR_DRM_NO_LICENSE));

    UT_ASSERT_TRUE_FATAL(licensed.close());
    status = licensed.crypto->decrypt(args, &decryptedBytes);
    UT_ASSERT_TRUE(hasServiceSpecificError(
            status, DrmErrors::ERROR_DRM_SESSION_NOT_OPENED));

    UT_ASSERT_TRUE(buffers.cleanup());
}

/**
 * @brief Checks admission at a configured security level and secure-decoder requirement.
 *
 * This test checks DRM capability only; it does not prove secure buffer use,
 * decoder routing, or protected output policy.
 *
 * **Test Group ID:** L3: 03@n
 * **Test Case ID:** 003@n
 * **Priority:** High@n
 * **Pre-Conditions:** L3 and secure-session test enabled through YAML or environment, and target secure-session settings@n
 * **Dependencies:** DRM factory with the configured security level@n
 * **User Interaction:** None@n
 * **Test Procedure:**@n
 * |Variation / Step|Description|Test Data|Expected Result|Notes|
 * |----------------|-----------|---------|---------------|-----|
 * |01|Open a DRM session at the configured security level|DRM_L3_REQUIRED_SECURITY_LEVEL|Session reports the requested level|Should Pass|
 * |02|Query secure decoder requirements|DRM_L3_MIME_TYPE|The crypto plugin requires a secure decoder|Capability only|
 * |03|Close the DRM session|Session ID|Session closes successfully|Should Pass|
 */
void L3Cases::secureSessionAndDecoderRequirement_pos() {
    if (!enabled() || !secureSessionTestEnabled()) {
        UT_IGNORE_TEST();
    }
    SecurityLevel requiredLevel = SecurityLevel::UNKNOWN;
    UT_ASSERT_TRUE_FATAL(vts_drm_parse_security_level(
            vts_drm_config_value("DRM_L3_REQUIRED_SECURITY_LEVEL"), &requiredLevel));
    UT_ASSERT_TRUE_FATAL(requiredLevel != SecurityLevel::DEFAULT);

    const char* mimeType = vts_drm_config_value("DRM_L3_MIME_TYPE");
    UT_ASSERT_TRUE_FATAL(mimeType != nullptr && mimeType[0] != '\0');
    const char* codec = codecMimeType();
    UT_ASSERT_TRUE_FATAL(codec != nullptr && codec[0] != '\0');
    bool supported = false;
    UT_ASSERT_TRUE_FATAL(vts_drm_content_supported(mimeType, requiredLevel, &supported));
    if (!supported && !vts_drm_active_profile()->required) {
        UT_LOG_INFO("SKIP: optional secure-session security level not advertised");
        UT_IGNORE_TEST();
    }
    UT_ASSERT_TRUE_FATAL(supported);

    Uuid uuid;
    UT_ASSERT_TRUE_FATAL(vts_drm_getSupportedUuid(&uuid).isOk());
    sp<IDrmPlugin> drm;
    sp<ICryptoPlugin> crypto;
    UT_ASSERT_TRUE_FATAL(vts_drm_createPlugins(uuid, &drm, &crypto).isOk());
    UT_ASSERT_TRUE_FATAL(drm != nullptr);
    UT_ASSERT_TRUE_FATAL(crypto != nullptr);

    OpenDrmSession session;
    UT_ASSERT_TRUE_FATAL(session.open(drm, requiredLevel));

    SecurityLevel actualLevel = SecurityLevel::UNKNOWN;
    DRM_PLUGIN_GET_SECURITY_LEVEL_SUCCESS(drm, session.id(), actualLevel);
    UT_ASSERT_EQUAL(actualLevel, requiredLevel);

    DRM_CRYPTO_PLUGIN_SET_MEDIA_DRM_SESSION_SUCCESS(crypto, session.id());
    bool secureDecoderRequired = false;
    DRM_CRYPTO_PLUGIN_REQUIRES_SECURE_DECODER_COMPONENT_SUCCESS(
            crypto, codec, secureDecoderRequired);
    UT_ASSERT_TRUE(secureDecoderRequired);

    UT_ASSERT_TRUE(session.close());
}

#define DRM_L3_SCHEME(Suite, Scheme) \
    class Suite : public L3Cases { \
    public: Suite() : L3Cases(Scheme) {} \
    }; \
    UT_ADD_TEST_TO_GROUP(Suite, UT_TESTS_L3) \
    UT_ADD_TEST(Suite, decryptSampleWithAvBuffer_pos) { L3Cases::decryptSampleWithAvBuffer_pos(); } \
    UT_ADD_TEST(Suite, removedKeysAndClosedSessionRejectDecrypt_neg) { \
        L3Cases::removedKeysAndClosedSessionRejectDecrypt_neg(); \
    } \
    UT_ADD_TEST(Suite, secureSessionAndDecoderRequirement_pos) { \
        L3Cases::secureSessionAndDecoderRequirement_pos(); \
    }

DRM_L3_SCHEME(VTS_L3_Drm, "clearkey")
DRM_L3_SCHEME(VTS_L3_DrmWidevine, "widevine")
DRM_L3_SCHEME(VTS_L3_DrmPlayReady, "playready")
#undef DRM_L3_SCHEME

/** @} */ // End of DRM_HALTEST_L3
/** @} */ // End of DRM_HALTEST
/** @} */ // End of DRM
/** @} */ // End of HPK
