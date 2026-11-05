// WeChess - HTTP-level tests.
//
// These drive the real server with real requests: no browser, no JavaScript.
// They verify the pages a WebView would receive, the redirect flow, form
// handling, escaping and that the board always has exactly 64 squares.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <thread>

#include "server/app.hpp"
#include "server/http.hpp"

namespace {

int gChecks = 0;
int gFailures = 0;

void check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok) {
        ++gFailures;
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    }
}

void checkEq(int actual, int expected, const std::string& what) {
    ++gChecks;
    if (actual != expected) {
        ++gFailures;
        std::fprintf(stderr, "FAIL: %s (expected %d, got %d)\n", what.c_str(), expected, actual);
    }
}

std::string lower(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

bool containsFold(const std::string& haystack, const std::string& needle) {
    return lower(haystack).find(lower(needle)) != std::string::npos;
}

int countOccurrences(const std::string& haystack, const std::string& needle) {
    int count = 0;
    std::size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

struct Reply {
    bool transportOk = false;
    int status = 0;
    std::map<std::string, std::string> headers;
    std::string body;
};

bool sendAll(int fd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

Reply httpRequest(int port, const std::string& method, const std::string& path,
                  const std::string& body = std::string(),
                  const std::string& contentType = "application/x-www-form-urlencoded") {
    Reply reply;
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return reply;

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(port));
    if (::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1 ||
        ::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        ::close(fd);
        return reply;
    }

    std::string request = method + " " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                          "Connection: close\r\nAccept: */*\r\n";
    if (method == "POST") {
        request += "Content-Type: " + contentType + "\r\n";
        request += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    }
    request += "\r\n";
    request += body;

    if (!sendAll(fd, request)) {
        ::close(fd);
        return reply;
    }

    std::string raw;
    char buffer[8192];
    while (true) {
        const ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
        if (n <= 0) break;
        raw.append(buffer, static_cast<std::size_t>(n));
    }
    ::close(fd);
    reply.transportOk = true;

    const std::size_t headerEnd = raw.find("\r\n\r\n");
    if (headerEnd == std::string::npos) return reply;

    std::istringstream head(raw.substr(0, headerEnd));
    std::string line;
    if (!std::getline(head, line)) return reply;
    if (line.rfind("HTTP/", 0) != 0) return reply;
    const std::size_t firstSpace = line.find(' ');
    if (firstSpace == std::string::npos) return reply;
    reply.status = std::atoi(line.c_str() + firstSpace + 1);

    while (std::getline(head, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = lower(line.substr(0, colon));
        std::string value = line.substr(colon + 1);
        while (!value.empty() && value.front() == ' ') value.erase(value.begin());
        reply.headers[key] = value;
    }

    reply.body = raw.substr(headerEnd + 4);
    const auto it = reply.headers.find("content-length");
    if (it != reply.headers.end()) {
        const std::size_t length = static_cast<std::size_t>(std::atoll(it->second.c_str()));
        if (length <= reply.body.size()) reply.body.resize(length);
    }
    return reply;
}

std::string header(const Reply& reply, const std::string& name) {
    const auto it = reply.headers.find(lower(name));
    return it == reply.headers.end() ? std::string() : it->second;
}

// Extracted game id from a 303 Location header like "/analysis/ab12cd34ef56".
std::string idFromLocation(const std::string& location) {
    const std::size_t slash = location.find_last_of('/');
    if (slash == std::string::npos) return std::string();
    std::string id = location.substr(slash + 1);
    const std::size_t cut = id.find_first_of("?#");
    if (cut != std::string::npos) id.resize(cut);
    return id;
}

bool pageIsScriptFree(const std::string& html) {
    if (containsFold(html, "<script")) return false;
    if (containsFold(html, "javascript:")) return false;
    if (containsFold(html, "onclick=")) return false;
    if (containsFold(html, "onsubmit=")) return false;
    return true;
}

std::string multipartBody(const std::string& boundary, const std::string& filename,
                          const std::string& content) {
    std::string out;
    out += "--" + boundary + "\r\n";
    out += "Content-Disposition: form-data; name=\"file\"; filename=\"" + filename + "\"\r\n";
    out += "Content-Type: application/x-chess-pgn\r\n\r\n";
    out += content;
    out += "\r\n--" + boundary + "--\r\n";
    return out;
}

const char* kScholarMate =
    "[Event \"Test game\"]\n"
    "[Site \"?\"]\n"
    "[Date \"2024.01.01\"]\n"
    "[Round \"1\"]\n"
    "[White \"Alice\"]\n"
    "[Black \"Bob\"]\n"
    "[Result \"1-0\"]\n"
    "\n"
    "1. e4 e5 2. Qh5 Nc6 3. Bc4 Nf6 4. Qxf7# 1-0\n";

const char* kRuyLopez =
    "[Event \"Engine flow\"]\n"
    "[White \"White\"]\n"
    "[Black \"Black\"]\n"
    "[Result \"*\"]\n"
    "\n"
    "1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 *\n";

bool waitForReview(int port, const std::string& id, std::string& finalLocation, int timeoutSeconds) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);
    while (std::chrono::steady_clock::now() < deadline) {
        const Reply reply = httpRequest(port, "GET", "/analysis/" + id);
        if (reply.status == 303) {
            finalLocation = header(reply, "location");
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);

    std::string engine = argc > 1 ? argv[1] : std::string();
    if (engine.empty()) {
        if (const char* env = std::getenv("WECHESS_STOCKFISH")) engine = env;
    }
    const bool engineMode = !engine.empty();

    char dirTemplate[] = "/tmp/wechess-server-test-XXXXXX";
    const char* madeDir = ::mkdtemp(dirTemplate);
    if (madeDir == nullptr) {
        std::fprintf(stderr, "could not create temp dir\n");
        return 1;
    }
    const std::string dataDir = madeDir;

    // Without a real engine we pass an explicit impossible path so the server
    // reports "no engine" deterministically instead of auto-detecting one.
    const std::string enginePath = engineMode ? engine : "/nonexistent/stockfish";
    wechess::App app(dataDir, "", enginePath);
    std::string error;
    if (!app.init(error)) {
        std::fprintf(stderr, "app init failed: %s\n", error.c_str());
        return 1;
    }

    wechess::HttpServer server;
    server.handler = [&app](const wechess::HttpRequest& request) { return app.handle(request); };
    if (!server.listen("127.0.0.1", 0, error)) {
        std::fprintf(stderr, "listen failed: %s\n", error.c_str());
        return 1;
    }
    server.start();
    const int port = server.port();
    std::printf("server_test: listening on 127.0.0.1:%d (engine: %s)\n", port,
                engineMode ? engine.c_str() : "absent on purpose");

    // ---- Home page ---------------------------------------------------------
    {
        const Reply home = httpRequest(port, "GET", "/");
        check(home.transportOk && home.status == 200, "GET / returns 200");
        check(contains(header(home, "content-type"), "text/html"), "GET / is HTML");
        check(contains(home.body, "WeChess"), "home page has branding");
        check(contains(home.body, "action=\"/import\""), "home page has the paste form");
        check(contains(home.body, "action=\"/upload\""), "home page has the upload form");
        check(contains(home.body, "type=\"file\""), "home page has a file input");
        check(pageIsScriptFree(home.body), "home page contains no script");
        check(contains(header(home, "content-security-policy"), "script-src 'none'"),
              "CSP forbids scripts");
        check(contains(home.body, "href=\"/settings\""), "home page links to settings");
    }

    // ---- Stylesheet --------------------------------------------------------
    {
        const Reply css = httpRequest(port, "GET", "/app.css");
        check(css.status == 200, "GET /app.css returns 200");
        check(contains(header(css, "content-type"), "text/css"), "CSS is served as text/css");
        check(contains(css.body, "--board-light"), "stylesheet has theme tokens");
        check(contains(css.body, ".board"), "stylesheet styles the board");
        check(!containsFold(css.body, "url(javascript"), "stylesheet has no script URLs");
        check(!containsFold(css.body, "expression("), "stylesheet has no CSS expressions");
    }

    // ---- Import by paste ---------------------------------------------------
    std::string mateId;
    {
        const std::string form = "pgn=" + wechess::urlEncode(kScholarMate);
        const Reply reply = httpRequest(port, "POST", "/import", form);
        check(reply.status == 303, "POST /import redirects (303)");
        check(header(reply, "location").rfind("/analysis/", 0) == 0,
              "POST /import redirects to the analysis page");
        mateId = idFromLocation(header(reply, "location"));
        check(wechess::Store::validGameId(mateId), "game id looks valid");
    }

    // ---- Missing PGN -------------------------------------------------------
    {
        const Reply reply = httpRequest(port, "POST", "/import", "pgn=");
        checkEq(reply.status, 400, "empty paste is a 400");
        check(containsFold(reply.body, "paste a pgn"), "empty paste explains the problem");
    }

    // ---- Invalid PGN -------------------------------------------------------
    {
        // 2.Ke3 walks the king into an attacked square: the move itself is illegal.
        const std::string form = "pgn=" + wechess::urlEncode("1. e4 e5 2. Ke3");
        const Reply reply = httpRequest(port, "POST", "/import", form);
        checkEq(reply.status, 400, "illegal move in PGN is a 400");
        check(containsFold(reply.body, "error"), "invalid PGN shows an error");
        check(pageIsScriptFree(reply.body), "error page contains no script");
    }

    // ---- Upload ------------------------------------------------------------
    std::string uploadId;
    {
        const std::string boundary = "----WeChessTestBoundary";
        const Reply reply = httpRequest(port, "POST", "/upload",
                                        multipartBody(boundary, "game.pgn", kScholarMate),
                                        "multipart/form-data; boundary=" + boundary);
        check(reply.status == 303, "multipart upload redirects");
        uploadId = idFromLocation(header(reply, "location"));
        check(!uploadId.empty(), "upload produced a game id");
    }

    // ---- Analysis page -----------------------------------------------------
    {
        const Reply reply = httpRequest(port, "GET", "/analysis/" + mateId);
        check(reply.status == 200 || reply.status == 303, "analysis page answers");
        if (reply.status == 200) {
            check(pageIsScriptFree(reply.body), "progress page contains no script");
            check(containsFold(reply.body, "analysis") || containsFold(reply.body, "stockfish"),
                  "progress page describes the analysis state");
        }
    }

    // ---- Review page -------------------------------------------------------
    {
        const Reply review = httpRequest(port, "GET", "/game/" + mateId + "?ply=8");
        checkEq(review.status, 200, "GET /game/<id> returns 200");
        checkEq(countOccurrences(review.body, "class=\"sq "), 64, "the board has 64 squares");
        check(contains(review.body, "id=\"board\""), "the board is anchored for #board links");
        check(contains(review.body, "class=\"evalbar\""), "the evaluation bar is rendered");
        check(contains(review.body, "aria-label=\"First move\""), "navigation controls exist");
        check(contains(review.body, "Qxf7#") || contains(review.body, "Qxf7"), "move list shows SAN");
        check(pageIsScriptFree(review.body), "review page contains no script");

        const Reply start = httpRequest(port, "GET", "/game/" + mateId + "?ply=0");
        checkEq(start.status, 200, "start position renders");
        checkEq(countOccurrences(start.body, "class=\"sq "), 64, "start board has 64 squares");
        check(!contains(start.body, "on e5"), "start position has no pawn on e5");
        check(contains(start.body, "on e2"), "start position has the e2 pawn");

        const Reply afterE4 = httpRequest(port, "GET", "/game/" + mateId + "?ply=1");
        check(contains(afterE4.body, "on e4"), "after 1.e4 the pawn is on e4");
        check(!contains(afterE4.body, "on e2"), "after 1.e4 the e2 square is empty");

        // Out-of-range ply values are clamped, never a crash.
        const Reply clamped = httpRequest(port, "GET", "/game/" + mateId + "?ply=9999");
        checkEq(clamped.status, 200, "out-of-range ply is clamped");
        checkEq(countOccurrences(clamped.body, "class=\"sq "), 64, "clamped board has 64 squares");
    }

    // ---- HTML escaping of metadata ----------------------------------------
    {
        const std::string evil =
            "[Event \"<script>alert(1)</script>\"]\n[White \"A & B\"]\n[Black \"C\"]\n"
            "[Result \"*\"]\n\n1. e4 e5 *\n";
        const Reply reply = httpRequest(port, "POST", "/import", "pgn=" + wechess::urlEncode(evil));
        check(reply.status == 303, "game with markup in headers still imports");
        const std::string id = idFromLocation(header(reply, "location"));
        const Reply review = httpRequest(port, "GET", "/game/" + id + "?ply=2");
        checkEq(review.status, 200, "escaped review renders");
        check(!containsFold(review.body, "<script"), "metadata markup is not injected");
        check(contains(review.body, "&lt;script&gt;"), "metadata markup is escaped");
        check(contains(review.body, "A &amp; B"), "ampersands are escaped");
        const Reply home = httpRequest(port, "GET", "/");
        check(!containsFold(home.body, "<script"), "recent list does not inject markup");
    }

    // ---- Bad routes and traversal -----------------------------------------
    {
        checkEq(httpRequest(port, "GET", "/does-not-exist").status, 404, "unknown route is 404");
        checkEq(httpRequest(port, "GET", "/game/..%2F..%2Fetc%2Fpasswd").status, 404,
                "path traversal is rejected");
        checkEq(httpRequest(port, "GET", "/game/not-a-real-id").status, 404, "unknown game is 404");
        checkEq(httpRequest(port, "GET", "/analysis/ZZZZ/../../").status, 404, "malformed path is 404");
        checkEq(httpRequest(port, "POST", "/game/" + mateId + "/nonsense").status, 404,
                "unknown POST action is 404");
        checkEq(httpRequest(port, "GET", "/healthz").status, 200, "health check answers");
    }

    // ---- Settings ----------------------------------------------------------
    {
        const Reply page = httpRequest(port, "GET", "/settings");
        checkEq(page.status, 200, "settings page renders");
        check(pageIsScriptFree(page.body), "settings page contains no script");
        check(contains(page.body, "name=\"depth\""), "settings expose the engine depth");

        const Reply saved = httpRequest(port, "POST", "/settings",
                                        "depth=12&multipv=2&critical_depth=18&gemini_model=gemini-2.5-flash");
        check(saved.status == 303, "saving settings redirects");
        checkEq(app.settings().depth, 12, "depth was persisted");
        checkEq(app.settings().multipv, 2, "multipv was persisted");

        const Reply bad = httpRequest(port, "POST", "/settings",
                                      "depth=99&multipv=9&critical_depth=64&gemini_model=x");
        check(bad.status == 303, "out-of-range settings fall back instead of failing");
        checkEq(app.settings().depth, 14, "depth falls back to the default");

        const Reply insecure = httpRequest(
            port, "POST", "/settings",
            "gemini_proxy_url=" + wechess::urlEncode("http://evil.example.com/v1") +
                "&gemini_model=gemini-2.5-flash");
        checkEq(insecure.status, 400, "plain-http proxy URL is rejected");
    }

    // ---- Delete ------------------------------------------------------------
    {
        const Reply deleted = httpRequest(port, "POST", "/game/" + uploadId + "/delete");
        check(deleted.status == 303, "delete redirects home");
        checkEq(httpRequest(port, "GET", "/game/" + uploadId).status, 404, "deleted game is gone");
    }

    // ---- Full engine flow --------------------------------------------------
    if (engineMode) {
        // Cheap settings so the test stays fast but still real.
        httpRequest(port, "POST", "/settings",
                    "depth=6&multipv=2&critical_depth=8&gemini_model=gemini-2.5-flash");
        const Reply imported = httpRequest(port, "POST", "/import", "pgn=" + wechess::urlEncode(kRuyLopez));
        check(imported.status == 303, "engine game imports");
        const std::string id = idFromLocation(header(imported, "location"));

        std::string location;
        const bool finished = waitForReview(port, id, location, 180);
        check(finished, "analysis finishes and redirects to the review");
        if (finished) {
            check(location.rfind("/game/" + id, 0) == 0, "analysis redirects to the game review");
            const Reply review = httpRequest(port, "GET", "/game/" + id + "?ply=6");
            checkEq(review.status, 200, "review renders after analysis");
            checkEq(countOccurrences(review.body, "class=\"sq "), 64, "analysed board has 64 squares");
            check(containsFold(review.body, "accuracy"), "review shows accuracy");
            check(containsFold(review.body, "blunder"), "review shows classification counts");
            check(containsFold(review.body, "analysis"), "review shows engine metadata");
            check(pageIsScriptFree(review.body), "analysed review contains no script");

            // ---- Gemini explanation flow, through the UI -------------------
            wechess::HttpServer mockProxy;
            mockProxy.handler = [](const wechess::HttpRequest&) {
                return wechess::HttpResponse::text(
                    "{\"text\":\"Mock explanation: this move keeps the tension.\"}",
                    "application/json");
            };
            std::string proxyError;
            if (mockProxy.listen("127.0.0.1", 0, proxyError)) {
                mockProxy.start();
                const std::string settings =
                    "gemini_proxy_url=" +
                    wechess::urlEncode("http://127.0.0.1:" +
                                       std::to_string(mockProxy.port())) +
                    "&gemini_token=0123456789abcdef&gemini_model=gemini-2.5-flash&gemini_enabled=on";
                check(httpRequest(port, "POST", "/settings", settings).status == 303,
                      "proxy settings saved");

                const Reply explain =
                    httpRequest(port, "POST", "/game/" + id + "/explain", "ply=1");
                check(explain.status == 303, "explain request redirects");
                check(header(explain, "location").rfind("/explain-wait/", 0) == 0,
                      "explain goes to the wait page");

                bool ready = false;
                for (int i = 0; i < 100 && !ready; ++i) {
                    if (httpRequest(port, "GET", "/explain-wait/" + id + "?ply=1").status == 303) {
                        ready = true;
                    } else {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                }
                check(ready, "explanation finishes and redirects to the review");

                const Reply withText = httpRequest(port, "GET", "/game/" + id + "?ply=1");
                check(contains(withText.body,
                               "Mock explanation: this move keeps the tension."),
                      "the explanation is shown in the review panel");
                check(pageIsScriptFree(withText.body), "explanation page contains no script");
                check(httpRequest(port, "GET", "/explain-wait/" + id + "?ply=0").status == 303,
                      "wait page redirects for moves without a request");
                mockProxy.stop();
            } else {
                check(false, "mock proxy could not start: " + proxyError);
            }
        }
    } else {
        check(true, "engine flow skipped: no Stockfish binary supplied");
    }

    server.stop();

    std::printf("server_test: %d checks, %d failures\n", gChecks, gFailures);
    std::fflush(stdout);
    std::system(("rm -rf '" + dataDir + "'").c_str());
    return gFailures == 0 ? 0 : 1;
}
