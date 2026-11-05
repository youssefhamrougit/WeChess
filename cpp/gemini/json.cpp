#include "json.hpp"

#include <cmath>
#include <cstdio>

namespace wechess {
namespace {

struct Parser {
    const std::string& text;
    std::size_t position = 0;
    std::string error;

    explicit Parser(const std::string& input) : text(input) {}

    bool eof() const { return position >= text.size(); }
    char peek() const { return position < text.size() ? text[position] : '\0'; }

    void skipWhitespace() {
        while (!eof()) {
            const char c = text[position];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++position;
            else break;
        }
    }

    bool expect(char c) {
        if (peek() != c) {
            error = std::string("Expected '") + c + "' at offset " + std::to_string(position);
            return false;
        }
        ++position;
        return true;
    }

    static void appendUtf8(std::string& out, unsigned int code) {
        if (code <= 0x7f) {
            out.push_back(static_cast<char>(code));
        } else if (code <= 0x7ff) {
            out.push_back(static_cast<char>(0xc0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3f)));
        } else if (code <= 0xffff) {
            out.push_back(static_cast<char>(0xe0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3f)));
        } else {
            out.push_back(static_cast<char>(0xf0 | (code >> 18)));
            out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3f)));
        }
    }

    bool parseString(std::string& out) {
        if (!expect('"')) return false;
        out.clear();
        while (!eof()) {
            const char c = text[position++];
            if (c == '"') return true;
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (eof()) break;
            const char esc = text[position++];
            switch (esc) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    if (position + 4 > text.size()) {
                        error = "Truncated \\u escape";
                        return false;
                    }
                    unsigned int code = 0;
                    for (int i = 0; i < 4; ++i) {
                        const char digit = text[position++];
                        code <<= 4;
                        if (digit >= '0' && digit <= '9') code |= static_cast<unsigned>(digit - '0');
                        else if (digit >= 'a' && digit <= 'f') code |= static_cast<unsigned>(digit - 'a' + 10);
                        else if (digit >= 'A' && digit <= 'F') code |= static_cast<unsigned>(digit - 'A' + 10);
                        else {
                            error = "Invalid \\u escape";
                            return false;
                        }
                    }
                    // Surrogate pair.
                    if (code >= 0xd800 && code <= 0xdbff && position + 6 <= text.size() &&
                        text[position] == '\\' && text[position + 1] == 'u') {
                        position += 2;
                        unsigned int low = 0;
                        for (int i = 0; i < 4; ++i) {
                            const char digit = text[position++];
                            low <<= 4;
                            if (digit >= '0' && digit <= '9') low |= static_cast<unsigned>(digit - '0');
                            else if (digit >= 'a' && digit <= 'f') low |= static_cast<unsigned>(digit - 'a' + 10);
                            else if (digit >= 'A' && digit <= 'F') low |= static_cast<unsigned>(digit - 'A' + 10);
                            else return false;
                        }
                        code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
                    }
                    appendUtf8(out, code);
                    break;
                }
                default:
                    error = "Invalid escape sequence";
                    return false;
            }
        }
        error = "Unterminated string";
        return false;
    }

    bool parseNumber(JsonValue& out) {
        const std::size_t begin = position;
        if (peek() == '-') ++position;
        while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) ++position;
        if (peek() == '.') {
            ++position;
            while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) ++position;
        }
        if (peek() == 'e' || peek() == 'E') {
            ++position;
            if (peek() == '+' || peek() == '-') ++position;
            while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) ++position;
        }
        try {
            out.type = JsonValue::Type::Number;
            out.number = std::stod(text.substr(begin, position - begin));
        } catch (...) {
            error = "Invalid number";
            return false;
        }
        return true;
    }

    bool parseValue(JsonValue& out, int depth) {
        if (depth > 64) {
            error = "JSON nested too deeply";
            return false;
        }
        skipWhitespace();
        if (eof()) {
            error = "Unexpected end of JSON";
            return false;
        }
        const char c = peek();
        if (c == '{') {
            ++position;
            out.type = JsonValue::Type::Object;
            skipWhitespace();
            if (peek() == '}') {
                ++position;
                return true;
            }
            while (true) {
                skipWhitespace();
                std::string key;
                if (!parseString(key)) return false;
                skipWhitespace();
                if (!expect(':')) return false;
                JsonValue value;
                if (!parseValue(value, depth + 1)) return false;
                out.object.emplace_back(std::move(key), std::move(value));
                skipWhitespace();
                if (peek() == ',') {
                    ++position;
                    continue;
                }
                if (peek() == '}') {
                    ++position;
                    return true;
                }
                error = "Expected ',' or '}' in object";
                return false;
            }
        }
        if (c == '[') {
            ++position;
            out.type = JsonValue::Type::Array;
            skipWhitespace();
            if (peek() == ']') {
                ++position;
                return true;
            }
            while (true) {
                JsonValue value;
                if (!parseValue(value, depth + 1)) return false;
                out.array.push_back(std::move(value));
                skipWhitespace();
                if (peek() == ',') {
                    ++position;
                    continue;
                }
                if (peek() == ']') {
                    ++position;
                    return true;
                }
                error = "Expected ',' or ']' in array";
                return false;
            }
        }
        if (c == '"') {
            out.type = JsonValue::Type::String;
            return parseString(out.string);
        }
        if (text.compare(position, 4, "true") == 0) {
            position += 4;
            out.type = JsonValue::Type::Bool;
            out.boolean = true;
            return true;
        }
        if (text.compare(position, 5, "false") == 0) {
            position += 5;
            out.type = JsonValue::Type::Bool;
            out.boolean = false;
            return true;
        }
        if (text.compare(position, 4, "null") == 0) {
            position += 4;
            out.type = JsonValue::Type::Null;
            return true;
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parseNumber(out);
        error = std::string("Unexpected character '") + c + "' at offset " + std::to_string(position);
        return false;
    }
};

}  // namespace

const JsonValue* JsonValue::get(const std::string& key) const {
    for (const auto& entry : object) {
        if (entry.first == key) return &entry.second;
    }
    return nullptr;
}

const JsonValue* JsonValue::at(std::size_t index) const {
    if (index >= array.size()) return nullptr;
    return &array[index];
}

bool parseJson(const std::string& text, JsonValue& out, std::string& error) {
    Parser parser(text);
    JsonValue value;
    if (!parser.parseValue(value, 0)) {
        error = parser.error;
        return false;
    }
    parser.skipWhitespace();
    if (!parser.eof()) {
        error = "Trailing characters after JSON document";
        return false;
    }
    out = std::move(value);
    return true;
}

std::string jsonQuote(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    out.push_back('"');
    for (unsigned char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                    out += buffer;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    out.push_back('"');
    return out;
}

}  // namespace wechess
