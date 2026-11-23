// SPDX-License-Identifier: Apache-2.0
#include "json.h"

#include <cstdint>
#include <cstdlib>

namespace rdk::clearkey {

const JsonValue* JsonValue::find(const std::string& name) const {
    if (type != Type::Object) {
        return nullptr;
    }
    for (const auto& member : object) {
        if (member.first == name) {
            return &member.second;
        }
    }
    return nullptr;
}

namespace {

constexpr int kMaxDepth = 16;

class Parser {
public:
    explicit Parser(const std::string& text) : mText(text) {}

    bool parseDocument(JsonValue* value) {
        skipSpace();
        if (!parseValue(value, 0)) {
            return false;
        }
        skipSpace();
        return mPos == mText.size();
    }

private:
    void skipSpace() {
        while (mPos < mText.size() &&
                (mText[mPos] == ' ' || mText[mPos] == '\t' ||
                 mText[mPos] == '\n' || mText[mPos] == '\r')) {
            ++mPos;
        }
    }

    bool consume(char c) {
        if (mPos < mText.size() && mText[mPos] == c) {
            ++mPos;
            return true;
        }
        return false;
    }

    bool consumeWord(const char* word) {
        size_t i = 0;
        while (word[i] != '\0') {
            if (mPos + i >= mText.size() || mText[mPos + i] != word[i]) {
                return false;
            }
            ++i;
        }
        mPos += i;
        return true;
    }

    bool parseValue(JsonValue* value, int depth) {
        if (depth > kMaxDepth || mPos >= mText.size()) {
            return false;
        }
        const char c = mText[mPos];
        if (c == '{') {
            return parseObject(value, depth);
        }
        if (c == '[') {
            return parseArray(value, depth);
        }
        if (c == '"') {
            value->type = JsonValue::Type::String;
            return parseString(&value->string);
        }
        if (consumeWord("true")) {
            value->type = JsonValue::Type::Bool;
            value->boolean = true;
            return true;
        }
        if (consumeWord("false")) {
            value->type = JsonValue::Type::Bool;
            value->boolean = false;
            return true;
        }
        if (consumeWord("null")) {
            value->type = JsonValue::Type::Null;
            return true;
        }
        return parseNumber(value);
    }

    bool parseNumber(JsonValue* value) {
        const char* begin = mText.c_str() + mPos;
        char* end = nullptr;
        const double parsed = std::strtod(begin, &end);
        if (end == begin) {
            return false;
        }
        mPos += static_cast<size_t>(end - begin);
        value->type = JsonValue::Type::Number;
        value->number = parsed;
        return true;
    }

    bool parseHex4(uint32_t* code) {
        if (mPos + 4 > mText.size()) {
            return false;
        }
        uint32_t result = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = mText[mPos + static_cast<size_t>(i)];
            result <<= 4;
            if (c >= '0' && c <= '9') {
                result |= static_cast<uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                result |= static_cast<uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                result |= static_cast<uint32_t>(c - 'A' + 10);
            } else {
                return false;
            }
        }
        mPos += 4;
        *code = result;
        return true;
    }

    static void appendUtf8(uint32_t code, std::string* out) {
        if (code < 0x80) {
            out->push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out->push_back(static_cast<char>(0xc0 | (code >> 6)));
            out->push_back(static_cast<char>(0x80 | (code & 0x3f)));
        } else {
            out->push_back(static_cast<char>(0xe0 | (code >> 12)));
            out->push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
            out->push_back(static_cast<char>(0x80 | (code & 0x3f)));
        }
    }

    bool parseString(std::string* out) {
        if (!consume('"')) {
            return false;
        }
        out->clear();
        while (mPos < mText.size()) {
            const char c = mText[mPos++];
            if (c == '"') {
                return true;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                return false;
            }
            if (c != '\\') {
                out->push_back(c);
                continue;
            }
            if (mPos >= mText.size()) {
                return false;
            }
            const char escape = mText[mPos++];
            switch (escape) {
                case '"': out->push_back('"'); break;
                case '\\': out->push_back('\\'); break;
                case '/': out->push_back('/'); break;
                case 'b': out->push_back('\b'); break;
                case 'f': out->push_back('\f'); break;
                case 'n': out->push_back('\n'); break;
                case 'r': out->push_back('\r'); break;
                case 't': out->push_back('\t'); break;
                case 'u': {
                    uint32_t code = 0;
                    if (!parseHex4(&code)) {
                        return false;
                    }
                    appendUtf8(code, out);
                    break;
                }
                default:
                    return false;
            }
        }
        return false;
    }

    bool parseArray(JsonValue* value, int depth) {
        value->type = JsonValue::Type::Array;
        consume('[');
        skipSpace();
        if (consume(']')) {
            return true;
        }
        while (true) {
            JsonValue element;
            skipSpace();
            if (!parseValue(&element, depth + 1)) {
                return false;
            }
            value->array.push_back(std::move(element));
            skipSpace();
            if (consume(',')) {
                continue;
            }
            return consume(']');
        }
    }

    bool parseObject(JsonValue* value, int depth) {
        value->type = JsonValue::Type::Object;
        consume('{');
        skipSpace();
        if (consume('}')) {
            return true;
        }
        while (true) {
            std::string name;
            skipSpace();
            if (!parseString(&name)) {
                return false;
            }
            skipSpace();
            if (!consume(':')) {
                return false;
            }
            skipSpace();
            JsonValue member;
            if (!parseValue(&member, depth + 1)) {
                return false;
            }
            value->object.emplace_back(std::move(name), std::move(member));
            skipSpace();
            if (consume(',')) {
                continue;
            }
            return consume('}');
        }
    }

    const std::string& mText;
    size_t mPos = 0;
};

}  // namespace

bool parseJson(const std::string& text, JsonValue* value) {
    *value = JsonValue();
    return Parser(text).parseDocument(value);
}

}  // namespace rdk::clearkey
