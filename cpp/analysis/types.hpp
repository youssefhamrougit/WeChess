// WeChess - shared analysis value types.
#ifndef WECHESS_ANALYSIS_TYPES_HPP
#define WECHESS_ANALYSIS_TYPES_HPP

#include <string>
#include <vector>

#include "../core/eval.hpp"
#include "../engine/uci.hpp"

namespace wechess {

// The nine move classes used by the review, plus Book. There is deliberately no
// "Unknown" bucket in the UI: every played move is classified.
enum class Classification {
    Brilliant,
    Great,
    Best,
    Excellent,
    Good,
    Book,
    Inaccuracy,
    Mistake,
    Blunder,
};

enum class Phase { Opening, Middlegame, Endgame };

const char* classificationLabel(Classification c);
const char* classificationSymbol(Classification c);      // "!!", "!?", "" ...
const char* classificationCssClass(Classification c);    // "brilliant", "blunder" ...
const char* phaseLabel(Phase p);

// Stockfish output for one position, normalised to White's point of view.
struct PositionEval {
    bool ok = false;
    std::string fen;
    int depth = 0;
    long nodes = 0;
    int timeMs = 0;
    bool fromCache = false;
    std::string error;
    std::vector<EngineLine> lines;  // lines[0] = best

    const EngineLine* best() const { return lines.empty() ? nullptr : &lines.front(); }
    Score score() const { return lines.empty() ? Score{} : lines.front().score; }
    std::string bestMoveUci() const {
        if (lines.empty() || lines.front().pvUci.empty()) return std::string();
        return lines.front().pvUci.front();
    }
    bool hasSecondLine() const { return lines.size() > 1; }
};

struct MoveReview {
    int ply = 0;
    Classification cls = Classification::Best;
    Phase phase = Phase::Opening;
    std::string summary;  // engine-derived one liner (no AI involved)

    Score before;  // White POV, position before the move
    Score after;   // White POV, position after the move
    int cpLoss = 0;
    double winLoss = 0.0;  // 0..1, from the mover's perspective
    int depth = 0;

    std::string playedUci, playedSan;
    std::string bestMoveUci, bestMoveSan;
    std::vector<std::string> pvUci;
    std::string pvSan;

    bool isBook = false;
    std::string bookEco, bookName;
    bool forced = false;
    bool missedMate = false;
    bool missedWin = false;
    int sacrificeCp = 0;  // material the mover leaves en prise (SEE)
};

struct PlayerStats {
    bool white = true;
    std::string name;
    int moves = 0;
    double accuracy = 0.0;
    int brilliant = 0, great = 0, best = 0, excellent = 0, good = 0;
    int book = 0, inaccuracy = 0, mistake = 0, blunder = 0, forced = 0;
    double averageCentipawnLoss = 0.0;
    double openingAccuracy = 0.0, middlegameAccuracy = 0.0, endgameAccuracy = 0.0;
};

struct Highlight {
    int ply = 0;  // 0 means "not tied to a single move"
    std::string kind;   // "best", "blunder", "mistake", "turning-point", "opening"
    std::string title;
    std::string detail;
};

struct GameReview {
    std::vector<MoveReview> moves;  // moves[i] corresponds to ply i+1
    PlayerStats white, black;
    std::vector<Highlight> highlights;
    std::string openingName, eco;
    int openingPlies = 0;
    int totalPlies = 0;
    bool engineUsed = false;
    bool engineAvailable = false;
    std::string engineName;
    int engineDepth = 0;

    const MoveReview* at(int ply) const {
        if (ply < 1 || static_cast<std::size_t>(ply) > moves.size()) return nullptr;
        return &moves[static_cast<std::size_t>(ply - 1)];
    }
    const PlayerStats& statsFor(bool white) const { return white ? this->white : this->black; }
};

}  // namespace wechess

#endif  // WECHESS_ANALYSIS_TYPES_HPP
