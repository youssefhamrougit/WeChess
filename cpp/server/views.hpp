// WeChess - HTML rendering. All pages are produced on the server; the client
// only ever receives HTML and CSS.
#ifndef WECHESS_SERVER_VIEWS_HPP
#define WECHESS_SERVER_VIEWS_HPP

#include <string>
#include <vector>

#include "../core/game.hpp"
#include "session.hpp"
#include "store.hpp"

namespace wechess {

struct PageChrome {
    bool geminiConfigured = false;
    bool engineAvailable = false;
    std::string stockfishName;
    std::string appVersion = "1.0.0";
};

std::string renderHome(const PageChrome& chrome, const std::vector<RecentGame>& recents,
                       const AppSettings& settings, const std::string& flash, const std::string& error);

std::string renderReview(const PageChrome& chrome, const GameSession& session, int ply,
                         const std::string& flash, const std::string& error);

std::string renderAnalysisProgress(const PageChrome& chrome, const GameSession& session);

std::string renderExplainWait(const PageChrome& chrome, const GameSession& session, int ply);

std::string renderSettings(const PageChrome& chrome, const AppSettings& settings,
                           const std::string& resolvedStockfish, const std::string& flash,
                           const std::string& error);

std::string renderNotFound();

// Pieces are inline SVG symbols; exposed for tests.
std::string pieceSprite();

}  // namespace wechess

#endif  // WECHESS_SERVER_VIEWS_HPP
