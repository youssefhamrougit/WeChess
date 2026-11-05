// WeChess - small JSON reader/writer used for the Gemini request and response.
// Only what the API needs: objects, arrays, strings, numbers, booleans, null.
#ifndef WECHESS_GEMINI_JSON_HPP
#define WECHESS_GEMINI_JSON_HPP

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace wechess {

struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<JsonValue> array;
    std::vector<std::pair<std::string, JsonValue>> object;

    bool isString() const { return type == Type::String; }
    bool isObject() const { return type == Type::Object; }
    bool isArray() const { return type == Type::Array; }

    const JsonValue* get(const std::string& key) const;
    const JsonValue* at(std::size_t index) const;
};

// Parses a complete JSON document. Returns false and fills `error` on failure.
bool parseJson(const std::string& text, JsonValue& out, std::string& error);

// Escapes and quotes a string for embedding in JSON.
std::string jsonQuote(const std::string& value);

}  // namespace wechess

#endif  // WECHESS_GEMINI_JSON_HPP
