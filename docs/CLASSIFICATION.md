# Classification and accuracy

WeChess classifies moves from Stockfish output alone. Gemini never influences a
label. This document describes the exact method implemented in
`cpp/analysis/classify.cpp`, `stats.cpp` and `review.cpp`.

## 1. Evaluation model

Stockfish scores are converted to a **win probability** before any comparison:

```
WP(cp) = 1 / (1 + exp(-k * cp))        k = 0.00368208
```

Mate scores are not passed through the logistic; they map to 1.0 (mate for the
side to move) or 0.0 (mated), with distance used only for tie-breaking. This is the
same shape of curve used by Lichess, and it is why a 100 cp swing at +0.0 is judged
more harshly than the same swing at +9.0.

Every evaluation is stored from White's point of view (`Score::forColor`), then
re-oriented for the mover. A move is judged by:

```
winLoss = WP(after, mover) - WP(before, mover)      // >= 0 means "got worse"
```

## 2. Thresholds

From `namespace wechess::thresholds` (single source of truth):

| Classification | Rule |
| --- | --- |
| **Blunder** | `winLoss > 0.220` |
| **Mistake** | `winLoss > 0.120` |
| **Inaccuracy** | `winLoss > 0.060` |
| **Good** | `winLoss > 0.030` |
| **Excellent** | `winLoss > 0.005` (and not better than the engine's move) |
| **Best** | the played move is Stockfish's first choice, or within `kBestCpSlack = 10` cp of it |
| **Great** | an *only move*: second-best line is at least `kGreatOnlyMoveGapCp = 120` cp worse, depth ≥ `kGreatMinDepth = 10` |
| **Book** | position after the move is in the bundled opening book, subject to the gates below |
| **Brilliant** | see below |

Ordering matters: Book and Brilliant are evaluated first and can be downgraded,
so a "book" label can never hide a blunder.

### Book gating

`Book` is applied only when the move actually appears in the CC0 Lichess opening
data **and**:

- it does not deliver mate (a mating move is never "just theory"),
- `winLoss <= kInaccuracy (0.120)`, and
- the mover's win probability afterwards is at least `0.25`.

Without the opening data there is no Book classification at all — guessing is worse
than omitting.

### Brilliant

All of the following must hold, otherwise the move is classified normally:

1. **A real sacrifice**: static exchange evaluation on the destination square shows
   the mover leaves at least `kBrilliantSacrificeCp = 200` cp en prise (a piece, not
   a pawn — pawn sacs are common and rarely brilliant).
2. `winLoss <= kBrilliantMaxWinLoss = 0.02` — it must be essentially as good as the
   engine's choice.
3. Win probability after the move `>= kBrilliantMinWinProb = 0.55` — the sacrifice
   has to work, not merely be "interesting".
4. Win probability before the move `<= kBrilliantMaxWinBefore = 0.90` — you do not
   get a prize for saccing when already completely winning.
5. Analysis depth `>= kBrilliantMinDepth = 12`, so shallow noise cannot earn it.

Consequence: many pleasing sacrifices are labelled **Great** or **Best**, which is
the honest outcome. Brilliant is rare by construction.

### Forced and terminal positions

- If a position has exactly one legal move it is marked `forced` and is never
  penalised, whatever the win-loss arithmetic says.
- If the game ends, mate/stalemate scores are synthesised from the PGN result
  instead of asking the engine for a `bestmove` (there is none).
- Missing a mate when one was available produces `missedMate` and at least
  **Mistake**, regardless of the win-probability curve, because throwing away a
  forced win is a concrete, verifiable error.

## 3. Accuracy

Per move, from the same win-loss value:

```
accuracy = 103.1668 * exp(-0.04354 * lossPercentPoints) - 3.1667    clamped to [0, 100]
```

where `lossPercentPoints = winLoss * 100`. Aggregation is weighted so that moves in
already-decided positions contribute less than moves that decided the game, and
per-phase accuracies (opening / middlegame / endgame) use the same formula on the
relevant subset. Average centipawn loss is reported separately and is **not** used
for the accuracy number.

This is a published, independent approximation of the idea behind commercial
accuracy scores. It is **not** Chess.com's algorithm and will not reproduce its
numbers exactly.

## 4. Phases

Phase is decided from the position, not from a move counter:

- **Opening**: the move is inside the known opening sequence from the book.
- **Endgame**: total non-pawn material below a threshold, or few pieces left with
  kings becoming active.
- **Middlegame**: everything else.

## 5. Known limits

- Labels are only as reliable as the depth; the defaults are phone-friendly. At low
  depth, "Best" and "Excellent" can swap.
- MultiPV 1 would make "only move" detection impossible, so MultiPV ≥ 2 is used for
  correctness (default 3).
- Sacrifice detection is static (SEE). A deep positional sacrifice that keeps
  material equal will not be seen as a sacrifice, so it cannot be Brilliant.
- Openings not in the CC0 dataset are analysed as normal moves instead of Book.
