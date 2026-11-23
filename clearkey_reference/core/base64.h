// SPDX-License-Identifier: Apache-2.0
#ifndef RDK_CLEARKEY_BASE64_H
#define RDK_CLEARKEY_BASE64_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rdk::clearkey {

// Unpadded base64url, as used by JSON Web Key / W3C ClearKey messages.
std::string base64UrlEncode(const uint8_t* data, size_t size);

// Accepts base64 and base64url alphabets with or without '=' padding.
bool base64Decode(const std::string& text, std::vector<uint8_t>* out);

}  // namespace rdk::clearkey

#endif
