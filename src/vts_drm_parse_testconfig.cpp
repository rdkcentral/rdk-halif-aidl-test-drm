// SPDX-License-Identifier: Apache-2.0
#include "vts_drm_parse_testconfig.h"

#include <ut_kvp.h>
#include <ut_log.h>

#include <cstdlib>
#include <map>
#include <memory>
#include <string>

namespace {

struct ConfigField {
    const char* variable;
    const char* path;
    const char* defaultValue;
    bool boolean = false;
};

const ConfigField fields[] = {
    {"DRM_FACTORY_INSTANCE", "drm.factory_instance", "clearkey"},
    {"DRM_L2_ENABLE", "drm.l2.enabled", "0", true},
    {"DRM_L2_INIT_DATA", "drm.l2.content.init_data_file", ""},
    {"DRM_L2_CIPHERTEXT", "drm.l2.content.encrypted_sample_file", ""},
    {"DRM_L2_PLAINTEXT", "drm.l2.content.expected_plaintext_file", ""},
    {"DRM_L2_KID_HEX", "drm.l2.content.kid_hex", ""},
    {"DRM_L2_KEY_HEX", "drm.l2.content.key_hex", ""},
    {"DRM_L2_IV_HEX", "drm.l2.content.iv_hex", ""},
    {"DRM_L2_LICENSE_RESPONSE", "drm.l2.content.license_response_file", ""},
    {"DRM_L3_ENABLE", "drm.l3.enabled", "0", true},
    {"DRM_L3_INIT_DATA", "drm.l3.sample.init_data_file", ""},
    {"DRM_L3_ENCRYPTED_SAMPLE", "drm.l3.sample.encrypted_sample_file", ""},
    {"DRM_L3_EXPECTED_PLAINTEXT", "drm.l3.sample.expected_plaintext_file", ""},
    {"DRM_L3_LICENSE_RESPONSE", "drm.l3.sample.license_response_file", ""},
    {"DRM_L3_KID_HEX", "drm.l3.sample.kid_hex", ""},
    {"DRM_L3_IV_HEX", "drm.l3.sample.iv_hex", ""},
    {"DRM_L3_MIME_TYPE", "drm.l3.sample.mime_type", ""},
    {"DRM_L3_MODE", "drm.l3.sample.mode", "AES_CTR"},
    {"DRM_L3_SECURE_SESSION_ENABLE", "drm.l3.secure_session.enabled", "0", true},
    {"DRM_L3_REQUIRED_SECURITY_LEVEL", "drm.l3.secure_session.required_security_level", ""}
};

std::map<std::string, std::string> configValues;

}  // namespace

UT_status_t vts_drm_parse_testconfig(const char* path) {
    vts_drm_clear_testconfig();
    if (path == nullptr || path[0] == '\0') {
        UT_LOG_ERROR("DRM test configuration path must not be empty");
        return UT_STATUS_FAILURE;
    }
    std::unique_ptr<ut_kvp_instance_t, decltype(&ut_kvp_destroyInstance)> instance(
            ut_kvp_createInstance(), ut_kvp_destroyInstance);
    if (!instance) {
        UT_LOG_ERROR("Unable to create DRM test configuration parser");
        return UT_STATUS_FAILURE;
    }
    if (ut_kvp_open(instance.get(), path) != UT_KVP_STATUS_SUCCESS) {
        UT_LOG_ERROR("Unable to load DRM test configuration: %s", path);
        return UT_STATUS_FAILURE;
    }
    if (!ut_kvp_fieldPresent(instance.get(), "drm")) {
        UT_LOG_ERROR("DRM test configuration must contain the drm section");
        return UT_STATUS_FAILURE;
    }

    std::map<std::string, std::string> values;
    for (const auto& field : fields) {
        std::string value = field.defaultValue;
        if (ut_kvp_fieldPresent(instance.get(), field.path)) {
            char buffer[UT_KVP_MAX_ELEMENT_SIZE] = {};
            if (ut_kvp_getStringField(instance.get(), field.path, buffer, sizeof(buffer)) !=
                    UT_KVP_STATUS_SUCCESS) {
                UT_LOG_ERROR("Unable to read DRM configuration field: %s", field.path);
                return UT_STATUS_FAILURE;
            }
            if (buffer[sizeof(buffer) - 1] != '\0') {
                UT_LOG_ERROR("%s exceeds the maximum configuration value length (%zu)",
                        field.path, sizeof(buffer) - 1);
                return UT_STATUS_FAILURE;
            }
            value = buffer;
            if (field.boolean) {
                if (value != "true" && value != "false") {
                    UT_LOG_ERROR("%s must be true or false", field.path);
                    return UT_STATUS_FAILURE;
                }
                value = value == "true" ? "1" : "0";
            }
        }
        values.emplace(field.variable, value);
    }
    configValues.swap(values);
    UT_LOG_INFO("Loaded DRM test configuration: %s", path);
    return UT_STATUS_OK;
}

void vts_drm_clear_testconfig() {
    configValues.clear();
}

const char* vts_drm_config_value(const char* variable) {
    const char* overrideValue = std::getenv(variable);
    if (overrideValue != nullptr) {
        return overrideValue;
    }
    const auto value = configValues.find(variable);
    return value == configValues.end() ? nullptr : value->second.c_str();
}
