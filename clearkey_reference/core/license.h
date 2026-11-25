// SPDX-License-Identifier: Apache-2.0
#ifndef RDK_CLEARKEY_LICENSE_H
#define RDK_CLEARKEY_LICENSE_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "error.h"

namespace rdk::clearkey {

using Bytes = std::vector<uint8_t>;

constexpr std::array<uint8_t, 16> kClearKeyUuid = {
        0xe2, 0x71, 0x9d, 0x58, 0xa9, 0x85, 0xb3, 0xc9,
        0x78, 0x1a, 0xb0, 0x30, 0xaf, 0x78, 0xd3, 0x0e};

// W3C common PSSH system ID, also accepted by Android's ClearKey plugin.
constexpr std::array<uint8_t, 16> kCommonPsshUuid = {
        0x10, 0x77, 0xef, 0xec, 0xc0, 0xb2, 0x4d, 0x02,
        0xac, 0xe3, 0x3c, 0x1e, 0x52, 0xe2, 0xfb, 0x4b};

struct KeyEntry {
    Bytes kid;
    Bytes key;
};

bool isSupportedUuid(const std::array<uint8_t, 16>& uuid);

// Extracts the key IDs from init data for the given MIME type:
//   video/mp4, audio/mp4, cenc : one or more 'pssh' boxes
//   video/webm, audio/webm, webm : a single 16 byte key ID
Err parseInitData(const Bytes& initData, const std::string& mimeType,
        std::vector<Bytes>* keyIds);

// Builds the W3C ClearKey license request: {"kids":["..."],"type":"temporary"}
// or "persistent-license" for offline requests.
std::string buildLicenseRequest(const std::vector<Bytes>& keyIds, bool persistent = false);

// Builds the W3C ClearKey license release message for persistent licenses.
std::string buildLicenseReleaseRequest(const std::vector<Bytes>& keyIds);

// Parses a W3C ClearKey license response (JSON Web Key set). Persistent licenses are
// rejected with CannotHandle unless persistent is non-null; it then reports the type.
Err parseLicenseResponse(const Bytes& response, std::vector<KeyEntry>* keys,
        bool* persistent = nullptr);

}  // namespace rdk::clearkey

#endif
