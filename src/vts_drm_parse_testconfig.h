// SPDX-License-Identifier: Apache-2.0
#ifndef VTS_DRM_PARSE_TESTCONFIG_H
#define VTS_DRM_PARSE_TESTCONFIG_H

#include <ut.h>

#define DRM_TEST_CONFIG_PATH "./drm_test_configuration/drm_test_config.yaml"

UT_status_t vts_drm_parse_testconfig(const char* path);
void vts_drm_clear_testconfig();
const char* vts_drm_config_value(const char* variable);

#endif
