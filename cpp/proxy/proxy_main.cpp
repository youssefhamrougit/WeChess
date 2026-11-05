// WeChess - server-side Gemini proxy.
//
// This is the only component that holds GEMINI_API_KEY. It is deployed on the
// internet, not shipped in the APK, and the app authenticates to it with a
// separate application token. It also owns the coaching system instruction, so
// the wording can be improved without shipping a new app.
#include <csignal>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>

#include "../gemini/http_client.hpp"
#include "../gemini/json.hpp"
#include "../server/http.hpp"

using namespace wechess;

namespace {

struct ProxyConfig {
    std::string apiKey;
    std::string appToken;
    std::string defaultModel = "gemini-2.5-flash";
    std::set<std::string> allowedModels;
    std::string apiBase = "https://generativelanguage.googleapis.com/v1beta";
    int port = 8787;
    std::string bindAddress = "0.0.0.0";
    int requestsPerMinute = 12;
};

ProxyConfig gConfig;
std::mutex gRateMutex;
std::map<std::string, std::deque<long long>> gRateWindows;

std::string env(const char* name, const std::string& fallback = "") {
    const char* value = std::getenv(name);
    return value == nullptr ? fallback : std::string(value);
}

long long nowSeconds() {
    return static_cast<long long>(std::time(nullptr));
}

bool constantTimeEquals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return diff == 0;
}

bool rateLimited(const std::string& ip) {
    std::lock_guard<std::mutex> lock(gRateMutex);
    auto& window = gRateWindows[ip];
    const long long now = nowSeconds();
    while (!window.empty() && now - window.front() > 60) window.pop_front();
    if (static_cast<int>(window.size()) >= gConfig.requestsPerMinute) return true;
    window.push_back(now);
    return false;
}

std::string jsonError(const std::string& message) {
    return "{\"error\":" + jsonQuote(message) + "}";
}

std::string systemInstruction() {
    return "You are a chess coach writing for an intermediate club player. You are given objective "
           "Stockfish analysis of one position. Explain the move using only the supplied facts. "
           "Never invent a line, an evaluation, a tactic, or an opening name. Never present "
           "yourself as the engine. Be concrete about this position, concise, and educational. "
           "If the supplied data is insufficient, say so.";
}

// Extracts text from a Gemini generateContent response.
bool extractText(const std::string& responseBody, std::string& text, std::string& error) {
    JsonValue root;
    std::string parseError;
    if (!parseJson(responseBody, root, parseError)) {
        error = "The Gemini response could not be parsed.";
        return false;
    }
    if (const JsonValue* apiError = root.get("error")) {
        if (const JsonValue* message = apiError->get("message")) {
            if (message->isString()) error = message->string;
        }
        if (error.empty()) error = "Gemini rejected the request.";
        return false;
    }
    const JsonValue* candidates = root.get("candidates");
    if (candidates == nullptr || !candidates->isArray() || candidates->array.empty()) {
        error = "Gemini returned no candidates.";
        return false;
    }
    const JsonValue* content = candidates->array[0].get("content");
    const JsonValue* parts = content == nullptr ? nullptr : content->get("parts");
    if (parts == nullptr || !parts->isArray()) {
        error = "Gemini returned no content.";
        return false;
    }
    std::string collected;
    for (const JsonValue& part : parts->array) {
        const JsonValue* partText = part.get("text");
        if (partText != nullptr && partText->isString()) {
            if (!collected.empty()) collected.push_back('\n');
            collected += partText->string;
        }
    }
    if (collected.empty()) {
        error = "Gemini returned an empty explanation.";
        return false;
    }
    text = collected;
    return true;
}

HttpResponse handleExplain(const HttpRequest& request) {
    if (!httpClientAvailable()) {
        return HttpResponse::text(jsonError("This proxy build has no HTTPS client."), "application/json");
    }
    const auto auth = request.headers.find("authorization");
    if (auth == request.headers.end() || auth->second.rfind("Bearer ", 0) != 0 ||
        !constantTimeEquals(auth->second.substr(7), gConfig.appToken)) {
        return HttpResponse::text(jsonError("Unauthorized."), "application/json");
    }
    if (request.body.size() > 64 * 1024) {
        HttpResponse response = HttpResponse::text(jsonError("Request too large."), "application/json");
        response.status = 413;
        return response;
    }
    if (rateLimited(request.clientIp)) {
        HttpResponse response = HttpResponse::text(jsonError("Too many requests."), "application/json");
        response.status = 429;
        return response;
    }

    JsonValue body;
    std::string parseError;
    if (!parseJson(request.body, body, parseError) || !body.isObject()) {
        HttpResponse response = HttpResponse::text(jsonError("Invalid JSON body."), "application/json");
        response.status = 400;
        return response;
    }
    const JsonValue* promptValue = body.get("prompt");
    if (promptValue == nullptr || !promptValue->isString() || promptValue->string.empty()) {
        HttpResponse response = HttpResponse::text(jsonError("Missing prompt."), "application/json");
        response.status = 400;
        return response;
    }
    if (promptValue->string.size() > 12000) {
        HttpResponse response = HttpResponse::text(jsonError("Prompt too long."), "application/json");
        response.status = 413;
        return response;
    }

    std::string model = gConfig.defaultModel;
    if (const JsonValue* requested = body.get("model"); requested != nullptr && requested->isString()) {
        if (gConfig.allowedModels.count(requested->string) == 0) {
            HttpResponse response =
                HttpResponse::text(jsonError("Unsupported model."), "application/json");
            response.status = 400;
            return response;
        }
        model = requested->string;
    }

    // Build the generateContent request. The API key travels in a header, never
    // in the URL, so it cannot leak through proxy or server logs.
    std::ostringstream payload;
    payload << "{";
    payload << "\"systemInstruction\":{\"parts\":[{\"text\":" << jsonQuote(systemInstruction()) << "}]},";
    payload << "\"contents\":[{\"role\":\"user\",\"parts\":[{\"text\":" << jsonQuote(promptValue->string)
            << "}]}],";
    payload << "\"generationConfig\":{\"temperature\":0.4,\"topP\":0.9,\"maxOutputTokens\":512}";
    payload << "}";

    const std::string url = gConfig.apiBase + "/models/" + model + ":generateContent";
    const HttpPostResult upstream =
        httpPostJson(url, {"x-goog-api-key: " + gConfig.apiKey}, payload.str(), 45);

    if (!upstream.ok) {
        // Never echo the upstream URL: it is fine here, but this keeps the habit.
        std::string message = upstream.error;
        std::string text;
        std::string upstreamError;
        if (extractText(upstream.body, text, upstreamError) && !upstreamError.empty()) {
            message = upstreamError;
        }
        if (message.size() > 300) message.resize(300);
        HttpResponse response = HttpResponse::text(jsonError(message), "application/json");
        response.status = upstream.status == 429 ? 429 : 502;
        return response;
    }

    std::string text;
    std::string error;
    if (!extractText(upstream.body, text, error)) {
        return HttpResponse::text(jsonError(error), "application/json");
    }

    std::string responseBody = "{\"text\":" + jsonQuote(text) + ",\"model\":" + jsonQuote(model) + "}";
    return HttpResponse::text(responseBody, "application/json");
}

void logSafe(const std::string& message) {
    // Deliberately does not log request bodies, tokens or the API key.
    std::cout << "[wechess-proxy] " << message << std::endl;
}

}  // namespace

int main() {
    signal(SIGPIPE, SIG_IGN);

    gConfig.apiKey = env("GEMINI_API_KEY");
    gConfig.appToken = env("WECHESS_PROXY_TOKEN");
    gConfig.defaultModel = env("GEMINI_MODEL", "gemini-2.5-flash");
    gConfig.apiBase = env("GEMINI_API_BASE", "https://generativelanguage.googleapis.com/v1beta");
    gConfig.port = std::atoi(env("PORT", "8787").c_str());
    gConfig.bindAddress = env("BIND_ADDRESS", "0.0.0.0");
    gConfig.requestsPerMinute = std::atoi(env("REQUESTS_PER_MINUTE", "12").c_str());

    const std::string models = env("GEMINI_MODELS", gConfig.defaultModel + ",gemini-2.0-flash");
    std::istringstream modelStream(models);
    std::string model;
    while (std::getline(modelStream, model, ',')) {
        if (!model.empty()) gConfig.allowedModels.insert(model);
    }
    gConfig.allowedModels.insert(gConfig.defaultModel);

    if (gConfig.apiKey.empty()) {
        std::cerr << "[wechess-proxy] GEMINI_API_KEY is not set. Refusing to start.\n";
        return 1;
    }
    if (gConfig.appToken.empty() || gConfig.appToken.size() < 16) {
        std::cerr << "[wechess-proxy] WECHESS_PROXY_TOKEN must be set to a long random value.\n";
        return 1;
    }
    if (!httpClientAvailable()) {
        std::cerr << "[wechess-proxy] Built without libcurl; HTTPS upstream calls are impossible.\n";
        return 1;
    }

    HttpServer server;
    server.handler = [](const HttpRequest& request) -> HttpResponse {
        if (request.method == "GET" && request.path == "/healthz") {
            return HttpResponse::text("{\"status\":\"ok\",\"model\":" + jsonQuote(gConfig.defaultModel) + "}",
                                      "application/json");
        }
        if (request.path == "/v1/explain") {
            if (request.method != "POST") {
                HttpResponse response =
                    HttpResponse::text(jsonError("Method not allowed."), "application/json");
                response.status = 405;
                return response;
            }
            return handleExplain(request);
        }
        HttpResponse response = HttpResponse::error(404, "Unknown endpoint.");
        return response;
    };

    std::string error;
    if (!server.listen(gConfig.bindAddress, gConfig.port, error)) {
        std::cerr << "[wechess-proxy] " << error << "\n";
        return 1;
    }
    logSafe("listening on " + gConfig.bindAddress + ":" + std::to_string(server.port()) +
            " (model " + gConfig.defaultModel + ")");
    server.start();
    pause();
    return 0;
}
