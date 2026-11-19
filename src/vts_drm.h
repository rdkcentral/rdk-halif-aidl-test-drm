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
 * @defgroup DRM_HALTEST_HEADER DRM HAL Test Common Header
 * @{
 * @parblock
 *
 * ### Common declarations used by DRM VTS tests :
 *
 * This header provides shared Binder types, service declarations, and helper
 * function declarations and per-scheme setup for DRM L1/L2/L3 test files.
 *
 * **Pre-Conditions:** None@n
 * **Dependencies:** Generated DRM AIDL headers and UT-Core@n
 *
 * @endparblock
 */

/**
 * @file vts_drm.h
 *
 */

#ifndef VTS_DRM_H
#define VTS_DRM_H

#include <binder/IServiceManager.h>
#include <binder/Status.h>
#include <utils/String16.h>

#include <com/rdk/hal/drm/IDrmFactory.h>
#include <com/rdk/hal/drm/ICryptoPlugin.h>
#include <com/rdk/hal/drm/IDrmPlugin.h>
#include <com/rdk/hal/drm/DrmErrors.h>

#include <ut.h>
#include <ut_log.h>
#include "vts_drm_parse_hfp.h"
#include "vts_drm_parse_testconfig.h"

#include <set>
#include <string>
#include <vector>

using android::sp;
using android::String16;
using android::binder::Status;
using com::rdk::hal::drm::ICryptoPlugin;
using com::rdk::hal::drm::IDrmFactory;
using com::rdk::hal::drm::IDrmPlugin;
using com::rdk::hal::drm::Uuid;

extern sp<IDrmFactory> gDrmFactory;
extern std::string gDrmFactoryServiceName;
extern DrmHfpCryptoSchemes gDrmHfpCryptoSchemes;

Status vts_drm_getSupportedUuid(Uuid* uuid);
Status vts_drm_createPlugins(
        const Uuid& uuid,
        sp<IDrmPlugin>* drmPlugin,
        sp<ICryptoPlugin>* cryptoPlugin);

bool vts_drm_parse_security_level(
        const char* value, com::rdk::hal::drm::SecurityLevel* level);
bool vts_drm_collect_security_levels(
        const com::rdk::hal::drm::CryptoSchemes& schemes,
        std::set<com::rdk::hal::drm::SecurityLevel>& levels);
bool vts_drm_content_supported(
        const std::string& mime, com::rdk::hal::drm::SecurityLevel level, bool* supported);

class DrmSchemeTest : public UTCore {
public:
    DrmSchemeTest(const char* scheme, const char* level) : mScheme(scheme), mLevel(level) {}
    void SetUp() override;
    void TearDown() override;

private:
    const char* mScheme;
    const char* mLevel;
    sp<IDrmFactory> mPreviousFactory;
    std::string mPreviousService;
};

#define DRM_APP_PACKAGE_NAME "com.rdk.vts.drm"

#define DRM_STATUS_SUCCESS(status) \
    do { \
        const Status& drmResult = (status); \
        UT_ASSERT_TRUE_FATAL(drmResult.isOk()) << drmResult.toString8().c_str(); \
        UT_ASSERT_EQUAL_FATAL(drmResult.exceptionCode(), Status::EX_NONE); \
    } while (0)

#define DRM_CALL_SUCCESS(call) \
    do { \
        UT_LOG_INFO("Calling %s", #call); \
        const Status drmStatus = (call); \
        UT_LOG_INFO("Result %s, status:[isOK:%s, exception:%d, error:%d]", \
                #call, drmStatus.isOk() ? "true" : "false", \
                drmStatus.exceptionCode(), drmStatus.serviceSpecificErrorCode()); \
        DRM_STATUS_SUCCESS(drmStatus); \
    } while (0)

#define DRM_CALL_FAILURE(call, expectedError) \
    do { \
        UT_LOG_INFO("Calling %s", #call); \
        const Status drmStatus = (call); \
        UT_LOG_INFO("Result %s, status:[isOK:%s, exception:%d, error:%d]", \
                #call, drmStatus.isOk() ? "true" : "false", \
                drmStatus.exceptionCode(), drmStatus.serviceSpecificErrorCode()); \
        UT_ASSERT_FALSE(drmStatus.isOk()) << #call; \
        UT_ASSERT_EQUAL(drmStatus.exceptionCode(), Status::EX_SERVICE_SPECIFIC) << #call; \
        UT_ASSERT_EQUAL(drmStatus.serviceSpecificErrorCode(), \
                static_cast<int32_t>(expectedError)) << #call; \
    } while (0)

#define DRM_CALL_FAILURE_ONE_OF(call, firstError, secondError) \
    do { \
        UT_LOG_INFO("Calling %s", #call); \
        const Status drmStatus = (call); \
        UT_LOG_INFO("Result %s, status:[isOK:%s, exception:%d, error:%d]", \
                #call, drmStatus.isOk() ? "true" : "false", \
                drmStatus.exceptionCode(), drmStatus.serviceSpecificErrorCode()); \
        UT_ASSERT_FALSE(drmStatus.isOk()) << #call; \
        UT_ASSERT_EQUAL(drmStatus.exceptionCode(), Status::EX_SERVICE_SPECIFIC) << #call; \
        const int32_t error = drmStatus.serviceSpecificErrorCode(); \
        UT_ASSERT_TRUE(error == static_cast<int32_t>(firstError) || \
                error == static_cast<int32_t>(secondError)) << #call; \
    } while (0)

#define DRM_PLUGIN_CLOSE_SESSION_FAILURE(drmPlugin, session, expectedError) \
    DRM_CALL_FAILURE((drmPlugin)->closeSession(session), expectedError)
#define DRM_PLUGIN_GET_PROPERTY_BYTE_ARRAY_FAILURE(drmPlugin, property, value, expectedError) \
    DRM_CALL_FAILURE((drmPlugin)->getPropertyByteArray(property, &(value)), expectedError)
#define DRM_PLUGIN_SET_PROPERTY_STRING_FAILURE(drmPlugin, property, value, expectedError) \
    DRM_CALL_FAILURE((drmPlugin)->setPropertyString(property, value), expectedError)
#define DRM_PLUGIN_SET_PROPERTY_BYTE_ARRAY_FAILURE(drmPlugin, property, value, expectedError) \
    DRM_CALL_FAILURE((drmPlugin)->setPropertyByteArray(property, value), expectedError)
#define DRM_PLUGIN_QUERY_KEY_STATUS_FAILURE(drmPlugin, session, keyStatus, expectedError) \
    DRM_CALL_FAILURE((drmPlugin)->queryKeyStatus(session, &(keyStatus)), expectedError)
#define DRM_PLUGIN_REMOVE_KEYS_FAILURE(drmPlugin, session, expectedError) \
    DRM_CALL_FAILURE((drmPlugin)->removeKeys(session), expectedError)
#define DRM_PLUGIN_GET_KEY_REQUEST_FAILURE(drmPlugin, scope, initData, mime, keyType, parameters, request, expectedError) \
    DRM_CALL_FAILURE((drmPlugin)->getKeyRequest( \
            scope, initData, mime, keyType, parameters, &(request)), expectedError)
#define DRM_PLUGIN_GET_KEY_REQUEST_FAILURE_ONE_OF( \
        drmPlugin, scope, initData, mime, keyType, parameters, request, firstError, secondError) \
    DRM_CALL_FAILURE_ONE_OF((drmPlugin)->getKeyRequest( \
            scope, initData, mime, keyType, parameters, &(request)), firstError, secondError)
#define DRM_PLUGIN_PROVIDE_KEY_RESPONSE_FAILURE(drmPlugin, scope, response, keySetId, expectedError) \
    DRM_CALL_FAILURE((drmPlugin)->provideKeyResponse(scope, response, &(keySetId)), expectedError)
#define DRM_PLUGIN_PROVIDE_PROVISION_RESPONSE_FAILURE(drmPlugin, response, result, expectedError) \
    DRM_CALL_FAILURE((drmPlugin)->provideProvisionResponse(response, &(result)), expectedError)
#define DRM_PLUGIN_SET_PLAYBACK_ID_FAILURE(drmPlugin, session, playbackId, expectedError) \
    DRM_CALL_FAILURE((drmPlugin)->setPlaybackId(session, playbackId), expectedError)
#define DRM_PLUGIN_SET_CIPHER_ALGORITHM_FAILURE(drmPlugin, session, algorithm, expectedError) \
    DRM_CALL_FAILURE((drmPlugin)->setCipherAlgorithm(session, algorithm), expectedError)
#define DRM_PLUGIN_SET_MAC_ALGORITHM_FAILURE(drmPlugin, session, algorithm, expectedError) \
    DRM_CALL_FAILURE((drmPlugin)->setMacAlgorithm(session, algorithm), expectedError)
#define DRM_PLUGIN_DECRYPT_FAILURE(drmPlugin, session, keyId, input, iv, output, expectedError) \
    DRM_CALL_FAILURE((drmPlugin)->decrypt(session, keyId, input, iv, &(output)), expectedError)
#define DRM_PLUGIN_ENCRYPT_FAILURE(drmPlugin, session, keyId, input, iv, output, expectedError) \
    DRM_CALL_FAILURE((drmPlugin)->encrypt(session, keyId, input, iv, &(output)), expectedError)
#define DRM_PLUGIN_SIGN_FAILURE(drmPlugin, session, keyId, message, signature, expectedError) \
    DRM_CALL_FAILURE((drmPlugin)->sign(session, keyId, message, &(signature)), expectedError)
#define DRM_PLUGIN_VERIFY_FAILURE(drmPlugin, session, keyId, message, signature, verified, expectedError) \
    DRM_CALL_FAILURE((drmPlugin)->verify(session, keyId, message, signature, &(verified)), expectedError)
#define DRM_PLUGIN_SIGN_RSA_FAILURE(drmPlugin, session, algorithm, message, wrappedKey, signature, expectedError) \
    DRM_CALL_FAILURE((drmPlugin)->signRSA( \
            session, algorithm, message, wrappedKey, &(signature)), expectedError)
#define DRM_CRYPTO_PLUGIN_SET_MEDIA_DRM_SESSION_FAILURE(cryptoPlugin, session, expectedError) \
    DRM_CALL_FAILURE((cryptoPlugin)->setMediaDrmSession(session), expectedError)

#define DRM_FACTORY_GET_SUPPORTED_CRYPTO_SCHEMES_SUCCESS(schemes) \
    do { \
        Status drmStatus; \
        UT_LOG_INFO("Calling IDrmFactory::getSupportedCryptoSchemes()"); \
        drmStatus = gDrmFactory->getSupportedCryptoSchemes(&(schemes)); \
        UT_LOG_INFO( \
                "Result IDrmFactory::getSupportedCryptoSchemes(), status:[isOK:%s, exception:%d], uuidCount:%zu", \
                drmStatus.isOk() ? "true" : "false", \
                drmStatus.exceptionCode(), \
                (schemes).uuids.size()); \
        UT_ASSERT_TRUE_FATAL(drmStatus.isOk()); \
        UT_ASSERT_EQUAL_FATAL(drmStatus.exceptionCode(), Status::EX_NONE); \
    } while (0)

#define DRM_FACTORY_CREATE_DRM_PLUGIN_SUCCESS(uuid, appPackageName, drmPlugin) \
    do { \
        UT_LOG_INFO("Calling IDrmFactory::createDrmPlugin()"); \
        const Status drmStatus = gDrmFactory->createDrmPlugin( \
                (uuid), (appPackageName), &(drmPlugin)); \
        UT_LOG_INFO( \
                "Result IDrmFactory::createDrmPlugin(), status:[isOK:%s, exception:%d], drmPlugin:%p", \
                drmStatus.isOk() ? "true" : "false", \
                drmStatus.exceptionCode(), \
                static_cast<void*>((drmPlugin).get())); \
        UT_ASSERT_TRUE_FATAL(drmStatus.isOk()); \
        UT_ASSERT_EQUAL_FATAL(drmStatus.exceptionCode(), Status::EX_NONE); \
        UT_ASSERT_TRUE_FATAL((drmPlugin) != nullptr); \
    } while (0)

#define DRM_FACTORY_CREATE_DRM_PLUGIN_FAILURE(uuid, appPackageName, drmPlugin) \
    do { \
        UT_LOG_INFO("Calling IDrmFactory::createDrmPlugin() with an unsupported UUID"); \
        const Status drmStatus = gDrmFactory->createDrmPlugin( \
                (uuid), (appPackageName), &(drmPlugin)); \
        UT_LOG_INFO( \
                "Result IDrmFactory::createDrmPlugin(), status:[isOK:%s, exception:%d, error:%d], drmPlugin:%p", \
                drmStatus.isOk() ? "true" : "false", \
                drmStatus.exceptionCode(), \
                drmStatus.serviceSpecificErrorCode(), \
                static_cast<void*>((drmPlugin).get())); \
        UT_ASSERT_FALSE(drmStatus.isOk()); \
        UT_ASSERT_EQUAL(drmStatus.exceptionCode(), Status::EX_SERVICE_SPECIFIC); \
        UT_ASSERT_EQUAL(drmStatus.serviceSpecificErrorCode(), \
                static_cast<int32_t>(com::rdk::hal::drm::DrmErrors::ERROR_DRM_CANNOT_HANDLE)); \
        UT_ASSERT_TRUE((drmPlugin) == nullptr); \
    } while (0)

#define DRM_FACTORY_CREATE_CRYPTO_PLUGIN_SUCCESS(uuid, initData, cryptoPlugin) \
    do { \
        UT_LOG_INFO("Calling IDrmFactory::createCryptoPlugin()"); \
        const Status drmStatus = gDrmFactory->createCryptoPlugin( \
                (uuid), initData, &(cryptoPlugin)); \
        UT_LOG_INFO( \
                "Result IDrmFactory::createCryptoPlugin(), status:[isOK:%s, exception:%d], cryptoPlugin:%p", \
                drmStatus.isOk() ? "true" : "false", \
                drmStatus.exceptionCode(), \
                static_cast<void*>((cryptoPlugin).get())); \
        UT_ASSERT_TRUE_FATAL(drmStatus.isOk()); \
        UT_ASSERT_EQUAL_FATAL(drmStatus.exceptionCode(), Status::EX_NONE); \
        UT_ASSERT_TRUE_FATAL((cryptoPlugin) != nullptr); \
    } while (0)

#define DRM_FACTORY_CREATE_CRYPTO_PLUGIN_FAILURE(uuid, initData, cryptoPlugin) \
    do { \
        UT_LOG_INFO("Calling IDrmFactory::createCryptoPlugin() with an unsupported UUID"); \
        const Status drmStatus = gDrmFactory->createCryptoPlugin( \
                (uuid), initData, &(cryptoPlugin)); \
        UT_LOG_INFO( \
                "Result IDrmFactory::createCryptoPlugin(), status:[isOK:%s, exception:%d, error:%d], cryptoPlugin:%p", \
                drmStatus.isOk() ? "true" : "false", \
                drmStatus.exceptionCode(), \
                drmStatus.serviceSpecificErrorCode(), \
                static_cast<void*>((cryptoPlugin).get())); \
        UT_ASSERT_TRUE(drmStatus.isOk() || drmStatus.exceptionCode() == Status::EX_SERVICE_SPECIFIC); \
        UT_ASSERT_TRUE((cryptoPlugin) == nullptr); \
    } while (0)

#define DRM_PLUGIN_OPEN_SESSION(drmPlugin, level, session, resultStatus) \
    do { \
        UT_LOG_INFO("Calling IDrmPlugin::openSession(), level:%d", static_cast<int32_t>(level)); \
        (resultStatus) = (drmPlugin)->openSession((level), &(session)); \
        UT_LOG_INFO("Result IDrmPlugin::openSession(), status:[isOK:%s, exception:%d, error:%d]", \
                (resultStatus).isOk() ? "true" : "false", \
                (resultStatus).exceptionCode(), (resultStatus).serviceSpecificErrorCode()); \
    } while (0)

#define DRM_PLUGIN_OPEN_SESSION_SUCCESS(drmPlugin, level, session) \
    do { \
        UT_LOG_INFO("Calling IDrmPlugin::openSession(), level:%d", static_cast<int32_t>(level)); \
        const Status drmStatus = (drmPlugin)->openSession((level), &(session)); \
        UT_LOG_INFO("Result IDrmPlugin::openSession(), status:[isOK:%s, exception:%d, error:%d]", \
                drmStatus.isOk() ? "true" : "false", \
                drmStatus.exceptionCode(), drmStatus.serviceSpecificErrorCode()); \
        UT_ASSERT_TRUE_FATAL(drmStatus.isOk()); \
        UT_ASSERT_EQUAL_FATAL(drmStatus.exceptionCode(), Status::EX_NONE); \
    } while (0)

#define DRM_PLUGIN_CLOSE_SESSION_SUCCESS(drmPlugin, session) \
    do { \
        UT_LOG_INFO("Calling IDrmPlugin::closeSession()"); \
        const Status drmStatus = (drmPlugin)->closeSession((session)); \
        UT_LOG_INFO("Result IDrmPlugin::closeSession(), status:[isOK:%s, exception:%d, error:%d]", \
                drmStatus.isOk() ? "true" : "false", \
                drmStatus.exceptionCode(), drmStatus.serviceSpecificErrorCode()); \
        UT_ASSERT_TRUE_FATAL(drmStatus.isOk()); \
        UT_ASSERT_EQUAL_FATAL(drmStatus.exceptionCode(), Status::EX_NONE); \
    } while (0)

#define DRM_PLUGIN_GET_SECURITY_LEVEL_SUCCESS(drmPlugin, session, level) \
    do { \
        UT_LOG_INFO("Calling IDrmPlugin::getSecurityLevel()"); \
        const Status drmStatus = (drmPlugin)->getSecurityLevel((session), &(level)); \
        UT_LOG_INFO("Result IDrmPlugin::getSecurityLevel(), status:[isOK:%s, exception:%d, error:%d]", \
                drmStatus.isOk() ? "true" : "false", \
                drmStatus.exceptionCode(), drmStatus.serviceSpecificErrorCode()); \
        UT_ASSERT_TRUE_FATAL(drmStatus.isOk()); \
        UT_ASSERT_EQUAL_FATAL(drmStatus.exceptionCode(), Status::EX_NONE); \
    } while (0)

#define DRM_PLUGIN_GET_SECURITY_LEVEL_FAILURE(drmPlugin, session, level, expectedError) \
    do { \
        UT_LOG_INFO("Calling IDrmPlugin::getSecurityLevel()"); \
        const Status drmStatus = (drmPlugin)->getSecurityLevel(session, &(level)); \
        UT_LOG_INFO("Result IDrmPlugin::getSecurityLevel(), status:[isOK:%s, exception:%d, error:%d]", \
                drmStatus.isOk() ? "true" : "false", \
                drmStatus.exceptionCode(), drmStatus.serviceSpecificErrorCode()); \
        UT_ASSERT_FALSE(drmStatus.isOk()) << "IDrmPlugin::getSecurityLevel()"; \
        UT_ASSERT_EQUAL(drmStatus.exceptionCode(), Status::EX_SERVICE_SPECIFIC) \
                << "IDrmPlugin::getSecurityLevel()"; \
        UT_ASSERT_EQUAL(drmStatus.serviceSpecificErrorCode(), \
                static_cast<int32_t>(expectedError)) << "IDrmPlugin::getSecurityLevel()"; \
    } while (0)

#define DRM_PLUGIN_GET_HDCP_LEVELS_SUCCESS(drmPlugin, levels) \
    do { \
        UT_LOG_INFO("Calling IDrmPlugin::getHdcpLevels()"); \
        const Status drmStatus = (drmPlugin)->getHdcpLevels(&(levels)); \
        UT_LOG_INFO("Result IDrmPlugin::getHdcpLevels(), status:[isOK:%s, exception:%d, error:%d]", \
                drmStatus.isOk() ? "true" : "false", \
                drmStatus.exceptionCode(), drmStatus.serviceSpecificErrorCode()); \
        UT_ASSERT_TRUE_FATAL(drmStatus.isOk()); \
        UT_ASSERT_EQUAL_FATAL(drmStatus.exceptionCode(), Status::EX_NONE); \
    } while (0)

#define DRM_PLUGIN_GET_NUMBER_OF_SESSIONS_SUCCESS(drmPlugin, sessions) \
    do { \
        UT_LOG_INFO("Calling IDrmPlugin::getNumberOfSessions()"); \
        const Status drmStatus = (drmPlugin)->getNumberOfSessions(&(sessions)); \
        UT_LOG_INFO("Result IDrmPlugin::getNumberOfSessions(), status:[isOK:%s, exception:%d, error:%d]", \
                drmStatus.isOk() ? "true" : "false", \
                drmStatus.exceptionCode(), drmStatus.serviceSpecificErrorCode()); \
        UT_ASSERT_TRUE_FATAL(drmStatus.isOk()); \
        UT_ASSERT_EQUAL_FATAL(drmStatus.exceptionCode(), Status::EX_NONE); \
    } while (0)

#define DRM_PLUGIN_GET_METRICS_SUCCESS(drmPlugin, metrics) \
    do { \
        UT_LOG_INFO("Calling IDrmPlugin::getMetrics()"); \
        const Status drmStatus = (drmPlugin)->getMetrics(&(metrics)); \
        UT_LOG_INFO("Result IDrmPlugin::getMetrics(), status:[isOK:%s, exception:%d, error:%d]", \
                drmStatus.isOk() ? "true" : "false", \
                drmStatus.exceptionCode(), drmStatus.serviceSpecificErrorCode()); \
        if (!drmStatus.isOk() && \
                drmStatus.exceptionCode() == Status::EX_SERVICE_SPECIFIC && \
                drmStatus.serviceSpecificErrorCode() == static_cast<int32_t>( \
                        com::rdk::hal::drm::DrmErrors::ERROR_DRM_INVALID_STATE)) { \
            UT_LOG_INFO("SKIP: provider reports metrics are unavailable"); \
            UT_IGNORE_TEST(); \
        } \
        UT_ASSERT_TRUE_FATAL(drmStatus.isOk()) << drmStatus.toString8().c_str(); \
        UT_ASSERT_EQUAL_FATAL(drmStatus.exceptionCode(), Status::EX_NONE); \
    } while (0)

#define DRM_PLUGIN_GET_METRICS_FAILURE(drmPlugin, metrics, expectedError) \
    do { \
        UT_LOG_INFO("Calling IDrmPlugin::getMetrics()"); \
        const Status drmStatus = (drmPlugin)->getMetrics(&(metrics)); \
        UT_LOG_INFO("Result IDrmPlugin::getMetrics(), status:[isOK:%s, exception:%d, error:%d]", \
                drmStatus.isOk() ? "true" : "false", \
                drmStatus.exceptionCode(), drmStatus.serviceSpecificErrorCode()); \
        UT_ASSERT_FALSE(drmStatus.isOk()) << "IDrmPlugin::getMetrics()"; \
        UT_ASSERT_EQUAL(drmStatus.exceptionCode(), Status::EX_SERVICE_SPECIFIC) \
                << "IDrmPlugin::getMetrics()"; \
        UT_ASSERT_EQUAL(drmStatus.serviceSpecificErrorCode(), \
                static_cast<int32_t>(expectedError)) << "IDrmPlugin::getMetrics()"; \
    } while (0)

#define DRM_PLUGIN_GET_PROPERTY_STRING_SUCCESS(drmPlugin, property, value) \
    do { \
        UT_LOG_INFO("Calling IDrmPlugin::getPropertyString()"); \
        const Status drmStatus = (drmPlugin)->getPropertyString((property), &(value)); \
        UT_LOG_INFO("Result IDrmPlugin::getPropertyString(), status:[isOK:%s, exception:%d, error:%d]", \
                drmStatus.isOk() ? "true" : "false", \
                drmStatus.exceptionCode(), drmStatus.serviceSpecificErrorCode()); \
        UT_ASSERT_TRUE_FATAL(drmStatus.isOk()); \
        UT_ASSERT_EQUAL_FATAL(drmStatus.exceptionCode(), Status::EX_NONE); \
    } while (0)

#define DRM_PLUGIN_GET_PROPERTY_STRING_FAILURE(drmPlugin, property, value, expectedError) \
    do { \
        UT_LOG_INFO("Calling IDrmPlugin::getPropertyString()"); \
        const Status drmStatus = (drmPlugin)->getPropertyString((property), &(value)); \
        UT_LOG_INFO("Result IDrmPlugin::getPropertyString(), status:[isOK:%s, exception:%d, error:%d]", \
                drmStatus.isOk() ? "true" : "false", \
                drmStatus.exceptionCode(), drmStatus.serviceSpecificErrorCode()); \
        UT_ASSERT_FALSE(drmStatus.isOk()) << "IDrmPlugin::getPropertyString()"; \
        UT_ASSERT_EQUAL(drmStatus.exceptionCode(), Status::EX_SERVICE_SPECIFIC) \
                << "IDrmPlugin::getPropertyString()"; \
        UT_ASSERT_EQUAL(drmStatus.serviceSpecificErrorCode(), \
                static_cast<int32_t>(expectedError)) << "IDrmPlugin::getPropertyString()"; \
    } while (0)

#define DRM_PLUGIN_GET_PROPERTY_BYTE_ARRAY_SUCCESS(drmPlugin, property, value) \
    do { \
        UT_LOG_INFO("Calling IDrmPlugin::getPropertyByteArray()"); \
        const Status drmStatus = (drmPlugin)->getPropertyByteArray((property), &(value)); \
        UT_LOG_INFO("Result IDrmPlugin::getPropertyByteArray(), status:[isOK:%s, exception:%d, error:%d]", \
                drmStatus.isOk() ? "true" : "false", \
                drmStatus.exceptionCode(), drmStatus.serviceSpecificErrorCode()); \
        if (!drmStatus.isOk() && \
                drmStatus.exceptionCode() == Status::EX_SERVICE_SPECIFIC && \
                drmStatus.serviceSpecificErrorCode() == static_cast<int32_t>( \
                        com::rdk::hal::drm::DrmErrors::ERROR_DRM_CANNOT_HANDLE)) { \
            UT_LOG_INFO("SKIP: provider does not support this byte-array property"); \
            UT_IGNORE_TEST(); \
        } \
        UT_ASSERT_TRUE_FATAL(drmStatus.isOk()) << drmStatus.toString8().c_str(); \
        UT_ASSERT_EQUAL_FATAL(drmStatus.exceptionCode(), Status::EX_NONE); \
    } while (0)

#define DRM_PLUGIN_QUERY_KEY_STATUS_SUCCESS(drmPlugin, session, keyStatus) \
    do { \
        UT_LOG_INFO("Calling IDrmPlugin::queryKeyStatus()"); \
        const Status drmStatus = (drmPlugin)->queryKeyStatus((session), &(keyStatus)); \
        UT_LOG_INFO("Result IDrmPlugin::queryKeyStatus(), status:[isOK:%s, exception:%d, error:%d]", \
                drmStatus.isOk() ? "true" : "false", \
                drmStatus.exceptionCode(), drmStatus.serviceSpecificErrorCode()); \
        UT_ASSERT_TRUE_FATAL(drmStatus.isOk()); \
        UT_ASSERT_EQUAL_FATAL(drmStatus.exceptionCode(), Status::EX_NONE); \
    } while (0)

#define DRM_PLUGIN_REMOVE_KEYS_SUCCESS(drmPlugin, session) \
    do { \
        UT_LOG_INFO("Calling IDrmPlugin::removeKeys()"); \
        const Status drmStatus = (drmPlugin)->removeKeys((session)); \
        UT_LOG_INFO("Result IDrmPlugin::removeKeys(), status:[isOK:%s, exception:%d, error:%d]", \
                drmStatus.isOk() ? "true" : "false", \
                drmStatus.exceptionCode(), drmStatus.serviceSpecificErrorCode()); \
        UT_ASSERT_TRUE_FATAL(drmStatus.isOk()); \
        UT_ASSERT_EQUAL_FATAL(drmStatus.exceptionCode(), Status::EX_NONE); \
    } while (0)

#define DRM_PLUGIN_GET_KEY_REQUEST_SUCCESS(drmPlugin, scope, initData, mime, keyType, parameters, request) \
    do { \
        UT_LOG_INFO("Calling IDrmPlugin::getKeyRequest()"); \
        const Status drmStatus = (drmPlugin)->getKeyRequest( \
                (scope), (initData), (mime), (keyType), parameters, &(request)); \
        UT_LOG_INFO("Result IDrmPlugin::getKeyRequest(), status:[isOK:%s, exception:%d, error:%d]", \
                drmStatus.isOk() ? "true" : "false", \
                drmStatus.exceptionCode(), drmStatus.serviceSpecificErrorCode()); \
        UT_ASSERT_TRUE_FATAL(drmStatus.isOk()); \
        UT_ASSERT_EQUAL_FATAL(drmStatus.exceptionCode(), Status::EX_NONE); \
    } while (0)

#define DRM_PLUGIN_GET_PROVISION_REQUEST(drmPlugin, certificateType, certificateAuthority, request, resultStatus) \
    do { \
        UT_LOG_INFO("Calling IDrmPlugin::getProvisionRequest()"); \
        (resultStatus) = (drmPlugin)->getProvisionRequest( \
                (certificateType), (certificateAuthority), &(request)); \
        UT_LOG_INFO("Result IDrmPlugin::getProvisionRequest(), status:[isOK:%s, exception:%d, error:%d]", \
                (resultStatus).isOk() ? "true" : "false", \
                (resultStatus).exceptionCode(), (resultStatus).serviceSpecificErrorCode()); \
    } while (0)

#define DRM_PLUGIN_PROVIDE_PROVISION_RESPONSE_SUCCESS(drmPlugin, response, result) \
    do { \
        UT_LOG_INFO("Calling IDrmPlugin::provideProvisionResponse()"); \
        const Status drmStatus = (drmPlugin)->provideProvisionResponse((response), &(result)); \
        UT_LOG_INFO("Result IDrmPlugin::provideProvisionResponse(), status:[isOK:%s, exception:%d, error:%d]", \
                drmStatus.isOk() ? "true" : "false", \
                drmStatus.exceptionCode(), drmStatus.serviceSpecificErrorCode()); \
        UT_ASSERT_TRUE_FATAL(drmStatus.isOk()); \
        UT_ASSERT_EQUAL_FATAL(drmStatus.exceptionCode(), Status::EX_NONE); \
    } while (0)

#define DRM_PLUGIN_SET_LISTENER_SUCCESS(drmPlugin, listener) \
    do { \
        UT_LOG_INFO("Calling IDrmPlugin::setListener()"); \
        const Status drmStatus = (drmPlugin)->setListener((listener)); \
        UT_LOG_INFO("Result IDrmPlugin::setListener(), status:[isOK:%s, exception:%d, error:%d]", \
                drmStatus.isOk() ? "true" : "false", \
                drmStatus.exceptionCode(), drmStatus.serviceSpecificErrorCode()); \
        UT_ASSERT_TRUE_FATAL(drmStatus.isOk()); \
        UT_ASSERT_EQUAL_FATAL(drmStatus.exceptionCode(), Status::EX_NONE); \
    } while (0)

#define DRM_PLUGIN_SET_PLAYBACK_ID_SUCCESS(drmPlugin, session, playbackId) \
    do { \
        UT_LOG_INFO("Calling IDrmPlugin::setPlaybackId()"); \
        const Status drmStatus = (drmPlugin)->setPlaybackId((session), (playbackId)); \
        UT_LOG_INFO("Result IDrmPlugin::setPlaybackId(), status:[isOK:%s, exception:%d, error:%d]", \
                drmStatus.isOk() ? "true" : "false", \
                drmStatus.exceptionCode(), drmStatus.serviceSpecificErrorCode()); \
        UT_ASSERT_TRUE_FATAL(drmStatus.isOk()); \
        UT_ASSERT_EQUAL_FATAL(drmStatus.exceptionCode(), Status::EX_NONE); \
    } while (0)

#define DRM_PLUGIN_SET_CIPHER_ALGORITHM_SUCCESS(drmPlugin, session, algorithm) \
    do { \
        UT_LOG_INFO("Calling IDrmPlugin::setCipherAlgorithm()"); \
        const Status drmStatus = (drmPlugin)->setCipherAlgorithm((session), (algorithm)); \
        UT_LOG_INFO("Result IDrmPlugin::setCipherAlgorithm(), status:[isOK:%s, exception:%d, error:%d]", \
                drmStatus.isOk() ? "true" : "false", \
                drmStatus.exceptionCode(), drmStatus.serviceSpecificErrorCode()); \
        UT_ASSERT_TRUE_FATAL(drmStatus.isOk()); \
        UT_ASSERT_EQUAL_FATAL(drmStatus.exceptionCode(), Status::EX_NONE); \
    } while (0)

#define DRM_PLUGIN_SET_MAC_ALGORITHM_SUCCESS(drmPlugin, session, algorithm) \
    do { \
        UT_LOG_INFO("Calling IDrmPlugin::setMacAlgorithm()"); \
        const Status drmStatus = (drmPlugin)->setMacAlgorithm((session), (algorithm)); \
        UT_LOG_INFO("Result IDrmPlugin::setMacAlgorithm(), status:[isOK:%s, exception:%d, error:%d]", \
                drmStatus.isOk() ? "true" : "false", \
                drmStatus.exceptionCode(), drmStatus.serviceSpecificErrorCode()); \
        UT_ASSERT_TRUE_FATAL(drmStatus.isOk()); \
        UT_ASSERT_EQUAL_FATAL(drmStatus.exceptionCode(), Status::EX_NONE); \
    } while (0)

#define DRM_PLUGIN_DECRYPT_SUCCESS(drmPlugin, session, keyId, input, iv, output) \
    do { \
        UT_LOG_INFO("Calling IDrmPlugin::decrypt()"); \
        const Status drmStatus = (drmPlugin)->decrypt( \
                (session), (keyId), (input), (iv), &(output)); \
        UT_LOG_INFO("Result IDrmPlugin::decrypt(), status:[isOK:%s, exception:%d, error:%d]", \
                drmStatus.isOk() ? "true" : "false", \
                drmStatus.exceptionCode(), drmStatus.serviceSpecificErrorCode()); \
        UT_ASSERT_TRUE_FATAL(drmStatus.isOk()); \
        UT_ASSERT_EQUAL_FATAL(drmStatus.exceptionCode(), Status::EX_NONE); \
    } while (0)

#define DRM_PLUGIN_REQUIRES_SECURE_DECODER_SUCCESS(drmPlugin, mime, level, required) \
    do { \
        UT_LOG_INFO("Calling IDrmPlugin::requiresSecureDecoder()"); \
        const Status drmStatus = (drmPlugin)->requiresSecureDecoder((mime), (level), &(required)); \
        UT_LOG_INFO("Result IDrmPlugin::requiresSecureDecoder(), status:[isOK:%s, exception:%d, error:%d]", \
                drmStatus.isOk() ? "true" : "false", \
                drmStatus.exceptionCode(), drmStatus.serviceSpecificErrorCode()); \
        UT_ASSERT_TRUE_FATAL(drmStatus.isOk()); \
        UT_ASSERT_EQUAL_FATAL(drmStatus.exceptionCode(), Status::EX_NONE); \
    } while (0)

#define DRM_CRYPTO_PLUGIN_SET_MEDIA_DRM_SESSION_SUCCESS(cryptoPlugin, session) \
    do { \
        UT_LOG_INFO("Calling ICryptoPlugin::setMediaDrmSession()"); \
        const Status drmStatus = (cryptoPlugin)->setMediaDrmSession((session)); \
        UT_LOG_INFO("Result ICryptoPlugin::setMediaDrmSession(), status:[isOK:%s, exception:%d, error:%d]", \
                drmStatus.isOk() ? "true" : "false", \
                drmStatus.exceptionCode(), drmStatus.serviceSpecificErrorCode()); \
        UT_ASSERT_TRUE_FATAL(drmStatus.isOk()); \
        UT_ASSERT_EQUAL_FATAL(drmStatus.exceptionCode(), Status::EX_NONE); \
    } while (0)

#define DRM_CRYPTO_PLUGIN_NOTIFY_RESOLUTION_SUCCESS(cryptoPlugin, width, height) \
    do { \
        UT_LOG_INFO("Calling ICryptoPlugin::notifyResolution()"); \
        const Status drmStatus = (cryptoPlugin)->notifyResolution((width), (height)); \
        UT_LOG_INFO("Result ICryptoPlugin::notifyResolution(), status:[isOK:%s, exception:%d, error:%d]", \
                drmStatus.isOk() ? "true" : "false", \
                drmStatus.exceptionCode(), drmStatus.serviceSpecificErrorCode()); \
        UT_ASSERT_TRUE_FATAL(drmStatus.isOk()); \
        UT_ASSERT_EQUAL_FATAL(drmStatus.exceptionCode(), Status::EX_NONE); \
    } while (0)

#define DRM_CRYPTO_PLUGIN_REQUIRES_SECURE_DECODER_COMPONENT_SUCCESS(cryptoPlugin, mime, required) \
    do { \
        UT_LOG_INFO("Calling ICryptoPlugin::requiresSecureDecoderComponent()"); \
        const Status drmStatus = (cryptoPlugin)->requiresSecureDecoderComponent((mime), &(required)); \
        UT_LOG_INFO("Result ICryptoPlugin::requiresSecureDecoderComponent(), status:[isOK:%s, exception:%d, error:%d]", \
                drmStatus.isOk() ? "true" : "false", \
                drmStatus.exceptionCode(), drmStatus.serviceSpecificErrorCode()); \
        UT_ASSERT_TRUE_FATAL(drmStatus.isOk()); \
        UT_ASSERT_EQUAL_FATAL(drmStatus.exceptionCode(), Status::EX_NONE); \
    } while (0)

#define DRM_CRYPTO_PLUGIN_DECRYPT_SUCCESS(cryptoPlugin, args, decryptedBytes) \
    do { \
        UT_LOG_INFO("Calling ICryptoPlugin::decrypt()"); \
        const Status drmStatus = (cryptoPlugin)->decrypt((args), &(decryptedBytes)); \
        UT_LOG_INFO("Result ICryptoPlugin::decrypt(), status:[isOK:%s, exception:%d, error:%d]", \
                drmStatus.isOk() ? "true" : "false", \
                drmStatus.exceptionCode(), drmStatus.serviceSpecificErrorCode()); \
        UT_ASSERT_TRUE_FATAL(drmStatus.isOk()); \
        UT_ASSERT_EQUAL_FATAL(drmStatus.exceptionCode(), Status::EX_NONE); \
    } while (0)

#endif

/** @} */ // End of DRM_HALTEST_HEADER
/** @} */ // End of DRM_HALTEST
/** @} */ // End of DRM
/** @} */ // End of HPK