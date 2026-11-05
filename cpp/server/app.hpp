// WeChess - request routing and application state.
#ifndef WECHESS_SERVER_APP_HPP
#define WECHESS_SERVER_APP_HPP

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "../gemini/client.hpp"
#include "http.hpp"
#include "session.hpp"
#include "store.hpp"
#include "views.hpp"

namespace wechess {

class App {
   public:
    App(std::string dataDir, std::string nativeLibDir, std::string stockfishOverride);

    bool init(std::string& error);

    HttpResponse handle(const HttpRequest& request);

    // Path of the Stockfish binary that will be used (empty when missing).
    std::string stockfishPath() const;
    AppSettings settings() const;

   private:
    HttpResponse routeHome(const HttpRequest& request);
    HttpResponse routeImport(const HttpRequest& request);
    HttpResponse routeUpload(const HttpRequest& request);
    HttpResponse routeAnalysis(const std::string& id);
    HttpResponse routeGame(const HttpRequest& request, const std::string& id);
    HttpResponse routeExplainWait(const HttpRequest& request, const std::string& id);
    HttpResponse routeExplain(const HttpRequest& request, const std::string& id);
    HttpResponse routeAnalyse(const std::string& id);
    HttpResponse routeCancel(const std::string& id);
    HttpResponse routeDelete(const std::string& id);
    HttpResponse routeSettings(const HttpRequest& request);

    HttpResponse importPgn(const std::string& pgn, const std::string& errorPrefix);
    std::shared_ptr<GameSession> findSession(const std::string& id);
    std::shared_ptr<GameSession> createSession(const std::string& pgn);
    void startAnalysis(const std::shared_ptr<GameSession>& session);
    PageChrome chrome() const;
    GeminiConfig geminiConfig() const;
    static HttpResponse page(const std::string& html, int status = 200);

    std::string dataDir_;
    std::string nativeLibDir_;
    std::string stockfishOverride_;

    Store store_;
    mutable std::mutex mutex_;
    AppSettings settings_;
    std::unordered_map<std::string, std::shared_ptr<GameSession>> sessions_;
};

}  // namespace wechess

#endif  // WECHESS_SERVER_APP_HPP
