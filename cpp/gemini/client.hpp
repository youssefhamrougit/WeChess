// WeChess - Gemini client. Talks to the WeChess proxy, never to Google directly,
// so no API key is ever present in the application.
#ifndef WECHESS_GEMINI_CLIENT_HPP
#define WECHESS_GEMINI_CLIENT_HPP

#include <string>

namespace wechess {

struct GeminiConfig {
    std::string proxyUrl;  // e.g. https://wechess-proxy.example.com
    std::string token;     // shared application token for the proxy
    std::string model = "gemini-2.5-flash";
    bool enabled = false;
};

struct GeminiResult {
    bool ok = false;
    bool configured = false;
    std::string text;
    std::string error;
    int status = 0;
};

class GeminiClient {
   public:
    explicit GeminiClient(GeminiConfig config);

    bool configured() const { return configured_; }
    const GeminiConfig& config() const { return config_; }

    // Sends the prompt to the proxy. Never logs the token or the prompt.
    GeminiResult explain(const std::string& prompt) const;

   private:
    GeminiConfig config_;
    bool configured_ = false;
};

}  // namespace wechess

#endif  // WECHESS_GEMINI_CLIENT_HPP
