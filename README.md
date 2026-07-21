# Twin — a UCI chess engine

A from-scratch, single-machine, traditional (alpha-beta) chess engine written in
C++17. Bitboard board representation, magic-bitboard sliding attacks, a tapered
evaluation, and a modern search stack (PVS + transposition table + null-move +
LMR + quiescence). Text-based, speaks the **UCI** protocol — plug it into any
chess GUI or match runner.

Built and verified on Apple Silicon (arm64) with Apple clang; the code is
portable C++17 (no x86-only intrinsics), so it also builds on Intel macOS/Linux.

---

## Build

```bash
make
# or, equivalently, a single command:
clang++ -std=c++17 -O3 -DNDEBUG -flto -o twin main.cpp
```

This produces the `twin` executable. (Optional: uncomment `-march=native` in the
`Makefile` for a small speed boost on the build machine.)

## Run

`twin` reads UCI commands on stdin and writes to stdout. Quick manual session:

```
uci
isready
position startpos
go movetime 3000
```

It will stream `info depth …` lines and finish with `bestmove …`.

Useful non-UCI convenience commands are also available: `d` (print the board),
`eval` (static eval of the current position), `perft <n>`, and `bench`.

### Play it in a GUI

Point any UCI-compatible GUI (Arena, Banksia GUI, Cute Chess, ScidvsPC, …) at the
`twin` binary and it's ready to play.

### Play it against the Java engine (the whole point 🙂)

Use [cute-chess-cli](https://github.com/cutechess/cutechess), which can run a
match between two engines and supports both UCI and XBoard/WinBoard opponents:

```bash
cutechess-cli \
  -engine name=Twin cmd=./twin proto=uci \
  -engine name=TheirJavaEngine cmd="java -jar their_engine.jar" proto=uci \
  -each tc=60+0.6 -rounds 100 -repeat -recover \
  -pgnout match.pgn
```

(Set `proto=xboard` for whichever engine uses that protocol.) `tc=60+0.6` is
60 s + 0.6 s/move; adjust to taste. Twin's time management is defensive — it will
not lose on time.

---

## What's implemented

**Board & move generation**
- 64-bit bitboards, redundant mailbox, incremental Zobrist hashing
- Magic bitboards for bishop/rook/queen attacks (magics generated at startup with
  a fixed seed → deterministic), precomputed leaper attacks
- Full legal move handling: castling (with through-check tests), en passant
  (incl. the discovered-check edge case), all promotions, pins, double check
- Fast `make`/`unmake` with an undo stack

**Search**
- Iterative deepening with aspiration windows
- Principal Variation Search (PVS) with a transposition table (bounded, aged)
- Null-move pruning, reverse-futility (static null) pruning, razoring
- Late Move Reductions (log-based table), late-move & futility pruning
- Check extensions, mate-distance pruning
- Quiescence search with SEE-based capture pruning and delta pruning
- Move ordering: TT move → good captures (MVV-LVA + SEE) → killers →
  countermove → history heuristic → losing captures
- Static Exchange Evaluation (SEE)
- Draw detection: repetition, fifty-move (mate takes precedence), insufficient
  material
- Defensive time management (sudden-death, increment, moves-to-go, movetime,
  fixed depth/nodes, infinite + `stop`)

**Evaluation** (tapered midgame↔endgame)
- Material + piece-square tables (PeSTO values)
- Passed / isolated / doubled pawns, bishop pair
- Piece mobility, rook on open / semi-open file
- King safety (open files near the king), tempo

---

## How it was verified (bug-free by construction, then by test)

Everything below was run during development; reproduce with `make test`.

| Test | What it proves | Result |
|------|----------------|--------|
| **Perft** — 6 standard positions, ~600M nodes incl. Kiwipete | move generation / make-unmake are exactly correct (castling, ep, promotions, pins, discovered checks) | all match reference counts, ~140 Mnps |
| **Eval symmetry** — mirror each position | evaluation is side-consistent (no sign/orientation bugs) | identical scores on all mirrors |
| **Self-play** — 24 full games from varied openings, real search depth | every move legal + board state (bitboards/mailbox/Zobrist) consistent after every make | ~2500 plies, all legal & consistent |
| **UCI driver** — handshake, mate-in-1, tactic, time control, `movetime`, `stop`, post-stop responsiveness | protocol correctness and no hangs/time losses | all pass |
| **Terminal positions** — checkmate / stalemate / K-vs-K | graceful `bestmove 0000`, no crash | pass |
| **Warnings** — `-Wall -Wextra -Wshadow` | code cleanliness | zero warnings |

In a 3-second search from a middlegame it reaches **depth 16–17**.

---

## Files

```
main.cpp        UCI front-end, threading, entry point
bitboard.h      types, move encoding, magic bitboards, attack tables
position.h      Position: FEN, make/unmake, move gen, attacks, SEE, Zobrist
eval.h          tapered evaluation
search.h        transposition table + search (PVS/qsearch/pruning) + time mgmt
Makefile        build + test targets
perft_test.cpp     movegen validation harness
eval_test.cpp      eval symmetry harness
selfplay_test.cpp  full-game legality/consistency harness
uci_driver.py      UCI protocol test harness
```
