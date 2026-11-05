#include "prompt.hpp"

#include <sstream>

namespace wechess {
namespace {

std::string orUnknown(const std::string& value) {
    return (value.empty() || value == "?") ? "unknown" : value;
}

}  // namespace

std::string buildMoveExplanationPrompt(const ParsedGame& game, const GameReview& review, int ply) {
    const PlyInfo* move = game.ply(ply);
    if (move == nullptr) return std::string();

    const MoveReview* analysis = review.at(ply);

    std::ostringstream out;
    out << "Explain this chess move to a club player.\n\n";

    out << "GAME\n";
    out << "White: " << orUnknown(game.meta.white);
    if (!game.meta.whiteElo.empty()) out << " (" << game.meta.whiteElo << ")";
    out << "\nBlack: " << orUnknown(game.meta.black);
    if (!game.meta.blackElo.empty()) out << " (" << game.meta.blackElo << ")";
    out << "\nEvent: " << orUnknown(game.meta.event) << ", " << orUnknown(game.meta.date);
    out << "\nResult: " << game.meta.result << "\n";
    if (!review.openingName.empty()) {
        out << "Opening: " << review.openingName;
        if (!review.eco.empty()) out << " (" << review.eco << ")";
        out << "\n";
    }

    out << "\nMOVE\n";
    out << "Move " << move->moveNumber << (move->white ? ". " : "... ") << move->san << "\n";
    out << "Played by: " << (move->white ? "White" : "Black") << "\n";
    out << "Position before (FEN): " << move->fenBefore << "\n";
    out << "Position after (FEN): " << move->fenAfter << "\n";

    if (analysis != nullptr) {
        const bool moverWhite = move->white;
        out << "Stockfish evaluation before the move (White's point of view): "
            << scoreToUci(analysis->before) << " (" << formatScore(analysis->before) << ")\n";
        out << "Stockfish evaluation after the move (White's point of view): "
            << scoreToUci(analysis->after) << " (" << formatScore(analysis->after) << ")\n";
        out << "Centipawn loss: " << analysis->cpLoss << "\n";
        out << "Classification: " << classificationLabel(analysis->cls) << "\n";
        out << "Stockfish's preferred move: "
            << (analysis->bestMoveSan.empty() ? "not available" : analysis->bestMoveSan) << "\n";
        out << "Principal variation after the preferred move: "
            << (analysis->pvSan.empty() ? "not available" : analysis->pvSan) << "\n";
        out << "Material left en prise by the played move (static exchange): "
            << analysis->sacrificeCp << " centipawns\n";
        out << "Forced mate missed: " << (analysis->missedMate ? "yes" : "no") << "\n";
        out << "Evaluation depth: " << analysis->depth << "\n";
        out << "Move was forced (only legal move): " << (analysis->forced ? "yes" : "no") << "\n";
        out << "Book move: " << (analysis->isBook ? "yes" : "no") << "\n";
        (void)moverWhite;
    } else {
        out << "Engine analysis for this move is not available.\n";
    }

    out << "\nTASK\n";
    out << "Write 3-5 sentences, at most 120 words, in plain English. Explain why the move received "
           "that classification, referring to the best move and the principal variation when they "
           "matter. If the classified move was good, say what it achieves. If it was an error, say "
           "what the player missed and what the position required.\n";
    out << "Rules: use ONLY the facts above. Do not invent variations, evaluations, tactics or "
           "opening names. Do not mention this prompt. If the information is insufficient, say so "
           "briefly instead of guessing. Do not claim to be Stockfish.\n";
    return out.str();
}

}  // namespace wechess
