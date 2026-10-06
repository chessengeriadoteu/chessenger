# Chessenger 1.6

Chessenger is a UCI chess engine in C99, written by AI coding assistants under the direction of Dariusz Domagała
(see [How Chessenger is made](#how-chessenger-is-made)).

- Version: 1.6 (code date 2026-10-06)
- Strength: 3146.12 [3130.90, 3161.70] (runs A190 and A191: 1500 games, Ordo anchored to MCERL ratings)
- What is new: Search, king safety, endgame rules with a KPK bitbase and time management adapted from Stash 37; transposition table modelled on it; evaluation weights and piece values retuned; versions 1.1 to 1.5 were not published.

## How Chessenger is made

Chessenger is the work of one author and AI coding assistants. This section says plainly who did what.

The C code was written by AI models: about 99% of the work was done with Anthropic's Claude models (Claude chat
in 2025, Claude Code from 2026), about 1% with Goose and OpenAI's GPT models.

The author's part:

- deciding what the engine should become: which ideas to try, in what order, and which open-source engines to study;
- setting the rules the AI works by: every statement about the code checked against the code or a measurement,
  and no change kept without the author's decision;
- setting the test conditions and reading the results: since August 2026, changes to search and evaluation have been
  played out in engine-vs-engine games at 2 s + 0.2 s per move with Hash 128 MB, the conditions of MCERL, the
  rating list the author maintains with hundreds of thousands of engine games;
- deciding, change by change, what stays in the engine and what is dropped;
- making sure that code and values taken from other engines are attributed and licensed (see the last two
  sections of this file).

The AI's part: writing and changing the code, building it, running the tests, and having changes to the engine
checked in a separate session that did not write them, against one question: could this make the engine weaker,
or break legal play or the UCI protocol?

Every strength figure in this repository comes from games; where a version was not measured on its own, its
README says how the figure was obtained. The author is responsible for the code and for everything published here.

## All versions

Every version is a tagged commit in this repository (`v0.1` … `v1.6`); from 0.2 on its GitHub Release carries the binaries.
Version numbers follow playing strength, not dates: 0.6 and 0.7 were skipped on purpose,
1.1 to 1.5 were not published, and 2.0 will be the first version with a neural network.

| Version | Code date | Strength (CElo, MCERL scale) | Notes |
|---|---|---|---|
| 0.1 | 2002-07-05 | not measured | Sedar, the predecessor: separate code from 2002 |
| 0.2 | 2026-08-09 | not measured | First Chessenger: bitboards, alpha-beta with quiescence search, transposition table, UCI. |
| 0.3 | 2026-08-24 | about 1950 (two runs: 1943.56 and 1962.74) | Magic bitboards for sliding pieces; many search and evaluation fixes. |
| 0.4 | 2026-09-02 | 2186.24 [2143.17, 2229.32] (chained anchor, run A76) | First UCI options (Hash, MultiPV, Clear Hash, Ponder); search and move ordering work. |
| 0.5 | 2026-09-08 | 2593.14 [2574.00, 2612.63] (run A111: 1000 games against Fruit 1.0, MCERL 2536) | New evaluation built on the PeSTO piece-square tables. |
| 0.8 | 2026-09-12 | 2872.78 [2852.48, 2894.25] (run A119: 1000 games against Berserk 2.0.0, MCERL 2700) | New search adapted from OliThink 5.10.5; versions 0.6 and 0.7 were skipped on purpose. |
| 0.9 | 2026-09-21 | 2932.38 [2919.52, 2945.24] (run A143: 2000 games, Ordo anchored to MCERL ratings; earlier run A142: 2921.39) | Evaluation weights partly based on 4ku 5.1; tuned search and time management. |
| 1.0 | 2026-10-05 | 3052.07 [3037.27, 3066.92] (runs A166 and A167: 1500 games, Ordo anchored to MCERL ratings) | Late move reductions, continuation history, singular extensions, ProbCut, king safety and time management adapted from Stash 37; transposition table modelled on it; rooks score open files; pawn hash table; search depth up to 128 plies. |
| 1.6 | 2026-10-06 | 3146.12 [3130.90, 3161.70] (runs A190 and A191: 1500 games, Ordo anchored to MCERL ratings) | Search, king safety, endgame rules with a KPK bitbase and time management adapted from Stash 37; transposition table modelled on it; evaluation weights and piece values retuned; versions 1.1 to 1.5 were not published. |

## Lineage

Sedar (0.1) is a chess program the author wrote in 2002. Chessenger shares no code with it.
Chessenger itself was started from scratch in 2025 with AI coding assistants (see How Chessenger is made),
and from 2026 on developed through measured experiments.

## How strength was measured

CElo on the scale of MCERL, the Mac Chess Engines Rating List (https://www.chessengeria.eu/mcerl),
which the author maintains. Chessenger played with one thread. A rating is the opponent's MCERL rating plus the
Elo difference measured in the games, and it belongs to the measured binary. MCERL ratings are not directly
comparable with other rating lists such as CCRL or CEGT.

1500 games against ten engines from MCERL (Komodo CCT, Senpai, Rice, Rodent, Polaris, Counter, Crafty, Patricia, OliThink, Toga), at 2 s + 0.2 s per move, Hash 128 MB: 760 games on a Mac mini M4 Pro and 740 on a MacBook Pro M1 Pro, rated together with Ordo anchored to their MCERL ratings.

## About the source

The source is published without comments and without the switches used during development;
the development history stays private. Removing comments and switches does not change the engine: built with
the same flags, the cleaned source and the original give the same binary. Identifiers and messages were then
translated into English; that changes names and text only, not the search (same nodes, moves and lines on the
24 test positions). The published source builds an engine that searches exactly the same nodes and plays the
same moves on those 24 positions as the measured binary.

## Build

```
make            # this machine
make release    # all binaries into dist/ (Linux and Windows need zig)
```

## Binaries

| File | For |
|---|---|
| `chessenger-1.6-macos-arm64.tar.gz` | Mac with Apple Silicon (M1 and newer), macOS 11 or newer |
| `chessenger-1.6-macos-x64.tar.gz` | Mac with Intel CPU, macOS 10.9 or newer |
| `chessenger-1.6-linux-x64.tar.gz` | Linux, x86-64 (static, x86-64-v2: SSE4.2 and POPCNT) |
| `chessenger-1.6-linux-arm64.tar.gz` | Linux, ARM64 (static) |
| `chessenger-1.6-windows-x64-avx2.zip` | Windows, most PCs from 2013 on (AVX2); the build rating lists usually use |
| `chessenger-1.6-windows-x64-bmi2.zip` | Windows, Intel Haswell and newer, AMD Zen and newer (x86-64-v3) |
| `chessenger-1.6-windows-x64-popcnt.zip` | Windows, older CPUs with POPCNT (x86-64-v2) |
| `chessenger-1.6-windows-x64.zip` | Windows, any x86-64 CPU (baseline, slowest) |
| `chessenger-1.6-windows-arm64.zip` | Windows on ARM (ARM64) |

Each archive holds the binary, `LICENSE`, `README.md` and, where the version has one, `LICENSES/`.

If unsure on Windows, take `avx2`; on Windows on ARM, take `arm64`.

## UCI

Use Chessenger with a UCI chess GUI or from the command line.
The engine reports itself as `id name Chessenger 1.6`.

### Commands

| Command | What it does |
|---|---|
| `uci` | Engine name, author and options, then `uciok` |
| `isready` | Answers `readyok` |
| `ucinewgame` | Starts a new game |
| `position startpos` or `position fen <FEN>`, optionally followed by `moves <move> ...` | Sets the position |
| `setoption name <name> value <value>` | Sets an option from the table below |
| `go` with `depth`, `wtime`, `btime`, `winc`, `binc`, `movestogo`, `movetime`, `nodes`, `mate`, `infinite`, `ponder`, `searchmoves` | Starts a search, which ends with `bestmove` |
| `stop` | Ends the search; the engine answers with `bestmove` |
| `ponderhit` | The expected move was played: pondering becomes a normal search |
| `quit` | Exits |

Commands outside the UCI protocol, for testing:

| Command | What it does |
|---|---|
| `perft <N>` | Counts the positions N plies ahead (N up to 8) |
| `divide <N>` | The same count split by first move (N up to 6) |
| `d` | Prints the current position (FEN, side to move, castling, en passant) |
| `eval` | Prints the static evaluation of the current position |
| `hash` | Prints the hash key of the current position |

`debug` and `register` are not supported. After `uci`, any command the engine does not know is answered with `info string <command>: command NOT SUPPORTED`.

### Options

| Option | Type | Default | Range | What it does |
|---|---|---|---|---|
| `Hash` | spin | 64 | 1-1024 | Transposition table size in MB |
| `MultiPV` | spin | 1 | 1-218 | Number of best lines reported |
| `Clear Hash` | button | | | Clears the transposition table, killer moves and history |
| `Threads` | spin | 1 | 1-1 | The engine is single-threaded |
| `Ponder` | check | false | | Thinking on the opponent's time (`go ponder`, `ponderhit`) |

## Thanks

- Oliver Brausch, for OliThink 5.10.5, which our search is adapted from (MIT license, see `LICENSES/OLITHINK-MIT.txt`).
- Ronald Friederich, for PeSTO's piece-square tables.
- kz04px, for 4ku, which some of our evaluation weights are based on.
- Morgan Houppin, for Stash 37, which much of our search, king safety, endgame code (including the KPK bitbase) and time management is adapted from.
- The Stockfish developers, for Stockfish: the KPK bitbase in Stash 37, which ours is adapted from, follows the one in Stockfish's `bitbase.cpp`.
- Tomasz Michniewski, for the Simplified Evaluation Function on the Chess Programming Wiki, whose piece-square tables our `eval_data.h` is based on.
- The Chess Programming Wiki and its authors.

## License

GPL-3.0-or-later, see `LICENSE`.
Code adapted from OliThink 5.10.5 is also under its MIT license, see `LICENSES/OLITHINK-MIT.txt`.
Evaluation weights based on 4ku 5.1 are under its MIT license, see `LICENSES/4KU-MIT.txt`.

Chessenger is free software: you can redistribute it and/or modify it under the terms of the GNU General Public
License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any
later version. Chessenger is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
more details.
