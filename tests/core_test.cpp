// Core model tests: PGN import, rule handling, FEN materialisation.
#include "../tests/test_main.hpp"

#include <string>

#include "../cpp/core/game.hpp"

using namespace wechess;

namespace {

// Morphy - Duke of Brunswick & Count Isouard, Paris 1858 ("Opera Game").
// Chosen because every move is legal, famous and easy to verify by hand.
const char* kOperaGame = R"PGN([Event "Casual game"]
[Site "Paris FRA"]
[Date "1858.??.??"]
[Round "?"]
[White "Morphy, Paul"]
[Black "Duke Karl / Count Isouard"]
[Result "1-0"]
[ECO "C41"]
[Opening "Philidor Defense"]

1. e4 e5 2. Nf3 d6 3. d4 Bg4 4. dxe5 Bxf3 5. Qxf3 dxe5 6. Bc4 Nf6 7. Qb3 Qe7
8. Nc3 c6 {Black prepares ...b5.} 9. Bg5 b5 10. Nxb5 cxb5 11. Bxb5+ Nbd7
12. O-O-O Rd8 13. Rxd7 Rxd7 14. Rd1 Qe6 15. Bxd7+ Nxd7 16. Qb8+ Nxb8 17. Rd8# 1-0
)PGN";

const char* kEnPassant = R"PGN([Event "En passant demo"]
[Result "*"]

1. e4 Nf6 2. e5 d5 3. exd6 exd6 4. d4 *
)PGN";

const char* kPromotion = R"PGN([Event "Promotion demo"]
[Site "?"]
[SetUp "1"]
[FEN "8/P6k/8/8/8/8/8/7K w - - 0 1"]
[Result "*"]

1. a8=Q Kh6 2. Qf8+ Kh5 *
)PGN";

}  // namespace

TEST(pgn_headers_are_parsed) {
    const auto parsed = parsePgn(kOperaGame);
    CHECK(parsed.ok);
    CHECK_EQ(parsed.error, std::string());
    CHECK_EQ(parsed.game.meta.event, std::string("Casual game"));
    CHECK_EQ(parsed.game.meta.site, std::string("Paris FRA"));
    CHECK_EQ(parsed.game.meta.date, std::string("1858.??.??"));
    CHECK_EQ(parsed.game.meta.white, std::string("Morphy, Paul"));
    CHECK_EQ(parsed.game.meta.black, std::string("Duke Karl / Count Isouard"));
    CHECK_EQ(parsed.game.meta.result, std::string("1-0"));
    CHECK_EQ(parsed.game.meta.eco, std::string("C41"));
    CHECK_EQ(parsed.game.meta.opening, std::string("Philidor Defense"));
}

TEST(pgn_moves_are_materialised) {
    const auto parsed = parsePgn(kOperaGame);
    CHECK(parsed.ok);
    CHECK_EQ(parsed.game.plies.size(), std::size_t(33));
    CHECK_EQ(parsed.game.plies[0].san, std::string("e4"));
    CHECK_EQ(parsed.game.plies[0].uci, std::string("e2e4"));
    CHECK_EQ(parsed.game.plies[1].san, std::string("e5"));
    CHECK_EQ(parsed.game.plies[1].moveNumber, 1);
    CHECK_EQ(parsed.game.plies[1].white, false);

    // Comments travel with the move they follow.
    CHECK_EQ(parsed.game.plies[15].san, std::string("c6"));
    CHECK_EQ(parsed.game.plies[15].comment, std::string("Black prepares ...b5."));

    // 16.Qb8+ is a queen sacrifice; the material snapshot shows it.
    const auto& queenSac = parsed.game.plies[30];
    CHECK_EQ(queenSac.san, std::string("Qb8+"));
    CHECK(queenSac.isCheck);

    // Castling is flagged and the king lands on c1.
    const auto& castle = parsed.game.plies[22];
    CHECK_EQ(castle.san, std::string("O-O-O"));
    CHECK(castle.isCastle);
    const BoardPlacement after = parseFenPlacement(castle.fenAfter);
    CHECK_EQ(after.squares[58], std::string("wK"));  // c1
    CHECK_EQ(after.squares[59], std::string("wR"));  // d1

    // The game ends in mate.
    CHECK(parsed.game.plies.back().isCheckmate);
    CHECK(parsed.game.plies.back().san.find("Rd8") == 0);
}

TEST(every_position_is_accurate) {
    const auto parsed = parsePgn(kOperaGame);
    CHECK(parsed.ok);
    for (std::size_t i = 0; i < parsed.game.plies.size(); ++i) {
        const auto& move = parsed.game.plies[i];
        const BoardPlacement placement = parseFenPlacement(move.fenAfter);
        int pieces = 0;
        for (const auto& square : placement.squares) {
            if (!square.empty()) ++pieces;
        }
        CHECK(pieces >= 2);
        // After White moves it is Black's turn and vice versa.
        CHECK_EQ(placement.whiteToMove, !move.white);
        // The moving piece is really on its destination square.
        const int file = move.toSquare[0] - 'a';
        const int rank = move.toSquare[1] - '1';
        const int index = (7 - rank) * 8 + file;
        CHECK(!placement.squares[index].empty());
        CHECK_EQ(placement.squares[index][0], move.white ? 'w' : 'b');
    }
    // White is down material at the end (queen sacrifice) but delivers mate.
    CHECK(materialForSide(parsed.game.plies.back().fenAfter, true) <
          materialForSide(parsed.game.plies.back().fenAfter, false));
}

TEST(castling_en_passant_and_promotion) {
    {
        const auto ep = parsePgn(kEnPassant);
        CHECK(ep.ok);
        bool sawEnPassant = false;
        for (const auto& move : ep.game.plies) {
            if (!move.isEnPassant) continue;
            sawEnPassant = true;
            CHECK_EQ(move.uci, std::string("e5d6"));
            const BoardPlacement after = parseFenPlacement(move.fenAfter);
            CHECK_EQ(after.squares[19], std::string("wP"));  // d6
            CHECK_EQ(after.squares[27], std::string());      // d5 vacated
        }
        CHECK(sawEnPassant);
    }
    {
        const auto promo = parsePgn(kPromotion);
        CHECK(promo.ok);
        bool sawPromotion = false;
        for (const auto& move : promo.game.plies) {
            if (!move.isPromotion) continue;
            sawPromotion = true;
            CHECK_EQ(move.promotionPiece, 'Q');
            const BoardPlacement after = parseFenPlacement(move.fenAfter);
            CHECK_EQ(after.squares[0], std::string("wQ"));  // a8
        }
        CHECK(sawPromotion);
    }
}

TEST(annotations_and_variations_are_ignored) {
    const char* pgn = R"PGN([Event "Annotated"]
[Result "*"]

1. e4!? e5 2. Nf3 $1 Nc6 (2... d6 3. d4) 3. Bb5 a6 4. Ba4 Nf6 *
)PGN";
    const auto parsed = parsePgn(pgn);
    CHECK(parsed.ok);
    CHECK_EQ(parsed.game.plies.size(), std::size_t(8));
    CHECK_EQ(parsed.game.plies[0].san, std::string("e4"));
    CHECK_EQ(parsed.game.plies[2].san, std::string("Nf3"));
    CHECK_EQ(parsed.game.plies[3].san, std::string("Nc6"));
    CHECK_EQ(parsed.game.plies[7].san, std::string("Nf6"));
}

TEST(incomplete_pgns_still_parse) {
    const auto parsed = parsePgn("1. e4 e5 2. Nf3");
    CHECK(parsed.ok);
    CHECK_EQ(parsed.game.plies.size(), std::size_t(3));
    CHECK_EQ(parsed.game.meta.result, std::string("*"));
}

TEST(invalid_pgns_report_errors) {
    {
        const auto empty = parsePgn("   \n  ");
        CHECK(!empty.ok);
        CHECK(empty.error.find("empty") != std::string::npos);
    }
    {
        const auto noMoves = parsePgn("[Event \"Nothing\"]\n[Result \"*\"]\n");
        CHECK(!noMoves.ok);
        CHECK(!noMoves.error.empty());
    }
    {
        const auto illegal = parsePgn("1. e4 e5 2. Bc4 Nc6 3. Qh5 Nf6 4. Qxf9 *");
        CHECK(!illegal.ok);
        if (!illegal.ok) std::cout << "    info: " << illegal.error << "\n";
        CHECK(illegal.error.find("legal") != std::string::npos);
    }
    {
        const auto badFen = parsePgn("[FEN \"not-a-position\"]\n\n1. e4 *");
        CHECK(!badFen.ok);
        CHECK(badFen.error.find("FEN") != std::string::npos);
    }
    {
        const auto tooLong = parsePgn(kOperaGame, 20);
        CHECK(!tooLong.ok);
        CHECK(tooLong.error.find("longer") != std::string::npos);
    }
}

TEST(custom_start_position) {
    const auto parsed = parsePgn(kPromotion);
    CHECK(parsed.ok);
    CHECK_EQ(parsed.game.plies.size(), std::size_t(4));
    CHECK(parsed.game.startFen.find("8/P6k/8/8/8/8/8/7K") == 0);
    CHECK_EQ(parsed.game.plies[0].moveNumber, 1);
    CHECK_EQ(parsed.game.plies[0].san, std::string("a8=Q"));
}

TEST(fen_helpers) {
    CHECK_EQ(sanitizeSan("e4!?"), std::string("e4"));
    CHECK_EQ(sanitizeSan("0-0"), std::string("O-O"));
    CHECK_EQ(sanitizeSan("$12"), std::string());
    CHECK_EQ(sanitizeSan("exd6e.p."), std::string("exd6"));
    CHECK_EQ(materialForSide("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", true),
             8 * 100 + 2 * 320 + 2 * 330 + 2 * 500 + 900);
    CHECK_EQ(nonPawnMaterialForSide("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1", true), 0);
    CHECK_EQ(fullMoveNumberFromFen("4k3/8/8/8/8/8/4P3/4K3 w - - 3 42"), 42);
    const BoardPlacement start = parseFenPlacement("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    CHECK_EQ(start.squares[0], std::string("bR"));    // a8
    CHECK_EQ(start.squares[4], std::string("bK"));    // e8
    CHECK_EQ(start.squares[60], std::string("wK"));   // e1
    CHECK_EQ(start.squares[63], std::string("wR"));   // h1
    CHECK(start.whiteToMove);
}

int main() {
    return wctest::run();
}
