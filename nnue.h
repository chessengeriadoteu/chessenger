/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 * Portions of this file are adapted from Starzix 6.0 (zzzzz, WTFPL v2, see LICENSES/STARZIX-WTFPL.txt). Modified by Dariusz Domagała, 2026-10-17.
 */
#ifndef NNUE_H
#define NNUE_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "board.h"

#define NNUE_HIDDEN 1024
#define NNUE_BUCKETS 5
#define NNUE_STACK_SIZE 160

typedef struct {
    int16_t acc[2][NNUE_HIDDEN] __attribute__((aligned(64)));
    bool mirror[2];
    int bucket[2];
} NnueAccumulators;

enum { NNUE_STALE = 0, NNUE_CURRENT = 1, NNUE_NULL_MOVE = 2 };
typedef struct {
    NnueAccumulators a;
    Bitboard bb[2][6];
    int state;
} NnueEntry;

typedef struct {
    int16_t acc[NNUE_HIDDEN] __attribute__((aligned(64)));
    Bitboard bb[2][6];
} NnueRefreshEntry;

typedef struct NnueThread {
    NnueEntry stack[NNUE_STACK_SIZE];
    NnueRefreshEntry refresh[2][2][NNUE_BUCKETS];
} NnueThread;

bool nnue_load(const char* net_file);
bool nnue_load_embedded(void);
bool nnue_ready(void);
void nnue_recompute(const Position* pos, NnueAccumulators* a);
int nnue_eval(const NnueAccumulators* a, Color for_side);
int nnue_eval_full(const Position* pos);
int nnue_scale_by_material(const Position* pos, int eval_value);
int nnue_to_search(int scaled_eval);

NnueThread* nnue_thread_new(void);
void nnue_root(NnueThread* w, const Position* pos);
int nnue_eval_node(NnueThread* w, int ply, const Position* pos);

static inline void nnue_after_move(NnueThread* w, int ply, const Position* pos) {
    NnueEntry* e = &w->stack[ply];
    memcpy(e->bb[WHITE], pos->pieces[WHITE], sizeof(e->bb[WHITE]));
    memcpy(e->bb[BLACK], pos->pieces[BLACK], sizeof(e->bb[BLACK]));
    e->state = NNUE_STALE;
}

static inline void nnue_after_null(NnueThread* w, int ply) {
    w->stack[ply].state = NNUE_NULL_MOVE;
}

#endif
