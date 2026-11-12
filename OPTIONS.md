# Chessenger 4.2: UCI options and built-in tablebases

This page belongs to the [README](README.md) of Chessenger 4.2.

## Options

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
| `UCI_ShowWDL` | check | false | | With `true`, a chess GUI shows the chances of win, draw and loss |
| `Skill Level` | spin | 10 | 1-10 | Playing strength: 10 is full strength, 1 to 9 play at about 1200 to 2800 Elo (200 apart) |
| `UCI_LimitStrength` | check | false | | With `true`, the engine plays at the strength set by `UCI_Elo` |
| `UCI_Elo` | spin | 1200 | 1200-2800 | Target strength in Elo while `UCI_LimitStrength` is `true` |
| `Use NNUE` | check | true | | With `false`, the engine plays with its classic evaluation instead of the network, on one thread |

## Built-in tablebases

- The binary has the Syzygy tablebases for up to 4 pieces built in, used while SyzygyPath is empty.
- A path set in the GUI replaces them.
- `SyzygyProbeLimit 0` turns tablebase probing off.
- `make TB_EMBEDDED=0` builds without them.
