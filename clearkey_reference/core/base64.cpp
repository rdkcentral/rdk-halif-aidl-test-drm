// SPDX-License-Identifier: Apache-2.0
#include "base64.h"

namespace rdk::clearkey {

std::string base64UrlEncode(const uint8_t* data, size_t size) {
    static constexpr char kAlphabet[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    out.reserve((size * 4 + 2) / 3);
    uint32_t buffer = 0;
    int bits = 0;
    for (size_t i = 0; i < size; ++i) {
        buffer = (buffer << 8) | data[i];
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            out.push_back(kAlphabet[(buffer >> bits) & 0x3f]);
        }
    }
    if (bits > 0) {
        out.push_back(kAlphabet[(buffer << (6 - bits)) & 0x3f]);
    }
    return out;
}

bool base64Decode(const std::string& text, std::vector<uint8_t>* out) {
    out->clear();
    uint32_t buffer = 0;
    int bits = 0;
    size_t symbols = 0;
    bool padding = false;
    for (char c : text) {
        if (c == '=') {
            padding = true;
            continue;
        }
        if (padding) {
            return false;
        }
        uint32_t value;
        if (c >= 'A' && c <= 'Z') {
            value = static_cast<uint32_t>(c - 'A');
        } else if (c >= 'a' && c <= 'z') {
            value = static_cast<uint32_t>(c - 'a' + 26);
        } else if (c >= '0' && c <= '9') {
            value = static_cast<uint32_t>(c - '0' + 52);
        } else if (c == '-' || c == '+') {
            value = 62;
        } else if (c == '_' || c == '/') {
            value = 63;
        } else {
            return false;
        }
        buffer = (buffer << 6) | value;
        bits += 6;
        ++symbols;
        if (bits >= 8) {
            bits -= 8;
            out->push_back(static_cast<uint8_t>((buffer >> bits) & 0xff));
        }
    }
    return symbols % 4 != 1;
}

}  // namespace rdk::clearkey
