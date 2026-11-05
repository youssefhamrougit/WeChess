#include "client.hpp"

#include "http_client.hpp"
#include "json.hpp"

namespace wechess {
namespace {

std::string trimTrailingSlash(std::string value) {
    while (!value.empty() && value.back() == '/') value.pop_back();
    return value;
}

}  // namespace

GeminiClient::GeminiClient(GeminiConfig config) : config_(std::move(config)) {
    configured_ = config_.enabled && !config_.proxyUrl.empty() && !config_.token.empty() &&
                  (config_.proxyUrl.rfind("https://", 0) == 0 ||
                   config_.proxyUrl.rfind("http://127.0.0.1", 0) == 0 ||
                   config_.proxyUrl.rfind("http://localhost", 0) == 0);
}

GeminiResult GeminiClient::explain(const std::string& prompt) const {
    GeminiResult result;
    result.configured = configured_;
    if (!configured_) {
        result.error =
            "Gemini explanations are not configured. Set the proxy URL and token in Settings.";
        return result;
    }
    if (!httpClientAvailable()) {
        result.error =
            "This build has no HTTPS client, so Gemini explanations are unavailable. "
            "Stockfish review still works.";
        return result;
    }

    std::string body = "{";
    body += "\"model\":" + jsonQuote(config_.model);
    body += ",\"prompt\":" + jsonQuote(prompt);
    body += "}";

    const std::string url = trimTrailingSlash(config_.proxyUrl) + "/v1/explain";
    const HttpPostResult response =
        httpPostJson(url, {"Authorization: Bearer " + config_.token}, body, 45);

    if (!response.ok) {
        result.status = response.status;
        result.error = response.error;
        if (response.status == 401) result.error = "The proxy rejected the application token.";
        if (response.status == 429) result.error = "Too many explanation requests. Try again shortly.";
        if (response.status == 503) result.error = "The proxy has no Gemini key configured.";
        if (!response.body.empty() && response.status >= 400) {
            JsonValue parsed;
            std::string parseError;
            if (parseJson(response.body, parsed, parseError)) {
                if (const JsonValue* message = parsed.get("error")) {
                    if (message->isString()) result.error = message->string;
                }
            }
        }
        return result;
    }

    JsonValue parsed;
    std::string parseError;
    if (!parseJson(response.body, parsed, parseError)) {
        result.error = "The proxy returned a malformed response.";
        return result;
    }
    const JsonValue* text = parsed.get("text");
    if (text == nullptr || !text->isString() || text->string.empty()) {
        result.error = "The proxy returned no explanation text.";
        return result;
    }
    result.text = text->string;
    result.ok = true;
    return result;
}

}  // namespace wechess
