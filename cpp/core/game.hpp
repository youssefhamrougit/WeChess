// WeChess - game model and PGN import.
//
// The chess rules themselves come from the vendored chess.hpp library
// (MIT, https://github.com/Disservin/chess-library). This layer is responsible
// for turning PGN text into a fully materialised list of positions, keeping
// metadata, comments and special-move flags, and for producing readable errors
// instead of throwing at the UI.
#ifndef WECHESS_CORE_GAME_HPP
#define WECHESS_CORE_GAME_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace wechess {

// Standard PGN header fields we surface in the UI. Missing tags stay empty.
struct GameMeta {
    std::string event    = "?";
    std::string site     = "?";
    std::string date     = "????.??.??";
    std::string round    = "?";
    std::string white    = "?";
    std::string black    = "?";
    std::string whiteElo;
    std::string blackElo;
    std::string result   = "*";
    std::string eco;
    std::string opening;   // from PGN tag, e.g. "Sicilian Defense"
    std::string timeControl;
    std::string termination;
    std::string annotator;
};

// A single half-move with the position on both sides of it.
struct PlyInfo {
    int ply = 0;            // 1-based half-move index
    int moveNumber = 0;     // full-move number, e.g. 17
    bool white = true;
    std::string san;        // normalised SAN as played ("Nf3", "exd8=Q+")
    std::string uci;        // "g1f3"
    std::string fenBefore;
    std::string fenAfter;
    std::string comment;    // PGN comment attached to this move, if any
    std::string fromSquare; // "g1"
    std::string toSquare;   // "f3"

    bool isCapture = false;
    bool isCheck = false;
    bool isCheckmate = false;
    bool isCastle = false;
    bool isEnPassant = false;
    bool isPromotion = false;
    bool isNullMove = false;   // PGN "--" placeholder
    char promotionPiece = 0;   // 'Q', 'R', 'B', 'N'

    int legalMoveCount = 0;    // legal moves available before the move
    int materialWhiteCp = 0;   // material balance snapshot before the move
    int materialBlackCp = 0;
};

struct ParsedGame {
    GameMeta meta;
    std::string startFen;
    std::string startingFenTag;  // raw [FEN] value if the PGN set one
    std::vector<PlyInfo> plies;

    bool hasMoves() const { return !plies.empty(); }
    // FEN of the position after `ply` half-moves (0 = starting position).
    std::string fenAtPly(int ply) const;
    const PlyInfo* ply(int index1Based) const;
};

struct PgnParseResult {
    bool ok = false;
    std::string error;
    ParsedGame game;
};

// Parse a single game (variations are ignored, the mainline is kept). Never
// throws; all failures come back through `PgnParseResult::error`.
PgnParseResult parsePgn(const std::string& pgn, std::size_t maxPlies = 800);

// Strip PGN move annotations that the rules library's SAN parser does not want
// ("!?", "!!", "??", "$4", trailing "e.p.") and normalise castling to O-O form.
std::string sanitizeSan(const std::string& raw);

// ---- Small FEN helpers used by the board renderer ---------------------------
struct BoardPlacement {
    // 64 entries, index 0 = a8 ... 63 = h1 (reading order of the FEN).
    // Each entry is "wP", "bK", ... or empty for an empty square.
    std::string squares[64];
    bool whiteToMove = true;
};

BoardPlacement parseFenPlacement(const std::string& fen);

// Centipawn material count (P=100 N=320 B=330 R=500 Q=900) from a FEN.
int materialForSide(const std::string& fen, bool white);
// Total non-pawn, non-king material in centipawns, used for game-phase detection.
int nonPawnMaterialForSide(const std::string& fen, bool white);
// Full-move number encoded in the FEN (used when a PGN omits [FEN]).
int fullMoveNumberFromFen(const std::string& fen);

// "1-0", "0-1", "1/2-1/2", "*"
std::string normalizeResult(const std::string& raw);

}  // namespace wechess

#endif  // WECHESS_CORE_GAME_HPP
