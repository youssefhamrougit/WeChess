# PGN test corpus

| File | Covers |
| --- | --- |
| `opera-game.pgn` | Full historical game, castling on both sides, queen sacrifice, mate |
| `castling.pgn` | Long middlegame with both castlings and an exchange sacrifice |
| `promotion.pgn` | Promotion with check, comments |
| `blunder.pgn` | Fool's Mate, PGN annotations and a multi-line comment |
| `incomplete.pgn` | Missing result and headerless-feeling metadata |

These files are used by `tests/core_test.cpp`, by the HTTP tests and for manual
verification. `scripts/smoke_test.sh` imports every one of them into a running
server.
