#include "game.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "chess.hpp"

namespace wechess {
namespace {

std::string trim(const std::string& value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return value.substr(begin, end - begin);
}

bool startsWith(const std::string& haystack, const std::string& needle) {
    return haystack.size() >= needle.size() && haystack.compare(0, needle.size(), needle) == 0;
}

// Column index of a piece character in the FEN placement field.
int pieceValueCp(char piece) {
    switch (std::toupper(static_cast<unsigned char>(piece))) {
        case 'P': return 100;
        case 'N': return 320;
        case 'B': return 330;
        case 'R': return 500;
        case 'Q': return 900;
        default: return 0;
    }
}

struct Collector : public chess::pgn::Visitor {
    struct RawMove {
        std::string san;
        std::string comment;
    };
    GameMeta* meta = nullptr;
    std::vector<RawMove> moves;
    std::string fenTag;
    std::string error;
    bool started = false;
    bool sawGameEnd = false;

    void startPgn() override {
        // Only the first game of a collection is imported; a PGN without any
        // header tags never calls startPgn, so `move()` starts collection too.
        if (sawGameEnd) {
            skipPgn(true);
            return;
        }
        started = true;
    }

    void header(std::string_view key, std::string_view value) override {
        if (!started || meta == nullptr) return;
        const std::string k = trim(std::string(key));
        const std::string v = trim(std::string(value));
        if (k == "Event") meta->event = v;
        else if (k == "Site") meta->site = v;
        else if (k == "Date") meta->date = v;
        else if (k == "Round") meta->round = v;
        else if (k == "White") meta->white = v;
        else if (k == "Black") meta->black = v;
        else if (k == "WhiteElo" || k == "WhiteRating") meta->whiteElo = v;
        else if (k == "BlackElo" || k == "BlackRating") meta->blackElo = v;
        else if (k == "Result") meta->result = v;
        else if (k == "ECO") meta->eco = v;
        else if (k == "Opening") meta->opening = v;
        else if (k == "TimeControl") meta->timeControl = v;
        else if (k == "Termination") meta->termination = v;
        else if (k == "Annotator") meta->annotator = v;
        else if (k == "FEN") fenTag = v;
    }

    void startMoves() override {}

    void move(std::string_view moveText, std::string_view comment) override {
        if (sawGameEnd) return;
        started = true;
        RawMove m;
        m.san = trim(std::string(moveText));
        m.comment = trim(std::string(comment));
        if (m.san.empty() && m.comment.empty()) return;
        if (m.san.empty() && !moves.empty()) {
            // A comment that stands alone belongs to the previous move.
            if (!moves.back().comment.empty()) moves.back().comment += " ";
            moves.back().comment += m.comment;
            return;
        }
        moves.push_back(std::move(m));
    }

    void endPgn() override {
        if (started) sawGameEnd = true;
    }
};

}  // namespace

std::string normalizeResult(const std::string& raw) {
    const std::string value = trim(raw);
    if (value == "1-0" || value == "0-1" || value == "1/2-1/2" || value == "*") return value;
    if (value == "1/2" || value == "0.5-0.5" || value == "1/2 - 1/2") return "1/2-1/2";
    return "*";
}

std::string sanitizeSan(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
        if (c == '!' || c == '?') continue;   // annotation glyphs
        out.push_back(c);
    }
    // Trailing en-passant marker: "exd6e.p." -> "exd6"
    const std::string ep = "e.p.";
    if (out.size() >= ep.size() && out.compare(out.size() - ep.size(), ep.size(), ep) == 0) {
        out.erase(out.size() - ep.size());
    }
    // NAG tokens such as "$14" are not moves.
    if (startsWith(out, "$")) return std::string();
    // Normalise zero-based castling to the SAN the library expects.
    if (out == "0-0") out = "O-O";
    else if (out == "0-0-0") out = "O-O-O";
    // Strip a UTF-8 BOM / stray non-ASCII bytes that appear in copy-pasted PGNs.
    std::string ascii;
    ascii.reserve(out.size());
    for (char c : out) {
        if (static_cast<unsigned char>(c) < 0x80) ascii.push_back(c);
    }
    return ascii;
}

BoardPlacement parseFenPlacement(const std::string& fen) {
    BoardPlacement placement;
    std::istringstream stream(fen);
    std::string board;
    std::string side;

    // A FEN may arrive without its side-to-move field from user input.
    std::string first;
    stream >> first;
    bool looksLikePlacement = first.find('/') != std::string::npos;
    if (!looksLikePlacement) return placement;

    board = first;
    if (!(stream >> side)) side = "w";
    placement.whiteToMove = (side == "w");

    int index = 0;
    int rank = 0;
    for (char c : board) {
        if (c == '/') {
            ++rank;
            index = rank * 8;
            continue;
        }
        if (c >= '1' && c <= '8') {
            index += c - '0';
            continue;
        }
        if (index < 0 || index >= 64) continue;
        char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower == 'p' || lower == 'n' || lower == 'b' || lower == 'r' || lower == 'q' || lower == 'k') {
            char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            placement.squares[index] = std::string(std::isupper(static_cast<unsigned char>(c)) ? "w" : "b") + upper;
            ++index;
        }
    }
    return placement;
}

int materialForSide(const std::string& fen, bool white) {
    BoardPlacement placement = parseFenPlacement(fen);
    int total = 0;
    for (const std::string& square : placement.squares) {
        if (square.empty()) continue;
        const bool isWhite = square[0] == 'w';
        if (isWhite != white) continue;
        total += pieceValueCp(square[1]);
    }
    return total;
}

int nonPawnMaterialForSide(const std::string& fen, bool white) {
    BoardPlacement placement = parseFenPlacement(fen);
    int total = 0;
    for (const std::string& square : placement.squares) {
        if (square.empty()) continue;
        if ((square[0] == 'w') != white) continue;
        const char kind = square[1];
        if (kind == 'P' || kind == 'K') continue;
        total += pieceValueCp(kind);
    }
    return total;
}

int fullMoveNumberFromFen(const std::string& fen) {
    std::istringstream stream(fen);
    std::string field;
    int index = 0;
    int number = 1;
    while (stream >> field) {
        if (index == 5) {
            try {
                number = std::max(1, std::stoi(field));
            } catch (...) {
                number = 1;
            }
            break;
        }
        ++index;
    }
    return number;
}

std::string ParsedGame::fenAtPly(int plyIndex) const {
    if (plyIndex <= 0) return startFen;
    const std::size_t index = static_cast<std::size_t>(plyIndex - 1);
    if (index >= plies.size()) return plies.empty() ? startFen : plies.back().fenAfter;
    return plies[index].fenAfter;
}

const PlyInfo* ParsedGame::ply(int index1Based) const {
    if (index1Based < 1 || static_cast<std::size_t>(index1Based) > plies.size()) return nullptr;
    return &plies[static_cast<std::size_t>(index1Based - 1)];
}

PgnParseResult parsePgn(const std::string& pgn, std::size_t maxPlies) {
    PgnParseResult result;
    if (trim(pgn).empty()) {
        result.error = "The PGN text is empty. Paste a game or choose a .pgn file.";
        return result;
    }

    // The stream parser only enters move text after a tag pair, so a bare list of
    // moves needs a minimal header. This is also what makes pasted move lists
    // from scoresheets work.
    std::string text = pgn;
    {
        std::size_t firstVisible = 0;
        while (firstVisible < text.size() &&
               std::isspace(static_cast<unsigned char>(text[firstVisible]))) {
            ++firstVisible;
        }
        if (firstVisible >= text.size() || text[firstVisible] != '[') {
            text = "[Event \"Imported game\"]\n\n" + text;
        }
    }

    std::istringstream input(text);
    Collector collector;
    collector.meta = &result.game.meta;
    chess::pgn::StreamParser parser(input);
    try {
        chess::pgn::StreamParserError error = parser.readGames(collector);
        if (error.hasError() && collector.moves.empty()) {
            result.error = "This PGN could not be read (malformed header or move text).";
            return result;
        }
    } catch (const std::exception& ex) {
        result.error = std::string("This PGN could not be read: ") + ex.what();
        return result;
    }

    if (collector.moves.empty()) {
        result.error = "The PGN does not contain any moves.";
        return result;
    }

    result.game.meta.result = normalizeResult(result.game.meta.result);
    result.game.startingFenTag = collector.fenTag;

    std::string startFen = chess::constants::STARTPOS;
    if (!collector.fenTag.empty()) startFen = collector.fenTag;
    chess::Board board;
    if (!startFen.empty() && startFen != chess::constants::STARTPOS) {
        if (!board.setFen(startFen)) {
            result.error = "The PGN [FEN] tag is not a valid chess position.";
            return result;
        }
    }
    result.game.startFen = board.getFen();

    int plyNumber = 0;
    for (const auto& raw : collector.moves) {
        if (static_cast<std::size_t>(plyNumber) >= maxPlies) {
            result.error = "This game is longer than the supported " + std::to_string(maxPlies) +
                           " half-moves.";
            return result;
        }

        const std::string san = sanitizeSan(raw.san);
        if (san.empty()) continue;

        PlyInfo info;
        info.ply = plyNumber + 1;
        info.moveNumber = static_cast<int>(board.fullMoveNumber());
        info.white = board.sideToMove() == chess::Color::WHITE;
        info.fenBefore = board.getFen();
        info.comment = raw.comment;
        info.materialWhiteCp = materialForSide(info.fenBefore, true);
        info.materialBlackCp = materialForSide(info.fenBefore, false);

        chess::Movelist legal;
        chess::movegen::legalmoves(legal, board);
        info.legalMoveCount = legal.size();

        if (san == "--") {
            // Null move used by some annotation tools; keep the position but do
            // not pretend the game continued legally.
            info.isNullMove = true;
            info.uci.clear();
            info.fenAfter = info.fenBefore;
            info.fromSquare.clear();
            info.toSquare.clear();
            result.game.plies.push_back(std::move(info));
            ++plyNumber;
            continue;
        }

        chess::Move move = chess::Move::NO_MOVE;
        try {
            move = chess::uci::parseSan(board, san);
        } catch (const std::exception& ex) {
            const std::string side = info.white ? "White" : "Black";
            result.error = "Move " + std::to_string(info.moveNumber) + " (" + side + ") \"" + raw.san +
                           "\" is not legal in this position: " + ex.what();
            return result;
        }
        if (move == chess::Move::NO_MOVE) {
            const std::string side = info.white ? "White" : "Black";
            result.error = "Move " + std::to_string(info.moveNumber) + " (" + side + ") \"" + raw.san +
                           "\" is not legal in this position.";
            return result;
        }

        info.san = chess::uci::moveToSan(board, move);
        info.uci = chess::uci::moveToUci(move);
        info.fromSquare = info.uci.substr(0, 2);
        info.toSquare = info.uci.substr(2, 2);
        info.isCapture = board.at(move.to()) != chess::Piece::NONE || move.typeOf() == chess::Move::ENPASSANT;
        info.isCastle = move.typeOf() == chess::Move::CASTLING;
        info.isEnPassant = move.typeOf() == chess::Move::ENPASSANT;
        info.isPromotion = move.typeOf() == chess::Move::PROMOTION;
        if (info.isPromotion) {
            const std::string piece = static_cast<std::string>(move.promotionType());
            if (!piece.empty()) {
                info.promotionPiece = static_cast<char>(std::toupper(static_cast<unsigned char>(piece[0])));
            }
        }

        board.makeMove(move);
        info.fenAfter = board.getFen();
        info.isCheck = board.inCheck();
        const auto state = board.isGameOver();
        info.isCheckmate = state.first == chess::GameResultReason::CHECKMATE;

        result.game.plies.push_back(std::move(info));
        ++plyNumber;
    }

    if (result.game.plies.empty()) {
        result.error = "The PGN does not contain any playable moves.";
        return result;
    }

    result.ok = true;
    return result;
}

}  // namespace wechess
