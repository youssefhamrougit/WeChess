#include "review.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "chess.hpp"
#include "../engine/uci.hpp"
#include "classify.hpp"
#include "opening.hpp"
#include "stats.hpp"

namespace wechess {
namespace {

char pieceLetter(chess::Piece piece) {
    const std::string text = static_cast<std::string>(piece.type());
    if (text.empty()) return '?';
    return static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
}

// Recursive static exchange evaluation. The board is restored after every line,
// and the recursion is bounded by the number of captures available.
int seeRecursive(chess::Board& board, chess::Square target, int depth) {
    if (depth >= 6) return 0;
    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);

    int best = 0;
    for (const chess::Move& move : moves) {
        if (move.to() != target) continue;
        int gain = 0;
        if (move.typeOf() == chess::Move::ENPASSANT) {
            gain = pieceValueCp('P');
        } else {
            const chess::Piece captured = board.at(target);
            if (captured == chess::Piece::NONE) continue;
            gain = pieceValueCp(pieceLetter(captured));
        }
        board.makeMove(move);
        gain -= seeRecursive(board, target, depth + 1);
        board.unmakeMove(move);
        best = std::max(best, gain);
    }
    return best;
}

int sacrificeOnSquare(const std::string& fen, const std::string& square) {
    if (square.size() < 2) return 0;
    chess::Board board;
    if (!board.setFen(fen)) return 0;
    chess::Square target;
    try {
        target = chess::Square(square);
    } catch (...) {
        return 0;
    }
    if (!target.is_valid()) return 0;
    const int gain = seeRecursive(board, target, 0);
    return std::max(0, gain);
}

std::string playerName(const std::string& value, bool white) {
    if (value.empty() || value == "?" || value == "-") return white ? "White" : "Black";
    return value;
}

}  // namespace

int staticExchangeGain(const std::string& fen, const std::string& targetSquare) {
    return sacrificeOnSquare(fen, targetSquare);
}

GameReview buildReview(const ParsedGame& game, const std::vector<PositionEval>& evals,
                       const ReviewOptions& options) {
    GameReview review;
    review.engineUsed = options.engineUsed;
    review.engineAvailable = options.engineAvailable;
    review.engineName = options.engineName;
    review.engineDepth = options.engineDepth;
    review.totalPlies = static_cast<int>(game.plies.size());
    review.white = computePlayerStats({}, true, playerName(game.meta.white, true));
    review.black = computePlayerStats({}, false, playerName(game.meta.black, false));

    const OpeningBook& book = OpeningBook::instance();
    std::vector<std::string> uciSoFar;
    uciSoFar.reserve(game.plies.size());

    // First pass: opening book membership (needed before phases are assigned).
    std::vector<bool> isBook(game.plies.size(), false);
    std::vector<OpeningInfo> bookAtPly(game.plies.size());
    for (std::size_t i = 0; i < game.plies.size(); ++i) {
        const PlyInfo& ply = game.plies[i];
        if (ply.isNullMove || ply.uci.empty()) {
            uciSoFar.push_back(ply.uci);
            continue;
        }
        if (options.useOpeningBook && book.isBookMove(uciSoFar, ply.uci)) {
            isBook[i] = true;
        }
        uciSoFar.push_back(ply.uci);
        if (options.useOpeningBook) {
            bookAtPly[i] = book.lookup(uciSoFar);
        }
    }

    int openingPlies = 0;
    while (openingPlies < static_cast<int>(game.plies.size()) && isBook[static_cast<std::size_t>(openingPlies)]) {
        ++openingPlies;
    }
    review.openingPlies = openingPlies;

    const OpeningInfo opening = options.useOpeningBook ? book.lookup(uciSoFar) : OpeningInfo{};
    if (!opening.name.empty()) {
        review.openingName = opening.name;
        review.eco = opening.eco;
    } else {
        // Never invent an opening: fall back to the PGN's own tags when present.
        review.openingName = game.meta.opening;
        review.eco = game.meta.eco;
    }

    // Second pass: classify every move.
    review.moves.reserve(game.plies.size());
    for (std::size_t i = 0; i < game.plies.size(); ++i) {
        const PlyInfo& ply = game.plies[i];
        MoveReview move;
        move.ply = ply.ply;
        move.playedSan = ply.isNullMove ? std::string("--") : ply.san;
        move.playedUci = ply.uci;
        move.isBook = isBook[i];
        move.bookEco = bookAtPly[i].eco;
        move.bookName = bookAtPly[i].name;
        move.forced = ply.legalMoveCount == 1;
        move.phase = phaseForPosition(ply.fenBefore, ply.ply, openingPlies);

        const PositionEval& before = evals.empty() ? PositionEval{}
                                                   : evals[static_cast<std::size_t>(ply.ply - 1)];
        const PositionEval& after = evals.empty() ? PositionEval{}
                                                  : evals[static_cast<std::size_t>(ply.ply)];
        move.before = before.score();
        move.after = after.score();
        move.depth = before.depth;

        if (before.best() && !before.lines[0].pvUci.empty()) {
            move.bestMoveUci = before.lines[0].pvUci.front();
            move.pvUci = before.lines[0].pvUci;
            const std::vector<std::string> bestSan = uciLineToSan(ply.fenBefore, {move.bestMoveUci});
            if (!bestSan.empty()) move.bestMoveSan = bestSan.front();
            move.pvSan = formatPv(ply.fenBefore, move.pvUci);
        }

        // Detect material genuinely left en prise by the played move.
        move.sacrificeCp = ply.isNullMove ? 0 : sacrificeOnSquare(ply.fenAfter, ply.toSquare);

        ClassifyInput input;
        input.moverWhite = ply.white;
        input.before = before;
        input.after = after;
        input.playedUci = ply.uci;
        input.isBook = isBook[i];
        input.bookEco = move.bookEco;
        input.bookName = move.bookName;
        input.sacrificeCp = move.sacrificeCp;

        const ClassifyResult result = classifyMove(input);
        move.cls = result.cls;
        move.cpLoss = result.cpLoss;
        move.winLoss = result.winLoss;
        move.forced = result.forced || move.forced;
        move.missedMate = result.missedMate;
        move.missedWin = result.missedWin;
        move.summary = result.summary;

        if (move.forced) {
            move.summary = "Only legal move in this position.";
        }
        if (ply.isCheckmate) {
            move.summary = "Checkmate — the game ends here.";
        }
        if (ply.isNullMove) {
            move.summary = "Null move placeholder from the PGN; no position change.";
        }

        review.moves.push_back(std::move(move));
    }

    if (!review.moves.empty()) {
        review.white = computePlayerStats(review.moves, true, playerName(game.meta.white, true));
        review.black = computePlayerStats(review.moves, false, playerName(game.meta.black, false));
    }

    buildHighlights(review);

    if (!review.openingName.empty()) {
        Highlight openingHighlight;
        openingHighlight.ply = 0;
        openingHighlight.kind = "opening";
        openingHighlight.title = "Opening";
        openingHighlight.detail =
            review.openingName + (review.eco.empty() ? "" : " (" + review.eco + ")") + " — " +
            std::to_string(openingPlies) + " book half-moves played.";
        review.highlights.push_back(openingHighlight);
    }

    // Keep the report ordered by move so the UI reads naturally.
    std::stable_sort(review.highlights.begin(), review.highlights.end(),
                     [](const Highlight& a, const Highlight& b) { return a.ply < b.ply; });
    return review;
}

}  // namespace wechess
