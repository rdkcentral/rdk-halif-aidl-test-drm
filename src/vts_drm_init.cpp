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
 * @defgroup DRM_HALTEST_INIT DRM HAL Test Initialization
 * @{
 * @parblock
 *
 * ### Initialization functions for DRM VTS :
 *
 * This file resolves the configured DRM factory service, initializes shared
 * Binder state, and exposes plugin-creation helpers used by the test cases.
 *
 * **Pre-Conditions:** Binder service manager is available@n
 * **Dependencies:** Registered DRM AIDL factory and generated DRM interface@n
 *
 * @endparblock
 */

/**
 * @file vts_drm_init.cpp
 *
 */

#include "vts_drm.h"
#include "vts_drm_init.h"

#include <cstdlib>
#include <algorithm>
#include <com/rdk/hal/drm/SecurityLevel.h>
#include <ut_kvp_profile.h>

sp<IDrmFactory> gDrmFactory;
std::string gDrmFactoryServiceName;
DrmHfpCryptoSchemes gDrmHfpCryptoSchemes;

UT_status_t VTS_Drm_init() {
    gDrmHfpCryptoSchemes = {};
    if (vts_drm_parse_hfp(
                ut_kvp_profile_getInstance(), "drm", &gDrmHfpCryptoSchemes) != UT_STATUS_OK) {
        UT_LOG_ERROR("Failed to process the DRM HFP file");
        VTS_Drm_destroy();
        return UT_STATUS_FAILURE;
    }

    const char* configPath = std::getenv("DRM_TEST_CONFIG");
    if (vts_drm_parse_testconfig(configPath == nullptr ? DRM_TEST_CONFIG_PATH : configPath) !=
            UT_STATUS_OK) {
        VTS_Drm_destroy();
        return UT_STATUS_FAILURE;
    }
    // L1 retains one factory; L2/L3 select and restore each configured scheme.
    for (const auto& profile : vts_drm_test_profiles()) {
        vts_drm_select_profile(profile.scheme.c_str());
        const char* instance = vts_drm_config_value("DRM_FACTORY_INSTANCE");
        if (instance == nullptr || instance[0] == '\0') {
            UT_LOG_ERROR("DRM factory instance must not be empty");
            VTS_Drm_destroy();
            return UT_STATUS_FAILURE;
        }
        const std::string service = std::string("com.rdk.hal.drm.IDrmFactory/") + instance;
        const sp<android::IBinder> binder = android::defaultServiceManager()->checkService(
                String16(service.c_str()));
        if (binder != nullptr) {
            const sp<IDrmFactory> factory = android::interface_cast<IDrmFactory>(binder);
            if (factory != nullptr) {
                if (gDrmFactory == nullptr) {
                    gDrmFactory = factory;
                    gDrmFactoryServiceName = service;
                }
                continue;
            }
        }
        if (profile.required) {
            UT_LOG_ERROR("Required DRM factory service unavailable: %s", service.c_str());
            VTS_Drm_destroy();
            return UT_STATUS_FAILURE;
        }
    }
    vts_drm_reset_profile();
    if (gDrmFactory != nullptr) {
        return UT_STATUS_OK;
    }
    UT_LOG_ERROR("No configured DRM factory service is available");
    VTS_Drm_destroy();
    return UT_STATUS_FAILURE;
}

void VTS_Drm_destroy() {
    gDrmFactory.clear();
    gDrmFactoryServiceName.clear();
    gDrmHfpCryptoSchemes = {};
    vts_drm_clear_testconfig();
}

void DrmSchemeTest::SetUp() {
    mPreviousFactory = gDrmFactory;
    mPreviousService = gDrmFactoryServiceName;
    if (!vts_drm_select_profile(mScheme)) {
        UT_LOG_INFO("SKIP %s: scheme not enabled in configuration", mScheme);
        UT_IGNORE_TEST();
    }
    const std::string enableVariable = std::string("DRM_") + mLevel + "_ENABLE";
    const char* enabled = vts_drm_config_value(enableVariable.c_str());
    if (enabled == nullptr || std::string(enabled) == "0") {
        UT_LOG_INFO("SKIP %s %s: tests disabled", mScheme, mLevel);
        UT_IGNORE_TEST();
    }
    UT_ASSERT_TRUE_FATAL(std::string(enabled) == "1");
    const DrmTestProfile* activeProfile = vts_drm_active_profile();
    UT_ASSERT_TRUE_FATAL(activeProfile != nullptr);
    const DrmTestProfile& profile = *activeProfile;
    const char* configuredInstance = vts_drm_config_value("DRM_FACTORY_INSTANCE");
    UT_ASSERT_TRUE_FATAL(configuredInstance != nullptr && configuredInstance[0] != '\0');
    const std::string instance = configuredInstance;
    gDrmFactoryServiceName = "com.rdk.hal.drm.IDrmFactory/" + instance;
    const sp<android::IBinder> binder = android::defaultServiceManager()->checkService(
            String16(gDrmFactoryServiceName.c_str()));
    if (binder == nullptr) {
        if (!profile.required) {
            UT_LOG_INFO("SKIP %s: optional factory unavailable: %s",
                    mScheme, gDrmFactoryServiceName.c_str());
            UT_IGNORE_TEST();
        }
        UT_FAIL_FATAL("Required DRM factory service unavailable");
    }
    gDrmFactory = android::interface_cast<IDrmFactory>(binder);
    UT_ASSERT_TRUE_FATAL(gDrmFactory != nullptr);
    com::rdk::hal::drm::CryptoSchemes schemes;
    UT_ASSERT_TRUE_FATAL(gDrmFactory->getSupportedCryptoSchemes(&schemes).isOk());
    Uuid uuid;
    uuid.uuid = profile.uuid;
    const bool advertised = std::find(schemes.uuids.begin(), schemes.uuids.end(), uuid) !=
            schemes.uuids.end();
    if (!advertised && !profile.required) {
        UT_LOG_INFO("SKIP %s: optional scheme UUID not advertised", mScheme);
        UT_IGNORE_TEST();
    }
    UT_ASSERT_TRUE_FATAL(advertised);
    const std::string prefix = std::string("DRM_") + mLevel;
    const char* mime = vts_drm_config_value((prefix + "_MIME_TYPE").c_str());
    UT_ASSERT_TRUE_FATAL(mime != nullptr && mime[0] != '\0');
    com::rdk::hal::drm::SecurityLevel level;
    UT_ASSERT_TRUE_FATAL(vts_drm_parse_security_level(
            vts_drm_config_value((prefix + "_SECURITY_LEVEL").c_str()), &level));
    bool supported = false;
    UT_ASSERT_TRUE_FATAL(vts_drm_content_supported(mime, level, &supported));
    if (!supported && !profile.required) {
        UT_LOG_INFO("SKIP %s: optional MIME/security-level combination not advertised", mScheme);
        UT_IGNORE_TEST();
    }
    UT_ASSERT_TRUE_FATAL(supported);
    UT_LOG_INFO("Testing %s %s using %s", mScheme, mLevel, gDrmFactoryServiceName.c_str());
}

void DrmSchemeTest::TearDown() {
    gDrmFactory = mPreviousFactory;
    gDrmFactoryServiceName = mPreviousService;
    vts_drm_reset_profile();
}

Status vts_drm_getSupportedUuid(Uuid* uuid) {
    com::rdk::hal::drm::CryptoSchemes schemes;
    const Status status = gDrmFactory->getSupportedCryptoSchemes(&schemes);
    if (!status.isOk() || schemes.uuids.empty()) {
        return status.isOk() ? Status::fromStatusT(android::BAD_VALUE) : status;
    }
    if (const auto* profile = vts_drm_active_profile()) {
        uuid->uuid = profile->uuid;
        if (std::find(schemes.uuids.begin(), schemes.uuids.end(), *uuid) == schemes.uuids.end()) {
            UT_LOG_ERROR("Configured DRM scheme UUID is not advertised");
            return Status::fromStatusT(android::BAD_VALUE);
        }
    } else {
        *uuid = schemes.uuids.front();
    }
    return Status::ok();
}

Status vts_drm_createPlugins(
        const Uuid& uuid,
        sp<IDrmPlugin>* drmPlugin,
        sp<ICryptoPlugin>* cryptoPlugin) {
    Status status = gDrmFactory->createDrmPlugin(
            uuid, DRM_APP_PACKAGE_NAME, drmPlugin);
    if (!status.isOk()) {
        return status;
    }
    if (*drmPlugin == nullptr) {
        UT_LOG_ERROR("DRM factory returned a null DRM plugin");
        return Status::fromStatusT(android::BAD_VALUE);
    }
    status = gDrmFactory->createCryptoPlugin(uuid, {}, cryptoPlugin);
    if (status.isOk() && *cryptoPlugin == nullptr) {
        UT_LOG_ERROR("DRM factory returned a null crypto plugin");
        return Status::fromStatusT(android::BAD_VALUE);
    }
    return status;
}

bool vts_drm_parse_security_level(
        const char* value, com::rdk::hal::drm::SecurityLevel* level) {
    using com::rdk::hal::drm::SecurityLevel;
    if (value != nullptr) {
        const std::pair<const char*, SecurityLevel> levels[] = {
            {"DEFAULT", SecurityLevel::DEFAULT},
            {"SW_SECURE_CRYPTO", SecurityLevel::SW_SECURE_CRYPTO},
            {"SW_SECURE_DECODE", SecurityLevel::SW_SECURE_DECODE},
            {"HW_SECURE_CRYPTO", SecurityLevel::HW_SECURE_CRYPTO},
            {"HW_SECURE_DECODE", SecurityLevel::HW_SECURE_DECODE},
            {"HW_SECURE_ALL", SecurityLevel::HW_SECURE_ALL}
        };
        for (const auto& entry : levels) {
            if (value == std::string(entry.first)) {
                *level = entry.second;
                return true;
            }
        }
    }
    UT_LOG_ERROR("Invalid or missing DRM security level");
    return false;
}

bool vts_drm_collect_security_levels(
        const com::rdk::hal::drm::CryptoSchemes& schemes,
        std::set<com::rdk::hal::drm::SecurityLevel>& levels) {
    using com::rdk::hal::drm::SecurityLevel;
    levels.clear();
    std::set<SecurityLevel> supported;
    for (const auto& content : schemes.mimeTypes) {
        if (content.minLevel < SecurityLevel::SW_SECURE_CRYPTO ||
                content.maxLevel > SecurityLevel::HW_SECURE_ALL ||
                content.minLevel > content.maxLevel) {
            UT_LOG_ERROR("Invalid advertised security-level range for %s", content.mime.c_str());
            return false;
        }
        for (int32_t level = static_cast<int32_t>(content.minLevel);
                level <= static_cast<int32_t>(content.maxLevel); ++level) {
            supported.insert(static_cast<SecurityLevel>(level));
        }
    }
    if (supported.empty()) {
        UT_LOG_ERROR("No security-level ranges are advertised");
        return false;
    }
    levels.swap(supported);
    return true;
}

bool vts_drm_content_supported(
        const std::string& mime, com::rdk::hal::drm::SecurityLevel level, bool* supported) {
    com::rdk::hal::drm::CryptoSchemes schemes;
    if (!gDrmFactory->getSupportedCryptoSchemes(&schemes).isOk()) {
        UT_LOG_ERROR("Unable to query DRM content capabilities");
        return false;
    }
    *supported = false;
    for (const auto& content : schemes.mimeTypes) {
        if (content.mime == mime &&
                (content.minLevel < com::rdk::hal::drm::SecurityLevel::SW_SECURE_CRYPTO ||
                 content.maxLevel > com::rdk::hal::drm::SecurityLevel::HW_SECURE_ALL ||
                 content.minLevel > content.maxLevel)) {
            UT_LOG_ERROR("Factory advertised an invalid security range for %s", mime.c_str());
            return false;
        }
        if (content.mime == mime &&
                (level == com::rdk::hal::drm::SecurityLevel::DEFAULT ||
                 (level >= content.minLevel && level <= content.maxLevel))) {
            *supported = true;
        }
    }
    return true;
}

/** @} */ // End of DRM_HALTEST_INIT
/** @} */ // End of DRM_HALTEST
/** @} */ // End of DRM
/** @} */ // End of HPK