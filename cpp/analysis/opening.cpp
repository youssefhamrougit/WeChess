#include "opening.hpp"

#include <sstream>

#include "chess.hpp"
#include "../data/openings_book.inc"

namespace wechess {
namespace {

std::string joinMoves(const std::vector<std::string>& moves, std::size_t count) {
    std::string key;
    for (std::size_t i = 0; i < count; ++i) {
        if (i) key.push_back(' ');
        key += moves[i];
    }
    return key;
}

// Convert "1. e4 e5 2. Nf3" to a UCI sequence. Lines that the rules library
// rejects are dropped rather than guessed at.
std::vector<std::string> pgnLineToUci(const std::string& pgn) {
    std::vector<std::string> uci;
    chess::Board board;
    std::istringstream stream(pgn);
    std::string token;
    while (stream >> token) {
        if (token.empty()) continue;
        // Skip move numbers ("1.", "12...").
        if (std::isdigit(static_cast<unsigned char>(token[0])) && token.find_first_not_of("0123456789.") == std::string::npos) {
            continue;
        }
        // Strip result markers.
        if (token == "1-0" || token == "0-1" || token == "1/2-1/2" || token == "*") break;
        std::string san;
        for (char c : token) {
            if (c == '!' || c == '?') continue;
            san.push_back(c);
        }
        if (san == "0-0") san = "O-O";
        else if (san == "0-0-0") san = "O-O-O";
        if (san.empty()) continue;
        chess::Move move = chess::Move::NO_MOVE;
        try {
            move = chess::uci::parseSan(board, san);
        } catch (...) {
            return {};
        }
        if (move == chess::Move::NO_MOVE) return {};
        uci.push_back(chess::uci::moveToUci(move));
        board.makeMove(move);
        if (uci.size() >= static_cast<std::size_t>(kMaxBookPlies)) break;
    }
    return uci;
}

}  // namespace

OpeningBook::OpeningBook() {
    std::istringstream stream(data::kOpeningsTsv);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == '/') continue;
        const std::size_t firstTab = line.find('\t');
        if (firstTab == std::string::npos) continue;
        const std::size_t secondTab = line.find('\t', firstTab + 1);
        if (secondTab == std::string::npos) continue;

        OpeningInfo info;
        info.eco = line.substr(0, firstTab);
        info.name = line.substr(firstTab + 1, secondTab - firstTab - 1);
        const std::string pgn = line.substr(secondTab + 1);
        if (info.name.empty() || pgn.empty()) continue;

        const std::vector<std::string> uci = pgnLineToUci(pgn);
        if (uci.empty()) continue;
        info.plies = static_cast<int>(uci.size());

        namedLines_[joinMoves(uci, uci.size())] = info;
        for (std::size_t length = 1; length <= uci.size(); ++length) {
            prefixes_.insert(joinMoves(uci, length));
        }
    }
}

const OpeningBook& OpeningBook::instance() {
    static const OpeningBook book;
    return book;
}

OpeningInfo OpeningBook::lookup(const std::vector<std::string>& uciMoves) const {
    // Walk backwards to find the longest named line that the game has followed.
    for (std::size_t length = std::min(uciMoves.size(), static_cast<std::size_t>(kMaxBookPlies)); length > 0; --length) {
        const auto it = namedLines_.find(joinMoves(uciMoves, length));
        if (it != namedLines_.end()) return it->second;
    }
    return OpeningInfo{};
}

bool OpeningBook::isBookMove(const std::vector<std::string>& uciBefore,
                             const std::string& uciMove) const {
    if (uciBefore.size() >= static_cast<std::size_t>(kMaxBookPlies)) return false;
    std::string key = joinMoves(uciBefore, uciBefore.size());
    if (!key.empty()) key.push_back(' ');
    key += uciMove;
    return prefixes_.count(key) > 0;
}

}  // namespace wechess
