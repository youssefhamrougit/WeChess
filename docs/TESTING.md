# Testing

## Running

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"

ctest --test-dir build --output-on-failure

# HTTP + engine end to end (needs a Stockfish binary)
./build/test_server /path/to/stockfish
./build/test_analysis /path/to/stockfish

# Black-box smoke test over the real server binary
./scripts/smoke_test.sh build/wechess_server /path/to/stockfish
```

`test_analysis` and `test_server` skip their engine cases when no binary is given;
nothing is silently reported as passing.

## Suites

### `test_core` — 9 cases, 206 assertions

- PGN import: headers, comments, annotations (`!?`, `$4`, `e.p.`), result tags,
  headerless PGNs, `[FEN]` start positions, ply/move numbering.
- SAN sanitising and UCI/SAN round trips for castling, en passant, promotion,
  check and mate.
- FEN helpers: placement parsing, material counts, move number.
- Error paths: illegal move, malformed header, empty input, over-long game.
- Board invariants: every position has 64 squares and the right side to move.

### `test_analysis` — 15 cases, 167 assertions (Stockfish 17.1)

- Win probability and score formatting at poles, mate and equal positions.
- Perspective: a good White move and a good Black move must both be `Best`.
- Threshold behaviour across every classification band.
- Brilliant requires a real sacrifice and is refused for a pawn sacrifice.
- Great requires an only move; forced moves are never punished.
- Missed mate detection.
- Static exchange evaluation on captures, defended pieces and en prise queens.
- Opening book data loads and identifies known openings; book cannot mask a blunder.
- Graceful degradation when the engine is missing.
- **Independent-engine comparison**: `engine_analyses_a_position_consistently`
  runs the same FEN twice through separate engine processes and asserts identical
  best move and score, then compares a full-game classification run against a
  second, independent Stockfish invocation with the same settings.
- Cache round-trip through disk, and PV-to-SAN conversion.

### `test_server` — 75 checks with an engine, 67 without

Real HTTP requests against a real `HttpServer`:

- Home, stylesheet, settings, 404s, health check.
- Paste import, multipart upload, `303` redirect targets, invalid PGN → 400 with a
  readable message, empty paste → 400.
- Every rendered page contains **no** `<script`, `javascript:`, `onclick=` or
  `onsubmit=`, and carries a CSP with `script-src 'none'`.
- Board integrity at ply 0, after 1.e4, and at the final position: exactly 64
  squares, pieces on the right squares, coordinates, eval bar, navigation links.
- Out-of-range `?ply=` is clamped instead of crashing.
- HTML escaping: a PGN whose `[Event]` contains `<script>` renders as text, both on
  the review page and in the recent-games list.
- Path traversal (`/game/..%2F..%2Fetc%2Fpasswd`) and unknown ids → 404.
- Settings persistence and validation (depth 99 → default; plain-http proxy → 400).
- Delete removes the game from disk and from the recent list.
- With an engine: import → analysis → redirect → review page with accuracy and
  classification counts.

### `test_gemini` — 44 checks

- JSON parsing/quoting, malformed input rejection.
- Configuration gates: no URL, no token, disabled flag, plain-http remote URL.
- Prompt content: classification, centipawn loss, FENs, best move, player, and the
  explicit "use ONLY the facts above / do not invent" constraints; no prompt for
  ply 0.
- Transport against a local mock proxy: bearer header, model and prompt in the body,
  success parsing, 401/503 handling, malformed body, empty text, unexpected path.
- Errors never echo the token.
- Builds with `-DWECHESS_WITH_CURL=OFF` skip the transport cases and say so; they
  still verify JSON, configuration and prompt construction.

## PGN corpus

`tests/data/` contains a historical game with both castlings and a queen sacrifice,
a long middlegame, a promotion with check, Fool's Mate with annotations, and an
incomplete PGN. `scripts/smoke_test.sh` imports all of them over HTTP.

## What these tests do not prove

- **No APK was built here** (no JDK, Android SDK, NDK or Gradle in this container),
  and nothing was installed on a device or emulator.
- **No browser rendering check**: this environment has no browser automation, so
  layout, colour and touch ergonomics were verified by reading the CSS and by HTML
  structure assertions, not by pixels.
- JavaScript-disabled behaviour is enforced in code and by the absence of scripts in
  every served page, but the final proof is opening the APK's WebView once.
- Stockfish consistency is checked between two runs of the same build with
  threads=1. Cross-version comparisons may differ slightly.
