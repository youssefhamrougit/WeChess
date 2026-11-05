#include "app.hpp"

#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <sstream>

#include "../analysis/opening.hpp"
#include "../gemini/prompt.hpp"

namespace wechess {

// Implemented by the generated embedded_assets.cpp.
namespace assets {
const char* shellHtml();
const char* stylesheet();
}  // namespace assets

namespace {

bool isExecutable(const std::string& path) {
    return !path.empty() && ::access(path.c_str(), X_OK) == 0;
}

std::string trimCopy(const std::string& value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return value.substr(begin, end - begin);
}

int clampInt(int value, int low, int high, int fallback) {
    if (value < low || value > high) return fallback;
    return value;
}

// Splits "/game/abc123/analyse" into {"game", "abc123", "analyse"}.
std::vector<std::string> pathSegments(const std::string& path) {
    std::vector<std::string> segments;
    std::string current;
    for (std::size_t i = 1; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/') {
            if (!current.empty()) segments.push_back(current);
            current.clear();
        } else {
            current.push_back(path[i]);
        }
    }
    return segments;
}

std::string queryMessage(const HttpRequest& request, bool error) {
    return request.param(error ? "err" : "msg");
}

}  // namespace

App::App(std::string dataDir, std::string nativeLibDir, std::string stockfishOverride)
    : dataDir_(std::move(dataDir)),
      nativeLibDir_(std::move(nativeLibDir)),
      stockfishOverride_(std::move(stockfishOverride)) {}

bool App::init(std::string& error) {
    if (!store_.open(dataDir_, error)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    settings_ = store_.settings();
    // Warm the opening book once so the first review does not pay for it.
    OpeningBook::instance();
    return true;
}

AppSettings App::settings() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return settings_;
}

std::string App::stockfishPath() const {
    // An explicitly supplied binary is authoritative: if it is not executable we
    // report "no engine" instead of quietly picking a different build.
    if (!stockfishOverride_.empty()) {
        return isExecutable(stockfishOverride_) ? stockfishOverride_ : std::string();
    }
    const AppSettings current = settings();
    if (isExecutable(current.stockfishPath)) return current.stockfishPath;

    std::vector<std::string> candidates;
    if (const char* env = std::getenv("WECHESS_STOCKFISH")) candidates.emplace_back(env);
    if (!nativeLibDir_.empty()) {
        candidates.push_back(nativeLibDir_ + "/libstockfish.so");
        candidates.push_back(nativeLibDir_ + "/stockfish");
    }
    candidates.push_back("./stockfish/stockfish");
    candidates.push_back("/usr/games/stockfish");
    candidates.push_back("/usr/bin/stockfish");
    candidates.push_back("/usr/local/bin/stockfish");

    for (const std::string& candidate : candidates) {
        if (isExecutable(candidate)) return candidate;
    }
    return std::string();
}

PageChrome App::chrome() const {
    PageChrome info;
    const std::string engine = stockfishPath();
    info.engineAvailable = isExecutable(engine);
    info.stockfishName = "Stockfish";
    info.geminiConfigured = GeminiClient(geminiConfig()).configured();
    return info;
}

GeminiConfig App::geminiConfig() const {
    const AppSettings current = settings();
    GeminiConfig config;
    config.proxyUrl = current.geminiProxyUrl;
    config.token = current.geminiToken;
    config.model = current.geminiModel;
    config.enabled = current.geminiEnabled;
    return config;
}

HttpResponse App::page(const std::string& html, int status) {
    HttpResponse response = HttpResponse::html(html, status);
    return response;
}

std::shared_ptr<GameSession> App::findSession(const std::string& id) {
    if (!Store::validGameId(id)) return nullptr;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = sessions_.find(id);
    return it == sessions_.end() ? nullptr : it->second;
}

std::shared_ptr<GameSession> App::createSession(const std::string& pgn) {
    PgnParseResult parsed = parsePgn(pgn, 800);
    if (!parsed.ok) return nullptr;

    const std::string id = Store::newGameId();
    store_.writePgn(id, pgn);

    RecentGame recent;
    recent.id = id;
    recent.white = parsed.game.meta.white;
    recent.black = parsed.game.meta.black;
    recent.result = parsed.game.meta.result;
    recent.event = parsed.game.meta.event;
    recent.date = parsed.game.meta.date;
    recent.eco = parsed.game.meta.eco;
    recent.opening = parsed.game.meta.opening;
    recent.plies = static_cast<int>(parsed.game.plies.size());
    recent.created = 0;

    auto session = std::make_shared<GameSession>(id, std::move(parsed.game), pgn);
    recent.created = session->created();
    store_.addRecent(recent);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_[id] = session;
    }
    startAnalysis(session);
    return session;
}

void App::startAnalysis(const std::shared_ptr<GameSession>& session) {
    const AppSettings current = settings();
    AnalysisConfig config;
    config.stockfishPath = stockfishPath();
    config.engine.depth = current.depth;
    config.engine.multipv = current.multipv;
    config.engine.threads = 1;
    config.engine.hashMb = 64;
    config.criticalDepth = current.criticalDepth;
    config.deepPass = current.deepPass;

    std::string error;
    session->startAnalysis(config, &store_.cache(), error);
}

HttpResponse App::handle(const HttpRequest& request) {
    const std::string path = request.path;

    if (request.method == "GET" && (path == "/app.css" || path == "/app.css/")) {
        return HttpResponse::text(assets::stylesheet(), "text/css; charset=utf-8");
    }
    if (path == "/healthz") {
        return HttpResponse::text("ok");
    }
    if (path == "/" || path.empty()) {
        return routeHome(request);
    }
    if (path == "/import" && request.method == "POST") {
        return routeImport(request);
    }
    if (path == "/upload" && request.method == "POST") {
        return routeUpload(request);
    }
    if (path == "/settings") {
        return routeSettings(request);
    }

    const std::vector<std::string> segments = pathSegments(path);
    if (segments.size() >= 2) {
        if (segments[0] == "game") {
            const std::string& id = segments[1];
            if (segments.size() == 2) return routeGame(request, id);
            if (segments.size() == 3 && request.method == "POST") {
                if (segments[2] == "analyse") return routeAnalyse(id);
                if (segments[2] == "cancel") return routeCancel(id);
                if (segments[2] == "delete") return routeDelete(id);
                if (segments[2] == "explain") return routeExplain(request, id);
            }
        } else if (segments[0] == "analysis" && segments.size() == 2) {
            return routeAnalysis(segments[1]);
        } else if (segments[0] == "explain-wait" && segments.size() == 2) {
            return routeExplainWait(request, segments[1]);
        }
    }

    return page(renderNotFound(), 404);
}

HttpResponse App::routeHome(const HttpRequest& request) {
    return page(renderHome(chrome(), store_.recents(), settings(), queryMessage(request, false),
                           queryMessage(request, true)));
}

HttpResponse App::routeImport(const HttpRequest& request) {
    const std::string pgn = request.param("pgn");
    if (trimCopy(pgn).empty()) {
        return page(renderHome(chrome(), store_.recents(), settings(), "",
                               "Paste a PGN before starting a review."),
                    400);
    }
    const HttpResponse response = importPgn(pgn, "paste");
    return response;
}

HttpResponse App::routeUpload(const HttpRequest& request) {
    const auto it = request.files.find("file");
    if (it == request.files.end() || it->second.data.empty()) {
        return page(renderHome(chrome(), store_.recents(), settings(), "",
                               "No PGN file was received. Choose a .pgn file and try again."),
                    400);
    }
    if (it->second.data.size() > 4 * 1024 * 1024) {
        return page(renderHome(chrome(), store_.recents(), settings(), "",
                               "That file is larger than 4 MB. PGN games are much smaller."),
                    400);
    }
    return importPgn(it->second.data, "file");
}

HttpResponse App::importPgn(const std::string& pgn, const std::string& errorPrefix) {
    (void)errorPrefix;
    if (pgn.size() > 4 * 1024 * 1024) {
        return page(renderHome(chrome(), store_.recents(), settings(), "",
                               "That PGN is larger than 4 MB."),
                    400);
    }
    PgnParseResult parsed = parsePgn(pgn, 800);
    if (!parsed.ok) {
        return page(renderHome(chrome(), store_.recents(), settings(), "", parsed.error), 400);
    }

    const std::string id = Store::newGameId();
    if (!store_.writePgn(id, pgn)) {
        return page(renderHome(chrome(), store_.recents(), settings(), "",
                               "Could not store the game on this device."),
                    500);
    }

    RecentGame recent;
    recent.id = id;
    recent.white = parsed.game.meta.white;
    recent.black = parsed.game.meta.black;
    recent.result = parsed.game.meta.result;
    recent.event = parsed.game.meta.event;
    recent.date = parsed.game.meta.date;
    recent.eco = parsed.game.meta.eco;
    recent.opening = parsed.game.meta.opening;
    recent.plies = static_cast<int>(parsed.game.plies.size());

    auto session = std::make_shared<GameSession>(id, std::move(parsed.game), pgn);
    recent.created = session->created();
    store_.addRecent(recent);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_[id] = session;
    }
    startAnalysis(session);
    return HttpResponse::redirect("/analysis/" + id);
}

HttpResponse App::routeAnalysis(const std::string& id) {
    auto session = findSession(id);
    if (!session) {
        return page(renderNotFound(), 404);
    }
    if (session->analysisDone()) {
        if (session->hasReview()) {
            const int lastPly = static_cast<int>(session->game().plies.size());
            return HttpResponse::redirect("/game/" + id + "?ply=" + std::to_string(lastPly) + "#board");
        }
        // Failed or cancelled: show the error page with a retry action.
        return page(renderAnalysisProgress(chrome(), *session));
    }
    return page(renderAnalysisProgress(chrome(), *session));
}

HttpResponse App::routeGame(const HttpRequest& request, const std::string& id) {
    auto session = findSession(id);
    if (!session) {
        const std::string pgn = store_.readPgn(id);
        if (pgn.empty()) return page(renderNotFound(), 404);
        PgnParseResult parsed = parsePgn(pgn, 800);
        if (!parsed.ok) return page(renderNotFound(), 404);
        auto created = std::make_shared<GameSession>(id, std::move(parsed.game), pgn);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            sessions_[id] = created;
        }
        startAnalysis(created);
        return HttpResponse::redirect("/analysis/" + id);
    }

    const int totalPlies = static_cast<int>(session->game().plies.size());
    int ply = request.intParam("ply", totalPlies);
    ply = std::clamp(ply, 0, totalPlies);
    return page(renderReview(chrome(), *session, ply, queryMessage(request, false),
                             queryMessage(request, true)));
}

HttpResponse App::routeExplainWait(const HttpRequest& request, const std::string& id) {
    auto session = findSession(id);
    if (!session) return page(renderNotFound(), 404);
    const int totalPlies = static_cast<int>(session->game().plies.size());
    const int ply = std::clamp(request.intParam("ply", 0), 0, totalPlies);
    const ExplanationEntry entry = session->explanation(ply);
    if (entry.state == "ready" || entry.state == "failed" || entry.state == "none") {
        return HttpResponse::redirect("/game/" + id + "?ply=" + std::to_string(ply) + "#review");
    }
    return page(renderExplainWait(chrome(), *session, ply));
}

HttpResponse App::routeExplain(const HttpRequest& request, const std::string& id) {
    auto session = findSession(id);
    if (!session) return page(renderNotFound(), 404);

    const int totalPlies = static_cast<int>(session->game().plies.size());
    const int ply = std::clamp(request.intParam("ply", 0), 0, totalPlies);
    if (ply < 1) {
        return HttpResponse::redirect("/game/" + id + "?ply=" + std::to_string(ply) + "#review");
    }

    GeminiClient client(geminiConfig());
    if (!client.configured()) {
        return HttpResponse::redirect("/game/" + id + "?ply=" + std::to_string(ply) +
                                      "&err=Gemini+is+not+configured.+Add+the+proxy+URL+and+token+in+"
                                      "Settings.#review");
    }

    const GameReview review = session->review();
    if (review.moves.empty()) {
        return HttpResponse::redirect("/game/" + id + "?ply=" + std::to_string(ply) +
                                      "&err=Analysis+must+finish+before+an+explanation+can+be+"
                                      "written.#review");
    }

    const std::string prompt = buildMoveExplanationPrompt(session->game(), review, ply);
    if (prompt.empty()) {
        return HttpResponse::redirect("/game/" + id + "?ply=" + std::to_string(ply) + "#review");
    }

    if (session->explanationPending(ply)) {
        return HttpResponse::redirect("/explain-wait/" + id + "?ply=" + std::to_string(ply));
    }

    session->setExplanationPending(ply);
    std::thread([session, client, prompt, ply]() {
        GeminiResult result = client.explain(prompt);
        if (result.ok) {
            session->setExplanationReady(ply, result.text);
        } else {
            session->setExplanationFailed(ply, result.error);
        }
    }).detach();

    return HttpResponse::redirect("/explain-wait/" + id + "?ply=" + std::to_string(ply));
}

HttpResponse App::routeAnalyse(const std::string& id) {
    auto session = findSession(id);
    if (!session) return page(renderNotFound(), 404);
    if (!session->analysisRunning()) {
        startAnalysis(session);
    }
    return HttpResponse::redirect("/analysis/" + id);
}

HttpResponse App::routeCancel(const std::string& id) {
    auto session = findSession(id);
    if (!session) return page(renderNotFound(), 404);
    session->cancelAnalysis();
    return HttpResponse::redirect("/analysis/" + id);
}

HttpResponse App::routeDelete(const std::string& id) {
    if (Store::validGameId(id)) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = sessions_.find(id);
            if (it != sessions_.end()) {
                it->second->cancelAnalysis();
                sessions_.erase(it);
            }
        }
        store_.removeRecent(id);
    }
    return HttpResponse::redirect("/");
}

HttpResponse App::routeSettings(const HttpRequest& request) {
    if (request.method == "GET") {
        return page(renderSettings(chrome(), settings(), stockfishPath(), queryMessage(request, false),
                                   queryMessage(request, true)));
    }
    if (request.method != "POST") {
        return page(renderNotFound(), 404);
    }

    AppSettings updated = settings();
    updated.depth = clampInt(request.intParam("depth", updated.depth), 4, 30, 14);
    updated.multipv = clampInt(request.intParam("multipv", updated.multipv), 1, 5, 3);
    updated.criticalDepth = clampInt(request.intParam("critical_depth", updated.criticalDepth), 6, 32, 20);
    updated.deepPass = request.has("deep_pass");
    updated.stockfishPath = trimCopy(request.param("stockfish_path", updated.stockfishPath));
    updated.geminiEnabled = request.has("gemini_enabled");
    updated.geminiProxyUrl = trimCopy(request.param("gemini_proxy_url", updated.geminiProxyUrl));
    updated.geminiToken = trimCopy(request.param("gemini_token", updated.geminiToken));
    updated.geminiModel = trimCopy(request.param("gemini_model", updated.geminiModel));
    if (updated.geminiModel.empty()) updated.geminiModel = "gemini-2.5-flash";

    if (!updated.geminiProxyUrl.empty() && updated.geminiProxyUrl.rfind("https://", 0) != 0 &&
        updated.geminiProxyUrl.rfind("http://127.0.0.1", 0) != 0 &&
        updated.geminiProxyUrl.rfind("http://localhost", 0) != 0) {
        return page(renderSettings(chrome(), updated, stockfishPath(), "",
                                   "The proxy URL must use https:// (localhost is allowed for testing)."),
                    400);
    }

    store_.saveSettings(updated);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        settings_ = updated;
    }
    return HttpResponse::redirect("/settings?msg=Settings+saved");
}

}  // namespace wechess
