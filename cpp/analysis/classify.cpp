#include "classify.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace wechess {
namespace {

// Best score minus second-best score, from the mover's point of view.
int onlyMoveGapCp(const PositionEval& before, bool moverWhite) {
    if (before.lines.size() < 2) return 0;
    const int best = before.lines[0].score.forColor(moverWhite).cpClamped();
    const int second = before.lines[1].score.forColor(moverWhite).cpClamped();
    return best - second;
}

std::string reportScore(const Score& s, bool moverWhite) {
    return formatScore(s.forColor(moverWhite));
}

}  // namespace

const char* classificationLabel(Classification c) {
    switch (c) {
        case Classification::Brilliant: return "Brilliant";
        case Classification::Great: return "Great";
        case Classification::Best: return "Best";
        case Classification::Excellent: return "Excellent";
        case Classification::Good: return "Good";
        case Classification::Book: return "Book";
        case Classification::Inaccuracy: return "Inaccuracy";
        case Classification::Mistake: return "Mistake";
        case Classification::Blunder: return "Blunder";
    }
    return "Best";
}

const char* classificationSymbol(Classification c) {
    switch (c) {
        case Classification::Brilliant: return "!!";
        case Classification::Great: return "!";
        case Classification::Best: return "";
        case Classification::Excellent: return "";
        case Classification::Good: return "";
        case Classification::Book: return "";
        case Classification::Inaccuracy: return "?!";
        case Classification::Mistake: return "?";
        case Classification::Blunder: return "??";
    }
    return "";
}

const char* classificationCssClass(Classification c) {
    switch (c) {
        case Classification::Brilliant: return "brilliant";
        case Classification::Great: return "great";
        case Classification::Best: return "best";
        case Classification::Excellent: return "excellent";
        case Classification::Good: return "good";
        case Classification::Book: return "book";
        case Classification::Inaccuracy: return "inaccuracy";
        case Classification::Mistake: return "mistake";
        case Classification::Blunder: return "blunder";
    }
    return "good";
}

const char* phaseLabel(Phase p) {
    switch (p) {
        case Phase::Opening: return "Opening";
        case Phase::Middlegame: return "Middlegame";
        case Phase::Endgame: return "Endgame";
    }
    return "Middlegame";
}

ClassifyResult classifyMove(const ClassifyInput& input) {
    using namespace thresholds;

    ClassifyResult out;
    const bool white = input.moverWhite;
    const Score before = input.before.score();
    const Score after = input.after.score();

    const double winBefore = winProbabilitySideToMove(before, white);
    const double winAfter = winProbabilitySideToMove(after, white);
    out.winLoss = std::clamp(winBefore - winAfter, 0.0, 1.0);

    const int cpBefore = before.forColor(white).cpClamped();
    const int cpAfter = after.forColor(white).cpClamped();
    out.cpLoss = std::clamp(cpBefore - cpAfter, 0, 2000);

    const std::string bestUci = input.before.bestMoveUci();
    const bool playedEngineBest = !bestUci.empty() && input.playedUci == bestUci;
    const bool withinSlack = !input.playedUci.empty() && out.cpLoss <= kBestCpSlack;
    // Only treat "the engine's move" as best when the evaluations agree that the
    // move kept the evaluation. This protects against inconsistent cached data
    // (e.g. a shallow after-evaluation paired with a deep before-evaluation).
    const bool bestAndConsistent = playedEngineBest && out.winLoss <= kGood;

    // A forced move is not a decision, so it is never punished.
    const bool mateForMover = before.hasMate() && (before.mate > 0) == white;
    const bool mateAfterForMover = after.hasMate() && (after.mate > 0) == white;
    out.missedMate = mateForMover && !mateAfterForMover;
    out.missedWin = (winBefore >= 0.90 && winAfter < 0.60) || out.missedMate;

    // A move that ends the game is never "just a book move".
    const bool deliversMate = after.hasMate() && ((after.mate > 0) == white);
    // Opening databases contain trap lines (Fool's Mate is a named line), so a
    // book move is only book when the engine agrees it is not bad. This is what
    // stops theory from hiding a blunder.
    const bool isBookMove = input.isBook && !deliversMate && out.winLoss <= kInaccuracy && winAfter >= 0.25;

    // ---- Brilliant -----------------------------------------------------------
    // A sacrifice is not brilliant by itself: the engine must still consider the
    // move essentially best and the resulting position must still be good.
    const bool brilliant = !isBookMove && input.sacrificeCp >= kBrilliantSacrificeCp &&
                           (playedEngineBest || withinSlack) &&
                           out.winLoss <= kBrilliantMaxWinLoss &&
                           winAfter >= kBrilliantMinWinProb && winBefore < kBrilliantMaxWinBefore &&
                           input.before.depth >= kBrilliantMinDepth && !mateForMover;

    // ---- Great ---------------------------------------------------------------
    const int gap = onlyMoveGapCp(input.before, white);
    const bool onlyGoodMove = gap >= kGreatOnlyMoveGapCp && (playedEngineBest || withinSlack);
    const bool rescueMove = playedEngineBest && winBefore <= 0.40 && winAfter >= 0.50;
    const bool great = !brilliant && (onlyGoodMove || rescueMove) &&
                       (playedEngineBest || withinSlack) && out.winLoss <= kGood &&
                       input.before.depth >= kGreatMinDepth;

    if (isBookMove) {
        out.cls = Classification::Book;
    } else if (brilliant) {
        out.cls = Classification::Brilliant;
    } else if (great) {
        out.cls = Classification::Great;
    } else if (bestAndConsistent || (withinSlack && out.winLoss <= kExcellent)) {
        out.cls = Classification::Best;
    } else if (out.winLoss <= kExcellent) {
        out.cls = Classification::Excellent;
    } else if (out.winLoss <= kGood) {
        out.cls = Classification::Good;
    } else if (out.winLoss <= kInaccuracy) {
        out.cls = Classification::Inaccuracy;
    } else if (out.winLoss <= kMistake) {
        out.cls = Classification::Mistake;
    } else {
        out.cls = Classification::Blunder;
    }

    // ---- Human-readable, engine-derived summary ------------------------------
    char buffer[512];
    switch (out.cls) {
        case Classification::Book:
            if (!input.bookName.empty()) {
                const std::string opening =
                    input.bookEco.empty() ? input.bookName : input.bookName + " (" + input.bookEco + ")";
                out.summary = "Book move from the " + opening + ".";
            } else {
                out.summary = "Book move: this position is still inside a known opening line.";
            }
            break;
        case Classification::Brilliant:
            std::snprintf(buffer, sizeof(buffer),
                          "Brilliant: gives up %.2f pawns of material and still keeps a winning "
                          "evaluation (%s).",
                          input.sacrificeCp / 100.0, reportScore(after, white).c_str());
            out.summary = buffer;
            break;
        case Classification::Great:
            std::snprintf(buffer, sizeof(buffer),
                          "Great: the position demanded exactly this move. The next-best continuation "
                          "was %.2f pawns worse.",
                          gap / 100.0);
            out.summary = buffer;
            break;
        case Classification::Best:
            out.summary = "Best move: matches Stockfish's first choice.";
            break;
        case Classification::Excellent:
            std::snprintf(buffer, sizeof(buffer),
                          "Excellent: keeps the evaluation within %.2f pawns of the engine's choice.",
                          out.cpLoss / 100.0);
            out.summary = buffer;
            break;
        case Classification::Good:
            std::snprintf(buffer, sizeof(buffer), "Good: a sound move, %.2f pawns below the engine's "
                                                  "preference.",
                          out.cpLoss / 100.0);
            out.summary = buffer;
            break;
        case Classification::Inaccuracy:
            std::snprintf(buffer, sizeof(buffer),
                          "Inaccuracy: hands over %.2f pawns of evaluation and lets the opponent "
                          "equalise more easily.",
                          out.cpLoss / 100.0);
            out.summary = buffer;
            break;
        case Classification::Mistake:
            std::snprintf(buffer, sizeof(buffer), "Mistake: %.2f pawns of evaluation lost. Stockfish "
                                                  "had a clearly better continuation.",
                          out.cpLoss / 100.0);
            out.summary = buffer;
            break;
        case Classification::Blunder:
            std::snprintf(buffer, sizeof(buffer),
                          "Blunder: loses %.2f pawns of evaluation (%s before, %s after).",
                          out.cpLoss / 100.0, reportScore(before, white).c_str(),
                          reportScore(after, white).c_str());
            out.summary = buffer;
            break;
    }
    if (out.missedMate) {
        out.summary += " A forced mate was available before this move.";
    }
    return out;
}

}  // namespace wechess
