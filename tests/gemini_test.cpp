// WeChess - Gemini client, prompt and JSON tests.
//
// A local mock proxy (plain HTTP on 127.0.0.1, which the client accepts for
// tests only) stands in for the real service, so these tests never touch the
// network or need an API key.
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <string>
#include <thread>

#include "analysis/review.hpp"
#include "gemini/client.hpp"
#include "gemini/http_client.hpp"
#include "gemini/json.hpp"
#include "gemini/prompt.hpp"
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

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

struct MockState {
    std::atomic<int> hits{0};
    std::atomic<int> lastStatus{200};
    std::string body = "{\"text\":\"This is a mock explanation.\"}";
    std::string lastAuth;
    std::string lastBody;
    std::string expectedPath = "/v1/explain";
};

wechess::GeminiConfig localConfig(int port, const std::string& token = "test-token-0123456789") {
    wechess::GeminiConfig config;
    config.enabled = true;
    config.proxyUrl = "http://127.0.0.1:" + std::to_string(port);
    config.token = token;
    config.model = "gemini-2.5-flash";
    return config;
}

}  // namespace

int main() {
    std::signal(SIGPIPE, SIG_IGN);

    // ---- JSON --------------------------------------------------------------
    {
        check(wechess::jsonQuote("plain") == "\"plain\"", "jsonQuote wraps plain strings");
        check(wechess::jsonQuote("a\"b") == "\"a\\\"b\"", "jsonQuote escapes quotes");
        check(wechess::jsonQuote("a\nb\tc") == "\"a\\nb\\tc\"", "jsonQuote escapes control characters");
        check(wechess::jsonQuote("caf\xc3\xa9") == "\"caf\xc3\xa9\"", "UTF-8 passes through unchanged");

        wechess::JsonValue parsed;
        std::string error;
        const bool ok = wechess::parseJson(
            "{\"text\":\"hi\",\"n\":3,\"arr\":[1,2],\"nested\":{\"flag\":true}}", parsed, error);
        check(ok, "parseJson reads an object: " + error);
        const wechess::JsonValue* text = parsed.get("text");
        check(text != nullptr && text->isString() && text->string == "hi", "string field parsed");
        const wechess::JsonValue* number = parsed.get("n");
        check(number != nullptr && number->type == wechess::JsonValue::Type::Number &&
                  number->number == 3.0,
              "number field parsed");
        const wechess::JsonValue* nested = parsed.get("nested");
        check(nested != nullptr && nested->get("flag") != nullptr &&
                  nested->get("flag")->boolean,
              "nested object parsed");

        wechess::JsonValue broken;
        check(!wechess::parseJson("{\"text\":", broken, error), "truncated JSON is rejected");
        check(!wechess::parseJson("not json at all", broken, error), "garbage JSON is rejected");
        check(!wechess::parseJson("", broken, error), "empty JSON is rejected");
    }

    // ---- Configuration -----------------------------------------------------
    {
        wechess::GeminiClient disabled{wechess::GeminiConfig{}};
        check(!disabled.configured(), "default config is not configured");
        const wechess::GeminiResult result = disabled.explain("hello");
        check(!result.ok, "unconfigured client refuses to call out");
        check(contains(result.error, "not configured"), "unconfigured error is actionable");

        wechess::GeminiConfig noToken = localConfig(1, "");
        check(!wechess::GeminiClient(noToken).configured(), "missing token disables Gemini");

        wechess::GeminiConfig plainHttp;
        plainHttp.enabled = true;
        plainHttp.proxyUrl = "http://evil.example.com";
        plainHttp.token = "abcdefghijklmnop";
        check(!wechess::GeminiClient(plainHttp).configured(), "plain-http remote proxy is refused");

        wechess::GeminiConfig off = localConfig(1);
        off.enabled = false;
        check(!wechess::GeminiClient(off).configured(), "the enabled flag is honoured");
    }

    // ---- Prompt ------------------------------------------------------------
    {
        const std::string pgn =
            "[Event \"Prompt test\"]\n[White \"Student\"]\n[Black \"Coach\"]\n[Result \"0-1\"]\n\n"
            "1. f3 e5 2. g4 Qh4# 0-1\n";
        const wechess::PgnParseResult parsed = wechess::parsePgn(pgn);
        check(parsed.ok && parsed.game.plies.size() == 4, "prompt fixture parsed");

        wechess::GameReview review;
        review.openingName = "Test Opening";
        review.eco = "A00";
        review.moves.resize(parsed.game.plies.size());
        wechess::MoveReview& blunder = review.moves[2];
        blunder.ply = 3;
        blunder.cls = wechess::Classification::Blunder;
        blunder.cpLoss = 350;
        blunder.depth = 18;
        blunder.playedUci = "g2g4";
        blunder.playedSan = "g4";
        blunder.bestMoveSan = "Qh4#";
        blunder.pvSan = "1... Qh4#";
        blunder.summary = "Allows mate in one.";
        blunder.sacrificeCp = 0;

        const std::string prompt =
            wechess::buildMoveExplanationPrompt(parsed.game, review, 3);
        check(!prompt.empty(), "prompt is built for a real move");
        check(contains(prompt, "Classification: Blunder"), "prompt states the classification");
        check(contains(prompt, "Centipawn loss: 350"), "prompt states the centipawn loss");
        check(contains(prompt, "Qh4#"), "prompt includes the engine's best move");
        check(contains(prompt, parsed.game.plies[2].fenBefore), "prompt includes the FEN before");
        check(contains(prompt, parsed.game.plies[2].fenAfter), "prompt includes the FEN after");
        check(contains(prompt, "Played by: White"), "prompt names the player");
        check(contains(prompt, "Do not invent"), "prompt forbids invented analysis");
        check(contains(prompt, "use ONLY the facts above"), "prompt constrains the model to the data");
        check(wechess::buildMoveExplanationPrompt(parsed.game, review, 0).empty(),
              "ply 0 has no prompt");
        check(wechess::buildMoveExplanationPrompt(parsed.game, review, 99).empty(),
              "unknown ply has no prompt");
    }

    // ---- Transport against a local mock proxy ------------------------------
    // Builds configured with -DWECHESS_WITH_CURL=OFF have no HTTPS client: the
    // transport cases cannot run, so report that instead of failing.
    if (!wechess::httpClientAvailable()) {
        wechess::GeminiClient client(localConfig(1));
        const wechess::GeminiResult result = client.explain("x");
        check(client.configured(), "config is valid even without libcurl");
        check(!result.ok, "without HTTPS transport no explanation is attempted");
        check(contains(result.error, "HTTPS"), "the missing transport is explained");
        std::printf("gemini_test: %d checks, %d failures (transport tests skipped: no libcurl)\n",
                    gChecks, gFailures);
        std::fflush(stdout);
        return gFailures == 0 ? 0 : 1;
    }

    MockState mock;
    wechess::HttpServer server;
    server.handler = [&mock](const wechess::HttpRequest& request) {
        mock.hits.fetch_add(1);
        mock.lastBody = request.body;
        const auto auth = request.headers.find("authorization");
        mock.lastAuth = auth == request.headers.end() ? std::string() : auth->second;
        if (request.path != mock.expectedPath) {
            return wechess::HttpResponse::error(404, "{\"error\":\"unknown path\"}");
        }
        const int status = mock.lastStatus.load();
        wechess::HttpResponse response =
            wechess::HttpResponse::text(mock.body, "application/json");
        response.status = status;
        return response;
    };
    std::string error;
    if (!server.listen("127.0.0.1", 0, error)) {
        std::fprintf(stderr, "mock server failed: %s\n", error.c_str());
        return 1;
    }
    server.start();
    const int port = server.port();

    {
        wechess::GeminiClient client(localConfig(port));
        check(client.configured(), "localhost mock proxy counts as configured");
        const wechess::GeminiResult result = client.explain("Explain move 3.");
        check(result.ok, "successful explanation parsed: " + result.error);
        check(result.text == "This is a mock explanation.", "explanation text returned verbatim");
        check(mock.lastAuth == "Bearer test-token-0123456789", "token sent as a bearer header");
        check(contains(mock.lastBody, "\"model\":\"gemini-2.5-flash\""), "model sent in the body");
        check(contains(mock.lastBody, "\"prompt\":\"Explain move 3.\""), "prompt sent in the body");
    }

    {
        mock.lastStatus.store(401);
        mock.body = "{\"error\":\"invalid application token\"}";
        wechess::GeminiClient client(localConfig(port));
        const wechess::GeminiResult result = client.explain("x");
        check(!result.ok, "401 is an error");
        check(contains(result.error, "invalid application token"), "proxy error message surfaced");
        check(!contains(result.error, "test-token"), "the token never appears in errors");

        mock.lastStatus.store(503);
        mock.body = "{\"error\":\"no gemini key\"}";
        check(!client.explain("x").ok, "503 is an error");

        mock.lastStatus.store(200);
        mock.body = "this is not json";
        const wechess::GeminiResult malformed = client.explain("x");
        check(!malformed.ok, "malformed body is rejected");
        check(contains(malformed.error, "malformed"), "malformed error is explicit");

        mock.body = "{\"text\":\"\"}";
        const wechess::GeminiResult empty = client.explain("x");
        check(!empty.ok, "empty text is rejected");
        check(contains(empty.error, "no explanation"), "empty text error is explicit");

        mock.body = "{\"text\":\"ok\"}";
        mock.expectedPath = "/v1/other";
        const wechess::GeminiResult wrongPath = client.explain("x");
        mock.expectedPath = "/v1/explain";
        check(!wrongPath.ok, "unexpected proxy path is an error");
    }

    server.stop();
    std::printf("gemini_test: %d checks, %d failures (%d mock requests)\n", gChecks, gFailures,
                mock.hits.load());
    std::fflush(stdout);
    return gFailures == 0 ? 0 : 1;
}
