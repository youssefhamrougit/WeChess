// WeChess - engine evaluation value type.
//
// A Score represents a Stockfish evaluation from White's point of view. It is
// the single canonical representation used everywhere in the backend so that
// sign conventions never have to be re-derived at call sites.
#ifndef WECHESS_CORE_EVAL_HPP
#define WECHESS_CORE_EVAL_HPP

#include <cmath>
#include <cstdlib>
#include <string>

namespace wechess {

// Centipawn value used when a side is considered "winning" without a forced mate.
inline constexpr int kMaxEvalCp = 1500;
// Artificial mate distance used to keep win-probability arithmetic finite.
inline constexpr int kMateDistanceCap = 12;

// `mate` is signed from White's point of view: positive means White delivers
// mate in `mate` plies, negative means Black does. When `mate` is 0 the position
// has no forced mate in the analysed line and `cp` is authoritative.
struct Score {
    int cp = 0;
    int mate = 0;

    static Score fromCp(int whiteCp) {
        Score s;
        s.cp = whiteCp;
        return s;
    }
    static Score fromMate(int whiteMatePlies) {
        Score s;
        s.mate = whiteMatePlies;
        return s;
    }

    bool hasMate() const { return mate != 0; }
    bool isMateForWhite() const { return mate > 0; }

    // Score from the point of view of `whiteToMove`.
    Score operator-() const {
        Score s;
        s.cp   = -cp;
        s.mate = -mate;
        return s;
    }

    Score forColor(bool white) const { return white ? *this : -*this; }

    // Clamped centipawn value; mate scores collapse to the clamp limits so the
    // rest of the pipeline can treat everything as a number.
    int cpClamped() const {
        if (hasMate()) return mate > 0 ? kMaxEvalCp : -kMaxEvalCp;
        if (cp > kMaxEvalCp) return kMaxEvalCp;
        if (cp < -kMaxEvalCp) return -kMaxEvalCp;
        return cp;
    }
};

// Probability (0..1) that the side to move wins, from a White-relative score.
// Uses the logistic mapping popularised by Lichess (k = 0.00368208), which is a
// smooth, monotonic and widely used calibration. Mate scores are pinned just
// inside 0/1 so that a missed mate always registers as a large swing.
inline double winProbabilityWhite(const Score& s) {
    if (s.hasMate()) {
        // A mate for White is a win; being mated is a loss. Deeper mates are
        // marginally less certain in practical play, mirroring common practice.
        double certainty = 0.995 - 0.004 * std::min(std::abs(s.mate), kMateDistanceCap);
        return s.mate > 0 ? certainty : 1.0 - certainty;
    }
    double cp = static_cast<double>(s.cpClamped());
    double w  = 1.0 / (1.0 + std::exp(-0.00368208 * cp));
    return w;
}

// Probability (0..1) that the side that is about to move wins.
inline double winProbabilitySideToMove(const Score& whitePov, bool whiteToMove) {
    double w = winProbabilityWhite(whitePov);
    return whiteToMove ? w : 1.0 - w;
}

// ---- Display helpers -------------------------------------------------------
// "+1.35", "-0.42", "M5", "-M3", "0.00"
std::string formatScore(const Score& s);
// UCI-style token: "cp 35" or "mate 5"; used for cache files and engine dumps.
std::string scoreToUci(const Score& s);
// Signed pawns value from White's point of view, e.g. "+1.35".
std::string formatPawns(int whiteCp);

}  // namespace wechess

#endif  // WECHESS_CORE_EVAL_HPP
