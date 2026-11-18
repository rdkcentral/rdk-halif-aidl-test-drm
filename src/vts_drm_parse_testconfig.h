// SPDX-License-Identifier: Apache-2.0
#ifndef VTS_DRM_PARSE_TESTCONFIG_H
#define VTS_DRM_PARSE_TESTCONFIG_H

#include <ut.h>
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#define DRM_TEST_CONFIG_PATH "./drm_test_configuration/drm_test_config.yaml"

UT_status_t vts_drm_parse_testconfig(const char* path);
void vts_drm_clear_testconfig();
const char* vts_drm_config_value(const char* variable);

struct DrmTestProfile {
    std::string scheme;
    std::string instance;
    std::array<uint8_t, 16> uuid;
    bool required = true;
    std::map<std::string, std::string> values;
};

const std::vector<DrmTestProfile>& vts_drm_test_profiles();
const DrmTestProfile* vts_drm_active_profile();
bool vts_drm_select_profile(const char* scheme);
void vts_drm_reset_profile();

#endif
