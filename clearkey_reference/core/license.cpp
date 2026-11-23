// SPDX-License-Identifier: Apache-2.0
#include "license.h"

#include <algorithm>
#include <cstring>

#include "base64.h"
#include "json.h"

namespace rdk::clearkey {

namespace {

constexpr size_t kKeySize = 16;

uint32_t readU32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
            (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

Err parseKidsJson(const uint8_t* data, size_t size, std::vector<Bytes>* keyIds) {
    JsonValue root;
    if (!parseJson(std::string(reinterpret_cast<const char*>(data), size), &root)) {
        return Err::InitData;
    }
    const JsonValue* kids = root.find("kids");
    if (kids == nullptr || kids->type != JsonValue::Type::Array) {
        return Err::InitData;
    }
    for (const JsonValue& kid : kids->array) {
        Bytes decoded;
        if (kid.type != JsonValue::Type::String ||
                !base64Decode(kid.string, &decoded) || decoded.size() != kKeySize) {
            return Err::InitData;
        }
        keyIds->push_back(std::move(decoded));
    }
    return Err::Ok;
}

Err parsePssh(const Bytes& initData, std::vector<Bytes>* keyIds) {
    size_t offset = 0;
    while (offset < initData.size()) {
        const size_t remaining = initData.size() - offset;
        // size(4) type(4) version/flags(4) systemId(16) dataSize(4)
        if (remaining < 32) {
            return Err::InitData;
        }
        const uint8_t* box = initData.data() + offset;
        const uint32_t boxSize = readU32(box);
        if (boxSize < 32 || boxSize > remaining || std::memcmp(box + 4, "pssh", 4) != 0) {
            return Err::InitData;
        }
        const uint8_t version = box[8];
        std::array<uint8_t, 16> systemId;
        std::memcpy(systemId.data(), box + 12, systemId.size());

        size_t cursor = 28;
        std::vector<Bytes> boxKids;
        if (version > 1) {
            return Err::InitData;
        }
        if (version == 1) {
            const uint32_t kidCount = readU32(box + cursor);
            cursor += 4;
            if (kidCount > (boxSize - cursor) / kKeySize) {
                return Err::InitData;
            }
            for (uint32_t i = 0; i < kidCount; ++i) {
                boxKids.emplace_back(box + cursor, box + cursor + kKeySize);
                cursor += kKeySize;
            }
        }
        if (boxSize - cursor < 4) {
            return Err::InitData;
        }
        const uint32_t dataSize = readU32(box + cursor);
        cursor += 4;
        if (dataSize > boxSize - cursor) {
            return Err::InitData;
        }
        if (isSupportedUuid(systemId)) {
            if (version == 0 && dataSize > 0 &&
                    parseKidsJson(box + cursor, dataSize, &boxKids) != Err::Ok) {
                return Err::InitData;
            }
            keyIds->insert(keyIds->end(), boxKids.begin(), boxKids.end());
        }
        offset += boxSize;
    }
    return keyIds->empty() ? Err::InitData : Err::Ok;
}

}  // namespace

bool isSupportedUuid(const std::array<uint8_t, 16>& uuid) {
    return uuid == kClearKeyUuid || uuid == kCommonPsshUuid;
}

Err parseInitData(const Bytes& initData, const std::string& mimeType,
        std::vector<Bytes>* keyIds) {
    keyIds->clear();
    if (initData.empty()) {
        return Err::BadValue;
    }
    if (mimeType == "video/mp4" || mimeType == "audio/mp4" || mimeType == "cenc") {
        return parsePssh(initData, keyIds);
    }
    if (mimeType == "video/webm" || mimeType == "audio/webm" || mimeType == "webm") {
        if (initData.size() != kKeySize) {
            return Err::InitData;
        }
        keyIds->push_back(initData);
        return Err::Ok;
    }
    return Err::CannotHandle;
}

std::string buildLicenseRequest(const std::vector<Bytes>& keyIds) {
    std::string request = "{\"kids\":[";
    for (size_t i = 0; i < keyIds.size(); ++i) {
        if (i != 0) {
            request += ",";
        }
        request += "\"" + base64UrlEncode(keyIds[i].data(), keyIds[i].size()) + "\"";
    }
    request += "],\"type\":\"temporary\"}";
    return request;
}

Err parseLicenseResponse(const Bytes& response, std::vector<KeyEntry>* keys) {
    keys->clear();
    JsonValue root;
    if (!parseJson(std::string(response.begin(), response.end()), &root)) {
        return Err::LicenseParse;
    }
    const JsonValue* list = root.find("keys");
    if (list == nullptr || list->type != JsonValue::Type::Array || list->array.empty()) {
        return Err::LicenseParse;
    }
    const JsonValue* type = root.find("type");
    if (type != nullptr &&
            (type->type != JsonValue::Type::String || type->string != "temporary")) {
        // Persistent licenses need storage that this reference plugin does not provide.
        return Err::CannotHandle;
    }
    for (const JsonValue& entry : list->array) {
        const JsonValue* kty = entry.find("kty");
        const JsonValue* kid = entry.find("kid");
        const JsonValue* k = entry.find("k");
        if (entry.type != JsonValue::Type::Object || kid == nullptr || k == nullptr ||
                kid->type != JsonValue::Type::String || k->type != JsonValue::Type::String ||
                (kty != nullptr && (kty->type != JsonValue::Type::String || kty->string != "oct"))) {
            return Err::LicenseParse;
        }
        KeyEntry parsed;
        if (!base64Decode(kid->string, &parsed.kid) || parsed.kid.size() != kKeySize ||
                !base64Decode(k->string, &parsed.key) || parsed.key.size() != kKeySize) {
            return Err::LicenseParse;
        }
        keys->push_back(std::move(parsed));
    }
    return Err::Ok;
}

}  // namespace rdk::clearkey
