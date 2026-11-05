#include "analyzer.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "chess.hpp"

#include "../core/eval.hpp"

namespace wechess {
namespace {

std::string join(const std::vector<std::string>& values) {
    std::string out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) out.push_back(' ');
        out += values[i];
    }
    return out;
}

std::vector<std::string> split(const std::string& value, char separator) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : value) {
        if (c == separator) {
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    parts.push_back(current);
    return parts;
}

// A position where the game is already over needs no search. Stockfish prints
// "bestmove (none)" and no PV there, so we synthesise the terminal score.
bool terminalEvaluation(const std::string& fen, PositionEval& out) {
    chess::Board board;
    if (!board.setFen(fen)) return false;
    const auto state = board.isGameOver();
    if (state.first == chess::GameResultReason::NONE) return false;

    Score score;
    if (state.first == chess::GameResultReason::CHECKMATE) {
        // The side to move has been mated.
        score = Score::fromMate(board.sideToMove() == chess::Color::WHITE ? -1 : 1);
    } else {
        score = Score::fromCp(0);  // stalemate, repetition, 50-move, material
    }

    out = PositionEval{};
    out.ok = true;
    out.fen = fen;
    out.depth = 0;
    EngineLine line;
    line.multipv = 1;
    line.depth = 0;
    line.score = score;
    out.lines.push_back(line);
    return true;
}

// Positions worth a second, deeper look: big evaluation swings, forced mates,
// and anything where the shallow pass disagrees sharply with the alternative.
bool isCriticalPosition(const PositionEval& eval) {
    if (!eval.ok) return false;
    if (eval.score().hasMate()) return true;
    if (std::abs(eval.score().cpClamped()) > 800) return true;
    if (eval.lines.size() > 1) {
        const int gap = std::abs(eval.lines[0].score.cpClamped() - eval.lines[1].score.cpClamped());
        if (gap >= 250) return true;
    }
    return false;
}

Score parseCachedScore(const std::string& token) {
    if (token.rfind("mate", 0) == 0) {
        try {
            return Score::fromMate(std::stoi(token.substr(4)));
        } catch (...) {
            return Score{};
        }
    }
    if (token.rfind("cp", 0) == 0) {
        try {
            return Score::fromCp(std::stoi(token.substr(2)));
        } catch (...) {
            return Score{};
        }
    }
    return Score{};
}

std::string encodeCachedScore(const Score& score) {
    return score.hasMate() ? "mate" + std::to_string(score.mate) : "cp" + std::to_string(score.cp);
}

}  // namespace

// ---- EvalCache -------------------------------------------------------------

void EvalCache::open(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    path_ = path;
    entries_.clear();
    pending_.clear();
    loaded_ = true;

    std::ifstream input(path_);
    if (!input) return;
    std::string line;
    while (std::getline(input, line)) {
        PositionEval eval;
        if (!decode(line, eval)) continue;
        entries_[eval.fen] = std::move(eval);
    }
}

std::size_t EvalCache::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
}

bool EvalCache::lookup(const std::string& fen, int minDepth, PositionEval& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = entries_.find(fen);
    if (it == entries_.end()) return false;
    if (it->second.depth < minDepth) return false;
    out = it->second;
    out.fromCache = true;
    return true;
}

void EvalCache::store(const PositionEval& eval) {
    if (!eval.ok) return;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = entries_.find(eval.fen);
    if (it != entries_.end() && it->second.depth >= eval.depth) return;
    entries_[eval.fen] = eval;
    pending_.push_back(eval);
}

std::string EvalCache::encode(const PositionEval& eval) {
    // fen|depth|score|pv|score|pv...
    std::string line = eval.fen;
    line += '|';
    line += std::to_string(eval.depth);
    for (const EngineLine& engineLine : eval.lines) {
        line += '|';
        line += encodeCachedScore(engineLine.score);
        line += '|';
        line += join(engineLine.pvUci);
    }
    return line;
}

bool EvalCache::decode(const std::string& line, PositionEval& out) {
    const std::vector<std::string> parts = split(line, '|');
    if (parts.size() < 2) return false;
    PositionEval eval;
    eval.fen = parts[0];
    if (eval.fen.empty() || eval.fen.find('/') == std::string::npos) return false;
    try {
        eval.depth = std::stoi(parts[1]);
    } catch (...) {
        return false;
    }
    for (std::size_t i = 2; i + 1 < parts.size(); i += 2) {
        EngineLine engineLine;
        engineLine.multipv = static_cast<int>(eval.lines.size()) + 1;
        engineLine.depth = eval.depth;
        engineLine.score = parseCachedScore(parts[i]);
        if (!parts[i + 1].empty()) {
            std::istringstream stream(parts[i + 1]);
            std::string move;
            while (stream >> move) engineLine.pvUci.push_back(move);
        }
        eval.lines.push_back(std::move(engineLine));
    }
    if (eval.lines.empty()) return false;
    eval.ok = true;
    eval.fromCache = true;
    out = std::move(eval);
    return true;
}

void EvalCache::flush() {
    std::vector<PositionEval> toWrite;
    std::string path;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pending_.empty() || path_.empty()) return;
        toWrite.swap(pending_);
        path = path_;
    }
    std::ofstream output(path, std::ios::app);
    if (!output) return;
    for (const PositionEval& eval : toWrite) {
        output << encode(eval) << '\n';
    }
}

// ---- analysis runner -------------------------------------------------------

AnalysisOutput runAnalysis(const ParsedGame& game, const AnalysisConfig& config, EvalCache* cache,
                           AnalysisProgress& progress, const std::atomic<bool>& cancel) {
    AnalysisOutput output;
    const int positions = static_cast<int>(game.plies.size()) + 1;
    output.evals.resize(static_cast<std::size_t>(positions));

    progress.running.store(true);
    progress.finished.store(false);
    progress.failed.store(false);
    progress.completed.store(0);
    progress.total.store(positions);
    progress.pass.store(1);
    progress.setStatus("Starting Stockfish");

    UciEngine engine;
    if (!engine.start(config.stockfishPath, config.engine)) {
        output.error = engine.lastError();
        progress.failed.store(true);
        progress.running.store(false);
        progress.finished.store(true);
        progress.setStatus("Stockfish is unavailable");
        return output;
    }
    output.engineAvailable = true;
    output.engineName = engine.name();

    const int depth = std::max(1, config.engine.depth);
    const int multipv = std::max(1, config.engine.multipv);

    std::vector<int> needsDeep;
    needsDeep.reserve(static_cast<std::size_t>(positions));

    for (int i = 0; i < positions; ++i) {
        if (cancel.load()) {
            output.error = "cancelled";
            progress.cancelled.store(true);
            progress.running.store(false);
            progress.finished.store(true);
            engine.stop();
            if (cache) cache->flush();
            return output;
        }
        const std::string fen = game.fenAtPly(i);
        PositionEval eval;
        if (terminalEvaluation(fen, eval)) {
            // Nothing to search; counts as analysed without spending engine time.
        } else if (cache && cache->lookup(fen, depth, eval)) {
            ++output.cached;
        } else {
            progress.setStatus("Analysing position " + std::to_string(i + 1) + " of " +
                               std::to_string(positions));
            const EngineResult result = engine.analyse(fen, depth, multipv, cancel);
            if (!result.ok) {
                if (result.error == "cancelled" || cancel.load()) {
                    output.error = "cancelled";
                    progress.cancelled.store(true);
                    progress.running.store(false);
                    progress.finished.store(true);
                    engine.stop();
                    if (cache) cache->flush();
                    return output;
                }
                eval.fen = fen;
                eval.error = result.error;
            } else {
                eval.ok = true;
                eval.fen = fen;
                eval.depth = result.depth;
                eval.nodes = result.nodes;
                eval.timeMs = result.timeMs;
                eval.lines = result.lines;
                ++output.analysed;
                if (cache) cache->store(eval);
            }
        }
        if (eval.ok && isCriticalPosition(eval)) {
            needsDeep.push_back(i);
        }
        output.evals[static_cast<std::size_t>(i)] = std::move(eval);
        progress.completed.fetch_add(1);
    }

    if (config.deepPass && !needsDeep.empty() && !cancel.load()) {
        progress.pass.store(2);
        progress.total.fetch_add(static_cast<int>(needsDeep.size()));
        const int deepDepth = std::max(depth + 1, config.criticalDepth);
        progress.setStatus("Deepening " + std::to_string(needsDeep.size()) + " critical positions");
        for (int index : needsDeep) {
            if (cancel.load()) break;
            PositionEval& eval = output.evals[static_cast<std::size_t>(index)];
            if (eval.depth >= deepDepth || eval.lines.empty() || eval.lines[0].pvUci.empty()) {
                progress.completed.fetch_add(1);
                continue;
            }
            const EngineResult result = engine.analyse(eval.fen, deepDepth, multipv, cancel);
            if (result.ok) {
                eval.ok = true;
                eval.depth = result.depth;
                eval.nodes = result.nodes;
                eval.timeMs = result.timeMs;
                eval.lines = result.lines;
                ++output.analysed;
                if (cache) cache->store(eval);
            }
            progress.completed.fetch_add(1);
        }
    }

    engine.stop();
    if (cache) cache->flush();

    if (cancel.load()) {
        output.error = "cancelled";
        progress.cancelled.store(true);
        progress.running.store(false);
        progress.finished.store(true);
        return output;
    }

    output.ok = true;
    progress.running.store(false);
    progress.finished.store(true);
    progress.setStatus("Analysis complete");
    return output;
}

}  // namespace wechess
