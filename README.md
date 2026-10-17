# Chessenger 3.0

Chessenger is a UCI chess engine in C (C99, plus the C23 `#embed` directive for the network file), written by AI coding assistants under the direction of Dariusz Domagała
(see [How Chessenger is made](#how-chessenger-is-made)).

- Version: 3.0 (code date 2026-10-17)
- Strength: 3517.86 [3507.25, 3528.38] (runs A330 and A331: 1800 games, Ordo anchored to MCERL ratings)
- What is new: NNUE evaluation with our own network, built into the binary (one file); the new `nnue` command prints the network evaluation.

The network is our own, trained from scratch with bullet (MIT) on Chessenger's own games, started half from random positions and half from positions of MCERL games run by the author: first G0 (100 million positions scored by Chessenger's classical evaluation), then G1 (423 million positions from Chessenger playing with G0), which is built into the binary. The network architecture and inference code are adapted from Starzix 6.0.

## How Chessenger is made

The C code was written by AI models: about 99% of the work with Anthropic's Claude models (Claude chat in 2025,
Claude Code from 2026), about 1% with Goose and OpenAI's GPT models. The author decides what to try, sets the rules
the AI works by (every statement about the code checked against the code or a measurement), sets the test conditions
(since August 2026, engine games at 2 s + 0.2 s per move with Hash 128 MB, the conditions of MCERL), decides what
stays in the engine, and makes sure that code taken from other engines is attributed and licensed. The AI writes,
builds and tests the code; changes to the engine are checked in a separate session against one question: could
it make the engine weaker, or break legal play or the UCI protocol? The author is responsible for everything published here.

## All versions

Every version is a tagged commit in this repository (`v0.1` … `v3.0`); from 0.2 on its GitHub Release carries the binaries,
and its README says how its strength was measured. 0.1 is Sedar, a program the author wrote in 2002; Chessenger shares
no code with it. Version numbers follow playing strength: 0.6 and 0.7 were skipped, 1.1 to 1.5, 1.7 to 1.9 and 2.1 to 2.9.4 were not published.

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
| 3.0 | 2026-10-17 | 3517.86 [3507.25, 3528.38] |

## How strength was measured

CElo on the scale of MCERL, the Mac Chess Engines Rating List (https://www.chessengeria.eu/mcerl),
which the author maintains. Chessenger played with one thread. A rating is the opponent's MCERL rating plus the
Elo difference measured in the games, and it belongs to the measured binary. MCERL ratings are not directly
comparable with other rating lists such as CCRL or CEGT.

1800 games against fourteen MCERL engines (Stockfish 12, Velvet 8.1.0, Alexandria 5.1.1, Altair 7.0.0, Black Marlin 9.0, rofChade 3.1, Dragon 3.2 MCTS, Akimbo 1.0.0, Arasan 25.0, Wasp 7.00, Viridithas 10.0.0, Texel 1.11, Reckless 0.7.0, Tucano 12), Hash 128 MB, rated with Ordo anchored to their MCERL ratings: 1008 games on a Mac mini M4 Pro at 2 s + 0.2 s per move and 792 on a MacBook Pro M1 Pro, where each engine's time was scaled by its speed ratio to the Mac mini. 216 MacBook games against Arasan 25.0, Velvet 8.1.0 and Wasp 7.00 are left out, because the search depth there measurably differed from the Mac mini.

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

The network file `siec_g1_w01.bin` must lie next to `nnue.c`: the build embeds it in the binary with the C23 `#embed` directive (tested with Apple clang 21, zig 0.17 and llvm-mingw), and a missing file or a different md5 stops the build.

## Binaries

This version's GitHub Release carries ten archives: macOS (`macos-arm64`, `macos-x64`), Linux (`linux-x64`, `linux-arm64`) and Windows
(`windows-x64-avx2`, `windows-x64-bmi2`, `windows-x64-popcnt`, `windows-x64`, `windows-x32`, `windows-arm64`); the release notes
say which fits which machine. If unsure on Windows, take `avx2`; on Windows on ARM, take `arm64`. `windows-x32` needs a CPU with SSE2.
The Windows x86 binaries were built with clang (llvm-mingw) with LTO and profile-guided optimisation; `make release` and `make release-x32` build them with zig without either, with the same nodes.

## UCI

The engine reports itself as `id name Chessenger 3.0` and supports `uci`, `isready`, `ucinewgame`, `position` (`startpos` or `fen`, then `moves`), `setoption`, `go` (`depth`, `wtime`, `btime`, `winc`, `binc`, `movestogo`, `movetime`, `nodes`, `mate`, `infinite`, `ponder`, `searchmoves`), `stop`, `ponderhit` and `quit`; `debug` and `register` are not supported. For testing it also accepts `perft <N>` (N up to 8), `divide <N>` (up to 6), `d` (prints the position), `eval`, `nnue` (network evaluation), `hash` and `bench [depth]` (default 15; also `chessenger bench [depth]` from the command line).

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

Chessenger contains code adapted from OliThink 5.10.5 (Oliver Brausch, MIT), Stash 37 (Morgan Houppin, GPL-3.0), Starzix 6.0 (zzzzz, WTFPL), Stockfish (the Stockfish developers, GPL-3.0), SimpleChess 3.5.0 (Sam Moore, GPL-3.0) and Fathom (Ronald de Man, basil00 and Jon Dart, MIT); each adapted source file says so in its header. It also uses evaluation weights from 4ku (kz04px), piece-square tables from PeSTO (Ronald Friederich) and the Simplified Evaluation Function (Tomasz Michniewski), the bullet trainer (Jamie Whiting) and the Chess Programming Wiki.

## License

GPL-3.0-or-later, see `LICENSE`.
Code adapted from OliThink 5.10.5 is also under its MIT license, see `LICENSES/OLITHINK-MIT.txt`.
Evaluation weights based on 4ku 5.1 are under its MIT license, see `LICENSES/4KU-MIT.txt`.
Fathom, in `fathom/`, is under its MIT license, see `fathom/LICENSE` (the same text is in `LICENSES/FATHOM-MIT.txt`).
Code adapted from SimpleChess 3.5.0, in `main.c`, is under GPL-3.0-only (version 3 only), so `main.c` and the binaries built from it are distributed under GPL version 3 only.
Code adapted from Starzix 6.0, in `nnue.c` and `nnue.h`, was released under the WTFPL version 2, see `LICENSES/STARZIX-WTFPL.txt`.

Chessenger is free software: you can redistribute it and/or modify it under the terms of the GNU General Public
License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any
later version. Chessenger is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
more details.
