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
 * function declarations for DRM L1 and L2 test files.
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

#include "drm_string_compat.h"
#include <ut.h>
#include <ut_log.h>
#include "vts_drm_parse_testconfig.h"

#include <string>
#include <vector>

using android::sp;
using android::String16;
using android::binder::Status;
using com::rdk::hal::drm::ICryptoPlugin;
using com::rdk::hal::drm::IDrmFactory;
using com::rdk::hal::drm::IDrmPlugin;
using com::rdk::hal::drm::Uuid;
using ::rdk::drm_compat::DrmString;
using ::rdk::drm_compat::fromUtf8;

extern sp<IDrmFactory> gDrmFactory;
extern std::string gDrmFactoryServiceName;

Status vts_drm_getSupportedUuid(Uuid* uuid);
Status vts_drm_createPlugins(
        const Uuid& uuid,
        sp<IDrmPlugin>* drmPlugin,
        sp<ICryptoPlugin>* cryptoPlugin);

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

#endif

/** @} */ // End of DRM_HALTEST_HEADER
/** @} */ // End of DRM_HALTEST
/** @} */ // End of DRM
/** @} */ // End of HPK