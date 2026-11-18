// SPDX-License-Identifier: Apache-2.0
#ifndef VTS_DRM_PARSE_HFP_H
#define VTS_DRM_PARSE_HFP_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <ut.h>
#include <ut_kvp.h>

struct DrmHfpCryptoSchemes {
    std::vector<std::array<uint8_t, 16>> uuids;
    std::vector<std::string> supportedContainerMimeTypes;
};

// Reads DRM HFP values from an already-loaded KVP profile instance.
UT_status_t vts_drm_parse_hfp(
        ut_kvp_instance_t* instance, const std::string& drmPath, DrmHfpCryptoSchemes* hfp);

#endif
