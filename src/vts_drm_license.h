// SPDX-License-Identifier: Apache-2.0
#ifndef VTS_DRM_LICENSE_H
#define VTS_DRM_LICENSE_H

#include <cstdint>
#include <vector>

bool vts_drm_license_uses_http(const char* level, bool* useHttp);
bool vts_drm_request_http_license(
        const char* level,
        const std::vector<uint8_t>& request,
        std::vector<uint8_t>* response);

#endif
