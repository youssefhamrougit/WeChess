// WeChess - asynchronous whole-game analysis with Stockfish and a disk cache.
#ifndef WECHESS_ENGINE_ANALYZER_HPP
#define WECHESS_ENGINE_ANALYZER_HPP

#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "../analysis/types.hpp"
#include "../core/game.hpp"
#include "uci.hpp"

namespace wechess {

struct AnalysisConfig {
    std::string stockfishPath;
    EngineSettings engine;         // base pass settings (depth, multipv, threads, hash)
    int criticalDepth = 20;        // deeper pass for tactical positions
    bool deepPass = true;
};

// Thread-safe progress snapshot shared with the HTTP layer.
struct AnalysisProgress {
    std::atomic<int> completed{0};
    std::atomic<int> total{0};
    std::atomic<int> pass{1};
    std::atomic<bool> running{false};
    std::atomic<bool> finished{false};
    std::atomic<bool> failed{false};
    std::atomic<bool> cancelled{false};

    void setStatus(const std::string& value) {
        std::lock_guard<std::mutex> lock(statusMutex_);
        status_ = value;
    }
    std::string status() const {
        std::lock_guard<std::mutex> lock(statusMutex_);
        return status_;
    }
    int percent() const {
        const int t = total.load();
        if (t <= 0) return finished.load() ? 100 : 0;
        const int p = completed.load() * 100 / t;
        return p > 100 ? 100 : p;
    }

   private:
    mutable std::mutex statusMutex_;
    std::string status_ = "Waiting to start";
};

struct AnalysisOutput {
    bool ok = false;
    bool engineAvailable = false;
    std::string error;
    std::string engineName;
    int analysed = 0;
    int cached = 0;
    std::vector<PositionEval> evals;  // size plies+1
};

// Persistent evaluation cache, keyed by FEN. Stores enough information for
// classification (all MultiPV lines) so re-opening a game is instant.
class EvalCache {
   public:
    void open(const std::string& path);
    bool lookup(const std::string& fen, int minDepth, PositionEval& out) const;
    void store(const PositionEval& eval);
    void flush();
    std::size_t size() const;
    const std::string& path() const { return path_; }

   private:
    static std::string encode(const PositionEval& eval);
    static bool decode(const std::string& line, PositionEval& out);

    mutable std::mutex mutex_;
    std::unordered_map<std::string, PositionEval> entries_;
    std::vector<PositionEval> pending_;
    std::string path_;
    bool loaded_ = false;
};

// Runs a full-game analysis. Blocking: call it from a worker thread. `cancel`
// is polled continuously; the engine is always shut down before returning.
AnalysisOutput runAnalysis(const ParsedGame& game, const AnalysisConfig& config, EvalCache* cache,
                           AnalysisProgress& progress, const std::atomic<bool>& cancel);

}  // namespace wechess

#endif  // WECHESS_ENGINE_ANALYZER_HPP
