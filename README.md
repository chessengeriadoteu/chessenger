# Chessenger 2.9.5

Chessenger is a UCI chess engine in C99, written by AI coding assistants under the direction of Dariusz Domagała
(see [How Chessenger is made](#how-chessenger-is-made)).

- Version: 2.9.5 (code date 2026-10-15)
- Strength: 3295.58 [3283.38, 3307.71] (runs A305 and A306: 1764 games, Ordo anchored to MCERL ratings)
- What is new: Evaluation correction learned during search; futility pruning and move ordering for captures from history; reworked late move reductions and quiescence search; time management that stops early when one move takes most of the search and skips iterations it cannot finish; search constants retuned by SPSA; `Hash` up to 16384 MB on 64 bit; `SyzygyProbeDepth` and `Syzygy50MoveRule` options; versions 2.1 to 2.9.4 were not published.

## How Chessenger is made

The C code was written by AI models: about 99% of the work with Anthropic's Claude models (Claude chat in 2025,
Claude Code from 2026), about 1% with Goose and OpenAI's GPT models. The author decides what to try, sets the rules
the AI works by (every statement about the code checked against the code or a measurement), sets the test conditions
(since August 2026, engine games at 2 s + 0.2 s per move with Hash 128 MB, the conditions of MCERL), decides what
stays in the engine, and makes sure that code taken from other engines is attributed and licensed. The AI writes,
builds and tests the code; changes to the engine are checked in a separate session against one question: could
it make the engine weaker, or break legal play or the UCI protocol? The author is responsible for everything published here.

## All versions

Every version is a tagged commit in this repository (`v0.1` … `v2.9.5`); from 0.2 on its GitHub Release carries the binaries,
and its README says how its strength was measured. 0.1 is Sedar, a program the author wrote in 2002; Chessenger shares
no code with it. Version numbers follow playing strength: 0.6 and 0.7 were skipped, 1.1 to 1.5, 1.7 to 1.9 and 2.1 to 2.9.4 were not published, and 3.0 will be the first version with a neural network.

| Version | Code date | Strength (CElo, MCERL scale) |
|---|---|---|
| 0.1 | 2002-07-05 | not measured |
| 0.2 | 2026-08-09 | not measured |
| 0.3 | 2026-08-24 | about 1950 |
| 0.4 | 2026-09-02 | 2186.24 [2143.17, 2229.32] |
| 0.5 | 2026-09-08 | 2593.14 [2574.00, 2612.63] |
| 0.8 | 2026-09-12 | 2872.78 [2852.48, 2894.25] |
| 0.9 | 2026-09-21 | 2932.38 [2919.52, 2945.24] |
| 1.0 | 2026-10-05 | 3052.07 [3037.27, 3066.92] |
| 1.6 | 2026-10-06 | 3146.12 [3130.90, 3161.70] |
| 2.0 | 2026-10-10 | 3216.96 [3203.05, 3230.90] |
| 2.9.5 | 2026-10-15 | 3295.58 [3283.38, 3307.71] |

## How strength was measured

CElo on the scale of MCERL, the Mac Chess Engines Rating List (https://www.chessengeria.eu/mcerl),
which the author maintains. Chessenger played with one thread. A rating is the opponent's MCERL rating plus the
Elo difference measured in the games, and it belongs to the measured binary. MCERL ratings are not directly
comparable with other rating lists such as CCRL or CEGT.

1764 games against twelve MCERL engines (Cheng 4.48, Viridithas 8.0.0, Stockfish 7, Komodo 12.1.1, Komodo 13.02, Stormphrax 1.0.0, Princhess 0.18.0, Texel 1.09, Svart 6, Stockfish 4.5 DD, Marvin 6.1.0, Deep Shredder 13), Hash 128 MB, rated with Ordo anchored to their MCERL ratings: 1008 games on a Mac mini M4 Pro at 2 s + 0.2 s per move and 756 on a MacBook Pro M1 Pro, where each engine's time was scaled by its speed ratio to the Mac mini. 252 MacBook games against Deep Shredder 13, Marvin and Texel are left out, because the search depth there was measurably lower.

## About the source

The source is published without comments and without development switches, with identifiers and messages in English;
the development history stays private. Built with the same flags, it searches exactly the same nodes and plays the same
moves on 24 test positions as the measured binary.

## Build

```
make              # this machine
make release      # all binaries except 32-bit Windows into dist/ (Linux and Windows need zig)
make release-x32  # 32-bit Windows binary into dist/ (needs zig)
```

## Binaries

This version's GitHub Release carries ten archives: macOS (`macos-arm64`, `macos-x64`), Linux (`linux-x64`, `linux-arm64`) and Windows
(`windows-x64-avx2`, `windows-x64-bmi2`, `windows-x64-popcnt`, `windows-x64`, `windows-x32`, `windows-arm64`); the release notes
say which fits which machine. Each archive holds the binary, `LICENSE`, `README.md` and, where the version has one, `LICENSES/`. If unsure on Windows, take `avx2`; on Windows on ARM, take `arm64`.
The Windows x86 binaries were built with clang from llvm-mingw with LTO and profile-guided optimisation; `make release` and
`make release-x32` build them with zig, without either. Both search the same nodes; only speed differs.

## UCI

The engine reports itself as `id name Chessenger 2.9.5` and supports `uci`, `isready`, `ucinewgame`, `position` (`startpos` or `fen`, then `moves`), `setoption`, `go` (`depth`, `wtime`, `btime`, `winc`, `binc`, `movestogo`, `movetime`, `nodes`, `mate`, `infinite`, `ponder`, `searchmoves`), `stop`, `ponderhit` and `quit`; `debug` and `register` are not supported. For testing it also accepts `perft <N>` (N up to 8), `divide <N>` (up to 6), `d` (prints the position), `eval`, `hash` and `bench [depth]` (default 15; also `chessenger bench [depth]` from the command line).

| Option | Type | Default | Range | What it does |
|---|---|---|---|---|
| `Hash` | spin | 64 | 1-16384 | Transposition table size in MB (up to 1024 in the 32-bit Windows binary) |
| `MultiPV` | spin | 1 | 1-218 | Number of best lines reported |
| `Clear Hash` | button | | | Clears the transposition table, killer moves and history |
| `Threads` | spin | 1 | 1-64 | Number of search threads (Lazy SMP) |
| `Ponder` | check | false | | Thinking on the opponent's time (`go ponder`, `ponderhit`) |
| `Move Overhead` | spin | 0 | 0-5000 | Milliseconds taken off the remaining clock time before the engine plans each move (GUI and network delays) |
| `SyzygyPath` | string | `<empty>` | | Folder with Syzygy tablebase files; several folders separated by `:` (`;` on Windows) |
| `SyzygyProbeLimit` | spin | 7 | 0-7 | Largest number of pieces for which the tablebases are probed |
| `SyzygyProbeDepth` | spin | 1 | 1-100 | Smallest search depth at which positions with exactly `SyzygyProbeLimit` pieces are probed |
| `Syzygy50MoveRule` | check | true | | With `false`, tablebase wins and losses spoiled by the 50-move rule count as real wins and losses |
| `UCI_Chess960` | check | false | | Chess960 (Fischer Random): castling is sent as the king capturing its own rook |

## Thanks

- Oliver Brausch: OliThink 5.10.5, which our search is adapted from (MIT, `LICENSES/OLITHINK-MIT.txt`).
- Ronald Friederich: PeSTO's piece-square tables.
- kz04px: 4ku, which some of our evaluation weights are based on.
- Morgan Houppin: Stash 37, which much of our search, king safety, endgame code (with the KPK bitbase) and time management is adapted from.
- The Stockfish developers: our correction history is adapted from Stockfish 19, and our futility pruning, capture-history move ordering and pruning, and depth adjustments after late move reductions use its formulas with constants rescaled to our evaluation scale; the development version of 31 July 2020 guided our extensions, reductions and move ordering; the KPK bitbase of Stash 37 follows Stockfish's `bitbase.cpp`.
- Sam Moore: SimpleChess 3.5.0; our time management uses its rule, with its thresholds, for stopping early when the best move takes most of the nodes.
- Ronald de Man, basil00 and Jon Dart: Fathom, the Syzygy probing code in `fathom/` (MIT, `fathom/LICENSE`).
- Tomasz Michniewski: the Simplified Evaluation Function, whose piece-square tables our `eval_data.h` is based on.
- The Chess Programming Wiki and its authors.

## License

GPL-3.0-or-later, see `LICENSE`.
Code adapted from OliThink 5.10.5 is also under its MIT license, see `LICENSES/OLITHINK-MIT.txt`.
Evaluation weights based on 4ku 5.1 are under its MIT license, see `LICENSES/4KU-MIT.txt`.
Fathom, in `fathom/`, is under its MIT license, see `fathom/LICENSE` (the same text is in `LICENSES/FATHOM-MIT.txt`).
Code adapted from SimpleChess 3.5.0, in `main.c`, is under GPL-3.0-only (version 3 only), so `main.c` and the binaries built from it are distributed under GPL version 3 only.

Chessenger is free software: you can redistribute it and/or modify it under the terms of the GNU General Public
License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any
later version. Chessenger is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
more details.
