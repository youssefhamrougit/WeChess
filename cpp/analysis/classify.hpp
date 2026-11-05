// WeChess - move classification from Stockfish evaluations.
//
// The method is documented in docs/CLASSIFICATION.md. In short: we compare the
// engine's win probability for the mover before and after the move (not raw
// centipawns alone), then refine with opening-book knowledge, "only move"
// detection and a static-exchange sacrifice test. Brilliant is deliberately
// hard to earn.
#ifndef WECHESS_ANALYSIS_CLASSIFY_HPP
#define WECHESS_ANALYSIS_CLASSIFY_HPP

#include <string>

#include "types.hpp"

namespace wechess {

struct ClassifyInput {
    bool moverWhite = true;
    PositionEval before;
    PositionEval after;
    std::string playedUci;
    bool isBook = false;
    std::string bookEco, bookName;
    // Material (centipawns) the mover genuinely leaves en prise, from a static
    // exchange evaluation on the destination square after the move.
    int sacrificeCp = 0;
};

struct ClassifyResult {
    Classification cls = Classification::Best;
    int cpLoss = 0;
    double winLoss = 0.0;
    bool forced = false;
    bool missedMate = false;
    bool missedWin = false;
    std::string summary;
};

// Classification thresholds live here so the README and tests can refer to one
// source of truth.
namespace thresholds {
// Win-probability loss (0..1) ceilings.
inline constexpr double kExcellent = 0.030;
inline constexpr double kGood      = 0.060;
inline constexpr double kInaccuracy = 0.120;
inline constexpr double kMistake   = 0.220;

// A move within this many centipawns of the engine's best move still counts as
// "Best" even if the UCI strings differ (e.g. transpositions).
inline constexpr int kBestCpSlack = 10;

// Brilliant requirements.
inline constexpr int kBrilliantSacrificeCp = 200;  // a real piece, not a pawn
inline constexpr double kBrilliantMinWinProb = 0.55;
inline constexpr double kBrilliantMaxWinLoss = 0.02;
inline constexpr double kBrilliantMaxWinBefore = 0.90;
inline constexpr int kBrilliantMinDepth = 12;

// Great requirements.
inline constexpr int kGreatOnlyMoveGapCp = 120;
inline constexpr int kGreatMinDepth = 10;
}  // namespace thresholds

ClassifyResult classifyMove(const ClassifyInput& input);

}  // namespace wechess

#endif  // WECHESS_ANALYSIS_CLASSIFY_HPP
