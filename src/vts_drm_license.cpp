// SPDX-License-Identifier: Apache-2.0
#include "vts_drm_license.h"
#include "vts_drm_parse_testconfig.h"

#include <curl/curl.h>
#include <ut_log.h>

#include <charconv>
#include <limits>
#include <memory>
#include <new>
#include <string>

namespace {

constexpr size_t maxLicenseSize = 1024 * 1024;

struct CurlRuntime {
    CURLcode status = curl_global_init(CURL_GLOBAL_DEFAULT);
    ~CurlRuntime() {
        if (status == CURLE_OK) {
            curl_global_cleanup();
        }
    }
};

size_t receiveResponse(char* data, size_t size, size_t count, void* context) {
    auto* response = static_cast<std::vector<uint8_t>*>(context);
    if (size != 0 && count > (maxLicenseSize - response->size()) / size) {
        UT_LOG_ERROR("HTTP license response exceeds 1 MiB");
        return 0;
    }
    const size_t bytes = size * count;
    if (bytes == 0) {
        return 0;
    }
    try {
        response->insert(response->end(), data, data + bytes);
    } catch (const std::bad_alloc&) {
        UT_LOG_ERROR("Unable to allocate HTTP license response buffer");
        return 0;
    }
    return bytes;
}

}  // namespace

bool vts_drm_license_uses_http(const char* level, bool* useHttp) {
    const std::string variable = std::string("DRM_") + level + "_LICENSE_MODE";
    const char* mode = vts_drm_config_value(variable.c_str());
    if (mode != nullptr && std::string(mode) == "http") {
        *useHttp = true;
        return true;
    }
    if (mode == nullptr || std::string(mode) == "local") {
        *useHttp = false;
        return true;
    }
    UT_LOG_ERROR("%s must be local or http", variable.c_str());
    return false;
}

bool vts_drm_request_http_license(
        const char* level,
        const std::vector<uint8_t>& request,
        std::vector<uint8_t>* response) {
    response->clear();
    const std::string prefix = std::string("DRM_") + level + "_LICENSE_";
    const char* configuredUrl = vts_drm_config_value((prefix + "URL").c_str());
    const char* configuredTimeout =
            vts_drm_config_value((prefix + "REQUEST_TIMEOUT_SECONDS").c_str());
    const std::string url = configuredUrl == nullptr ? "" : configuredUrl;
    const std::string timeoutText = configuredTimeout == nullptr ? "10" : configuredTimeout;
    long timeout = 0;
    const auto parsed = std::from_chars(
            timeoutText.data(), timeoutText.data() + timeoutText.size(), timeout);
    if (parsed.ec != std::errc() || parsed.ptr != timeoutText.data() + timeoutText.size() ||
            timeout <= 0) {
        UT_LOG_ERROR("%sREQUEST_TIMEOUT_SECONDS must be a positive integer", prefix.c_str());
        return false;
    }
    if ((url.compare(0, 7, "http://") != 0 && url.compare(0, 8, "https://") != 0) ||
            request.empty() ||
            request.size() > static_cast<size_t>(std::numeric_limits<curl_off_t>::max())) {
        UT_LOG_ERROR("HTTP license exchange requires an HTTP(S) URL and a non-empty key request");
        return false;
    }

    static CurlRuntime runtime;
    if (runtime.status != CURLE_OK) {
        UT_LOG_ERROR("Unable to initialize HTTP license transport: %s",
                curl_easy_strerror(runtime.status));
        return false;
    }
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(
            curl_slist_append(nullptr, "Content-Type: application/json"), curl_slist_free_all);
    if (!curl || !headers) {
        UT_LOG_ERROR("Unable to allocate HTTP license transport");
        return false;
    }
    std::vector<uint8_t> received;
    const auto setOption = [&curl](CURLoption option, auto value) {
        const CURLcode status = curl_easy_setopt(curl.get(), option, value);
        if (status != CURLE_OK) {
            UT_LOG_ERROR("Unable to configure HTTP license transport: %s",
                    curl_easy_strerror(status));
        }
        return status == CURLE_OK;
    };
    if (!setOption(CURLOPT_URL, url.c_str()) ||
            !setOption(CURLOPT_POST, 1L) ||
            !setOption(CURLOPT_HTTPHEADER, headers.get()) ||
            !setOption(CURLOPT_POSTFIELDS, reinterpret_cast<const char*>(request.data())) ||
            !setOption(CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.size())) ||
            !setOption(CURLOPT_TIMEOUT, timeout) ||
            !setOption(CURLOPT_CONNECTTIMEOUT, timeout) ||
            !setOption(CURLOPT_NOSIGNAL, 1L) ||
            !setOption(CURLOPT_FOLLOWLOCATION, 0L) ||
            !setOption(CURLOPT_PROXY, "") ||
            !setOption(CURLOPT_WRITEFUNCTION, receiveResponse) ||
            !setOption(CURLOPT_WRITEDATA, &received)) {
        return false;
    }
    const CURLcode status = curl_easy_perform(curl.get());
    if (status != CURLE_OK) {
        UT_LOG_ERROR("HTTP license exchange failed: %s", curl_easy_strerror(status));
        return false;
    }
    long httpStatus = 0;
    if (curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &httpStatus) != CURLE_OK ||
            httpStatus < 200 || httpStatus >= 300 || received.empty()) {
        UT_LOG_ERROR("HTTP license exchange rejected: status=%ld, response bytes=%zu",
                httpStatus, received.size());
        return false;
    }
    response->swap(received);
    return true;
}
