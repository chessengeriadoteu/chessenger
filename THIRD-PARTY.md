# Third-party code and data in Chessenger 3.1

Chessenger is under GPL-3.0-or-later (see `LICENSE`). This file lists the code and data of other authors in this source
and in the binaries built from it: what was taken, the author and version, the license, the files that hold it, and where
the license text is. Each adapted source file also says so in its header.

## Code

### OliThink 5.10.5

- Author: Oliver Brausch
- License: MIT, text in `LICENSES/OLITHINK-MIT.txt`
- What: the search is adapted from it, and parts of the time management
- Files: `search.c`, `search_core.c`, `main.c`, `levels/lvl_search.c`, `levels/lvl_driver.c`

### Stash 37

- Author: Morgan Houppin, Copyright (C) 2019-2025
- License: GPL-3.0-or-later, text in `LICENSE`
- What: much of the search, the transposition table, king safety, endgame code (with the KPK bitbase) and time management
  are adapted from it
- Files: `search_core.c`, `tt.c`, `tt.h`, `eval.c`, `main.c`, `levels/lvl_search.c`, `levels/lvl_tt.c`, `levels/lvl_eval.c`,
  `levels/lvl_driver.c`

### Stockfish

- Authors: the Stockfish developers, Copyright (C) 2004-2026 (Stockfish 19)
- License: GPL-3.0-or-later, text in `LICENSE`
- What: the correction history is adapted from Stockfish 19, and futility pruning, capture-history move ordering and
  pruning, and depth adjustments after late move reductions use its formulas with constants rescaled to our evaluation
  scale; the development version of 31 July 2020 guided the extensions, reductions and move ordering; the KPK bitbase of
  Stash 37, which ours is adapted from, follows Stockfish's `bitbase.cpp`
- Files: `search_core.c`, `tune.h`, `levels/lvl_search.c`; the KPK bitbase in `eval.c` and `levels/lvl_eval.c`

### SimpleChess 3.5.0

- Author: Sam Moore
- License: GPL-3.0-only (GPL version 3 only), text in `LICENSE`; the files below and the binaries built from them are
  distributed under GPL version 3 only
- What: the time management uses its rule, with its thresholds, for stopping early when the best move takes most of the
  nodes
- Files: `main.c`, `levels/lvl_driver.c`

### Starzix 6.0

- Author: zzzzz
- License: WTFPL version 2, text in `LICENSES/STARZIX-WTFPL.txt`
- What: the network architecture and inference code are adapted from it
- Files: `nnue.c`, `nnue.h`

### Fathom

- Authors: Ronald de Man (2013-2018), basil00 (2015), Jon Dart (2016-2023)
- License: MIT, text in `fathom/LICENSE` (the same text is in `LICENSES/FATHOM-MIT.txt`, which the binary archives carry)
- What: the Syzygy tablebase probing code, with small changes: `tb_probe_wdl` and `tb_probe_root` are `static inline` in
  `tbprobe.h`, `tbprobe.c` can read the tablebases built into the binary from memory, and `stdendian.h` maps the byte swap
  functions to the ones OpenBSD has
- Files: `fathom/tbprobe.c`, `fathom/tbprobe.h`, `fathom/tbchess.c`, `fathom/tbconfig.h`, `fathom/stdendian.h`

## Data

### 4ku 5.1

- Author: kz04px, Copyright (c) 2021
- License: MIT, text in `LICENSES/4KU-MIT.txt`
- What: some of the evaluation weights (pawns, king, bishop pair, open files) are based on it
- Files: `eval.c`, `levels/lvl_eval.c`

### PeSTO piece-square tables

- Author: Ronald Friederich
- License: none comes with the tables
- What: the piece-square tables in these files share 225 of their 768 values with PeSTO's
- Files: `eval_tables.h`, `levels/lvl_eval.c`

### Simplified Evaluation Function

- Author: Tomasz Michniewski
- License: none stated for the tables; the Chess Programming Wiki page Simplified Evaluation Function
  (https://www.chessprogramming.org/Simplified_Evaluation_Function) gives them as originally posted by the author on the
  Polish chess programming discussion list
- What: the seven piece-square tables in these files are based on it
- Files: `eval_data.h`, `levels/lvl_eval.c`

### Syzygy tablebase files

- Author: generated with the Syzygy tablebase generator by Ronald de Man (https://github.com/syzygy1/tb)
- License: may be freely redistributed (statement of the generator's author), see `LICENSES/SYZYGY-TABLES.txt`
- What: the tablebases for 3 and 4 pieces (35 endings, `.rtbw` and `.rtbz`), built into the binary
- Files: `tablebases/` (70 files)

## Tool, not in this source

### bullet

- Author: Jamie Whiting
- License: MIT
- What: the network `siec_g1_w01.bin` (our own) was trained with it on Chessenger's own games; no code of bullet is in
  this source or in the binaries
