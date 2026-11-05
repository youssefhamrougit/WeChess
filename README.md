# WeChess

A chess game-review app for Android in the spirit of Chess.com's Game Review:
import a PGN, let Stockfish analyse it, then walk through the game on a board
with an evaluation bar, a classification for every move, accuracy statistics and
short written explanations of the key moments.

The interface is **HTML5 + CSS3 with zero JavaScript**. All behaviour lives in a
C++17 backend that serves the pages over a loopback HTTP server and is hosted by
a thin Kotlin WebView wrapper with JavaScript explicitly disabled.

```
PGN  ──►  C++ backend (rules, UCI, classification, statistics)
                  │  HTTP on 127.0.0.1
                  ▼
      WebView rendering HTML/CSS (JavaScript off)
                  │
                  └─►  wechess_proxy ──► Gemini (explanations only)
```

## What works

- **Import**: pick a `.pgn` from storage (system file chooser) or paste the text.
  Headers, comments, annotations, castling, en passant, promotion, and games
  starting from a `[FEN]` are handled; invalid input returns a readable error.
- **Board**: 64 squares with inline SVG Staunton pieces, coordinates, last-move
  and check highlighting, arrows for the best move and the played move, and a
  vertical evaluation bar. The position always matches the selected ply.
- **Navigation**: first / previous / next / last links plus a clickable move list
  with classification badges. All state is in the URL (`?ply=N`), so the WebView's
  back button and bookmarking work — no script required.
- **Analysis**: Stockfish over UCI, asynchronous, with progress, cancellation and
  a persistent FEN-keyed evaluation cache. A deeper second pass runs on critical
  positions.
- **Review**: Brilliant / Great / Best / Excellent / Good / Book / Inaccuracy /
  Mistake / Blunder for every move, per-colour accuracy and counts, opening
  identification from a real opening book, game phase detection from material,
  highlights (best move, biggest blunder, turning point), and a per-move panel
  with evaluations, centipawn loss, best move and principal variation.
- **Explanations**: a separate C++ proxy holds the Gemini key; the app only ever
  holds a low-value shared token. With Gemini absent or offline, the whole
  Stockfish review still works and the panel says why it is unavailable.

## Requirements

| Component | Version used here |
| --- | --- |
| C++ | 17 (g++ 13.3 or clang 14+) |
| CMake | 3.22+ (tested with 3.30.5) |
| Stockfish | 16+ (tested with 17.1) |
| libcurl | 8.x, optional (Gemini only) |
| Android | minSdk 26, compileSdk 35, NDK r27, AGP 8.5.2, Kotlin 2.0.21 |

## Build and run on a workstation

The same C++ backend the app embeds runs as a normal server, which is the fastest
way to use or test the interface:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
./build/wechess_server --port 8080 --stockfish /usr/games/stockfish
# open http://127.0.0.1:8080/
```

If libcurl headers are missing and you only want chess analysis, configure with
`-DWECHESS_WITH_CURL=OFF` (Gemini will report itself as unavailable). The server
refuses to bind to anything but `127.0.0.1`.

Options: `--port`, `--data DIR` (default `./wechess-data`), `--stockfish PATH`.
Environment equivalents: `WECHESS_PORT`, `WECHESS_DATA`, `WECHESS_STOCKFISH`.
An explicit `--stockfish` is authoritative: if the path is not executable the app
reports "no engine" rather than silently picking another binary.

## Build the Android app

The Android project is in `android/`. It needs the Android SDK, NDK r27 and a JDK
17 — none of which are present in this development container, so the APK itself
was **not** produced here (see [Status](#status-what-was-and-was-not-verified)).

```bash
# 1. Engine (GPL: read stockfish/README.md first)
export ANDROID_NDK_HOME=$HOME/Android/Sdk/ndk/27.0.12077973
./scripts/fetch_stockfish_android.sh arm64-v8a
./scripts/fetch_stockfish_android.sh x86_64        # emulator

# 2. APK
cd android
echo "sdk.dir=$HOME/Android/Sdk" > local.properties
gradle wrapper --gradle-version 8.7    # once: the wrapper JAR is a binary, so
                                       # it is generated, not committed
./gradlew assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

The native library is built by Gradle through the root `CMakeLists.txt`
(`externalNativeBuild`). At startup `MainActivity` calls
`NativeBridge.startServer()` on a background thread, which creates the C++
`App`, binds a free loopback port and returns it; the WebView then loads
`http://127.0.0.1:<port>/`. Every WebView setting that could enable scripting is
off, cookies and DOM storage are off, and any navigation away from the loopback
origin is blocked.

## Gemini explanations

Explanations are optional and never touch the chess numbers. The app talks to a
proxy you run; the proxy holds `GEMINI_API_KEY` and enforces a model allowlist and
rate limit.

```bash
# server side (never shipped in the APK)
export GEMINI_API_KEY=...            # your Google AI Studio key
export WECHESS_PROXY_TOKEN=$(openssl rand -hex 24)
export GEMINI_MODEL=gemini-2.5-flash
./build/wechess_proxy                # listens on :8787 by default
```

Then in WeChess → Settings set the proxy URL (`https://…`; `http://127.0.0.1` is
accepted for local testing), the same token, and the model. Full details, including
TLS termination, systemd and rotation, are in [docs/GEMINI_PROXY.md](docs/GEMINI_PROXY.md).

The key must never be placed in the APK, the repository, the WebView, or a log.
The app stores only the proxy token in its private data directory.

## Testing

```bash
ctest --test-dir build --output-on-failure            # all suites
./build/test_server /path/to/stockfish                # full HTTP + engine flow
./scripts/smoke_test.sh build/wechess_server /path/to/stockfish
```

Verified in this repository:

- `test_core` — 9 cases / 206 assertions (PGN, FEN, special moves, errors).
- `test_analysis` — 15 cases / 167 assertions against Stockfish 17.1.
- `test_server` — 75 HTTP checks with the engine, 67 without.
- `test_gemini` — 44 checks against a local mock proxy.

[docs/TESTING.md](docs/TESTING.md) lists exactly what is and is not covered, and
how results are compared with independent Stockfish runs.

## Packaging and deployment

- **ZIP**: `./scripts/package_zip.sh` → `dist/wechess-1.0.0-source.zip` (full source,
  no build output, no keystores).
- **APK in CI**: `.github/workflows/android.yml` runs the native tests, then builds
  the APK with JDK 17, SDK 35, NDK 27 and Gradle 8.7, uploads it as the
  `wechess-debug-apk` artifact and attaches it to GitHub releases.
- **Landing page**: `site/` (plain HTML/CSS) is deployed to Vercel by
  `.github/workflows/vercel.yml`. Vercel cannot host the app itself, because the
  backend is a native C++ server.

Step-by-step instructions, required secrets and troubleshooting are in
[docs/DEPLOYMENT.md](docs/DEPLOYMENT.md).

## Project layout

```
android/                 Kotlin WebView wrapper + Gradle/NDK build
cpp/core/                PGN import, game model, evaluation types
cpp/analysis/            classification, statistics, review, opening book
cpp/engine/              Stockfish UCI subprocess, async analyzer, eval cache
cpp/gemini/              proxy client, JSON, prompt construction
cpp/server/              HTTP server, routing, rendering, persistence, sessions
cpp/proxy/               server-side Gemini proxy (separate binary)
cpp/main/                desktop/server entry point
cpp/android/             JNI bridge (start/stop only)
frontend/                shell.html + wechess.css, embedded at build time
tests/                   C++ test suites and a PGN corpus
scripts/                 Android Stockfish build, smoke test
docs/                    architecture, classification, security, testing, proxy
```

## Honest limitations

- This is **not** Chess.com's algorithm. Classification and accuracy are an
  independent, documented approximation based on Stockfish; see
  [docs/CLASSIFICATION.md](docs/CLASSIFICATION.md).
- Classifications are only as good as the search depth. Defaults are chosen for a
  phone (depth 14, MultiPV 3, a depth-20 second pass on critical moves); raise them
  for stronger judgement at the cost of time.
- Without an opening book a "Book" label would be guesswork, so a CC0 Lichess
  opening list is compiled in. If that data is ever removed, Book is omitted.
- Zero JavaScript means no drag-and-drop pieces and no live clock; navigation is
  link- and form-based by design. Exact scroll restoration after a reload cannot
  be expressed without script, so pages use `#board` / `#review` anchors instead.
- The loopback server is reachable by other apps on the same device. It only
  serves local HTML and never returns the proxy token; see
  [docs/SECURITY.md](docs/SECURITY.md).

## Status: what was and was not verified

Verified here: the C++ backend, HTTP server, PGN pipeline, Stockfish integration,
classification, statistics, HTML rendering, and all four test suites — on Linux
with a real Stockfish 17.1 binary.

Not verified here: the APK build and on-device behaviour (no JDK/SDK/NDK/emulator
in this container) and pixel-level rendering in a browser or WebView (no browser
available). The Android sources and Gradle/NDK configuration are complete and
mirror the tested C++ code, but they must be built and installed once on a machine
with the Android toolchain.

## Licenses

WeChess is MIT licensed. Stockfish is GPL-3.0 and is not bundled here; shipping it
inside an APK brings GPL obligations for the combined work. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and `stockfish/README.md`.
