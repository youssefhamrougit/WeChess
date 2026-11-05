#include "uci.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <mutex>
#include <poll.h>
#include <sstream>

#include "chess.hpp"

namespace wechess {
int pieceValueCp(char pieceUpper) {
    switch (pieceUpper) {
        case 'P': return 100;
        case 'N': return 320;
        case 'B': return 330;
        case 'R': return 500;
        case 'Q': return 900;
        case 'K': return 0;
        default: return 0;
    }
}

UciEngine::~UciEngine() { stop(); }

void UciEngine::killProcess() {
    if (pid_ > 0) {
        ::kill(pid_, SIGTERM);
        int status = 0;
        // Give the engine a moment to exit, then insist.
        for (int i = 0; i < 20; ++i) {
            const pid_t done = ::waitpid(pid_, &status, WNOHANG);
            if (done == pid_ || done < 0) break;
            ::usleep(10 * 1000);
        }
        if (::waitpid(pid_, &status, WNOHANG) == 0) {
            ::kill(pid_, SIGKILL);
            ::waitpid(pid_, &status, 0);
        }
        pid_ = -1;
    }
    if (inFd_ >= 0) {
        ::close(inFd_);
        inFd_ = -1;
    }
    if (outFd_ >= 0) {
        ::close(outFd_);
        outFd_ = -1;
    }
    readBuffer_.clear();
}

void UciEngine::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pid_ > 0 && outFd_ >= 0) {
        const std::string quit = "quit\n";
        (void)!::write(outFd_, quit.data(), quit.size());
    }
    killProcess();
}

bool UciEngine::writeLine(const std::string& line) {
    if (outFd_ < 0) {
        lastError_ = "Stockfish is not running.";
        return false;
    }
    std::string payload = line;
    payload.push_back('\n');
    std::size_t written = 0;
    while (written < payload.size()) {
        const ssize_t n = ::write(outFd_, payload.data() + written, payload.size() - written);
        if (n <= 0) {
            if (errno == EINTR) continue;
            lastError_ = std::string("Could not write to Stockfish: ") + std::strerror(errno);
            return false;
        }
        written += static_cast<std::size_t>(n);
    }
    return true;
}

bool UciEngine::readLine(std::string& line, int timeoutMs) {
    line.clear();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (true) {
        const std::size_t newline = readBuffer_.find('\n');
        if (newline != std::string::npos) {
            line = readBuffer_.substr(0, newline);
            readBuffer_.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return true;
        }
        if (inFd_ < 0) return false;

        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) return false;
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();

        struct pollfd pfd;
        pfd.fd = inFd_;
        pfd.events = POLLIN;
        pfd.revents = 0;
        const int ready = ::poll(&pfd, 1, static_cast<int>(remaining));
        if (ready < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (ready == 0) return false;

        char chunk[4096];
        const ssize_t n = ::read(inFd_, chunk, sizeof(chunk));
        if (n <= 0) return false;
        readBuffer_.append(chunk, static_cast<std::size_t>(n));
        if (readBuffer_.size() > (1u << 20)) {
            // A runaway engine is not something we can recover from safely.
            lastError_ = "Stockfish produced an unreasonably large output stream.";
            return false;
        }
    }
}

bool UciEngine::waitFor(const std::string& token, int timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    std::string line;
    while (true) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            lastError_ = "Stockfish did not respond in time.";
            return false;
        }
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        if (!readLine(line, static_cast<int>(remaining))) return false;
        if (line.rfind(token, 0) == 0) return true;
    }
}

void UciEngine::drain() {
    std::string line;
    // Non-blocking read of anything still buffered.
    while (inFd_ >= 0) {
        struct pollfd pfd;
        pfd.fd = inFd_;
        pfd.events = POLLIN;
        pfd.revents = 0;
        if (::poll(&pfd, 1, 0) <= 0) break;
        if (!readLine(line, 10)) break;
    }
}

bool UciEngine::start(const std::string& binaryPath, const EngineSettings& settings) {
    std::lock_guard<std::mutex> lock(mutex_);
    lastError_.clear();
    killProcess();
    settings_ = settings;
    binaryPath_ = binaryPath;

    if (binaryPath.empty()) {
        lastError_ = "No Stockfish binary is configured.";
        return false;
    }
    if (::access(binaryPath.c_str(), X_OK) != 0) {
        lastError_ = "Stockfish binary not found or not executable: " + binaryPath;
        return false;
    }

    int stdinPipe[2] = {-1, -1};
    int stdoutPipe[2] = {-1, -1};
    if (::pipe(stdinPipe) != 0 || ::pipe(stdoutPipe) != 0) {
        lastError_ = std::string("Could not create engine pipes: ") + std::strerror(errno);
        return false;
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        lastError_ = std::string("Could not start Stockfish: ") + std::strerror(errno);
        ::close(stdinPipe[0]);
        ::close(stdinPipe[1]);
        ::close(stdoutPipe[0]);
        ::close(stdoutPipe[1]);
        return false;
    }
    if (pid == 0) {
        // Child: wire the pipes to stdio and exec the engine.
        ::dup2(stdinPipe[0], STDIN_FILENO);
        ::dup2(stdoutPipe[1], STDOUT_FILENO);
        ::close(stdinPipe[0]);
        ::close(stdinPipe[1]);
        ::close(stdoutPipe[0]);
        ::close(stdoutPipe[1]);
        ::execl(binaryPath.c_str(), binaryPath.c_str(), static_cast<char*>(nullptr));
        ::_exit(127);
    }

    ::close(stdinPipe[0]);
    ::close(stdoutPipe[1]);
    outFd_ = stdinPipe[1];
    inFd_ = stdoutPipe[0];
    pid_ = static_cast<int>(pid);

    if (!writeLine("uci")) {
        killProcess();
        return false;
    }

    bool handshake = false;
    std::string line;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < deadline) {
        if (!readLine(line, 2000)) {
            if (!running()) break;
            continue;
        }
        if (line.rfind("id name", 0) == 0) {
            name_ = line.substr(8);
        } else if (line == "uciok") {
            handshake = true;
            break;
        }
    }
    if (!handshake) {
        lastError_ = "Stockfish did not complete the UCI handshake.";
        killProcess();
        return false;
    }

    applySettings(settings);
    drain();
    return true;
}

bool UciEngine::applySettings(const EngineSettings& settings) {
    settings_ = settings;
    if (!running()) return false;
    if (!writeLine("setoption name Threads value " + std::to_string(settings.threads))) return false;
    if (!writeLine("setoption name Hash value " + std::to_string(settings.hashMb))) return false;
    // Keep analysis reproducible: no random skill weakening, no ponder.
    if (!writeLine("setoption name MultiPV value " + std::to_string(settings.multipv))) return false;
    if (!writeLine("setoption name UCI_LimitStrength value false")) return false;
    if (!writeLine("isready")) return false;
    return waitFor("readyok", 20000);
}

EngineResult UciEngine::analyse(const std::string& fen, int depth, int multipv,
                                const std::atomic<bool>& cancel) {
    std::lock_guard<std::mutex> lock(mutex_);
    EngineResult result;
    result.fen = fen;
    if (!running()) {
        result.error = lastError_.empty() ? "Stockfish is not running." : lastError_;
        return result;
    }

    chess::Board board;
    if (!board.setFen(fen)) {
        result.error = "Invalid FEN passed to the engine: " + fen;
        return result;
    }
    const bool whiteToMove = board.sideToMove() == chess::Color::WHITE;

    const int pvCount = std::max(1, std::min(multipv, 10));
    if (!writeLine("setoption name MultiPV value " + std::to_string(pvCount))) {
        result.error = lastError_;
        return result;
    }
    if (!writeLine("ucinewgame")) {
        result.error = lastError_;
        return result;
    }
    if (!writeLine("position fen " + fen)) {
        result.error = lastError_;
        return result;
    }

    std::string go = "go depth " + std::to_string(depth);
    if (settings_.moveTimeMs > 0) {
        go += " movetime " + std::to_string(settings_.moveTimeMs);
    }
    if (!writeLine(go)) {
        result.error = lastError_;
        return result;
    }

    const auto started = std::chrono::steady_clock::now();
    std::vector<EngineLine> best(pvCount);
    std::vector<bool> seen(pvCount, false);
    bool gotBestMove = false;
    const int timeoutMs = std::max(30000, depth * 4000);
    const auto deadline = started + std::chrono::milliseconds(timeoutMs);

    std::string line;
    while (true) {
        if (cancel.load()) {
            writeLine("stop");
            result.error = "cancelled";
            return result;
        }
        if (std::chrono::steady_clock::now() > deadline) {
            writeLine("stop");
            result.error = "Stockfish timed out after " + std::to_string(timeoutMs / 1000) + "s.";
            return result;
        }
        if (!readLine(line, 500)) {
            if (!running()) {
                result.error = "Stockfish exited unexpectedly.";
                return result;
            }
            continue;
        }
        if (line.rfind("info ", 0) == 0 && line.find(" pv ") != std::string::npos) {
            EngineLine parsed;
            if (!parseInfoLine(line, whiteToMove, parsed)) continue;
            const int index = parsed.multipv - 1;
            if (index < 0 || index >= pvCount) continue;
            if (parsed.pvUci.empty()) continue;
            // Keep the deepest line reported for each rank.
            if (!seen[index] || parsed.depth >= best[index].depth) {
                best[index] = parsed;
                seen[index] = true;
            }
        } else if (line.rfind("bestmove", 0) == 0) {
            std::istringstream stream(line);
            std::string token;
            stream >> token;  // "bestmove"
            stream >> result.bestMoveUci;
            gotBestMove = true;
            break;
        }
    }

    result.timeMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now() - started)
                                        .count());
    if (!gotBestMove) {
        result.error = "Stockfish did not return a best move.";
        return result;
    }

    for (int i = 0; i < pvCount; ++i) {
        if (seen[i]) {
            result.lines.push_back(best[i]);
            result.depth = std::max(result.depth, best[i].depth);
            result.nodes = std::max(result.nodes, best[i].nodes);
        }
    }
    if (result.lines.empty()) {
        result.error = "Stockfish returned no scored lines.";
        return result;
    }
    result.ok = true;
    return result;
}

bool parseInfoLine(const std::string& line, bool whiteToMove, EngineLine& out) {
    EngineLine parsed;
    parsed.multipv = 1;
    std::istringstream stream(line);
    std::string token;
    bool haveScore = false;

    while (stream >> token) {
        if (token == "depth") {
            stream >> parsed.depth;
        } else if (token == "seldepth") {
            stream >> parsed.seldepth;
        } else if (token == "multipv") {
            stream >> parsed.multipv;
        } else if (token == "nodes") {
            stream >> parsed.nodes;
        } else if (token == "score") {
            std::string kind;
            int value = 0;
            if (!(stream >> kind >> value)) return false;
            if (kind == "cp") {
                parsed.score = Score::fromCp(value);
                haveScore = true;
            } else if (kind == "mate") {
                parsed.score = Score::fromMate(value);
                haveScore = true;
            }
        } else if (token == "pv") {
            std::string move;
            while (stream >> move) {
                if (move.rfind("info", 0) == 0) break;
                parsed.pvUci.push_back(move);
            }
            break;
        }
    }

    if (!haveScore) return false;
    // UCI scores are from the point of view of the side to move; WeChess stores
    // everything from White's point of view.
    out = parsed;
    if (!whiteToMove) out.score = -out.score;
    return true;
}

std::vector<std::string> uciLineToSan(const std::string& fen, const std::vector<std::string>& uciMoves) {
    std::vector<std::string> san;
    chess::Board board;
    if (!board.setFen(fen)) return san;
    for (const std::string& uci : uciMoves) {
        chess::Move move = chess::Move::NO_MOVE;
        try {
            move = chess::uci::uciToMove(board, uci);
        } catch (...) {
            break;
        }
        if (move == chess::Move::NO_MOVE) break;
        try {
            san.push_back(chess::uci::moveToSan(board, move));
            board.makeMove(move);
        } catch (...) {
            break;
        }
    }
    return san;
}

std::string formatPv(const std::string& fen, const std::vector<std::string>& uciMoves) {
    const std::vector<std::string> san = uciLineToSan(fen, uciMoves);
    if (san.empty()) return "";
    chess::Board board;
    if (!board.setFen(fen)) return "";
    int number = static_cast<int>(board.fullMoveNumber());
    bool white = board.sideToMove() == chess::Color::WHITE;
    std::ostringstream out;
    for (std::size_t i = 0; i < san.size(); ++i) {
        if (white) {
            out << number << ". ";
        } else if (i == 0) {
            out << number << "... ";
        }
        out << san[i];
        if (i + 1 < san.size()) out << ' ';
        if (!white) ++number;
        white = !white;
    }
    return out.str();
}

}  // namespace wechess
