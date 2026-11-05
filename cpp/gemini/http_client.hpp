// WeChess - HTTPS POST helper.
//
// Uses libcurl when the build provides it. The proxy and the app both need an
// HTTPS client; the stock Android NDK does not ship one, so the build option
// WECHESS_WITH_CURL (documented in README.md) is how it gets linked. When it is
// absent the Gemini features degrade with a clear error and Stockfish review
// keeps working.
#ifndef WECHESS_GEMINI_HTTP_CLIENT_HPP
#define WECHESS_GEMINI_HTTP_CLIENT_HPP

#include <string>
#include <vector>

namespace wechess {

struct HttpPostResult {
    bool ok = false;
    int status = 0;
    std::string body;
    std::string error;
};

HttpPostResult httpPostJson(const std::string& url, const std::vector<std::string>& headers,
                            const std::string& body, int timeoutSeconds);

// True when this build can make HTTPS requests at all.
bool httpClientAvailable();

}  // namespace wechess

#endif  // WECHESS_GEMINI_HTTP_CLIENT_HPP
