# Chessenger 3.1

Chessenger is a UCI chess engine that also speaks [WinBoard/XBoard](#winboardxboard) and has a [console mode](#console).
It is written in C (C99, plus the C23 `#embed` directive for the network and tablebase files) by AI coding assistants under the direction of Dariusz Domagała (see [How Chessenger is made](#how-chessenger-is-made)).

- **Version:** 3.1 (code date 2026-10-21)
- **Strength:** 3527.40 [3515.91, 3538.79] (runs A356 and A357: 1512 games, Ordo anchored to MCERL ratings)
- **Network:** our own, built into the binary (see [Neural network](#neural-network))
- **Download:** eight binaries for macOS, Linux and Windows (see [Download and run](#download-and-run))
- **Build:** macOS, Linux, Windows, FreeBSD, OpenBSD and Linux on RISC-V (see [Build from source](#build-from-source))

## Contents

- [What is new in 3.1](#what-is-new-in-31)
- [Download and run](#download-and-run)
- [Using Chessenger in a chess GUI (UCI)](#using-chessenger-in-a-chess-gui-uci)
- [WinBoard/XBoard](#winboardxboard)
- [Console](#console)
- [Build from source](#build-from-source)
- [Neural network](#neural-network)
- [Strength](#strength)
- [How Chessenger is made](#how-chessenger-is-made)
- [About the source](#about-the-source)
- [License](#license)

## What is new in 3.1

### For players

- A console mode in English for playing and analysing in a terminal, without a chess GUI: the binary started with `console`, `play`, `analyze` or `annotate`.
- Weaker play on request: `Skill Level` 1-10, or `UCI_LimitStrength` with `UCI_Elo` 1200-2800.
- The WinBoard/XBoard protocol, for older chess GUIs.

### Engine

- Syzygy tablebases for up to 4 pieces built into the binary.
- The `Use NNUE` option.
- Elementary mates (a lone king against enough material) are scored by the endgame rules instead of the network.
- Castling rights and en passant squares in a FEN that cannot be used are dropped.

### Builds and binaries

- `make LIGHT=1` for a light binary without the network and the tablebases inside.
- Eight release binaries, four of them built with profile-guided optimisation.
- On Windows, a command sent through a pipe right after a search command no longer loses its first character.

## Download and run

This version's GitHub Release carries eight archives; each holds the binary, `LICENSE`, `README.md`, `THIRD-PARTY.md` and `LICENSES/`.

| File | For |
|---|---|
| `chessenger-3.1-macos-arm64-pgo.tar.gz` | Mac with Apple Silicon (M1 and newer), macOS 11 or newer (fastest) |
| `chessenger-3.1-macos-x64-avx2.tar.gz` | Mac with Intel CPU with AVX2, macOS 10.9 or newer |
| `chessenger-3.1-linux-arm64-pgo.tar.gz` | Linux, ARM64 (static) (fastest) |
| `chessenger-3.1-linux-x64-avx2-pgo.tar.gz` | Linux, x86-64 with AVX2 (static) (fastest) |
| `chessenger-3.1-windows-x64-avx2.zip` | Windows, most PCs from 2013 on (AVX2); the build rating lists usually use |
| `chessenger-3.1-windows-x64-bmi2.zip` | Windows, Intel Haswell and newer, AMD Zen and newer (x86-64-v3) |
| `chessenger-3.1-windows-arm64-pgo.zip` | Windows on ARM (ARM64) (fastest) |
| `chessenger-3.1-windows-x32.zip` | Windows, 32-bit (needs SSE2): old PCs and 32-bit Windows |

### Which file?

- Windows: if unsure, take `avx2`.
- Windows on ARM: take `arm64`.
- CPU without AVX2: on Windows take `x32`; on Linux and macOS build from source (`make`).
- `-pgo` in a file name means the binary was built with profile-guided optimisation (training run on `bench`), the fastest build for that system, with the same nodes as a build without it.
- `make release` builds the macOS and Linux PGO binaries; `windows-arm64-pgo` was built with clang from llvm-mingw and trained on Windows on ARM, and `make release` builds it with zig, without PGO.

### First start on macOS

A binary downloaded with a web browser is quarantined, and macOS stops it with no message in Terminal.

Unblock it in one of two ways.

**1. In Terminal**, inside the unpacked folder.

Mac with Apple Silicon:

```
xattr -d com.apple.quarantine chessenger-3.1-macos-arm64-pgo
```

Mac with Intel CPU:

```
xattr -d com.apple.quarantine chessenger-3.1-macos-x64-avx2
```

**2. In System Settings:** Privacy & Security > Open Anyway.

### First start on Linux

After unpacking, inside the unpacked folder, run:

```
chmod +x chessenger-3.1-linux-x64-avx2-pgo
```

### First start on Windows

- On Windows 11, Smart App Control may block the unsigned `.exe`.
- It has no exception for a single program, so the binary then runs only with Smart App Control turned off in the Windows Security app (https://support.microsoft.com/en-us/topic/what-is-smart-app-control-285ea03d-fa88-4d56-882e-6698afdb7003).

### VirusTotal

- All eight binaries of this release were scanned on VirusTotal on 2026-10-20.
- Seven: no detections.
- `chessenger-3.1-windows-x32.exe`: 1 detection of 71 (Trapmine, a machine-learning score), which we consider a false positive.

## Using Chessenger in a chess GUI (UCI)

The engine reports itself as `id name Chessenger 3.1`.

- UCI commands: `uci`, `isready`, `ucinewgame`, `position` (`startpos` or `fen`, then `moves`), `setoption`, `go` (`depth`, `wtime`, `btime`, `winc`, `binc`, `movestogo`, `movetime`, `nodes`, `mate`, `infinite`, `ponder`, `searchmoves`), `stop`, `ponderhit` and `quit`.
- Not supported: `debug` and `register`.
- `xboard` switches to the WinBoard/XBoard protocol (see [WinBoard/XBoard](#winboardxboard)).
- `console` switches to the console mode for people (see [Console](#console)).
- `help` lists both.
- For testing: `perft <N>` (N up to 8), `divide <N>` (up to 6), `d` (prints the position), `eval`, `nnue` (network evaluation), `hash`, `bench [depth]` (default 15) and `bench levels` (the playing levels).
- From the command line, after the name of the binary file: `bench [depth]`, `version`, `--help` and the words that start the console (see [Console](#console)).

### Options

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
| `Skill Level` | spin | 10 | 1-10 | Playing strength: 10 is full strength, 1 to 9 play at about 1200 to 2800 Elo (200 apart) |
| `UCI_LimitStrength` | check | false | | With `true`, the engine plays at the strength set by `UCI_Elo` |
| `UCI_Elo` | spin | 1200 | 1200-2800 | Target strength in Elo while `UCI_LimitStrength` is `true` |
| `Use NNUE` | check | true | | With `false`, the engine plays with its classic evaluation instead of the network, on one thread |

### Built-in tablebases

- The binary has the Syzygy tablebases for up to 4 pieces built in, used while SyzygyPath is empty.
- A path set in the GUI replaces them.
- `SyzygyProbeLimit 0` turns tablebase probing off.
- `make TB_EMBEDDED=0` builds without them.

### Weaker play

- `Skill Level` below 10, or `UCI_LimitStrength` with `UCI_Elo`, makes the engine play weaker.
- Weaker play uses a shallower search and a random pick among the best lines, on the classic evaluation.
- Its Elo scale is engine Elo at blitz speed, calibrated in 2300 games between engines at 2 s + 0.2 s per move; it was not measured against people.

## WinBoard/XBoard

- Besides UCI, Chessenger speaks the WinBoard/XBoard protocol (version 2).
- Together with the 32-bit Windows binary for old PCs (see [Download and run](#download-and-run)), this is meant for older chess GUIs on older computers.
- Tested with WinBoard 4.8.0 and the 32-bit Windows binary.
- Nothing has to be set: a GUI of this kind starts with the command `xboard`, and Chessenger switches to that protocol by itself.
- To `protover 2` it answers with its features, among them the options `MultiPV`, `Skill Level`, `UCI_LimitStrength` and `UCI_Elo`.
- Typed by hand, `xboard` switches in the same way, in UCI mode and in the console.

## Console

The console is a text mode for people: playing against Chessenger, analysing and annotating games in a terminal, without a
chess GUI. All its messages are in English.

### Starting the console

Put one of these words after the name of the binary file. The examples use the macOS file; on Linux use the Linux file the
same way, and after your own build `./chessenger`:

```
./chessenger-3.1-macos-arm64-pgo console
./chessenger-3.1-macos-arm64-pgo play white 5+3
./chessenger-3.1-macos-arm64-pgo analyze
./chessenger-3.1-macos-arm64-pgo annotate game.pgn 1
```

- `console` opens the console; in a terminal it waits for commands after the prompt `chessenger> `.
- `play [white|black] [T]` starts a game at once. You take White unless you say `black`; T is the time control, for example `5+3` (5 minutes, 3 seconds added a move), `10` or `40/90`.
- `analyze [FEN]` analyses the start position, or the position given as a FEN (in quotes), until you type `stop`.
- `annotate FILE [S]` marks `?!`, `?` and `??` in game 1 of a PGN file, S seconds a move (default 1), and writes the result to `FILE-annotated.pgn`: for `game.pgn`, `game-annotated.pgn`.

On Windows the same words follow the name of the `.exe` file, for example `chessenger-3.1-windows-x64-avx2.exe console`.
`--help` lists the words, with `bench [DEPTH]` and `version`.

Started without a word, the binary is in UCI mode and waits for a chess GUI; in a terminal it shows its banner and
`Type 'help' for commands, 'console' for the interactive mode.` There `help` lists `console`, `xboard`, `uci` and `quit`,
and `console` opens the console.

### Help

`help` shows all commands, by group:

```
Chessenger 3.1 console.
Play a game: play white 5+3 (you take White; 5 minutes, plus 3 seconds a move).
Details: help <command> or help <group>, for example: help play, help game.

 Game      play  new  undo  redo  switch  hint  resign  draw  clock  time  load
           save  first  prev  next  last  goto  history
 Position  board  fen  setboard  moves  flip
 Analysis  analyze  stop  go  eval  mate  lines  annotate
 Engine    skill  elo  strength  hash  threads  syzygy  ponder  set  options
 Display   color  theme  pieces  pov  post  width
 Info      about  version  license  uci  xboard  quit

Moves: SAN (Nf3, exd5, O-O, e8=Q) or coordinates (g1f3, e7e8q).
```

`help <command>` explains one command with an example, for example `help play`, `help time` or `help elo`;
`help <group>` gives one line for each command of a group, for example `help game` or `help engine`.

### Commands

Moves need no command: type them in SAN (`Nf3`, `exd5`, `O-O`, `e8=Q`) or in coordinates (`g1f3`, `e7e8q`). A square alone
(`d5`) or a capture (`xd5`) plays the only legal move onto it; outside a game several moves can be typed at once (`e4 e5 Nf3`).

Game:

- `play [white|black] [T]`: new game against Chessenger
- `new`: restart the game from the initial position (in a game: a new game with the same sides and time control)
- `undo [N]`, `redo [N]`: take back N moves (default: yours and the reply), replay moves taken back
- `switch`: swap sides, Chessenger moves now
- `hint`: show the move Chessenger suggests for you
- `resign`, `draw`: resign the game, or offer a draw (the offer goes with your move; Chessenger accepts when its score is 0.00 or less)
- `clock`: show both clocks
- `time T`: the time control, `5+3`, `40/90+30`, `move 5`, `depth 12` or `nodes N`
- `load FILE [N]`: load game N (default 1) from a PGN file
- `save [FILE]`: save the game as PGN (default: `chessenger-DATE.pgn`; a file that exists keeps its games, this one is added at the end)
- `first`, `prev [N]`, `next [N]`, `last`, `goto M`: step through the game, or jump to move M (`goto 12` White's move, `goto 12...` Black's)
- `history`: list the moves played so far

Position:

- `board`: show the board, side to move, material and FEN
- `fen`: print the FEN of the current position
- `setboard FEN`: set up a position (the UCI `position` also works)
- `moves`: list the legal moves
- `flip`: turn the board around

Analysis:

- `analyze`: analyse until `stop`; a typed move is played and the analysis starts again on the new position
- `stop`: stop analysing or thinking (or Ctrl+C); in a game Chessenger then plays its best move at once
- `go [LIMITS]`: one search, for example `go depth 20`, `go movetime 5000` or `go nodes N`; nothing is played
- `eval`: static evaluation in pawns, from White's view (no search)
- `mate N`: look for a mate in N moves ("No mate in N found" is not a proof: the search prunes)
- `lines N`: show the N best lines (the option MultiPV)
- `annotate [FILE] [S]`: mark `?!`, `?` and `??` in a PGN game (without FILE: the game on the board), S seconds per move

Engine:

- `skill N`, `elo N`, `strength full`: playing strength, see below
- `hash MB`: hash table size in MB
- `threads N`: number of search threads
- `syzygy PATH`: Syzygy tablebases from PATH instead of the built-in 3-4 piece ones; `syzygy <empty>` goes back to them
- `ponder on|off`: think on your time during a game (off by default)
- `set NAME VALUE`: set any engine option, as `setoption` does (`set Move Overhead 100`)
- `options`: list engine options and their values

Display:

- `color auto|on|off`: colors (auto: in a terminal only; `NO_COLOR` is honored), also `color 16`, `256` or `true`
- `theme NAME`: `dark`, `light`, `mono` or `retro`
- `pieces unicode|ascii`: piece symbols (default: Unicode when the terminal uses UTF-8, otherwise ASCII)
- `pov white|you`: scores in games from White's view or yours
- `post on|off`: in a game, one line of Chessenger's thinking for each depth (off by default)
- `width N`: line width (default: the terminal width from 40 to 80, otherwise 80)

Info:

- `about`, `version`: name, version, build, authors
- `license`: the license (GNU GPL v3) and where to read it
- `help [CMD|GROUP]`: the help above
- `uci`, `xboard`, `quit`: leave the console, see below

Short forms: `d` for `board`, `back` for `undo`, `score` for `eval`, `multipv` for `lines`, `exit` for `quit`, and for `time`:
`level T`, `st S` (`move S`), `sd N` (`depth N`), `sn N` (`nodes N`). The UCI commands `setoption`, `position`, `isready`
and `ucinewgame` work in the console as in UCI mode.

### Playing strength

- `skill N`: level 1 to 10; 10 is full strength, 1 is about 1200 Elo, each level 200 more, 9 about 2800
- `elo N`: play at about N Elo, from 1200 to 2800
- `strength full`: back to full strength; `strength` alone shows the setting, for example `Strength: skill level 5 (about 2000 Elo, calibrated in 2300 games at 2+0.2, classic evaluation).`

A level holds in your games (`play`); `analyze`, `go`, `hint` and `annotate` keep full strength. The weaker play has its own
search, in the folder `levels/` of the source: shallower, with a random pick among its best lines, on the classic evaluation
and without the built-in tablebases. Its Elo is engine Elo at blitz speed, calibrated in 2300 games between engines at
2 s + 0.2 s per move, not measured against people. In a GUI, the UCI options `Skill Level`, `UCI_LimitStrength` and
`UCI_Elo` set it (see [UCI](#using-chessenger-in-a-chess-gui-uci)).

### Leaving the console

- `uci`: back to UCI mode; Chessenger then answers as to a GUI (`id name Chessenger 3.1`, its options, `uciok`)
- `xboard`: to the WinBoard/XBoard protocol (see [WinBoard/XBoard](#winboardxboard))
- `quit` or `exit`: end Chessenger

## Build from source

Chessenger has been adapted to be built and used on the systems below; each link leads to its build steps.

- [macOS, Apple Silicon](#macos-apple-silicon)
- [macOS, Intel](#macos-intel)
- [Linux](#linux): x86-64 and ARM64
- [Windows, Command Prompt](#windows-command-prompt): x86-64, 32-bit x86 and ARM64
- [Windows, MSYS2](#windows-msys2): x86-64 and ARM64
- [FreeBSD](#freebsd): ARM64
- [OpenBSD](#openbsd): ARM64
- [Linux on RISC-V](#linux-on-risc-v): cross-compiled with zig, tested under QEMU (user mode)
- [All release binaries at once](#all-release-binaries-at-once)
- [Light binary, build without tablebases and neural network (NNUE)](#light-binary-build-without-tablebases-and-neural-network-nnue)

### Before you build

- Every standard build (not the light builds) puts the network file `siec_g1_w01.bin` (next to `nnue.c`) and the 70 tablebase files in `tablebases/` into the binary with the C23 `#embed` directive.
- So the compiler must know `#embed`: gcc 15 or newer, or a recent clang.
- Tested: gcc 15.2, Apple clang 21, clang 21 on FreeBSD, clang 22 on OpenBSD, clang 22 in zig 0.17, clang 23 in llvm-mingw and in MSYS2.
- A missing file or a network with a different md5 stops a `make` build.

### Check your build

Run `bench`. The last lines must show `Nodes searched  : 3307130`, the same nodes as every release binary.

```
./chessenger bench
```

On Windows:

```
chessenger.exe bench
```

The playing levels must show `total nodes 24620884`:

```
./chessenger bench levels
```

### macOS, Apple Silicon

**Needs**

- The Xcode Command Line Tools, which bring clang and make:

```
xcode-select --install
```

**Build**

```
make
./chessenger bench
```

**Tested**

- Apple clang 21, on an M1 and an M4.

### macOS, Intel

**Build** on an Intel Mac (`make` builds for Intel, `-march=x86-64-v2`):

```
make
./chessenger bench
```

**Build** an Intel binary on a Mac with Apple Silicon:

```
arch -x86_64 make CC="cc -arch x86_64"
```

**Tested**

- The Intel binary built on Apple Silicon was checked under Rosetta 2.
- `make` on an Intel Mac was not tried.

### Linux

**Needs**

- make and gcc 15 or newer (the default gcc of Ubuntu 26.04 is 15.2).

**Build**

```
make
./chessenger bench
```

**Tested**

- gcc 15.2 on Ubuntu 26.04 ARM64.
- On x86-64 Linux, `make` runs the same way; there it was checked with binaries built by zig (`make release`, below) and run on Ubuntu 24.04 in WSL 1.

**Notes**

- gcc prints some warnings (for example unused variables); the binary still searches the same nodes.

### Windows, Command Prompt

**Needs**

- llvm-mingw (clang for Windows), for example:

```
winget install MartinStorsjo.LLVM-MinGW.UCRT
```

**Build:** open Command Prompt in the source folder and run the two lines for your Windows.

64-bit Windows, for CPUs with AVX2 and BMI2 (`-march=x86-64-v3`):

```
clang -std=c99 -O3 -flto -DNDEBUG -DSMP=1 -pthread -DTB_EMBEDDED=1 -D_WIN32_WINNT=0x0A00 --embed-dir=tablebases -march=x86-64-v3 -static -s -o chessenger.exe *.c fathom\tbprobe.c levels\levels.c -lm
chessenger.exe bench
```

32-bit Windows (`-march=pentium4`):

```
i686-w64-mingw32-clang -std=c99 -O3 -flto -DNDEBUG -DSMP=1 -pthread -DTB_EMBEDDED=1 -D_WIN32_WINNT=0x0A00 --embed-dir=tablebases -march=pentium4 -static -s -o chessenger.exe *.c fathom\tbprobe.c levels\levels.c -lm
chessenger.exe bench
```

Windows on ARM (no `-march`):

```
aarch64-w64-mingw32-clang -std=c99 -O3 -flto -DNDEBUG -DSMP=1 -pthread -DTB_EMBEDDED=1 -D_WIN32_WINNT=0x0A00 --embed-dir=tablebases -static -s -o chessenger.exe *.c fathom\tbprobe.c levels\levels.c -lm
chessenger.exe bench
```

**Tested**

- llvm-mingw 20260908 (clang 23.1.1).
- All three were built on Windows 11 x64: the 64-bit and the 32-bit binary ran there, the ARM64 binary on Windows 11 ARM64.
- Building on Windows on ARM itself was tested with MSYS2 (below).

**Notes**

- The Makefile checks the network file with POSIX shell tools (`md5sum`), which the Windows Command Prompt does not have, so here the compiler is called directly.
- In MSYS2 make works.

### Windows, MSYS2

**Needs**

- MSYS2 (https://www.msys2.org/) with make and clang.

**Build** in the MSYS2 CLANG64 shell (x86-64):

```
pacman -S make mingw-w64-clang-x86_64-clang
make
./chessenger.exe bench
```

**Build** in the MSYS2 CLANGARM64 shell (Windows on ARM), where `ARCH=` leaves out `-march=x86-64-v3`:

```
pacman -S make mingw-w64-clang-aarch64-clang
make ARCH=
./chessenger.exe bench
```

**Build a binary that also runs outside MSYS2**, for example in a GUI (the binaries above need `libwinpthread-1.dll` from MSYS2, so they run inside that shell).

x86-64:

```
make LDLIBS="-static -lm"
```

Windows on ARM:

```
make ARCH= LDLIBS="-static -lm"
```

**Tested**

- clang 23.1.3 on Windows 11 x64 (CLANG64) and Windows 11 ARM64 (CLANGARM64).

**Notes**

- clang prints three warnings (variables set but not used); the binary still searches the same nodes.

### FreeBSD

**Needs**

- GNU make; the clang of the base system builds it:

```
pkg install gmake
```

**Build**

```
gmake
./chessenger bench
```

**Tested**

- FreeBSD 14.5 ARM64 with clang 21.1.8, in a virtual machine (QEMU on an M4).

**Notes**

- The `make` of the base system does not read this Makefile.
- clang prints one warning (`gettimeofday` in `utils.c`); the binary still searches the same nodes.

### OpenBSD

**Needs**

- GNU make; the clang of the base system builds it:

```
pkg_add gmake
```

**Build**

```
gmake
./chessenger bench
```

**Tested**

- OpenBSD 8.0 ARM64 with clang 22.1.6, in a virtual machine (QEMU on an M4).

**Notes**

- The linker prints three warnings of the OpenBSD C library (`strcpy`, `sprintf`, `strcat`); the binary still searches the same nodes.

### Linux on RISC-V

Cross-compiled with zig, tested under QEMU (user mode).

**Step 1:** build on a Mac with zig 0.17 (tested on an M4):

```
make CC="zig cc -target riscv64-linux-musl -D_POSIX_C_SOURCE=200809L -static" ARCH=""
```

**Step 2:** run on Linux with QEMU (package `qemu-user` on Ubuntu):

```
qemu-riscv64 ./chessenger bench
```

**Tested**

- It ran under QEMU on Ubuntu 26.04 ARM64, not on RISC-V hardware.

**Notes**

- zig prints one note about the hard-float ABI; the binary still searches the same nodes.

### All release binaries at once

**Needs**

- A Mac with Apple Silicon, zig 0.17 and Homebrew LLVM 22 (`LLVM_BIN` points to it):

```
brew install zig llvm@22
```

**Build**

macOS, Linux and Windows binaries into `dist/` (the macOS and Linux ones with PGO, except `macos-x64-avx2`; the Windows ones without):

```
make release
```

32-bit Windows binary into `dist/`:

```
make release-x32
```

**Tested**

- On an M4: seven of the eight release files come out byte for byte the same as the release files.
- `windows-arm64-pgo` was built with llvm-mingw on Windows, and `make release` builds `windows-arm64` with zig, without PGO.

### Light binary, build without tablebases and neural network (NNUE)

With make (macOS, Linux); run `make clean` first when switching between them.

Without the network and the tablebases inside:

```
make LIGHT=1
```

With the network inside; tablebases only from SyzygyPath:

```
make TB_EMBEDDED=0
```

Release binaries named with `-light`:

```
make release LIGHT=1
```

**How the light binary finds its files**

- The network `siec_g1_w01.bin`: from the UCI option EvalFile, from its own folder or from the current folder.
- The Syzygy tablebases: from the folder `tablebases/` next to it or from SyzygyPath.
- Without a network it plays on the classic evaluation and says so (`bench` then gives 4554810 nodes).
- If the path of the binary's folder contains the path list separator (`:` on macOS and Linux, `;` on Windows), the light binary does not find `tablebases/` next to it, so keep it in a folder without that character.

**Tested**

- Both were tested on macOS (M4) and Ubuntu 26.04 ARM64.

## Neural network

- The network is our own, trained from scratch with bullet (MIT) on Chessenger's own games.
- The games started half from random positions and half from positions of MCERL games run by the author.
- Two generations of training data:
  - G0: 100 million positions scored by Chessenger's classical evaluation.
  - G1: 423 million positions from Chessenger playing with G0.
- The network trained on G1 is built into the binary (the file `siec_g1_w01.bin`).
- The network architecture and inference code are adapted from Starzix 6.0.
- With the UCI option `Use NNUE` set to `false`, the engine plays with its classic evaluation instead of the network, on one thread.

## Strength

Chessenger 3.1: 3527.40 [3515.91, 3538.79] (runs A356 and A357: 1512 games, Ordo anchored to MCERL ratings).

### How strength was measured

- Scale: CElo of MCERL, the Mac Chess Engines Rating List (https://www.chessengeria.eu/mcerl), which the author maintains.
- MCERL ratings are not directly comparable with other rating lists such as CCRL or CEGT.
- A rating is the opponent's MCERL rating plus the Elo difference measured in the games, and it belongs to the measured binary.
- Chessenger played with one thread, Hash 128 MB.
- 1512 games against fourteen MCERL engines, rated with Ordo anchored to their MCERL ratings:
  - 1008 games on a Mac mini M4 Pro at 2 s + 0.2 s per move;
  - 504 games on a MacBook Pro M1 Pro, where each engine's time was scaled by its speed ratio to the Mac mini (1.3498 for Chessenger).
- The fourteen engines: Stockfish 12, Velvet 8.1.0, Alexandria 5.1.1, Altair 7.0.0, Black Marlin 9.0, rofChade 3.1, Dragon 3.2 MCTS, Akimbo 1.0.0, Arasan 25.0, Wasp 7.00, Viridithas 10.0.0, Texel 1.11, Reckless 0.7.0, Tucano 12.
- Left out: 504 MacBook games against Altair 7.0.0, Arasan 25.0, Black Marlin 9.0, Dragon 3.2 MCTS, Reckless 0.7.0, Velvet 8.1.0 and Wasp 7.00, because the search depth there measurably differed from the Mac mini.

### All versions

- Every version is a tagged commit in this repository (`v0.1` … `v3.1`).
- From 0.2 on, its GitHub Release carries the binaries, and its README says how its strength was measured.
- 0.1 is Sedar, a program the author wrote in 2002; Chessenger shares no code with it.
- Up to 3.0, version numbers follow playing strength: 0.6 and 0.7 were skipped, 1.1 to 1.5, 1.7 to 1.9 and 2.1 to 2.9.4 were not published.

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
| 3.1 | 2026-10-21 | 3527.40 [3515.91, 3538.79] |

## How Chessenger is made

- The C code was written by AI models: about 99% of the work with Anthropic's Claude models (Claude chat in 2025, Claude Code from 2026), about 1% with Goose and OpenAI's GPT models.

**The author**

- decides what to try;
- sets the rules the AI works by (every statement about the code checked against the code or a measurement);
- sets the test conditions (since August 2026, engine games at 2 s + 0.2 s per move with Hash 128 MB, the conditions of MCERL);
- decides what stays in the engine;
- makes sure that code taken from other engines is attributed and licensed.

**The AI**

- writes, builds and tests the code;
- has changes to the engine checked in a separate session against one question: could it make the engine weaker, or break legal play or the UCI protocol?

The author is responsible for everything published here.

## About the source

- The source is published without comments and without development switches, with identifiers and messages in English.
- The development history stays private.
- Built with the same flags, it searches exactly the same nodes and plays the same moves on 24 test positions as a build of the development source.

## Thanks

Chessenger contains code adapted from these engines; each adapted source file says so in its header:

- OliThink 5.10.5 (Oliver Brausch, MIT)
- Stash 37 (Morgan Houppin, GPL-3.0-or-later)
- Starzix 6.0 (zzzzz, WTFPL)
- Stockfish (the Stockfish developers, GPL-3.0-or-later)
- SimpleChess 3.5.0 (Sam Moore, GPL-3.0-only)
- Fathom (Ronald de Man, basil00 and Jon Dart, MIT)

It also uses:

- evaluation weights from 4ku (kz04px)
- piece-square tables from PeSTO (Ronald Friederich) and the Simplified Evaluation Function (Tomasz Michniewski)
- the bullet trainer (Jamie Whiting)
- the Chess Programming Wiki

## License

- GPL-3.0-or-later, see `LICENSE`.
- Code adapted from OliThink 5.10.5 is also under its MIT license, see `LICENSES/OLITHINK-MIT.txt`.
- Evaluation weights based on 4ku 5.1 are under its MIT license, see `LICENSES/4KU-MIT.txt`.
- Fathom, in `fathom/`, is under its MIT license, see `fathom/LICENSE` (the same text is in `LICENSES/FATHOM-MIT.txt`).
- Code adapted from SimpleChess 3.5.0, in `main.c` and `levels/lvl_driver.c`, is under GPL-3.0-only (version 3 only), so these files and the binaries built from them are distributed under GPL version 3 only.
- Code adapted from Starzix 6.0, in `nnue.c` and `nnue.h`, was released under the WTFPL version 2, see `LICENSES/STARZIX-WTFPL.txt`.
- The Syzygy tablebase files built into the binary (`tablebases/`) were generated with Ronald de Man's generator and may be freely redistributed, see `LICENSES/SYZYGY-TABLES.txt`.

Chessenger is free software: you can redistribute it and/or modify it under the terms of the GNU General Public
License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any
later version. Chessenger is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
more details.
