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

sp<IDrmFactory> gDrmFactory;
std::string gDrmFactoryServiceName;

UT_status_t VTS_Drm_init() {
    const char* configuredInstance = std::getenv("DRM_FACTORY_INSTANCE");
    const std::string instance = configuredInstance == nullptr || configuredInstance[0] == '\0'
            ? "clearkey"
            : configuredInstance;
    gDrmFactoryServiceName = "com.rdk.hal.drm.IDrmFactory/" + instance;

    sp<android::IBinder> binder = android::defaultServiceManager()->getService(
            String16(gDrmFactoryServiceName.c_str()));
    if (binder == nullptr) {
        UT_LOG_ERROR("DRM factory service unavailable: %s", gDrmFactoryServiceName.c_str());
        return UT_STATUS_FAILURE;
    }

    gDrmFactory = android::interface_cast<IDrmFactory>(binder);
    if (gDrmFactory == nullptr) {
        UT_LOG_ERROR("Unable to cast DRM factory service: %s", gDrmFactoryServiceName.c_str());
        return UT_STATUS_FAILURE;
    }
    return UT_STATUS_OK;
}

void VTS_Drm_destroy() {
    gDrmFactory.clear();
    gDrmFactoryServiceName.clear();
}

Status vts_drm_getSupportedUuid(Uuid* uuid) {
    com::rdk::hal::drm::CryptoSchemes schemes;
    const Status status = gDrmFactory->getSupportedCryptoSchemes(&schemes);
    if (!status.isOk() || schemes.uuids.empty()) {
        return status.isOk() ? Status::fromStatusT(android::BAD_VALUE) : status;
    }
    *uuid = schemes.uuids.front();
    return Status::ok();
}

Status vts_drm_createPlugins(
        const Uuid& uuid,
        sp<IDrmPlugin>* drmPlugin,
        sp<ICryptoPlugin>* cryptoPlugin) {
    Status status = gDrmFactory->createDrmPlugin(
            uuid, String16("com.rdk.vts.drm"), drmPlugin);
    if (!status.isOk()) {
        return status;
    }
    return gDrmFactory->createCryptoPlugin(uuid, {}, cryptoPlugin);
}

/** @} */ // End of DRM_HALTEST_INIT
/** @} */ // End of DRM_HALTEST
/** @} */ // End of DRM
/** @} */ // End of HPK