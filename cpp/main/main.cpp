// WeChess - host/server entry point.
//
// Runs the same C++ backend the Android build embeds, on http://127.0.0.1:PORT,
// so the interface can be developed and tested on a workstation.
#include <csignal>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include "../server/app.hpp"
#include "../server/http.hpp"

namespace {

std::atomic<bool> gStop{false};

void handleSignal(int) { gStop.store(true); }

void printUsage() {
    std::cout << "Usage: wechess_server [--port N] [--data DIR] [--stockfish PATH]\n"
                 "Environment: WECHESS_PORT, WECHESS_DATA, WECHESS_STOCKFISH,\n"
                 "             WECHESS_GEMINI_PROXY, WECHESS_GEMINI_TOKEN\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);

    int port = 8080;
    std::string dataDir = "./wechess-data";
    std::string stockfish;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else if (arg == "--data" && i + 1 < argc) {
            dataDir = argv[++i];
        } else if (arg == "--stockfish" && i + 1 < argc) {
            stockfish = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            printUsage();
            return 2;
        }
    }

    if (const char* value = std::getenv("WECHESS_PORT")) port = std::atoi(value);
    if (const char* value = std::getenv("WECHESS_DATA")) dataDir = value;
    if (const char* value = std::getenv("WECHESS_STOCKFISH")) stockfish = value;

    if (port < 1 || port > 65535) {
        std::cerr << "Invalid port: " << port << "\n";
        return 2;
    }

    wechess::App app(dataDir, "", stockfish);
    std::string error;
    if (!app.init(error)) {
        std::cerr << "Could not start WeChess: " << error << "\n";
        return 1;
    }

    wechess::HttpServer server;
    server.handler = [&app](const wechess::HttpRequest& request) { return app.handle(request); };
    if (!server.listen("127.0.0.1", port, error)) {
        std::cerr << "Could not listen on 127.0.0.1:" << port << ": " << error << "\n";
        return 1;
    }

    server.start();

    const std::string engine = app.stockfishPath();
    std::cout << "WeChess 1.0.0\n"
              << "  URL:       http://127.0.0.1:" << server.port() << "/\n"
              << "  Data:      " << dataDir << "\n"
              << "  Stockfish: " << (engine.empty() ? "NOT FOUND (set --stockfish or Settings)" : engine)
              << "\n"
              << "Press Ctrl+C to stop.\n";

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);
    while (!gStop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    server.stop();
    std::cout << "WeChess stopped.\n";
    return 0;
}
