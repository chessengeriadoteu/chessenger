/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#ifndef LEVELS_H
#define LEVELS_H

#include <stdbool.h>
#include <sys/time.h>
#include "../board.h"

typedef struct {
    int    skill;
    bool   limit;
    int    elo;
    bool   env;
    double env_level;
} LevelStrength;

typedef struct {
    bool      infinite;
    bool      ponder;
    bool      stop_pending;
    bool      has_movetime;
    int       movetime;
    bool      has_clock;
    int       wtime, btime, winc, binc, movestogo;
    bool      has_depth;
    int       depth;
    int       mate;
    long long nodes;
    int       multipv;
    int       overhead_ms;
} LevelGo;

typedef struct {
    Move           this_move;
    char           pv[1024];
    long long      node_count;
    long long      go_nodes;
    int            depth_arg;
    int            eval_value;
    struct timeval start;
    bool           depth_mode;
    bool           must_wait;
    bool           level_done;
} LevelResult;

bool lvl_active(const LevelStrength* strength_cfg, bool use_nnue);

void lvl_go(Position* pos, const LevelGo* go, LevelResult* result_bb);

void lvl_pick_move(Position* pos, LevelResult* result_bb);

void lvl_new_game(void);

void lvl_main_buffer(void);

void lvl_bench(const char* const* fen, int n);

#endif
