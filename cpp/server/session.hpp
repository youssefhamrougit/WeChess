// WeChess - one analysed game: positions, review, progress and explanations.
#ifndef WECHESS_SERVER_SESSION_HPP
#define WECHESS_SERVER_SESSION_HPP

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "../analysis/review.hpp"
#include "../analysis/types.hpp"
#include "../core/game.hpp"
#include "../engine/analyzer.hpp"

namespace wechess {

struct ExplanationEntry {
    std::string state = "none";  // none | pending | ready | failed
    std::string text;
    std::string error;
};

class GameSession {
   public:
    GameSession(std::string id, ParsedGame game, std::string pgn);
    ~GameSession();

    GameSession(const GameSession&) = delete;
    GameSession& operator=(const GameSession&) = delete;

    const std::string& id() const { return id_; }
    const ParsedGame& game() const { return game_; }
    const std::string& pgn() const { return pgn_; }
    long long created() const { return created_; }

    bool hasReview() const;
    GameReview review() const;

    bool analysisRunning() const { return running_.load(); }
    bool analysisDone() const { return done_.load(); }
    std::string analysisError() const;
    int progressPercent() const { return progress_.percent(); }
    std::string progressStatus() const { return progress_.status(); }

    // Starts the background analysis. Returns false with `error` set when an
    // analysis is already running.
    bool startAnalysis(const AnalysisConfig& config, EvalCache* cache, std::string& error);
    void cancelAnalysis();

    ExplanationEntry explanation(int ply) const;
    void setExplanationPending(int ply);
    void setExplanationReady(int ply, const std::string& text);
    void setExplanationFailed(int ply, const std::string& error);
    bool explanationPending(int ply) const;

   private:
    void workerThread(AnalysisConfig config, EvalCache* cache);
    void joinWorker();

    std::string id_;
    ParsedGame game_;
    std::string pgn_;
    long long created_ = 0;

    mutable std::mutex mutex_;
    GameReview review_;
    bool hasReview_ = false;
    std::string analysisError_;
    std::unordered_map<int, ExplanationEntry> explanations_;

    std::atomic<bool> cancel_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> done_{false};
    AnalysisProgress progress_;

    std::thread worker_;
};

}  // namespace wechess

#endif  // WECHESS_SERVER_SESSION_HPP
