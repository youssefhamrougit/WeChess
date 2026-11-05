// WeChess - local persistence: settings, recent games and the evaluation cache.
#ifndef WECHESS_SERVER_STORE_HPP
#define WECHESS_SERVER_STORE_HPP

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "../engine/analyzer.hpp"

namespace wechess {

struct AppSettings {
    int depth = 14;
    int multipv = 3;
    bool deepPass = true;
    int criticalDepth = 20;
    std::string stockfishPath;   // empty = auto-detect
    std::string geminiProxyUrl;  // https://your-proxy.example.com
    std::string geminiToken;     // shared token for the proxy, NOT the Gemini key
    bool geminiEnabled = false;
    std::string geminiModel = "gemini-2.5-flash";
};

struct RecentGame {
    std::string id;
    long long created = 0;
    std::string white, black, result, event, date, eco, opening;
    int plies = 0;
};

class Store {
   public:
    // Creates the directory layout under `root` and loads settings/index.
    bool open(const std::string& root, std::string& error);

    const std::string& root() const { return root_; }
    EvalCache& cache() { return *cache_; }

    AppSettings settings() const;
    void saveSettings(const AppSettings& settings);

    std::vector<RecentGame> recents() const;
    void addRecent(const RecentGame& game);
    void removeRecent(const std::string& id);

    // Games are stored as the original PGN text: it is the smallest lossless
    // format, and re-analysis is cheap thanks to the evaluation cache.
    bool writePgn(const std::string& id, const std::string& pgn) const;
    std::string readPgn(const std::string& id) const;

    static std::string newGameId();
    static bool validGameId(const std::string& id);

   private:
    void loadLocked();
    void saveIndexLocked() const;
    std::string pathFor(const std::string& name) const;

    std::string root_;
    mutable std::mutex mutex_;
    AppSettings settings_;
    std::vector<RecentGame> recents_;
    std::unique_ptr<EvalCache> cache_;
};

}  // namespace wechess

#endif  // WECHESS_SERVER_STORE_HPP
