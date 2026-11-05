// WeChess - turns engine evaluations into a complete game review.
#ifndef WECHESS_ANALYSIS_REVIEW_HPP
#define WECHESS_ANALYSIS_REVIEW_HPP

#include <string>
#include <vector>

#include "../core/game.hpp"
#include "types.hpp"

namespace wechess {

struct ReviewOptions {
    bool useOpeningBook = true;
    std::string engineName = "Stockfish";
    bool engineUsed = true;
    bool engineAvailable = true;
    int engineDepth = 0;
};

// `evals[i]` must contain the evaluation of the position after i half-moves,
// so the vector has plies.size() + 1 entries.
GameReview buildReview(const ParsedGame& game, const std::vector<PositionEval>& evals,
                       const ReviewOptions& options);

// Static exchange evaluation helper exposed for tests: material (centipawns) the
// side to move can win by capturing on `targetSquare` in `fen`.
int staticExchangeGain(const std::string& fen, const std::string& targetSquare);

}  // namespace wechess

#endif  // WECHESS_ANALYSIS_REVIEW_HPP
