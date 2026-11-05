#include "eval.hpp"

#include <cstdio>

namespace wechess {
namespace {

std::string pawnString(double pawns) {
    char buf[32];
    // One decimal is what a chess player reads at a glance; two for small edges.
    if (std::fabs(pawns) < 10.0) {
        std::snprintf(buf, sizeof(buf), "%+.2f", pawns);
    } else {
        std::snprintf(buf, sizeof(buf), "%+.1f", pawns);
    }
    return std::string(buf);
}

}  // namespace

std::string formatScore(const Score& s) {
    if (s.hasMate()) {
        int plies = std::abs(s.mate);
        int moves = (plies + 1) / 2;  // plies -> full moves
        std::string out = "M" + std::to_string(moves);
        return s.mate > 0 ? out : "-" + out;
    }
    if (s.cp == 0) return "0.00";
    return pawnString(s.cp / 100.0);
}

std::string scoreToUci(const Score& s) {
    if (s.hasMate()) return "mate " + std::to_string(s.mate);
    return "cp " + std::to_string(s.cp);
}

std::string formatPawns(int whiteCp) {
    return pawnString(whiteCp / 100.0);
}

}  // namespace wechess
