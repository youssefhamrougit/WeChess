# Architecture

## Why a local server

HTML and CSS cannot execute application logic, and JavaScript is forbidden. So the
C++ backend owns every piece of state and renders complete HTML pages. Android's
WebView is only a renderer for `http://127.0.0.1:<port>/`.

```
MainActivity (Kotlin, ~200 lines)
  └─ NativeBridge.startServer(dataDir, nativeLibDir, enginePath)   [JNI]
       └─ wechess::App            owns Store, settings, sessions
            └─ wechess::HttpServer  thread-per-connection, loopback only
                 └─ App::handle(request) -> HttpResponse
                      ├─ views::render*   HTML strings
                      └─ assets::stylesheet()  embedded CSS
```

The Kotlin layer is limited to lifecycle, permissions, file picking and hosting the
WebView. It contains no chess logic, no networking and no scripts.

## Interaction model without JavaScript

| Need | Mechanism |
| --- | --- |
| Navigate plies | `GET /game/<id>?ply=N#board` links |
| Select a move | link in the move list to `?ply=N#board` |
| Import / settings / delete | `POST` form → `303 See Other` → `GET` |
| Progress | page refresh by the user, or a `Refresh`-style link (no meta-refresh loops) |
| Explanation request | `POST /game/<id>/explain` → `303` to a wait page → `303` back to `#review` |
| Preserved state | everything in the URL and on disk; the WebView back button works |

Anchors (`#board`, `#review`) are used instead of scripted scroll restoration. That
is a real limitation of the constraint and is documented in the README.

## Threading

- HTTP: one thread per connection, short-lived, 8 MB request cap, `SIGPIPE`
  ignored, `Connection: close`.
- Analysis: one worker thread per session inside `GameSession`, polling an
  `std::atomic<bool>` cancel flag. Progress is atomics plus a mutex-guarded status
  string, so the HTTP thread can render it at any time.
- Gemini: a detached thread per explanation request, state machine
  `none → pending → ready|failed`. The UI polls `GET /explain-wait/<id>?ply=N`,
  which redirects as soon as the state settles.
- Stockfish: a single child process per analysis run, protected by a mutex in
  `UciEngine`; it is always killed before the worker returns, including on failure
  and cancellation.

## Storage

```
<dataDir>/
  settings.conf        key=value, one per line (engine depth, proxy URL/token)
  index.tsv            recent games: id, players, result, opening, plies, created
  games/<id>.pgn       the original PGN text (lossless, small)
  cache/evals.tsv      FEN-keyed evaluation cache, MultiPV lines included
```

Games are stored as PGN rather than a serialised model: re-import is deterministic,
version-tolerant, and cheap because evaluations are cached by FEN.

## Analysis pipeline

1. Every position (0..plies) is analysed at the base depth with MultiPV.
2. Positions touched by a large win-probability drop, a missed mate, a sacrifice or
   a book exit are re-searched at `criticalDepth`.
3. `buildReview` converts evaluations into `MoveReview`s: classification, SAN for
   the played and best move, PV in SAN, phase, and a factual one-line summary.
4. Statistics and highlights are aggregated from those reviews only.

Evaluations are always normalised to White's point of view inside `PositionEval`
and re-oriented per mover where needed, which removes a whole class of sign bugs.

## Failure handling

| Failure | Behaviour |
| --- | --- |
| No Stockfish | Analysis stops with a clear message; board, PGN and navigation still work |
| Engine crashes / no `bestmove` | UCI wrapper times out, kills the process, reports the position as unavailable |
| Bad PGN | HTTP 400 with a readable, escaped explanation |
| No libcurl | Gemini disabled explicitly; review unaffected |
| Proxy 401/429/503 | Specific message in the review panel |
| Cancelled analysis | Worker stops, engine is reaped, session returns to idle |
| Process death | Nothing important is lost: games are on disk, evaluations cached |

## Security boundaries

- The HTTP server binds `127.0.0.1` only and sets a restrictive CSP
  (`default-src 'none'; script-src 'none'; style-src 'self' 'unsafe-inline'`).
- The WebView has JavaScript, DOM storage, file access and cookies disabled, and
  navigation is restricted to the loopback origin.
- Game ids are validated (`[a-z0-9]{12}`-style) before touching the filesystem, so
  path traversal cannot reach `games/`.
- The Gemini API key exists only in the proxy process' environment.
