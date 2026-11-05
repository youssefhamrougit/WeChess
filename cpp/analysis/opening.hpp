// WeChess - opening book backed by the Lichess chess-openings database (CC0).
//
// The database is a list of named lines in PGN. We convert every line into a
// UCI move sequence once, at first use, and index both complete lines and all
// their prefixes. That gives two things for free:
//   * the name/ECO of the longest opening line the game follows, and
//   * a reliable "is this a book move?" test that never relies on an LLM.
#ifndef WECHESS_ANALYSIS_OPENING_HPP
#define WECHESS_ANALYSIS_OPENING_HPP

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace wechess {

struct OpeningInfo {
    std::string eco;
    std::string name;
    int plies = 0;  // length of the matched line
};

class OpeningBook {
   public:
    // Loads the embedded database (lazily, thread-safe).
    static const OpeningBook& instance();

    // Longest named line matching the given move prefix.
    OpeningInfo lookup(const std::vector<std::string>& uciMoves) const;

    // True when `uciMove` continues a known opening line of at most kMaxBookPlies.
    bool isBookMove(const std::vector<std::string>& uciBefore, const std::string& uciMove) const;

    std::size_t lineCount() const { return namedLines_.size(); }

   private:
    OpeningBook();
    std::unordered_map<std::string, OpeningInfo> namedLines_;  // key: "e2e4 e7e5 ..."
    std::unordered_set<std::string> prefixes_;                 // every prefix of every line
};

// Maximum number of half-moves for which we still call a move "book".
inline constexpr int kMaxBookPlies = 24;

}  // namespace wechess

#endif  // WECHESS_ANALYSIS_OPENING_HPP
