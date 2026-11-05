#include "session.hpp"

#include <chrono>

namespace wechess {

GameSession::GameSession(std::string id, ParsedGame game, std::string pgn)
    : id_(std::move(id)), game_(std::move(game)), pgn_(std::move(pgn)) {
    created_ = std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();
}

GameSession::~GameSession() {
    cancel_.store(true);
    joinWorker();
}

void GameSession::joinWorker() {
    if (worker_.joinable()) worker_.join();
}

bool GameSession::hasReview() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return hasReview_;
}

GameReview GameSession::review() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return review_;
}

std::string GameSession::analysisError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return analysisError_;
}

bool GameSession::startAnalysis(const AnalysisConfig& config, EvalCache* cache, std::string& error) {
    if (running_.load()) {
        error = "This game is already being analysed.";
        return false;
    }
    joinWorker();
    cancel_.store(false);
    running_.store(true);
    done_.store(false);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        analysisError_.clear();
    }
    worker_ = std::thread([this, config, cache] { workerThread(config, cache); });
    return true;
}

void GameSession::cancelAnalysis() {
    cancel_.store(true);
    progress_.setStatus("Cancelling");
}

void GameSession::workerThread(AnalysisConfig config, EvalCache* cache) {
    AnalysisOutput output;
    try {
        output = runAnalysis(game_, config, cache, progress_, cancel_);
    } catch (const std::exception& ex) {
        output.ok = false;
        output.error = std::string("Analysis crashed: ") + ex.what();
    } catch (...) {
        output.ok = false;
        output.error = "Analysis crashed unexpectedly.";
    }

    if (output.ok) {
        ReviewOptions options;
        options.engineName = output.engineName.empty() ? "Stockfish" : output.engineName;
        options.engineUsed = true;
        options.engineAvailable = true;
        options.engineDepth = config.engine.depth;
        GameReview review;
        try {
            review = buildReview(game_, output.evals, options);
        } catch (const std::exception& ex) {
            output.ok = false;
            output.error = std::string("Could not build the review: ") + ex.what();
        }
        if (output.ok) {
            std::lock_guard<std::mutex> lock(mutex_);
            review_ = std::move(review);
            hasReview_ = true;
        }
    }

    if (!output.ok) {
        std::lock_guard<std::mutex> lock(mutex_);
        analysisError_ = output.error.empty() ? "Analysis failed." : output.error;
        if (analysisError_ == "cancelled") analysisError_ = "Analysis was cancelled.";
    }

    running_.store(false);
    done_.store(true);
}

ExplanationEntry GameSession::explanation(int ply) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = explanations_.find(ply);
    return it == explanations_.end() ? ExplanationEntry{} : it->second;
}

void GameSession::setExplanationPending(int ply) {
    std::lock_guard<std::mutex> lock(mutex_);
    ExplanationEntry entry;
    entry.state = "pending";
    explanations_[ply] = entry;
}

void GameSession::setExplanationReady(int ply, const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex_);
    ExplanationEntry entry;
    entry.state = "ready";
    entry.text = text;
    explanations_[ply] = entry;
}

void GameSession::setExplanationFailed(int ply, const std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    ExplanationEntry entry;
    entry.state = "failed";
    entry.error = error;
    explanations_[ply] = entry;
}

bool GameSession::explanationPending(int ply) const {
    return explanation(ply).state == "pending";
}

}  // namespace wechess
