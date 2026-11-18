// SPDX-License-Identifier: Apache-2.0
#include "vts_drm_parse_testconfig.h"
#include "vts_drm_parse_hfp.h"

#include <ut_kvp.h>
#include <ut_log.h>

#include <cstdlib>
#include <algorithm>
#include <cctype>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>

namespace {

struct ConfigField {
    const char* variable;
    const char* path;
    const char* defaultValue;
    bool boolean = false;
};

const ConfigField fields[] = {
    {"DRM_FACTORY_INSTANCE", "drm.factory_instance", "clearkey"},
    {"DRM_L1_INIT_DATA", "drm.l1.init_data_file", ""},
    {"DRM_L1_MIME_TYPE", "drm.l1.mime_type", "video/mp4"},
    {"DRM_PROVISIONING_ENABLE", "drm.provisioning.enabled", "0", true},
    {"DRM_PROVISIONING_URL", "drm.provisioning.url", ""},
    {"DRM_PROVISIONING_CONTENT_TYPE", "drm.provisioning.content_type", "application/octet-stream"},
    {"DRM_PROVISIONING_REQUEST_TIMEOUT_SECONDS", "drm.provisioning.request_timeout_seconds", "10"},
    {"DRM_PROVISIONING_CERTIFICATE_TYPE", "drm.provisioning.certificate_type", "X.509"},
    {"DRM_PROVISIONING_CERTIFICATE_AUTHORITY", "drm.provisioning.certificate_authority", ""},
    {"DRM_L2_ENABLE", "drm.l2.enabled", "0", true},
    {"DRM_L2_LICENSE_MODE", "drm.l2.license.mode", "local"},
    {"DRM_L2_LICENSE_URL", "drm.l2.license.url", "http://127.0.0.1:8765/license"},
    {"DRM_L2_LICENSE_REQUEST_TIMEOUT_SECONDS", "drm.l2.license.request_timeout_seconds", "10"},
    {"DRM_L2_INIT_DATA", "drm.l2.content.init_data_file", ""},
    {"DRM_L2_MIME_TYPE", "drm.l2.content.mime_type", "video/mp4"},
    {"DRM_L2_SECURITY_LEVEL", "drm.l2.security_level", "DEFAULT"},
    {"DRM_L2_LICENSE_CONTENT_TYPE", "drm.l2.license.content_type", "application/json"},
    {"DRM_L2_CIPHERTEXT", "drm.l2.content.encrypted_sample_file", ""},
    {"DRM_L2_PLAINTEXT", "drm.l2.content.expected_plaintext_file", ""},
    {"DRM_L2_MODE", "drm.l2.content.mode", "AES_CTR"},
    {"DRM_L2_PATTERN_ENCRYPT_BLOCKS", "drm.l2.content.pattern.encrypt_blocks", "0"},
    {"DRM_L2_PATTERN_SKIP_BLOCKS", "drm.l2.content.pattern.skip_blocks", "0"},
    {"DRM_L2_OFFLINE_LICENSE_RESPONSE", "drm.l2.content.offline_license_response_file", ""},
    {"DRM_L2_KID_HEX", "drm.l2.content.kid_hex", ""},
    {"DRM_L2_KEY_HEX", "drm.l2.content.key_hex", ""},
    {"DRM_L2_IV_HEX", "drm.l2.content.iv_hex", ""},
    {"DRM_L2_LICENSE_RESPONSE", "drm.l2.content.license_response_file", ""},
    {"DRM_L3_ENABLE", "drm.l3.enabled", "0", true},
    {"DRM_L3_SECURITY_LEVEL", "drm.l3.security_level", "DEFAULT"},
    {"DRM_L3_LICENSE_CONTENT_TYPE", "drm.l3.license.content_type", "application/json"},
    {"DRM_L3_LICENSE_MODE", "drm.l3.license.mode", "local"},
    {"DRM_L3_LICENSE_URL", "drm.l3.license.url", "http://127.0.0.1:8765/license"},
    {"DRM_L3_LICENSE_REQUEST_TIMEOUT_SECONDS", "drm.l3.license.request_timeout_seconds", "10"},
    {"DRM_L3_INIT_DATA", "drm.l3.sample.init_data_file", ""},
    {"DRM_L3_ENCRYPTED_SAMPLE", "drm.l3.sample.encrypted_sample_file", ""},
    {"DRM_L3_EXPECTED_PLAINTEXT", "drm.l3.sample.expected_plaintext_file", ""},
    {"DRM_L3_LICENSE_RESPONSE", "drm.l3.sample.license_response_file", ""},
    {"DRM_L3_KID_HEX", "drm.l3.sample.kid_hex", ""},
    {"DRM_L3_IV_HEX", "drm.l3.sample.iv_hex", ""},
    {"DRM_L3_MIME_TYPE", "drm.l3.sample.mime_type", ""},
    {"DRM_L3_CODEC_MIME_TYPE", "drm.l3.sample.codec_mime_type", ""},
    {"DRM_L3_MODE", "drm.l3.sample.mode", "AES_CTR"},
    {"DRM_L3_SECURE_SESSION_ENABLE", "drm.l3.secure_session.enabled", "0", true},
    {"DRM_L3_REQUIRED_SECURITY_LEVEL", "drm.l3.secure_session.required_security_level", ""}
};

std::map<std::string, std::string> configValues;
std::vector<DrmTestProfile> profiles;
const DrmTestProfile* activeProfile = nullptr;
bool matrixMode = false;

struct Scheme {
    const char* name;
    const char* uuid;
};

const Scheme schemes[] = {
    {"clearkey", "e2719d58-a985-b3c9-781a-b030af78d30e"},
    {"widevine", "edef8ba9-79d6-4ace-a3c8-27dcd51d21ed"},
    {"playready", "9a04f079-9840-4286-ab92-e65be0885f95"}
};

bool readValue(ut_kvp_instance_t* instance, const std::string& path,
        const char* fallback, bool boolean, std::string* value) {
    *value = fallback;
    if (!ut_kvp_fieldPresent(instance, path.c_str())) {
        return true;
    }
    char buffer[UT_KVP_MAX_ELEMENT_SIZE] = {};
    if (ut_kvp_getStringField(instance, path.c_str(), buffer, sizeof(buffer)) !=
            UT_KVP_STATUS_SUCCESS || buffer[sizeof(buffer) - 1] != '\0') {
        UT_LOG_ERROR("Invalid or oversized DRM configuration field: %s", path.c_str());
        return false;
    }
    *value = buffer;
    if (boolean) {
        if (*value != "true" && *value != "false") {
            UT_LOG_ERROR("%s must be true or false", path.c_str());
            return false;
        }
        *value = *value == "true" ? "1" : "0";
    }
    return true;
}

bool parseUuid(const std::string& text, std::array<uint8_t, 16>* uuid) {
    if (uuid == nullptr || text.size() != 36) {
        UT_LOG_ERROR("DRM UUID must use canonical 8-4-4-4-12 hexadecimal format");
        return false;
    }
    std::string hex;
    for (size_t i = 0; i < text.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (text[i] != '-') {
                UT_LOG_ERROR("Invalid DRM UUID separator");
                return false;
            }
        } else if (std::isxdigit(static_cast<unsigned char>(text[i]))) {
            hex += text[i];
        } else {
            UT_LOG_ERROR("Invalid DRM UUID hexadecimal digit");
            return false;
        }
    }
    for (size_t i = 0; i < uuid->size(); ++i) {
        (*uuid)[i] = static_cast<uint8_t>(std::stoul(hex.substr(i * 2, 2), nullptr, 16));
    }
    return true;
}

std::string profileVariable(const DrmTestProfile& profile, const char* variable) {
    std::string name = profile.scheme;
    std::transform(name.begin(), name.end(), name.begin(),
            [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return "DRM_" + name + "_" + (std::string(variable).substr(4));
}

}  // namespace

UT_status_t vts_drm_parse_hfp(
        ut_kvp_instance_t* instance, const std::string& drmPath, DrmHfpCryptoSchemes* hfp) {
    if (instance == nullptr || drmPath.empty() || hfp == nullptr) {
        UT_LOG_ERROR("DRM HFP instance, path, and output must not be empty or null");
        return UT_STATUS_FAILURE;
    }

    const std::string supportedCryptoSchemesPath = drmPath + ".SupportedCryptoSchemes";
    if (!ut_kvp_fieldPresent(instance, supportedCryptoSchemesPath.c_str())) {
        UT_LOG_ERROR("DRM HFP must contain SupportedCryptoSchemes");
        return UT_STATUS_FAILURE;
    }

    const std::string uuidListPath = supportedCryptoSchemesPath + ".uuids";
    const uint32_t uuidCount = ut_kvp_getListCount(instance, uuidListPath.c_str());
    if (uuidCount == 0) {
        UT_LOG_ERROR("DRM HFP has no SupportedCryptoSchemes UUIDs");
        return UT_STATUS_FAILURE;
    }

    DrmHfpCryptoSchemes parsed;
    std::set<std::array<uint8_t, 16>> uniqueUuids;
    for (uint32_t i = 0; i < uuidCount; ++i) {
        const std::string uuidPath = uuidListPath + "." + std::to_string(i);
        std::string uuidText;
        if (!readValue(instance, uuidPath, "", false, &uuidText) || uuidText.empty()) {
            UT_LOG_ERROR("Missing or invalid DRM HFP UUID at %s", uuidPath.c_str());
            return UT_STATUS_FAILURE;
        }
        std::array<uint8_t, 16> uuid{};
        if (!parseUuid(uuidText, &uuid)) {
            UT_LOG_ERROR("Invalid DRM HFP UUID at %s", uuidPath.c_str());
            return UT_STATUS_FAILURE;
        }
        if (!uniqueUuids.insert(uuid).second) {
            UT_LOG_ERROR("Duplicate DRM HFP UUID at %s", uuidPath.c_str());
            return UT_STATUS_FAILURE;
        }
        parsed.uuids.push_back(uuid);
    }

    const std::string mimeListPath =
            supportedCryptoSchemesPath + ".supportedContainerMimeTypes";
    if (!ut_kvp_fieldPresent(instance, mimeListPath.c_str())) {
        UT_LOG_ERROR("DRM HFP must declare supportedContainerMimeTypes");
        return UT_STATUS_FAILURE;
    }
    const uint32_t mimeCount = ut_kvp_getListCount(instance, mimeListPath.c_str());
    std::set<std::string> uniqueMimeTypes;
    for (uint32_t i = 0; i < mimeCount; ++i) {
        const std::string mimePath = mimeListPath + "." + std::to_string(i);
        std::string mime;
        if (!readValue(instance, mimePath, "", false, &mime) || mime.empty()) {
            UT_LOG_ERROR("Empty DRM HFP container MIME type at %s", mimePath.c_str());
            return UT_STATUS_FAILURE;
        }
        if (!uniqueMimeTypes.insert(mime).second) {
            UT_LOG_ERROR("Duplicate DRM HFP container MIME type at %s", mimePath.c_str());
            return UT_STATUS_FAILURE;
        }
        parsed.supportedContainerMimeTypes.push_back(std::move(mime));
    }

    *hfp = std::move(parsed);
    return UT_STATUS_OK;
}

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
        std::string value;
        if (!readValue(instance.get(), field.path, field.defaultValue, field.boolean, &value)) {
            return UT_STATUS_FAILURE;
        }
        values.emplace(field.variable, value);
    }
    std::vector<DrmTestProfile> loadedProfiles;
    const bool matrix = ut_kvp_fieldPresent(instance.get(), "drm.schemes");
    for (const auto& scheme : schemes) {
        const std::string prefix = std::string("drm.schemes.") + scheme.name;
        if (matrix) {
            std::string enabled;
            if (!readValue(instance.get(), prefix + ".enabled", "0", true, &enabled)) {
                return UT_STATUS_FAILURE;
            }
            if (enabled != "1") {
                continue;
            }
        } else {
            const char* overrideInstance = std::getenv("DRM_FACTORY_INSTANCE");
            const std::string factory = overrideInstance == nullptr
                    ? values.at("DRM_FACTORY_INSTANCE") : overrideInstance;
            const std::string inferredScheme = factory == "widevine" || factory == "playready"
                    ? factory : "clearkey";
            std::string selected;
            if (!readValue(instance.get(), "drm.scheme", inferredScheme.c_str(), false, &selected)) {
                return UT_STATUS_FAILURE;
            }
            if (selected != scheme.name) {
                continue;
            }
        }
        DrmTestProfile profile;
        profile.scheme = scheme.name;
        std::string required;
        std::string uuid;
        if (!readValue(instance.get(), matrix ? prefix + ".factory_instance" : "drm.factory_instance",
                    scheme.name, false, &profile.instance) ||
                !readValue(instance.get(), matrix ? prefix + ".uuid" : "drm.uuid",
                    scheme.uuid, false, &uuid) ||
                !readValue(instance.get(), matrix ? prefix + ".required" : "drm.required",
                    "1", true, &required) ||
                !parseUuid(uuid, &profile.uuid)) {
            return UT_STATUS_FAILURE;
        }
        if (profile.instance.empty()) {
            UT_LOG_ERROR("DRM factory instance must not be empty");
            return UT_STATUS_FAILURE;
        }
        profile.required = required == "1";
        for (const auto& field : fields) {
            if (std::string(field.variable) == "DRM_FACTORY_INSTANCE") {
                profile.values.emplace(field.variable, profile.instance);
                continue;
            }
            std::string value;
            const std::string path = matrix
                    ? prefix + std::string(field.path).substr(3) : field.path;
            const char* fallback = field.defaultValue;
            if (std::string(field.variable).find("_LICENSE_CONTENT_TYPE") != std::string::npos &&
                    profile.scheme != "clearkey") {
                fallback = profile.scheme == "playready" ? "text/xml" : "application/octet-stream";
            }
            if (!readValue(instance.get(), path, fallback, field.boolean, &value)) {
                return UT_STATUS_FAILURE;
            }
            profile.values.emplace(field.variable, value);
        }
        loadedProfiles.push_back(std::move(profile));
    }
    if (loadedProfiles.empty()) {
        UT_LOG_ERROR("Configure at least one enabled DRM scheme; supported names: clearkey, widevine, playready");
        return UT_STATUS_FAILURE;
    }
    configValues.swap(values);
    profiles.swap(loadedProfiles);
    matrixMode = matrix;
    UT_LOG_INFO("Loaded DRM test configuration: %s", path);
    return UT_STATUS_OK;
}

void vts_drm_clear_testconfig() {
    configValues.clear();
    activeProfile = nullptr;
    profiles.clear();
    matrixMode = false;
}

const char* vts_drm_config_value(const char* variable) {
    if (activeProfile != nullptr) {
        const std::string name = profileVariable(*activeProfile, variable);
        const char* scopedOverride = std::getenv(name.c_str());
        if (scopedOverride != nullptr) {
            return scopedOverride;
        }
        if (matrixMode) {
            const auto value = activeProfile->values.find(variable);
            return value == activeProfile->values.end() ? nullptr : value->second.c_str();
        }
    }
    const char* overrideValue = std::getenv(variable);
    if (overrideValue != nullptr) {
        return overrideValue;
    }
    const auto& values = activeProfile == nullptr ? configValues : activeProfile->values;
    const auto value = values.find(variable);
    return value == values.end() ? nullptr : value->second.c_str();
}

const std::vector<DrmTestProfile>& vts_drm_test_profiles() {
    return profiles;
}

const DrmTestProfile* vts_drm_active_profile() {
    return activeProfile;
}

bool vts_drm_select_profile(const char* scheme) {
    activeProfile = nullptr;
    for (const auto& profile : profiles) {
        if (profile.scheme == scheme) {
            activeProfile = &profile;
            return true;
        }
    }
    return false;
}

void vts_drm_reset_profile() {
    activeProfile = nullptr;
}
