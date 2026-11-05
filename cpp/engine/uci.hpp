// WeChess - Stockfish UCI transport.
//
// WeChess never re-implements a chess engine: it speaks the UCI protocol to a
// Stockfish process. This wrapper is deliberately small (spawn, write, read
// until "bestmove", kill) and leaves all search decisions to Stockfish.
#ifndef WECHESS_ENGINE_UCI_HPP
#define WECHESS_ENGINE_UCI_HPP

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "../core/eval.hpp"

namespace wechess {

struct EngineSettings {
    int threads = 1;       // phones: one search thread plus the OS scheduler
    int hashMb = 32;
    int multipv = 3;       // primary line + two alternatives
    int depth = 14;        // base depth for every position
    int criticalDepth = 20;  // deeper pass for tactical / critical positions
    int moveTimeMs = 0;    // optional hard cap per position (0 = depth only)
};

// One principal variation returned by Stockfish for a position.
struct EngineLine {
    int multipv = 1;
    int depth = 0;
    int seldepth = 0;
    Score score;                    // White's point of view
    std::vector<std::string> pvUci; // e2e4 e7e5 ...
    long nodes = 0;
};

// Full result for a single FEN.
struct EngineResult {
    bool ok = false;
    std::string error;
    std::string fen;
    int depth = 0;
    std::vector<EngineLine> lines;  // lines[0] is the best line
    std::string bestMoveUci;
    long nodes = 0;
    int timeMs = 0;

    const EngineLine* best() const { return lines.empty() ? nullptr : &lines.front(); }
};

// Thread-safe UCI engine handle. All calls are serialised with a mutex, so the
// analyzer worker thread and diagnostics can share one engine process.
class UciEngine {
   public:
    UciEngine() = default;
    ~UciEngine();
    UciEngine(const UciEngine&) = delete;
    UciEngine& operator=(const UciEngine&) = delete;

    // Spawns the engine and performs the UCI handshake. Returns false and sets
    // lastError() when the binary is missing or does not answer.
    bool start(const std::string& binaryPath, const EngineSettings& settings);
    void stop();
    bool running() const { return pid_ > 0; }

    // Analyse one position. `cancel` is polled between read chunks so a review
    // can be abandoned without killing the app.
    EngineResult analyse(const std::string& fen, int depth, int multipv, const std::atomic<bool>& cancel);

    const std::string& lastError() const { return lastError_; }
    const std::string& name() const { return name_; }
    std::string binaryPath() const { return binaryPath_; }

    // Applies new settings to the live process (SendOption + isready).
    bool applySettings(const EngineSettings& settings);

   private:
    bool writeLine(const std::string& line);
    bool readLine(std::string& line, int timeoutMs);
    bool waitFor(const std::string& token, int timeoutMs);
    void killProcess();
    void drain();

    int inFd_ = -1;   // engine stdout -> us
    int outFd_ = -1;  // us -> engine stdin
    int pid_ = -1;
    std::string lastError_;
    std::string name_ = "Stockfish";
    std::string binaryPath_;
    EngineSettings settings_;
    std::mutex mutex_;
    std::string readBuffer_;
};

// Values for material / tactical reasoning, in centipawns.
int pieceValueCp(char pieceUpper);

// Parses one UCI "info" line into an EngineLine (score converted to White POV
// using `whiteToMove`). Returns false for lines without a score/pv.
bool parseInfoLine(const std::string& line, bool whiteToMove, EngineLine& out);

// Converts a UCI move list to SAN using the rules library, starting at `fen`.
// Invalid moves stop the conversion (the prefix is still returned).
std::vector<std::string> uciLineToSan(const std::string& fen, const std::vector<std::string>& uciMoves);

// A readable one-line description of a PV, e.g. "1. e4 e5 2. Nf3".
std::string formatPv(const std::string& fen, const std::vector<std::string>& uciMoves);

}  // namespace wechess

#endif  // WECHESS_ENGINE_UCI_HPP
