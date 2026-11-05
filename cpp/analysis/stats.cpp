#include "stats.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

#include "../core/game.hpp"

namespace wechess {
namespace {

int tier(Classification c) {
    switch (c) {
        case Classification::Brilliant: return 5;
        case Classification::Great: return 4;
        case Classification::Best: return 3;
        case Classification::Excellent: return 2;
        case Classification::Good: return 1;
        default: return 0;
    }
}

// Weight moves by how much the position still mattered: a 99%-decided position
// should not dominate the accuracy score of a hard-fought game.
double relevanceWeight(const MoveReview& move) {
    // A move in a dead-equal position is harder than one in a decided game.
    const double wp = winProbabilitySideToMove(move.before, move.ply % 2 == 1);
    return std::clamp(4.0 * wp * (1.0 - wp), 0.30, 1.0);
}

std::string formatAccuracy(double value) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%.1f", value);
    return buffer;
}

std::string moveRef(const MoveReview& move) {
    char buffer[32];
    const int number = (move.ply + 1) / 2;
    std::snprintf(buffer, sizeof(buffer), "%d%s %s", number, (move.ply % 2 == 1) ? "." : "...",
                  move.playedSan.c_str());
    return buffer;
}

}  // namespace

double moveAccuracyFromWinLoss(double winLoss) {
    const double lossPoints = std::clamp(winLoss, 0.0, 1.0) * 100.0;
    const double accuracy = 103.1668 * std::exp(-0.04354 * lossPoints) - 3.1669;
    return std::clamp(accuracy, 0.0, 100.0);
}

Phase phaseForPosition(const std::string& fen, int ply, int openingPlies) {
    const int nonPawn =
        nonPawnMaterialForSide(fen, true) + nonPawnMaterialForSide(fen, false);

    // Endgame first: material is the honest signal, not the move number.
    if (nonPawn <= 1400) return Phase::Endgame;

    const bool queensOff = [&]() {
        const BoardPlacement placement = parseFenPlacement(fen);
        for (const std::string& square : placement.squares) {
            if (!square.empty() && square[1] == 'Q') return false;
        }
        return true;
    }();
    if (queensOff && nonPawn <= 2000) return Phase::Endgame;

    const int openingFloor = std::max(openingPlies, 6);
    if (ply <= openingFloor + 4 && ply <= 24) return Phase::Opening;
    if (nonPawn >= 3000 && ply <= 12) return Phase::Opening;
    return Phase::Middlegame;
}

PlayerStats computePlayerStats(const std::vector<MoveReview>& moves, bool white,
                               const std::string& playerName) {
    PlayerStats stats;
    stats.white = white;
    stats.name = playerName;

    double weightedAccuracy = 0.0;
    double weightSum = 0.0;
    double cpLossSum = 0.0;
    int counted = 0;

    double phaseAccuracy[3] = {0.0, 0.0, 0.0};
    double phaseWeight[3] = {0.0, 0.0, 0.0};

    for (const MoveReview& move : moves) {
        if ((move.ply % 2 == 1) != white) continue;
        ++stats.moves;
        ++counted;
        cpLossSum += std::min(move.cpLoss, 300);

        switch (move.cls) {
            case Classification::Brilliant: ++stats.brilliant; break;
            case Classification::Great: ++stats.great; break;
            case Classification::Best: ++stats.best; break;
            case Classification::Excellent: ++stats.excellent; break;
            case Classification::Good: ++stats.good; break;
            case Classification::Book: ++stats.book; break;
            case Classification::Inaccuracy: ++stats.inaccuracy; break;
            case Classification::Mistake: ++stats.mistake; break;
            case Classification::Blunder: ++stats.blunder; break;
        }
        if (move.forced) ++stats.forced;

        const double accuracy = moveAccuracyFromWinLoss(move.winLoss);
        const double weight = relevanceWeight(move);
        weightedAccuracy += accuracy * weight;
        weightSum += weight;

        const int phaseIndex = move.phase == Phase::Opening ? 0 : (move.phase == Phase::Middlegame ? 1 : 2);
        phaseAccuracy[phaseIndex] += accuracy * weight;
        phaseWeight[phaseIndex] += weight;
    }

    stats.accuracy = weightSum > 0.0 ? weightedAccuracy / weightSum : 0.0;
    stats.averageCentipawnLoss = counted > 0 ? cpLossSum / counted : 0.0;
    stats.openingAccuracy = phaseWeight[0] > 0.0 ? phaseAccuracy[0] / phaseWeight[0] : 0.0;
    stats.middlegameAccuracy = phaseWeight[1] > 0.0 ? phaseAccuracy[1] / phaseWeight[1] : 0.0;
    stats.endgameAccuracy = phaseWeight[2] > 0.0 ? phaseAccuracy[2] / phaseWeight[2] : 0.0;
    return stats;
}

void buildHighlights(GameReview& review) {
    // Appends to whatever the caller already collected (e.g. the opening note).
    if (review.moves.empty()) return;

    const MoveReview* bestMove = nullptr;
    const MoveReview* biggestMistake = nullptr;
    const MoveReview* biggestBlunder = nullptr;
    const MoveReview* turningPoint = nullptr;

    double bestTurningSwing = 0.0;

    for (const MoveReview& move : review.moves) {
        if (tier(move.cls) >= 3) {
            if (bestMove == nullptr || tier(move.cls) > tier(bestMove->cls) ||
                (tier(move.cls) == tier(bestMove->cls) && move.sacrificeCp > bestMove->sacrificeCp)) {
                bestMove = &move;
            }
        }
        if (move.cls == Classification::Mistake) {
            if (biggestMistake == nullptr || move.winLoss > biggestMistake->winLoss) biggestMistake = &move;
        }
        if (move.cls == Classification::Blunder) {
            if (biggestBlunder == nullptr || move.winLoss > biggestBlunder->winLoss) biggestBlunder = &move;
        }

        // Turning point: the move that moved White's expected score the most.
        const double before = winProbabilityWhite(move.before);
        const double after = winProbabilityWhite(move.after);
        const double swing = std::fabs(after - before);
        if (swing > bestTurningSwing) {
            bestTurningSwing = swing;
            turningPoint = &move;
        }
    }

    if (bestMove != nullptr) {
        Highlight h;
        h.ply = bestMove->ply;
        h.kind = "best";
        h.title = "Best move of the game";
        h.detail = moveRef(*bestMove) + " — " + classificationLabel(bestMove->cls) + ". " +
                   bestMove->summary;
        review.highlights.push_back(h);
    }
    if (turningPoint != nullptr && turningPoint->ply > 0) {
        Highlight h;
        h.ply = turningPoint->ply;
        h.kind = "turning-point";
        h.title = "Turning point";
        h.detail = moveRef(*turningPoint) + " (" + classificationLabel(turningPoint->cls) +
                   ") changed the result expectation by " +
                   formatAccuracy(turningPoint->winLoss * 100.0) + " percentage points.";
        review.highlights.push_back(h);
    }
    if (biggestMistake != nullptr) {
        Highlight h;
        h.ply = biggestMistake->ply;
        h.kind = "mistake";
        h.title = "Biggest mistake";
        h.detail = moveRef(*biggestMistake) + " — " + biggestMistake->summary;
        review.highlights.push_back(h);
    }
    if (biggestBlunder != nullptr) {
        Highlight h;
        h.ply = biggestBlunder->ply;
        h.kind = "blunder";
        h.title = "Biggest blunder";
        h.detail = moveRef(*biggestBlunder) + " — " + biggestBlunder->summary;
        review.highlights.push_back(h);
    }

    // Critical moments: the three largest win-probability drops in the game.
    std::vector<const MoveReview*> sorted;
    sorted.reserve(review.moves.size());
    for (const MoveReview& move : review.moves) sorted.push_back(&move);
    std::sort(sorted.begin(), sorted.end(), [](const MoveReview* a, const MoveReview* b) {
        return a->winLoss > b->winLoss;
    });
    int added = 0;
    for (const MoveReview* move : sorted) {
        if (added >= 3) break;
        if (move->winLoss < 0.05) break;
        Highlight h;
        h.ply = move->ply;
        h.kind = "critical";
        h.title = std::string("Critical moment: ") + classificationLabel(move->cls);
        h.detail = moveRef(*move) + " — " + move->summary;
        review.highlights.push_back(h);
        ++added;
    }
}

}  // namespace wechess
