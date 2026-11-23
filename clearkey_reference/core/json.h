// SPDX-License-Identifier: Apache-2.0
#ifndef RDK_CLEARKEY_JSON_H
#define RDK_CLEARKEY_JSON_H

#include <string>
#include <utility>
#include <vector>

namespace rdk::clearkey {

// Minimal JSON document model, sufficient for ClearKey license messages.
struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0;
    std::string string;
    std::vector<JsonValue> array;
    std::vector<std::pair<std::string, JsonValue>> object;

    const JsonValue* find(const std::string& name) const;
};

bool parseJson(const std::string& text, JsonValue* value);

}  // namespace rdk::clearkey

#endif
