// WeChess - accuracy and per-player statistics.
#ifndef WECHESS_ANALYSIS_STATS_HPP
#define WECHESS_ANALYSIS_STATS_HPP

#include <string>
#include <vector>

#include "types.hpp"

namespace wechess {

// Converts a win-probability loss (0..1) into a move accuracy percentage.
// Formula and rationale are documented in docs/CLASSIFICATION.md:
//   accuracy = 103.1668 * exp(-0.04354 * lossPercentPoints) - 3.1669
// clamped to [0, 100]. This is a published approximation of the idea behind
// commercial accuracy scores; it is not the proprietary Chess.com algorithm.
double moveAccuracyFromWinLoss(double winLoss);

// Aggregates the reviews for one colour. `game` supplies move numbers only.
PlayerStats computePlayerStats(const std::vector<MoveReview>& moves, bool white,
                               const std::string& playerName);

// Phase of a position, from material and opening knowledge rather than a fixed
// move number.
Phase phaseForPosition(const std::string& fen, int ply, int openingPlies);

// Builds the highlight list (best move, biggest mistake/blunder, turning point,
// critical moments) from the classified game.
void buildHighlights(GameReview& review);

}  // namespace wechess

#endif  // WECHESS_ANALYSIS_STATS_HPP
