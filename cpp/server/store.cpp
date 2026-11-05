#include "store.hpp"

#include <sys/stat.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>

namespace wechess {
namespace {

bool ensureDirectory(const std::string& path) {
    if (path.empty()) return false;
    std::string current;
    for (char c : path) {
        current.push_back(c);
        if (c == '/') {
            ::mkdir(current.c_str(), 0700);
        }
    }
    ::mkdir(path.c_str(), 0700);
    struct stat info {};
    return ::stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

// Tabs and newlines would corrupt the line-based index.
std::string sanitizeField(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        if (c == '\t' || c == '\n' || c == '\r') out.push_back(' ');
        else out.push_back(c);
    }
    if (out.size() > 120) out.resize(120);
    return out;
}

std::string trim(const std::string& value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return value.substr(begin, end - begin);
}

bool writeFileAtomically(const std::string& path, const std::string& contents) {
    const std::string temporary = path + ".tmp";
    {
        std::ofstream out(temporary, std::ios::trunc);
        if (!out) return false;
        out << contents;
        if (!out.good()) return false;
    }
    return std::rename(temporary.c_str(), path.c_str()) == 0;
}

}  // namespace

std::string Store::newGameId() {
    static std::mt19937_64 generator(std::random_device{}());
    const unsigned long long value = generator();
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%012llx", value & 0xffffffffffffULL);
    return buffer;
}

bool Store::validGameId(const std::string& id) {
    if (id.empty() || id.size() > 32) return false;
    for (char c : id) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

std::string Store::pathFor(const std::string& name) const { return root_ + "/" + name; }

bool Store::open(const std::string& root, std::string& error) {
    root_ = root;
    if (!ensureDirectory(root_)) {
        error = "Could not create the data directory: " + root;
        return false;
    }
    if (!ensureDirectory(pathFor("games"))) {
        error = "Could not create the games directory.";
        return false;
    }
    if (!ensureDirectory(pathFor("cache"))) {
        error = "Could not create the cache directory.";
        return false;
    }

    cache_.reset(new EvalCache());
    cache_->open(pathFor("cache/evals.tsv"));

    std::lock_guard<std::mutex> lock(mutex_);
    loadLocked();
    return true;
}

void Store::loadLocked() {
    recents_.clear();
    settings_ = AppSettings{};

    // settings.conf
    std::ifstream settingsFile(pathFor("settings.conf"));
    std::string line;
    while (std::getline(settingsFile, line)) {
        if (line.empty() || line[0] == '#') continue;
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = trim(line.substr(0, equals));
        const std::string value = trim(line.substr(equals + 1));
        if (key == "depth") settings_.depth = std::max(4, std::min(30, std::atoi(value.c_str())));
        else if (key == "multipv") settings_.multipv = std::max(1, std::min(5, std::atoi(value.c_str())));
        else if (key == "critical_depth") settings_.criticalDepth = std::max(6, std::min(32, std::atoi(value.c_str())));
        else if (key == "deep_pass") settings_.deepPass = (value == "1" || value == "true");
        else if (key == "stockfish_path") settings_.stockfishPath = value;
        else if (key == "gemini_proxy_url") settings_.geminiProxyUrl = value;
        else if (key == "gemini_token") settings_.geminiToken = value;
        else if (key == "gemini_enabled") settings_.geminiEnabled = (value == "1" || value == "true");
        else if (key == "gemini_model") settings_.geminiModel = value;
    }

    // recent.tsv
    std::ifstream index(pathFor("recent.tsv"));
    while (std::getline(index, line)) {
        if (line.empty()) continue;
        std::vector<std::string> parts;
        std::string current;
        for (char c : line) {
            if (c == '\t') {
                parts.push_back(current);
                current.clear();
            } else {
                current.push_back(c);
            }
        }
        parts.push_back(current);
        if (parts.size() < 10) continue;
        RecentGame game;
        game.id = parts[0];
        if (!validGameId(game.id)) continue;
        game.created = std::atoll(parts[1].c_str());
        game.white = parts[2];
        game.black = parts[3];
        game.result = parts[4];
        game.event = parts[5];
        game.date = parts[6];
        game.eco = parts[7];
        game.opening = parts[8];
        game.plies = std::atoi(parts[9].c_str());
        recents_.push_back(std::move(game));
    }
    std::sort(recents_.begin(), recents_.end(),
              [](const RecentGame& a, const RecentGame& b) { return a.created > b.created; });
    if (recents_.size() > 50) recents_.resize(50);
}

AppSettings Store::settings() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return settings_;
}

void Store::saveSettings(const AppSettings& settings) {
    std::ostringstream out;
    out << "# WeChess settings (app-private)\n";
    out << "depth=" << settings.depth << "\n";
    out << "multipv=" << settings.multipv << "\n";
    out << "critical_depth=" << settings.criticalDepth << "\n";
    out << "deep_pass=" << (settings.deepPass ? 1 : 0) << "\n";
    out << "stockfish_path=" << settings.stockfishPath << "\n";
    out << "gemini_proxy_url=" << settings.geminiProxyUrl << "\n";
    out << "gemini_token=" << settings.geminiToken << "\n";
    out << "gemini_enabled=" << (settings.geminiEnabled ? 1 : 0) << "\n";
    out << "gemini_model=" << settings.geminiModel << "\n";

    std::lock_guard<std::mutex> lock(mutex_);
    settings_ = settings;
    writeFileAtomically(pathFor("settings.conf"), out.str());
}

std::vector<RecentGame> Store::recents() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return recents_;
}

void Store::addRecent(const RecentGame& game) {
    std::lock_guard<std::mutex> lock(mutex_);
    recents_.erase(std::remove_if(recents_.begin(), recents_.end(),
                                  [&](const RecentGame& other) { return other.id == game.id; }),
                   recents_.end());
    recents_.insert(recents_.begin(), game);
    if (recents_.size() > 50) recents_.resize(50);
    saveIndexLocked();
}

void Store::removeRecent(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    recents_.erase(std::remove_if(recents_.begin(), recents_.end(),
                                  [&](const RecentGame& other) { return other.id == id; }),
                   recents_.end());
    saveIndexLocked();
    std::remove(pathFor("games/" + id + ".pgn").c_str());
}

void Store::saveIndexLocked() const {
    std::ostringstream out;
    for (const RecentGame& game : recents_) {
        out << game.id << '\t' << game.created << '\t' << sanitizeField(game.white) << '\t'
            << sanitizeField(game.black) << '\t' << sanitizeField(game.result) << '\t'
            << sanitizeField(game.event) << '\t' << sanitizeField(game.date) << '\t'
            << sanitizeField(game.eco) << '\t' << sanitizeField(game.opening) << '\t' << game.plies
            << '\n';
    }
    writeFileAtomically(pathFor("recent.tsv"), out.str());
}

bool Store::writePgn(const std::string& id, const std::string& pgn) const {
    if (!validGameId(id)) return false;
    return writeFileAtomically(pathFor("games/" + id + ".pgn"), pgn);
}

std::string Store::readPgn(const std::string& id) const {
    if (!validGameId(id)) return std::string();
    std::ifstream input(pathFor("games/" + id + ".pgn"));
    if (!input) return std::string();
    std::ostringstream out;
    out << input.rdbuf();
    return out.str();
}

}  // namespace wechess
