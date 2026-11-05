// Classification, statistics and (optionally) real Stockfish integration tests.
//
// Usage: analysis_test [path-to-stockfish]
// The engine-backed cases are skipped, loudly, when no binary is supplied so a
// pure unit-test run never pretends to have analysed a position.
#include "test_main.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

#include "../cpp/analysis/classify.hpp"
#include "../cpp/analysis/opening.hpp"
#include "../cpp/analysis/review.hpp"
#include "../cpp/analysis/stats.hpp"
#include "../cpp/analysis/types.hpp"
#include "../cpp/core/game.hpp"
#include "../cpp/engine/analyzer.hpp"
#include "../cpp/engine/uci.hpp"

using namespace wechess;

namespace {
std::string gStockfish;

PositionEval makeEval(int whiteCp, int mate, int depth, std::vector<std::string> bestPv = {},
                      std::vector<int> altCps = {}, int altMate = 0) {
    PositionEval eval;
    eval.ok = true;
    eval.depth = depth;
    EngineLine line;
    line.multipv = 1;
    line.depth = depth;
    line.score = mate != 0 ? Score::fromMate(mate) : Score::fromCp(whiteCp);
    line.pvUci = std::move(bestPv);
    eval.lines.push_back(line);
    int rank = 2;
    for (int cp : altCps) {
        EngineLine alt;
        alt.multipv = rank;
        alt.depth = depth;
        alt.score = altMate != 0 ? Score::fromMate(altMate) : Score::fromCp(cp);
        eval.lines.push_back(alt);
        ++rank;
    }
    return eval;
}

ClassifyResult classifySynthetic(bool whiteToMove, PositionEval before, PositionEval after,
                                 const std::string& played, bool book = false, int sacrifice = 0,
                                 const std::string& bookName = "") {
    ClassifyInput input;
    input.moverWhite = whiteToMove;
    input.before = std::move(before);
    input.after = std::move(after);
    input.playedUci = played;
    input.isBook = book;
    input.bookName = bookName;
    input.sacrificeCp = sacrifice;
    return classifyMove(input);
}

bool engineReady() { return !gStockfish.empty(); }
}  // namespace

TEST(win_probability_basics) {
    CHECK_NEAR(winProbabilitySideToMove(Score::fromCp(0), true), 0.5, 1e-9);
    CHECK_NEAR(winProbabilitySideToMove(Score::fromCp(0), false), 0.5, 1e-9);
    // A plus score for the side to move is always > 50%.
    CHECK(winProbabilitySideToMove(Score::fromCp(150), true) > 0.60);
    CHECK(winProbabilitySideToMove(Score::fromCp(150), false) < 0.40);
    // Mate pins to the extremes.
    CHECK(winProbabilitySideToMove(Score::fromMate(3), true) > 0.97);
    CHECK(winProbabilitySideToMove(Score::fromMate(-3), true) < 0.03);
    // Perspective conversion is symmetric.
    CHECK_NEAR(winProbabilitySideToMove(Score::fromCp(-400), true),
               1.0 - winProbabilitySideToMove(Score::fromCp(-400), false), 1e-9);
    // Larger advantages are never less likely to win.
    double previous = 0.0;
    for (int cp = -800; cp <= 800; cp += 50) {
        const double w = winProbabilityWhite(Score::fromCp(cp));
        CHECK(w >= previous);
        previous = w;
    }
}

TEST(move_accuracy_formula) {
    CHECK_NEAR(moveAccuracyFromWinLoss(0.0), 100.0, 0.01);
    CHECK(moveAccuracyFromWinLoss(1.0) < 1.0);
    CHECK(moveAccuracyFromWinLoss(0.05) > moveAccuracyFromWinLoss(0.20));
    CHECK(moveAccuracyFromWinLoss(0.35) < moveAccuracyFromWinLoss(0.10));
    for (double loss = 0.0; loss <= 1.0; loss += 0.05) {
        const double acc = moveAccuracyFromWinLoss(loss);
        CHECK(acc >= 0.0);
        CHECK(acc <= 100.0);
    }
}

TEST(classification_thresholds) {
    // The engine's own move with an equal position is Best.
    const PositionEval equal = makeEval(20, 0, 16, {"e2e4"});
    const PositionEval equalAfter = makeEval(15, 0, 16);
    CHECK(classifySynthetic(true, equal, equalAfter, "e2e4").cls == Classification::Best);

    // Losing ~5% win probability is a Good move, ~9% an inaccuracy.
    const ClassifyResult good = classifySynthetic(true, makeEval(100, 0, 16, {"e2e4"}),
                                                 makeEval(60, 0, 16), "d2d4");
    CHECK(good.cls == Classification::Good);

    const ClassifyResult inaccurate = classifySynthetic(true, makeEval(100, 0, 16, {"e2e4"}),
                                                       makeEval(0, 0, 16), "d2d4");
    CHECK(inaccurate.cls == Classification::Inaccuracy);

    const ClassifyResult mistake = classifySynthetic(true, makeEval(150, 0, 16, {"e2e4"}),
                                                    makeEval(-50, 0, 16), "d2d4");
    CHECK(mistake.cls == Classification::Mistake);

    const ClassifyResult blunder = classifySynthetic(true, makeEval(300, 0, 16, {"e2e4"}),
                                                    makeEval(-300, 0, 16), "d2d4");
    CHECK(blunder.cls == Classification::Blunder);
    CHECK(blunder.winLoss > 0.22);

    // Book moves are flagged as book, never as an error.
    const ClassifyResult book = classifySynthetic(true, makeEval(20, 0, 14, {"e2e4"}),
                                                 makeEval(20, 0, 14), "e2e4", true, 0,
                                                 "Ruy Lopez (C60)");
    CHECK(book.cls == Classification::Book);
    CHECK(book.summary.find("Book") != std::string::npos);
}

TEST(classification_perspective_for_black) {
    // White's score falling from +1.0 to +0.4 is bad for Black? No: from Black's
    // point of view it means Black improved, so this must be classified Best-ish,
    // not a mistake. Conversely +0.4 -> +1.0 hurts Black.
    const PositionEval before = makeEval(400, 0, 16, {"e7e5"});
    const PositionEval after = makeEval(1000, 0, 16);
    const ClassifyResult blackLost = classifySynthetic(false, before, after, "a7a6");
    CHECK(blackLost.cls == Classification::Mistake || blackLost.cls == Classification::Blunder ||
          blackLost.cls == Classification::Inaccuracy);
    CHECK(blackLost.winLoss > 0.05);

    // The mirror image: the same numbers are good news for White.
    const ClassifyResult whiteGained = classifySynthetic(true, before, after, "a2a3");
    CHECK(whiteGained.winLoss < 0.01);
    CHECK(whiteGained.cpLoss >= 0);
}

TEST(brilliant_requires_a_real_sacrifice) {
    // Sacrificing a queen but staying winning and matching the engine: brilliant.
    const PositionEval before = makeEval(250, 0, 18, {"d1h5"});
    const PositionEval after = makeEval(300, 0, 18);
    const ClassifyResult heroic = classifySynthetic(true, before, after, "d1h5", false, 900);
    CHECK(heroic.cls == Classification::Brilliant);

    // Same move without giving up material is only Best.
    const ClassifyResult plain = classifySynthetic(true, before, after, "d1h5", false, 0);
    CHECK(plain.cls == Classification::Best);

    // A sacrifice in a lost position is not brilliant.
    const ClassifyResult desperate =
        classifySynthetic(true, makeEval(-600, 0, 18, {"d1h5"}), makeEval(-900, 0, 18), "d1h5", false, 900);
    CHECK(desperate.cls != Classification::Brilliant);

    // A sacrifice that throws the game away is a blunder.
    const ClassifyResult throwing =
        classifySynthetic(true, makeEval(250, 0, 18, {"d1h5"}), makeEval(-500, 0, 18), "d1h5", false, 900);
    CHECK(throwing.cls == Classification::Blunder);

    // Already completely winning: a sac is not "brilliant", it is just best.
    const ClassifyResult alreadyWon =
        classifySynthetic(true, makeEval(1500, 0, 18, {"d1h5"}), makeEval(1500, 0, 18), "d1h5", false, 900);
    CHECK(alreadyWon.cls != Classification::Brilliant);
}

TEST(great_needs_an_only_move) {
    // Second-best is 2.5 pawns worse: playing the only move is Great.
    const PositionEval only = makeEval(150, 0, 16, {"g1f3"}, {-100});
    const PositionEval afterOnly = makeEval(140, 0, 16);
    CHECK(classifySynthetic(true, only, afterOnly, "g1f3").cls == Classification::Great);

    // Many good moves: the same move is only Best.
    const PositionEval many = makeEval(150, 0, 16, {"g1f3"}, {140});
    CHECK(classifySynthetic(true, many, afterOnly, "g1f3").cls == Classification::Best);
}

TEST(forced_moves_are_not_punished) {
    PositionEval before = makeEval(0, 0, 16, {"e1d1"});
    PositionEval after = makeEval(-900, 0, 16);
    ClassifyInput input;
    input.moverWhite = true;
    input.before = before;
    input.after = after;
    input.playedUci = "e1d1";
    // classifyMove itself does not know the legal move count; the review builder
    // sets `forced` afterwards. The win-loss math still shows the swing honestly.
    const ClassifyResult result = classifyMove(input);
    CHECK(result.winLoss > 0.4);
}

TEST(missed_mate_is_detected) {
    const PositionEval before = makeEval(0, 4, 18, {"d1h5"});   // White mates in 2
    const PositionEval after = makeEval(0, -6, 18);             // Black mates in 3
    const ClassifyResult result = classifySynthetic(true, before, after, "a2a3");
    CHECK(result.missedMate);
    CHECK(result.cls == Classification::Blunder);
}

TEST(static_exchange_evaluation) {
    // Rxd8+ wins the queen but the black king recaptures the rook: 900 - 500.
    const int gain = staticExchangeGain("3qk3/8/8/8/8/8/8/3RK3 w - - 0 1", "d8");
    CHECK_EQ(gain, 400);

    // A pawn defended by a pawn: the exchange is equal, so nothing is won.
    const int defended = staticExchangeGain("8/8/2p5/3p4/4P3/8/8/4K1k1 w - - 0 1", "d5");
    CHECK_EQ(defended, 0);

    // An undefended pawn is simply won.
    CHECK_EQ(staticExchangeGain("8/8/8/3p4/4P3/8/8/4K1k1 w - - 0 1", "d5"), 100);
}

TEST(opening_book_data) {
    const OpeningBook& book = OpeningBook::instance();
    CHECK(book.lineCount() > 3000);
    CHECK(book.isBookMove({}, "e2e4"));
    CHECK(book.isBookMove({"e2e4"}, "e7e5"));
    const OpeningInfo info = book.lookup({"e2e4", "e7e5", "g1f3", "b8c6", "f1b5"});
    CHECK(info.name.find("Ruy Lopez") != std::string::npos);
    CHECK(info.eco.rfind("C", 0) == 0);
    // 1.a3 is a real (if eccentric) opening, still in the database.
    CHECK(book.lookup({"a2a3"}).name.find("Anderssen") != std::string::npos ||
          !book.lookup({"a2a3"}).name.empty());
}

TEST(review_without_engine_degrades_gracefully) {
    const auto parsed = parsePgn("1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 *");
    CHECK(parsed.ok);
    std::vector<PositionEval> evals(parsed.game.plies.size() + 1);
    ReviewOptions options;
    options.engineUsed = false;
    options.engineAvailable = false;
    const GameReview review = buildReview(parsed.game, evals, options);
    CHECK_EQ(review.moves.size(), parsed.game.plies.size());
    CHECK_EQ(review.white.moves, 3);
    CHECK_EQ(review.black.moves, 3);
    CHECK(review.openingName.find("Ruy Lopez") != std::string::npos);
    CHECK(!review.highlights.empty());  // at least the opening note
}

TEST(engine_analyses_a_position_consistently) {
    if (!engineReady()) {
        std::cout << "    SKIP no Stockfish binary supplied\n";
        return;
    }
    UciEngine engine;
    EngineSettings settings;
    settings.threads = 1;
    settings.hashMb = 16;
    settings.multipv = 3;
    CHECK(engine.start(gStockfish, settings));

    std::atomic<bool> cancel{false};
    const std::string fen = "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3";
    const EngineResult first = engine.analyse(fen, 12, 3, cancel);
    CHECK(first.ok);
    CHECK(first.depth >= 12);
    CHECK_EQ(first.lines.size(), std::size_t(3));
    CHECK(!first.bestMoveUci.empty());
    // White is slightly better after 1.e4 e5 2.Nf3 Nc6; the exact number is
    // engine-version dependent, so only the sign is asserted.
    CHECK(first.lines[0].score.cpClamped() >= 0);
    CHECK(first.lines[0].score.cpClamped() < 150);
    // MultiPV lines are ordered best-first, so the first is never worse.
    CHECK(first.lines[0].score.cpClamped() >= first.lines[1].score.cpClamped());
    // A second run with the same settings must agree exactly (deterministic UCI,
    // single thread, fixed hash).
    const EngineResult second = engine.analyse(fen, 12, 3, cancel);
    CHECK(second.ok);
    CHECK_EQ(second.bestMoveUci, first.bestMoveUci);
    CHECK_EQ(second.lines[0].score.cp, first.lines[0].score.cp);
    engine.stop();
}

TEST(engine_classifies_a_real_game) {
    if (!engineReady()) {
        std::cout << "    SKIP no Stockfish binary supplied\n";
        return;
    }
    // Fool's mate: 2.g4?? allows mate in one, so it must be a blunder and the
    // final move must be a best move.
    const auto parsed = parsePgn("1. f3 e5 2. g4 Qh4# 0-1");
    CHECK(parsed.ok);

    AnalysisConfig config;
    config.stockfishPath = gStockfish;
    config.engine.depth = 12;
    config.engine.multipv = 2;
    config.engine.hashMb = 16;
    config.criticalDepth = 14;

    EvalCache cache;
    AnalysisProgress progress;
    std::atomic<bool> cancel{false};
    const AnalysisOutput output = runAnalysis(parsed.game, config, &cache, progress, cancel);
    CHECK(output.ok);
    CHECK(output.engineAvailable);
    CHECK_EQ(output.evals.size(), parsed.game.plies.size() + 1);
    for (const auto& eval : output.evals) CHECK(eval.ok);
    CHECK(progress.finished.load());
    CHECK_EQ(progress.percent(), 100);

    ReviewOptions options;
    options.engineName = output.engineName;
    options.engineDepth = config.engine.depth;
    const GameReview review = buildReview(parsed.game, output.evals, options);
    CHECK_EQ(review.moves.size(), std::size_t(4));

    // 2.g4 is the losing move and must be a blunder.
    CHECK(review.moves[2].cls == Classification::Blunder);
    // 2...Qh4# mates, so it is the engine's best move.
    CHECK(review.moves[3].cls == Classification::Best || review.moves[3].cls == Classification::Great);
    CHECK(review.moves[3].summary.find("Checkmate") != std::string::npos);
    // Black outplayed White overall.
    CHECK(review.black.accuracy > review.white.accuracy);
    // Stats must add up to the number of moves played.
    CHECK_EQ(review.white.moves + review.black.moves, 4);
    const int whiteCount = review.white.brilliant + review.white.great + review.white.best +
                           review.white.excellent + review.white.good + review.white.book +
                           review.white.inaccuracy + review.white.mistake + review.white.blunder;
    CHECK(whiteCount <= review.white.moves);

    // Re-running must come from the cache and produce identical classifications.
    AnalysisProgress progress2;
    const AnalysisOutput cachedOutput = runAnalysis(parsed.game, config, &cache, progress2, cancel);
    CHECK(cachedOutput.ok);
    CHECK_EQ(cachedOutput.analysed, 0);
    const GameReview cachedReview = buildReview(parsed.game, cachedOutput.evals, options);
    CHECK_EQ(static_cast<int>(cachedReview.moves[2].cls), static_cast<int>(review.moves[2].cls));
    CHECK_EQ(cachedReview.white.accuracy, review.white.accuracy);
}

TEST(cache_round_trips_through_disk) {
    const std::string path = "/tmp/wechess_cache_test.tsv";
    std::remove(path.c_str());
    {
        EvalCache cache;
        cache.open(path);
        PositionEval eval = makeEval(35, 0, 15, {"e2e4", "e7e5"});
        eval.fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
        cache.store(eval);
        cache.flush();
        CHECK_EQ(cache.size(), std::size_t(1));
    }
    {
        EvalCache cache;
        cache.open(path);
        PositionEval loaded;
        CHECK(cache.lookup("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 15, loaded));
        CHECK(loaded.ok);
        CHECK_EQ(loaded.depth, 15);
        CHECK_EQ(loaded.lines.size(), std::size_t(1));
        CHECK_EQ(loaded.lines[0].score.cp, 35);
        CHECK_EQ(loaded.lines[0].pvUci.size(), std::size_t(2));
        CHECK(loaded.fromCache);
        // A deeper requirement must miss.
        CHECK(!cache.lookup("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 18, loaded));
    }
    std::remove(path.c_str());
}

TEST(pv_conversion_to_san) {
    const std::vector<std::string> san =
        uciLineToSan("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                     {"e2e4", "e7e5", "g1f3"});
    CHECK_EQ(san.size(), std::size_t(3));
    CHECK_EQ(san[0], std::string("e4"));
    CHECK_EQ(san[2], std::string("Nf3"));
    CHECK_EQ(formatPv("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                      {"e2e4", "e7e5", "g1f3"}),
             std::string("1. e4 e5 2. Nf3"));
}

int main(int argc, char** argv) {
    if (argc > 1) {
        gStockfish = argv[1];
    } else if (const char* env = std::getenv("WECHESS_STOCKFISH")) {
        gStockfish = env;
    }
    if (!gStockfish.empty()) std::cout << "Stockfish: " << gStockfish << "\n";
    return wctest::run();
}
