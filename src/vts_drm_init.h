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
 * @defgroup DRM_HALTEST_INIT_HEADER DRM HAL Test Initialization Header
 * @{
 * @parblock
 *
 * ### Initialization declarations for DRM VTS :
 *
 * This header declares DRM Binder service initialization and cleanup for the
 * unified VTS test process.
 *
 * **Pre-Conditions:** None@n
 * **Dependencies:** UT-Core and Android Binder@n
 *
 * @endparblock
 */

/**
 * @file vts_drm_init.h
 *
 */

#ifndef VTS_DRM_INIT_H
#define VTS_DRM_INIT_H

#include <ut.h>

UT_status_t VTS_Drm_init();
void VTS_Drm_destroy();

#endif

/** @} */ // End of DRM_HALTEST_INIT_HEADER
/** @} */ // End of DRM_HALTEST
/** @} */ // End of DRM
/** @} */ // End of HPK