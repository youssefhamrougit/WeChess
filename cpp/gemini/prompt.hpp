// WeChess - builds the structured prompt handed to Gemini.
//
// Everything Gemini is allowed to talk about comes from here: engine scores,
// the best move, the principal variation, the classification and the FENs. The
// model is explicitly told not to invent anything else.
#ifndef WECHESS_GEMINI_PROMPT_HPP
#define WECHESS_GEMINI_PROMPT_HPP

#include <string>

#include "../analysis/types.hpp"
#include "../core/game.hpp"

namespace wechess {

std::string buildMoveExplanationPrompt(const ParsedGame& game, const GameReview& review, int ply);

}  // namespace wechess

#endif  // WECHESS_GEMINI_PROMPT_HPP
