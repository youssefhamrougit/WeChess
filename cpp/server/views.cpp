#include "views.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>

#include "../analysis/classify.hpp"
#include "../analysis/stats.hpp"
#include "http.hpp"

namespace wechess {
namespace assets {
// Provided by the generated embedded_assets.cpp.
const char* shellHtml();
const char* stylesheet();
}  // namespace assets

namespace {

std::string replaceAll(std::string text, const std::string& needle, const std::string& value) {
    if (needle.empty()) return text;
    std::size_t position = 0;
    while ((position = text.find(needle, position)) != std::string::npos) {
        text.replace(position, needle.size(), value);
        position += value.size();
    }
    return text;
}

std::string shell(const std::string& title, const std::string& content, const std::string& active,
                  const std::string& headExtra = "") {
    std::string page = assets::shellHtml();
    page = replaceAll(page, "{{title}}", htmlEscape(title));
    page = replaceAll(page, "{{content}}", content);
    page = replaceAll(page, "{{head_extra}}", headExtra);
    page = replaceAll(page, "{{nav_games}}", active == "games" ? "active" : "");
    page = replaceAll(page, "{{nav_settings}}", active == "settings" ? "active" : "");
    return page;
}

std::string noticeBlock(const std::string& flash, const std::string& error) {
    std::string out;
    if (!flash.empty()) out += "<div class=\"notice ok\">" + htmlEscape(flash) + "</div>";
    if (!error.empty()) out += "<div class=\"notice error\">" + htmlEscape(error) + "</div>";
    return out;
}

std::string playerLabel(const std::string& white, const std::string& black) {
    return htmlEscape(white) + " vs " + htmlEscape(black);
}

std::string orUnknown(const std::string& value) { return (value.empty() || value == "?") ? "?" : value; }

std::string pieceSvg(const std::string& square) {
    if (square.size() < 2) return std::string();
    const char color = square[0];
    char kind = square[1];
    const char cssColor = (color == 'w') ? 'w' : 'b';
    const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(kind)));
    std::string id = "pi-";
    id.push_back(lower);
    std::string out = "<svg class=\"pc pc-";
    out.push_back(cssColor);
    out += "\" viewBox=\"0 0 45 45\" aria-hidden=\"true\" focusable=\"false\"><use href=\"#";
    out += id;
    out += "\"/></svg>";
    return out;
}

std::string pieceNoun(const std::string& square) {
    if (square.size() < 2) return "piece";
    switch (std::toupper(static_cast<unsigned char>(square[1]))) {
        case 'P': return "pawn";
        case 'N': return "knight";
        case 'B': return "bishop";
        case 'R': return "rook";
        case 'Q': return "queen";
        case 'K': return "king";
        default: return "piece";
    }
}

int kingSquare(const BoardPlacement& placement, bool white) {
    const std::string target = white ? "wK" : "bK";
    for (int i = 0; i < 64; ++i) {
        if (placement.squares[i] == target) return i;
    }
    return -1;
}

bool parseSquare(const std::string& text, int& file, int& rank) {
    if (text.size() < 2) return false;
    if (text[0] < 'a' || text[0] > 'h' || text[1] < '1' || text[1] > '8') return false;
    file = text[0] - 'a';
    rank = text[1] - '1';
    return true;
}

std::string num(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.3f", value);
    return buffer;
}

std::string arrowLine(const std::string& from, const std::string& to, const std::string& color,
                      const std::string& marker) {
    int fromFile = 0, fromRank = 0, toFile = 0, toRank = 0;
    if (!parseSquare(from, fromFile, fromRank) || !parseSquare(to, toFile, toRank)) return std::string();
    double x1 = fromFile + 0.5;
    double y1 = (7 - fromRank) + 0.5;
    double x2 = toFile + 0.5;
    double y2 = (7 - toRank) + 0.5;
    const double dx = x2 - x1;
    const double dy = y2 - y1;
    const double length = std::sqrt(dx * dx + dy * dy);
    if (length < 0.01) return std::string();
    x1 += dx / length * 0.12;
    y1 += dy / length * 0.12;
    x2 -= dx / length * 0.34;
    y2 -= dy / length * 0.34;

    std::string out = "<line x1=\"" + num(x1) + "\" y1=\"" + num(y1) + "\" x2=\"" + num(x2) + "\" y2=\"" +
                      num(y2) + "\" stroke=\"" + color +
                      "\" stroke-width=\"0.15\" stroke-linecap=\"round\" marker-end=\"url(#" + marker +
                      ")\"/>";
    return out;
}

std::string boardHtml(const std::string& fen, const std::string& lastUci, bool check,
                      const std::string& playedUci, const std::string& bestUci) {
    const BoardPlacement placement = parseFenPlacement(fen);

    std::string from, to;
    if (lastUci.size() >= 4) {
        from = lastUci.substr(0, 2);
        to = lastUci.substr(2, 2);
    }

    int checkIndex = -1;
    if (check) checkIndex = kingSquare(placement, placement.whiteToMove);

    std::string out = "<div class=\"board-wrap\" id=\"board\">";
    out += "<div class=\"board\" role=\"img\" aria-label=\"Chess board\">";
    for (int index = 0; index < 64; ++index) {
        const int file = index % 8;
        const int rankIndex = index / 8;      // 0 = 8th rank
        const int rank = 7 - rankIndex;
        const bool light = ((file + rankIndex) % 2) == 0;

        out += "<div class=\"sq ";
        out += light ? "light" : "dark";
        if (!from.empty() && index == (7 - (from[1] - '1')) * 8 + (from[0] - 'a')) out += " from";
        if (!to.empty() && index == (7 - (to[1] - '1')) * 8 + (to[0] - 'a')) out += " last";
        if (index == checkIndex) out += " check";
        out += "\">";

        const std::string& piece = placement.squares[index];
        if (!piece.empty()) {
            out += pieceSvg(piece);
            out += "<span class=\"sr-only\">" + std::string(piece[0] == 'w' ? "White" : "Black") + " " +
                   pieceNoun(piece) + " on " +
                   std::string(1, static_cast<char>('a' + file)) + std::to_string(rank + 1) + "</span>";
        }
        if (rankIndex == 7) {
            out += "<span class=\"coord coord-file\">" + std::string(1, static_cast<char>('a' + file)) +
                   "</span>";
        }
        if (file == 0) {
            out += "<span class=\"coord coord-rank\">" + std::to_string(rank + 1) + "</span>";
        }
        out += "</div>";
    }
    out += "</div>";

    std::string arrows;
    if (!bestUci.empty() && bestUci != playedUci) {
        arrows += arrowLine(bestUci.substr(0, 2), bestUci.substr(2, 2), "#9ec27a", "arrow-best");
    }
    if (!playedUci.empty()) {
        arrows += arrowLine(playedUci.substr(0, 2), playedUci.substr(2, 2), "#d98b84", "arrow-played");
    }
    if (!arrows.empty()) {
        out += "<svg class=\"board-arrows\" viewBox=\"0 0 8 8\" preserveAspectRatio=\"none\" "
               "aria-hidden=\"true\"><defs>"
               "<marker id=\"arrow-best\" viewBox=\"0 0 10 10\" refX=\"7\" refY=\"5\" markerWidth=\"2.6\" "
               "markerHeight=\"2.6\" orient=\"auto\"><path d=\"M0 0 L10 5 L0 10 Z\" fill=\"#9ec27a\"/></marker>"
               "<marker id=\"arrow-played\" viewBox=\"0 0 10 10\" refX=\"7\" refY=\"5\" markerWidth=\"2.6\" "
               "markerHeight=\"2.6\" orient=\"auto\"><path d=\"M0 0 L10 5 L0 10 Z\" fill=\"#d98b84\"/></marker>"
               "</defs>" + arrows + "</svg>";
    }
    out += "</div>";
    return out;
}

std::string evalBar(const Score& whitePov) {
    double win = winProbabilityWhite(whitePov);
    win = std::clamp(win, 0.02, 0.98);
    char height[16];
    std::snprintf(height, sizeof(height), "%.1f", win * 100.0);
    const bool whiteAhead = whitePov.forColor(true).cpClamped() >= 0 && !(whitePov.hasMate() && whitePov.mate < 0);

    std::string out = "<div class=\"evalbar\" role=\"img\" aria-label=\"Evaluation bar\">";
    out += "<div class=\"eval-fill\" style=\"height:" + std::string(height) + "%\"></div>";
    out += "<div class=\"eval-mid\"></div>";
    out += "<div class=\"eval-score " + std::string(whiteAhead ? "bottom" : "top") + "\">" +
           htmlEscape(formatScore(whitePov)) + "</div>";
    out += "</div>";
    return out;
}

std::string classificationChip(Classification cls, bool withLabel) {
    const char* symbol = classificationSymbol(cls);
    std::string out = "<span class=\"chip " + std::string(classificationCssClass(cls)) + "\">";
    if (symbol[0] != '\0') {
        out += symbol;
        out += "&nbsp;";
    }
    if (withLabel) out += classificationLabel(cls);
    out += "</span>";
    return out;
}

std::string badge(Classification cls) {
    const char* symbol = classificationSymbol(cls);
    std::string out = "<span class=\"badge b-" + std::string(classificationCssClass(cls)) + "\">";
    out += symbol[0] != '\0' ? symbol : "";
    out += "</span>";
    return out;
}

std::string moveLink(const std::string& id, const MoveReview& move, bool selected) {
    std::string out = "<a class=\"mv" + std::string(selected ? " sel" : "") + "\" href=\"/game/" + id +
                      "?ply=" + std::to_string(move.ply) + "#board\"";
    if (selected) out += " aria-current=\"true\"";
    out += " title=\"" + htmlEscape(std::string(classificationLabel(move.cls)) + ": " + move.summary) + "\">";
    out += "<span class=\"san\">" + htmlEscape(move.playedSan) + "</span>";
    out += badge(move.cls);
    out += "</a>";
    return out;
}

std::string accuracyCard(const PlayerStats& stats, const std::string& label) {
    char accuracy[16];
    std::snprintf(accuracy, sizeof(accuracy), "%.1f", stats.accuracy);
    std::string out = "<div class=\"acc\"><div class=\"who\">" + htmlEscape(label) + "</div>";
    out += "<div class=\"value\">" + std::string(accuracy) + "<span class=\"unit\">%</span></div>";
    out += "<div class=\"acc-bar\"><span style=\"width:" + std::string(accuracy) + "%\"></span></div>";
    out += "<div class=\"small muted-2\" style=\"margin-top:5px\">ACPL " +
           std::to_string(static_cast<int>(stats.averageCentipawnLoss)) + "</div></div>";
    return out;
}

std::string statRow(const std::string& lineClass, const std::string& label, int white, int black) {
    std::string out = "<tr><td><span class=\"c\"><span class=\"dot " + lineClass + "\"></span>" +
                      htmlEscape(label) + "</span></td><td>" + std::to_string(white) + "</td><td>" +
                      std::to_string(black) + "</td></tr>";
    return out;
}

std::string moveNumberLabel(int ply) {
    const int number = (ply + 1) / 2;
    return std::to_string(number) + (ply % 2 == 1 ? "." : "...");
}

int moveNumberAt(const ParsedGame& game, int ply) {
    if (ply >= 1 && static_cast<std::size_t>(ply) <= game.plies.size()) {
        return game.plies[static_cast<std::size_t>(ply - 1)].moveNumber;
    }
    return 1;
}

std::string playerRow(const std::string& name, const std::string& rating, bool white, bool active) {
    std::string out = "<div class=\"player-row" + std::string(active ? " active" : "") + "\">";
    out += "<span class=\"player-name\"><span class=\"player-dot " + std::string(white ? "white" : "black") +
           "\"></span><b>" + htmlEscape(orUnknown(name)) + "</b></span>";
    out += "<span class=\"player-rating\">" + (rating.empty() ? std::string("&nbsp;") : htmlEscape(rating)) +
           "</span></div>";
    return out;
}

std::string settingsOptions(const std::vector<int>& values, int selected) {
    std::string out;
    for (int value : values) {
        out += "<option value=\"" + std::to_string(value) + "\"" +
               (value == selected ? " selected" : "") + ">" + std::to_string(value) + "</option>";
    }
    return out;
}

std::string importForm() {
    return R"HTML(<div class="import-block">
  <h3>Paste PGN</h3>
  <p>Copy a game from any chess site or a scoresheet. Headers are optional.</p>
  <form method="post" action="/import">
    <label class="field"><span class="sr-only">PGN text</span>
      <textarea class="input" name="pgn" required spellcheck="false"
        placeholder="[Event &quot;Club Championship&quot;]
1. e4 e5 2. Nf3 Nc6 3. Bb5 a6"></textarea>
    </label>
    <button class="btn primary" type="submit">Review this game</button>
  </form>
</div>
<div class="import-block">
  <h3>Import a .pgn file</h3>
  <p>The file is read on this device. Nothing is uploaded.</p>
  <form method="post" action="/upload" enctype="multipart/form-data">
    <label class="field"><span class="sr-only">PGN file</span>
      <input class="input" type="file" name="file" accept=".pgn,application/x-chess-pgn,text/plain" required>
    </label>
    <button class="btn" type="submit">Import file</button>
  </form>
</div>)HTML";
}

}  // namespace

std::string pieceSprite() {
    return R"SVG(<svg class="sprite" xmlns="http://www.w3.org/2000/svg" aria-hidden="true" focusable="false">
<symbol id="pi-p" viewBox="0 0 45 45"><g fill="currentColor" stroke="var(--pc-stroke)" stroke-width="1.5" stroke-linejoin="round" stroke-linecap="round">
<circle cx="22.5" cy="12.8" r="5.6"/>
<path d="M17.6 18.8h9.8l-1.1 3.4c2.7 1.7 4.4 4.4 4.4 7.5 0 2.7-1 5-2.8 6.7H17.1c-1.8-1.7-2.8-4-2.8-6.7 0-3.1 1.7-5.8 4.4-7.5Z"/>
<path d="M14.6 35.6h15.8l1.6 4.4H13Z"/></g></symbol>
<symbol id="pi-r" viewBox="0 0 45 45"><g fill="currentColor" stroke="var(--pc-stroke)" stroke-width="1.5" stroke-linejoin="round" stroke-linecap="round">
<path d="M12.4 11.4h5v3.8h4.2v-3.8h5.2v3.8h4.2v-3.8h5v9l-3.2 3v9.4H15.6v-9.4l-3.2-3Z"/>
<path d="M15 33.6h15l1.6 4.4H13.4Z"/></g></symbol>
<symbol id="pi-n" viewBox="0 0 45 45"><g fill="currentColor" stroke="var(--pc-stroke)" stroke-width="1.5" stroke-linejoin="round" stroke-linecap="round">
<path d="M14.4 34c-.2-6 1.2-10.4 4.4-13.6 1.9-1.9 3.6-4 5.2-6.4l-3.4 1c-1.8.5-3.1-.2-3.8-1.7-.6-1.4-.4-2.9.6-4.3C19.2 6 22 4 25.6 2.9c.9-.3 1.8-.4 2.7-.4 3.5 0 6.1 1.6 7.7 4.6 1.4 2.5 2 5.8 2 9.6 0 6 .3 11.7.9 17.3Z"/>
<path d="M19.8 22.4c1.7-1.1 3.6-1.5 5.7-1.3"/>
<circle class="eye" cx="25.8" cy="14.6" r="1.15" fill="var(--eye)" stroke="none"/>
<path d="M14.2 33.4h17.4l1.7 4.6H12.6Z"/></g></symbol>
<symbol id="pi-b" viewBox="0 0 45 45"><g fill="currentColor" stroke="var(--pc-stroke)" stroke-width="1.5" stroke-linejoin="round" stroke-linecap="round">
<circle cx="22.5" cy="8" r="2.2"/>
<path d="M22.5 10.2c5.2 4.2 8.1 7.9 8.1 11.7 0 4.5-3.6 8-8.1 8s-8.1-3.5-8.1-8c0-3.8 2.9-7.5 8.1-11.7Z"/>
<path d="M19 21.6h7"/>
<path d="M15 30.6h15l1.6 4.4H13.4Z"/></g></symbol>
<symbol id="pi-q" viewBox="0 0 45 45"><g fill="currentColor" stroke="var(--pc-stroke)" stroke-width="1.5" stroke-linejoin="round" stroke-linecap="round">
<path d="M11.8 15.4 15 27h15l3.2-11.6-5.2 4.6-2.9-8.2-2.6 8.4-2.6-8.4-2.9 8.2Z"/>
<circle cx="11.6" cy="14.2" r="1.8"/><circle cx="17" cy="11.6" r="1.5"/><circle cx="22.5" cy="14" r="1.6"/>
<circle cx="28" cy="11.6" r="1.5"/><circle cx="33.4" cy="14.2" r="1.8"/>
<path d="M15.2 29h14.6l1.5 4.4H13.7Z"/></g></symbol>
<symbol id="pi-k" viewBox="0 0 45 45"><g fill="currentColor" stroke="var(--pc-stroke)" stroke-width="1.5" stroke-linejoin="round" stroke-linecap="round">
<path d="M22.5 6.6v7.2M19.1 10.2h6.8" stroke-width="2.6"/>
<path d="M13.6 18.4h17.8l2.3 10.4H11.3Z"/>
<path d="M15.4 29.4h14.2l1.4 4.2H14Z"/></g></symbol>
</svg>)SVG";
}

std::string renderHome(const PageChrome& chrome, const std::vector<RecentGame>& recents,
                       const AppSettings& settings, const std::string& flash, const std::string& error) {
    (void)settings;
    std::string content;
    content += "<section class=\"hero\"><h1>Review your games properly</h1>";
    content += "<p>Import a PGN and WeChess analyses every move with Stockfish, classifies it, and "
               "explains the important moments. No account, no upload, no JavaScript.</p></section>";
    content += noticeBlock(flash, error);

    content += "<div class=\"home-grid\">";
    content += "<section class=\"panel\"><div class=\"panel-head\"><h2>New review</h2>";
    content += "<span class=\"panel-note\">";
    content += chrome.engineAvailable ? "Stockfish ready" : "Stockfish not found - see Settings";
    content += "</span></div><div class=\"import-tabs\">" + importForm() + "</div>";
    content += "<p class=\"hint\" style=\"margin-top:10px\">Games are stored on this device only. "
               "Analysis quality depends on the depth configured in Settings (currently depth " +
               std::to_string(settings.depth) + ").</p></section>";

    content += "<section class=\"panel\"><div class=\"panel-head\"><h2>Recent games</h2>";
    content += "<span class=\"panel-note\">" + std::to_string(recents.size()) + "</span></div>";
    if (recents.empty()) {
        content += "<p class=\"empty\">No games yet. Import a PGN to start your first review.</p>";
    } else {
        content += "<ul class=\"recent-list\">";
        for (const RecentGame& game : recents) {
            content += "<li class=\"recent-item\"><a class=\"recent-link\" href=\"/game/" + game.id + "\">";
            content += "<span class=\"recent-players\">" + htmlEscape(game.white) + " &ndash; " +
                       htmlEscape(game.black) + "</span>";
            content += "<span class=\"recent-result\">" + htmlEscape(game.result) + "</span>";
            content += "<span class=\"recent-meta\">";
            std::vector<std::string> meta;
            if (!game.event.empty() && game.event != "?") meta.push_back(game.event);
            if (!game.date.empty() && game.date != "????.??.??") meta.push_back(game.date);
            if (!game.opening.empty()) meta.push_back(game.opening);
            meta.push_back(std::to_string((game.plies + 1) / 2) + " moves");
            for (std::size_t i = 0; i < meta.size(); ++i) {
                if (i) content += " &middot; ";
                content += htmlEscape(meta[i]);
            }
            content += "</span></a></li>";
        }
        content += "</ul>";
    }
    content += "</section></div>";

    content += "<section class=\"panel\" style=\"margin-top:12px\"><div class=\"panel-head\">"
               "<h2>How the review works</h2></div><div class=\"howto\">";
    content += "<div><strong>Stockfish measures</strong><span>Every position is searched at the configured "
               "depth, with a second deeper pass for critical moments.</span></div>";
    content += "<div><strong>WeChess classifies</strong><span>Moves are graded from win-probability loss, "
               "only-move detection, book knowledge and material sacrificed.</span></div>";
    content += "<div><strong>Gemini explains</strong><span>Explanations are written from the engine facts "
               "only, through a proxy. The key never ships in the app.</span></div>";
    content += "</div></section>";
    return shell("WeChess - game review", content, "games");
}

std::string renderReview(const PageChrome& chrome, const GameSession& session, int ply,
                         const std::string& flash, const std::string& error) {
    const ParsedGame& game = session.game();
    const GameReview review = session.review();
    const int totalPlies = static_cast<int>(game.plies.size());
    const bool hasReview = !review.moves.empty();
    ply = std::clamp(ply, 0, totalPlies);

    const std::string id = session.id();
    const std::string fen = game.fenAtPly(ply);
    const PlyInfo* current = game.ply(ply);
    const MoveReview* currentReview = review.at(ply);

    std::string lastUci, playedUci, bestUci;
    bool check = false;
    if (current != nullptr) {
        lastUci = current->uci;
        playedUci = current->uci;
        check = current->isCheck;
        if (currentReview != nullptr) bestUci = currentReview->bestMoveUci;
    }
    const bool whiteToMove = parseFenPlacement(fen).whiteToMove;

    Score evalForBar;
    if (hasReview) {
        if (ply == 0) evalForBar = review.moves.front().before;
        else evalForBar = review.moves[static_cast<std::size_t>(ply - 1)].after;
    }

    std::string content;
    content += noticeBlock(flash, error);
    content += "<div class=\"game-head\"><div><div class=\"game-title\">" +
               playerLabel(orUnknown(game.meta.white), orUnknown(game.meta.black)) + "</div>";
    content += "<div class=\"game-sub\">" + htmlEscape(orUnknown(game.meta.event)) + " &middot; " +
               htmlEscape(orUnknown(game.meta.date));
    if (!review.openingName.empty()) {
        content += " &middot; " + htmlEscape(review.openingName);
        if (!review.eco.empty()) content += " (" + htmlEscape(review.eco) + ")";
    }
    content += "</div></div><span class=\"result-pill\">" + htmlEscape(game.meta.result) + "</span></div>";

    content += "<div class=\"review\"><section class=\"board-col\">";
    content += "<div class=\"players\">";
    content += playerRow(game.meta.black, game.meta.blackElo, false, !whiteToMove && ply < totalPlies);
    content += playerRow(game.meta.white, game.meta.whiteElo, true, whiteToMove && ply < totalPlies);
    content += "</div>";

    content += "<div class=\"board-row\">";
    content += evalBar(evalForBar);
    content += boardHtml(fen, lastUci, check, playedUci, bestUci);
    content += "</div>";

    const std::string base = "/game/" + id;
    content += "<nav class=\"nav-controls\" aria-label=\"Game navigation\">";
    content += "<a class=\"navbtn" + std::string(ply <= 0 ? " disabled" : "") + "\" href=\"" + base +
               "?ply=0#board\" aria-label=\"First move\">&laquo;</a>";
    content += "<a class=\"navbtn" + std::string(ply <= 0 ? " disabled" : "") + "\" href=\"" + base +
               "?ply=" + std::to_string(std::max(0, ply - 1)) + "#board\" aria-label=\"Previous move\">&lsaquo;</a>";
    content += "<span class=\"ply-readout\">" + std::to_string(ply) + " / " + std::to_string(totalPlies) +
               "</span>";
    content += "<a class=\"navbtn" + std::string(ply >= totalPlies ? " disabled" : "") + "\" href=\"" + base +
               "?ply=" + std::to_string(std::min(totalPlies, ply + 1)) + "#board\" aria-label=\"Next move\">&rsaquo;</a>";
    content += "<a class=\"navbtn" + std::string(ply >= totalPlies ? " disabled" : "") + "\" href=\"" + base +
               "?ply=" + std::to_string(totalPlies) + "#board\" aria-label=\"Last move\">&raquo;</a>";
    content += "</nav>";

    content += "<div class=\"turn-line\"><span>";
    if (ply >= totalPlies && !game.plies.empty() && game.plies.back().isCheckmate) {
        content += "Checkmate";
    } else {
        content += whiteToMove ? "White to move" : "Black to move";
    }
    content += "</span><span>Move " + std::to_string(moveNumberAt(game, ply)) + "</span></div>";
    content += "</section>";

    // ---- side column -------------------------------------------------------
    content += "<div class=\"side-col\">";

    // Selected move panel.
    content += "<section class=\"panel\" id=\"review\"><div class=\"panel-head\"><h2>Selected move</h2>";
    if (hasReview) content += "<span class=\"panel-note\">depth " + std::to_string(review.engineDepth) + "</span>";
    content += "</div>";

    if (!hasReview) {
        const std::string analysisError = session.analysisError();
        if (!analysisError.empty()) {
            content += "<div class=\"notice error\">" + htmlEscape(analysisError) + "</div>";
            content += "<form method=\"post\" action=\"" + base + "/analyse\"><button class=\"btn primary\" "
                       "type=\"submit\">Try analysis again</button></form>";
        } else {
            content += "<p class=\"muted\">This game has not been analysed yet.</p>";
        }
    } else if (ply == 0) {
        content += "<p class=\"muted\">Starting position. Select a move from the list to see the review.</p>";
    } else if (currentReview == nullptr || current == nullptr) {
        content += "<p class=\"muted\">No review data for this move.</p>";
    } else {
        const MoveReview& move = *currentReview;
        content += "<div class=\"mv-head\">" + classificationChip(move.cls, true);
        content += "<span class=\"mv-title\">" + htmlEscape(move.playedSan) + "</span>";
        content += "<span class=\"mv-actor\">" + moveNumberLabel(move.ply) + " " +
                   (current->white ? "White" : "Black") + "</span></div>";
        content += "<p class=\"mv-summary\">" + htmlEscape(move.summary) + "</p>";

        content += "<dl class=\"facts\">";
        content += "<div><dt>Evaluation before</dt><dd>" + htmlEscape(formatScore(move.before)) + "</dd></div>";
        content += "<div><dt>After the move</dt><dd>" + htmlEscape(formatScore(move.after)) + "</dd></div>";
        content += "<div><dt>Centipawn loss</dt><dd>" +
                   std::string(move.cpLoss >= 2000 ? "2000+" : std::to_string(move.cpLoss)) + "</dd></div>";
        content += "<div><dt>Engine's best</dt><dd>" +
                   htmlEscape(move.bestMoveSan.empty() ? "-" : move.bestMoveSan) + "</dd></div>";
        if (move.sacrificeCp > 0) {
            char sacrifice[48];
            std::snprintf(sacrifice, sizeof(sacrifice), "%.2f pawns en prise", move.sacrificeCp / 100.0);
            content += "<div class=\"wide\"><dt>Material</dt><dd>" + std::string(sacrifice) + "</dd></div>";
        }
        content += "</dl>";

        if (!move.pvSan.empty()) {
            content += "<div class=\"pv\"><strong class=\"muted-2\">Engine line:</strong> " +
                       htmlEscape(move.pvSan) + "</div>";
        }

        content += "<details class=\"adv\"><summary>Engine details</summary><div class=\"adv-body\">";
        content += "<dl class=\"kv\"><dt>Engine</dt><dd>" + htmlEscape(review.engineName) + "</dd>";
        content += "<dt>Depth</dt><dd>" + std::to_string(move.depth) + "</dd>";
        content += "<dt>Evaluation (White)</dt><dd>" + htmlEscape(formatScore(move.before)) + " &rarr; " +
                   htmlEscape(formatScore(move.after)) + "</dd>";
        content += "<dt>Book move</dt><dd>" + std::string(move.isBook ? "yes" : "no") + "</dd>";
        if (move.isBook && !move.bookName.empty()) {
            content += "<dt>Opening line</dt><dd>" + htmlEscape(move.bookName) + "</dd>";
        }
        content += "<dt>Forced move</dt><dd>" + std::string(move.forced ? "yes" : "no") + "</dd>";
        content += "<dt>FEN before</dt><dd class=\"mono small\">" + htmlEscape(current->fenBefore) + "</dd>";
        content += "<dt>FEN after</dt><dd class=\"mono small\">" + htmlEscape(current->fenAfter) + "</dd>";
        content += "</dl></div></details>";

        // ---- Gemini ---------------------------------------------------------
        const ExplanationEntry explanation = session.explanation(ply);
        content += "<section class=\"ai\"><div class=\"ai-head\"><span class=\"ai-label\">Explanation</span>";
        if (explanation.state == "ready") content += "<span class=\"panel-note\">Gemini</span>";
        content += "</div>";

        if (explanation.state == "ready") {
            content += "<div class=\"explain\">";
            std::istringstream paragraphs(explanation.text);
            std::string paragraph;
            while (std::getline(paragraphs, paragraph)) {
                if (!paragraph.empty()) content += "<p>" + htmlEscape(paragraph) + "</p>";
            }
            content += "</div>";
            content += "<form method=\"post\" action=\"" + base + "/explain\" style=\"margin-top:8px\">"
                       "<input type=\"hidden\" name=\"ply\" value=\"" + std::to_string(ply) + "\">"
                       "<button class=\"btn small ghost\" type=\"submit\">Regenerate</button></form>";
        } else if (explanation.state == "pending") {
            content += "<p class=\"muted\">Generating an explanation&hellip; <a href=\"/explain-wait/" + id +
                       "?ply=" + std::to_string(ply) + "\">check progress</a></p>";
        } else if (explanation.state == "failed") {
            content += "<div class=\"notice error\">" + htmlEscape(explanation.error) + "</div>";
            content += "<form method=\"post\" action=\"" + base + "/explain\">"
                       "<input type=\"hidden\" name=\"ply\" value=\"" + std::to_string(ply) + "\">"
                       "<button class=\"btn small\" type=\"submit\">Try again</button></form>";
        } else if (!chrome.geminiConfigured) {
            content += "<p class=\"muted-2 small\">Gemini explanations are off. Add the proxy URL and "
                       "token in <a href=\"/settings\">Settings</a>; Stockfish review works without it.</p>";
        } else {
            content += "<form method=\"post\" action=\"" + base + "/explain\">"
                       "<input type=\"hidden\" name=\"ply\" value=\"" + std::to_string(ply) + "\">"
                       "<button class=\"btn small primary\" type=\"submit\">Explain this move</button>"
                       "</form>";
            content += "<p class=\"muted-2 small\" style=\"margin-top:6px\">Uses only the engine data "
                       "shown above.</p>";
        }
        content += "</section>";
    }

    content += "<form method=\"post\" action=\"" + base + "/analyse\" style=\"margin-top:10px\">"
               "<button class=\"btn small ghost\" type=\"submit\">Re-run analysis</button></form>";
    content += "</section>";

    // Move list.
    content += "<section class=\"panel\" id=\"movelist\"><div class=\"panel-head\"><h2>Moves</h2>";
    if (hasReview) content += "<span class=\"panel-note\">" + std::to_string(totalPlies) + " half-moves</span>";
    content += "</div>";

    if (!hasReview) {
        content += "<div class=\"movelist\">";
        for (int i = 0; i < totalPlies; i += 2) {
            const PlyInfo& whiteMove = game.plies[static_cast<std::size_t>(i)];
            content += "<div class=\"mvrow\"><span class=\"mvnum\">" + std::to_string(whiteMove.moveNumber) +
                       ".</span><span class=\"mv\"><span class=\"san\">" + htmlEscape(whiteMove.san) +
                       "</span></span>";
            if (i + 1 < totalPlies) {
                content += "<span class=\"mv\"><span class=\"san\">" +
                           htmlEscape(game.plies[static_cast<std::size_t>(i + 1)].san) + "</span></span>";
            } else {
                content += "<span></span>";
            }
            content += "</div>";
        }
        content += "</div>";
    } else {
        content += "<div class=\"movelist\">";
        for (std::size_t i = 0; i < review.moves.size(); i += 2) {
            const MoveReview& whiteMove = review.moves[i];
            content += "<div class=\"mvrow\"><span class=\"mvnum\">" +
                       std::to_string((whiteMove.ply + 1) / 2) + ".</span>";
            content += moveLink(id, whiteMove, ply == whiteMove.ply);
            if (i + 1 < review.moves.size()) {
                const MoveReview& blackMove = review.moves[i + 1];
                content += moveLink(id, blackMove, ply == blackMove.ply);
            } else {
                content += "<span></span>";
            }
            content += "</div>";
        }
        content += "</div>";
    }
    content += "</section>";

    // Game report.
    content += "<section class=\"panel\" id=\"report\"><div class=\"panel-head\"><h2>Game report</h2>";
    if (hasReview) {
        content += "<span class=\"panel-note\">" + std::string(chrome.engineAvailable ? "Stockfish" : "engine off") +
                   "</span>";
    }
    content += "</div>";

    if (!hasReview) {
        content += "<p class=\"muted\">Statistics appear once the analysis finishes.</p>";
    } else {
        content += "<div class=\"acc-row\">";
        content += accuracyCard(review.white, "White");
        content += accuracyCard(review.black, "Black");
        content += "</div>";

        content += "<table class=\"stat-table\"><thead><tr><th>Move type</th><th>White</th><th>Black</th>"
                   "</tr></thead><tbody>";
        content += statRow("brilliant", "Brilliant (!!)", review.white.brilliant, review.black.brilliant);
        content += statRow("great", "Great (!)", review.white.great, review.black.great);
        content += statRow("best", "Best", review.white.best, review.black.best);
        content += statRow("excellent", "Excellent", review.white.excellent, review.black.excellent);
        content += statRow("good", "Good", review.white.good, review.black.good);
        content += statRow("book", "Book", review.white.book, review.black.book);
        content += statRow("inaccuracy", "Inaccuracy (?!", review.white.inaccuracy, review.black.inaccuracy);
        content += statRow("mistake", "Mistake (?)", review.white.mistake, review.black.mistake);
        content += statRow("blunder", "Blunder (??)", review.white.blunder, review.black.blunder);
        content += "</tbody></table>";

        content += "<hr class=\"sep\"><h3 class=\"section-title\">Key moments</h3>";
        if (review.highlights.empty()) {
            content += "<p class=\"muted small\">No standout moments were found.</p>";
        } else {
            content += "<ul class=\"moments\">";
            for (const Highlight& highlight : review.highlights) {
                content += "<li class=\"" + htmlEscape(highlight.kind) + "\">";
                if (highlight.ply > 0) {
                    content += "<a href=\"/game/" + id + "?ply=" + std::to_string(highlight.ply) + "#board\">" +
                               htmlEscape(highlight.title) + "</a>";
                } else {
                    content += "<span class=\"muted small\">" + htmlEscape(highlight.title) + "</span>";
                }
                content += "<span class=\"mdetail\">" + htmlEscape(highlight.detail) + "</span></li>";
            }
            content += "</ul>";
        }

        content += "<details class=\"adv\"><summary>Accuracy by phase</summary><div class=\"adv-body\">";
        content += "<table class=\"stat-table\"><tbody>";
        content += "<tr><td>Opening</td><td>" + std::to_string(static_cast<int>(review.white.openingAccuracy)) +
                   "%</td><td>" + std::to_string(static_cast<int>(review.black.openingAccuracy)) + "%</td></tr>";
        content += "<tr><td>Middlegame</td><td>" +
                   std::to_string(static_cast<int>(review.white.middlegameAccuracy)) + "%</td><td>" +
                   std::to_string(static_cast<int>(review.black.middlegameAccuracy)) + "%</td></tr>";
        content += "<tr><td>Endgame</td><td>" + std::to_string(static_cast<int>(review.white.endgameAccuracy)) +
                   "%</td><td>" + std::to_string(static_cast<int>(review.black.endgameAccuracy)) + "%</td></tr>";
        content += "</tbody></table></div></details>";
    }

    content += "<form method=\"post\" action=\"" + base + "/delete\" style=\"margin-top:10px\">"
               "<button class=\"btn small ghost\" type=\"submit\">Delete this game</button></form>";
    content += "</section>";

    content += "</div></div>";
    content += pieceSprite();
    return shell("WeChess - " + orUnknown(game.meta.white) + " vs " + orUnknown(game.meta.black), content, "games");
}

std::string renderAnalysisProgress(const PageChrome& chrome, const GameSession& session) {
    const ParsedGame& game = session.game();
    const int totalPlies = static_cast<int>(game.plies.size());
    const int percent = session.progressPercent();
    const bool done = session.analysisDone();
    const std::string analysisError = session.analysisError();
    const std::string id = session.id();

    std::string content;
    content += "<div class=\"game-head\"><div><div class=\"game-title\">" +
               playerLabel(orUnknown(game.meta.white), orUnknown(game.meta.black)) + "</div>";
    content += "<div class=\"game-sub\">" + htmlEscape(orUnknown(game.meta.event)) + " &middot; " +
               std::to_string(totalPlies) + " half-moves</div></div>";
    content += "<span class=\"result-pill\">" + htmlEscape(game.meta.result) + "</span></div>";

    content += "<section class=\"panel analyze-box\">";
    content += "<div class=\"panel-head\"><h2>Analysis</h2>";
    content += "<span class=\"panel-note\">";
    content += chrome.engineAvailable ? "Stockfish" : "engine unavailable";
    content += "</span></div>";

    if (!analysisError.empty()) {
        content += "<div class=\"notice error\">" + htmlEscape(analysisError) + "</div>";
        content += "<div class=\"row\"><form method=\"post\" action=\"/game/" + id + "/analyse\">"
                   "<button class=\"btn primary\" type=\"submit\">Retry analysis</button></form>";
        content += "<a class=\"btn ghost\" href=\"/game/" + id + "?ply=0\">Open without analysis</a></div>";
        content += "<p class=\"hint\" style=\"margin-top:10px\">If Stockfish is missing, set its path in "
                   "<a href=\"/settings\">Settings</a>. The board and move list work without it.</p>";
    } else {
        content += "<div class=\"progress-head\"><span class=\"muted\">" +
                   std::string(done ? "Finishing" : "Analysing game") + "</span><span class=\"live\">" +
                   std::to_string(percent) + "%</span></div>";
        content += "<div class=\"bar\"><span style=\"width:" + std::to_string(percent) + "%\"></span></div>";
        content += "<div class=\"progress-status\">" + htmlEscape(session.progressStatus()) + "</div>";
        content += "<p class=\"hint\" style=\"margin-top:10px\">This page refreshes automatically. Deep "
                   "analysis of long games can take a minute or two.</p>";
        content += "<form method=\"post\" action=\"/game/" + id + "/cancel\" style=\"margin-top:10px\">"
                   "<button class=\"btn small ghost\" type=\"submit\">Stop analysis</button></form>";
    }
    content += "</section>";

    const std::string headExtra = analysisError.empty()
                                      ? "<meta http-equiv=\"refresh\" content=\"1;url=/analysis/" + id + "\">"
                                      : std::string();
    return shell("WeChess - analysing", content, "games", headExtra);
}

std::string renderExplainWait(const PageChrome& chrome, const GameSession& session, int ply) {
    (void)chrome;
    const ParsedGame& game = session.game();
    const std::string id = session.id();
    const PlyInfo* move = game.ply(ply);

    std::string content;
    content += "<section class=\"panel analyze-box\"><div class=\"panel-head\"><h2>Explanation</h2>"
               "<span class=\"panel-note\">Gemini</span></div>";
    if (move != nullptr) {
        content += "<p class=\"muted\">Writing an explanation for " +
                   std::string(move->white ? "White" : "Black") + "'s move " +
                   std::to_string(move->moveNumber) + "... " + htmlEscape(move->san) + ".</p>";
    } else {
        content += "<p class=\"muted\">Writing an explanation...</p>";
    }
    content += "<div class=\"bar\"><span style=\"width:35%\"></span></div>";
    content += "<p class=\"hint\" style=\"margin-top:10px\">This page refreshes automatically.</p>";
    content += "<a class=\"btn small ghost\" href=\"/game/" + id + "?ply=" + std::to_string(ply) +
               "#review\" style=\"margin-top:10px\">Back to the board</a>";
    content += "</section>";

    const std::string headExtra = "<meta http-equiv=\"refresh\" content=\"1;url=/explain-wait/" + id +
                                  "?ply=" + std::to_string(ply) + "\">";
    return shell("WeChess - explanation", content, "games", headExtra);
}

std::string renderSettings(const PageChrome& chrome, const AppSettings& settings,
                           const std::string& resolvedStockfish, const std::string& flash,
                           const std::string& error) {
    std::string content;
    content += "<h1 style=\"margin-bottom:12px\">Settings</h1>";
    content += noticeBlock(flash, error);

    content += "<form method=\"post\" action=\"/settings\">";
    content += "<div class=\"settings-grid\">";

    content += "<section class=\"panel\"><div class=\"panel-head\"><h2>Analysis</h2></div>";
    content += "<label class=\"field\"><span>Engine depth for every position</span>"
               "<select class=\"input\" name=\"depth\">" +
               settingsOptions({10, 12, 14, 16, 18, 20}, settings.depth) + "</select></label>";
    content += "<label class=\"field\"><span>Engine lines considered (MultiPV)</span>"
               "<select class=\"input\" name=\"multipv\">" +
               settingsOptions({1, 2, 3, 4, 5}, settings.multipv) + "</select></label>";
    content += "<label class=\"field\"><span>Deeper pass for critical positions</span>"
               "<select class=\"input\" name=\"critical_depth\">" +
               settingsOptions({16, 18, 20, 22, 24}, settings.criticalDepth) + "</select></label>";
    content += "<label class=\"field\" style=\"display:flex;gap:8px;align-items:center\">"
               "<input type=\"checkbox\" name=\"deep_pass\" value=\"1\"" +
               std::string(settings.deepPass ? " checked" : "") +
               "><span style=\"margin:0\">Search critical positions more deeply</span></label>";
    content += "<label class=\"field\"><span>Stockfish binary (leave empty to auto-detect)</span>"
               "<input class=\"input mono\" type=\"text\" name=\"stockfish_path\" value=\"" +
               htmlEscape(settings.stockfishPath) + "\" placeholder=\"/path/to/stockfish\"></label>";
    content += "<p class=\"hint\">Currently using: <span class=\"mono\">" +
               htmlEscape(resolvedStockfish.empty() ? "not found" : resolvedStockfish) + "</span></p>";
    content += "</section>";

    content += "<section class=\"panel\"><div class=\"panel-head\"><h2>Gemini explanations</h2></div>";
    content += "<p class=\"hint\" style=\"margin-bottom:10px\">The Gemini API key lives on your proxy "
               "server, never in this app. Configure the proxy (see README) and paste its URL and the "
               "application token here.</p>";
    content += "<label class=\"field\" style=\"display:flex;gap:8px;align-items:center\">"
               "<input type=\"checkbox\" name=\"gemini_enabled\" value=\"1\"" +
               std::string(settings.geminiEnabled ? " checked" : "") +
               "><span style=\"margin:0\">Enable Gemini explanations</span></label>";
    content += "<label class=\"field\"><span>Proxy URL</span>"
               "<input class=\"input mono\" type=\"url\" name=\"gemini_proxy_url\" value=\"" +
               htmlEscape(settings.geminiProxyUrl) + "\" placeholder=\"https://wechess-proxy.example.com\">"
               "</label>";
    content += "<label class=\"field\"><span>Application token</span>"
               "<input class=\"input mono\" type=\"password\" name=\"gemini_token\" value=\"" +
               htmlEscape(settings.geminiToken) + "\" autocomplete=\"off\"></label>";
    content += "<label class=\"field\"><span>Model</span>"
               "<input class=\"input mono\" type=\"text\" name=\"gemini_model\" value=\"" +
               htmlEscape(settings.geminiModel) + "\"></label>";
    content += "<p class=\"hint\">HTTPS client in this build: " +
               std::string(settings.geminiEnabled && chrome.geminiConfigured ? "configured" : "check Settings") +
               "</p>";
    content += "</section></div>";
    content += "<div class=\"row\" style=\"margin-top:12px\"><button class=\"btn primary\" type=\"submit\">"
               "Save settings</button><a class=\"btn ghost\" href=\"/\">Back</a></div></form>";

    content += "<section class=\"panel\" style=\"margin-top:12px\"><div class=\"panel-head\">"
               "<h2>About</h2></div><dl class=\"kv\">";
    content += "<dt>Version</dt><dd>" + htmlEscape(chrome.appVersion) + "</dd>";
    content += "<dt>Engine</dt><dd>" + htmlEscape(chrome.stockfishName.empty() ? "Stockfish" : chrome.stockfishName) +
               "</dd>";
    content += "<dt>Frontend</dt><dd>HTML5 and CSS3 only; JavaScript is disabled in the WebView.</dd>";
    content += "<dt>Opening book</dt><dd>Lichess chess-openings database (CC0-1.0)</dd>";
    content += "</dl></section>";
    return shell("WeChess - settings", content, "settings");
}

std::string renderNotFound() {
    std::string content = "<section class=\"panel\" style=\"max-width:560px;margin:40px auto\">"
                          "<h1 style=\"font-size:20px;margin-bottom:8px\">Game not found</h1>"
                          "<p class=\"muted\">That review does not exist on this device.</p>"
                          "<p style=\"margin-top:12px\"><a class=\"btn\" href=\"/\">Back to games</a></p>"
                          "</section>";
    return shell("WeChess - not found", content, "games");
}

}  // namespace wechess
