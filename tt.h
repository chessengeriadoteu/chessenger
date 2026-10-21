/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 * Copyright (C) 2019-2025 Morgan Houppin (Stash 37)
 * Portions of this file are adapted from Stash 37. Modified by Dariusz Domagała, 2026-10-21.
 */
#ifndef TT_H
#define TT_H

#include "board.h"
#include <stdint.h>
#include <stddef.h>

typedef enum {
    HASH_ALPHA,
    HASH_BETA,
    HASH_EXACT
    , HASH_NONE
} HashFlag;

typedef struct {
    uint64_t key;
    int16_t  score;
    int16_t  eval;
    uint16_t best_move;
    uint8_t  depth;
    uint8_t  genflag;
} HashEntry;
#define TT_EVAL_NONE 32001
#define TT_BUCKET   4

typedef struct {
    HashEntry* entries;
    int size;
    int mask;
    int age;
} TranspositionTable;

extern uint64_t zobrist_pieces[2][7][64];
extern uint64_t zobrist_castling[16];
extern uint64_t zobrist_en_passant[64];
extern uint64_t zobrist_side_to_move;

void init_zobrist(void);
void init_tt(TranspositionTable* tt, int size_mb);
void clear_tt(TranspositionTable* tt);

int hashfull_tt(const TranspositionTable* tt);
void free_tt(TranspositionTable* tt);

#define TT_HASH_MB_MIN      1
#if UINTPTR_MAX > 0xFFFFFFFFu
#define TT_HASH_MB_MAX      16384
#else
#define TT_HASH_MB_MAX      1024
#endif
#define TT_HASH_MB_DEFAULT  64

size_t resize_tt(TranspositionTable* tt, int size_mb);

uint64_t compute_hash(Position* pos);

uint16_t pack_move(Move m);
Move     unpack_move(uint16_t packed);

void store_hash(TranspositionTable* tt, uint64_t key, int depth, int score, HashFlag flag, Move best_move, int ply);
bool probe_hash(TranspositionTable* tt, uint64_t key, int depth, int alpha, int beta, int* score, Move* best_move, int ply);

bool probe_hash_entry(TranspositionTable* tt, uint64_t key, int* depth, int* flag, int* score, int ply);

bool tt_node_value(TranspositionTable* tt, uint64_t key, int* score, int ply);

typedef struct {
    bool     result_bb;
    int      depth, flag, score, eval;
    uint16_t this_move;
} TTProbe;
bool tt_probe_entry(TranspositionTable* tt, uint64_t key, int ply, TTProbe* out);

void store_hash_eval(TranspositionTable* tt, uint64_t key, int depth, int score, HashFlag flag, Move best_move, int ply, int eval);

void tt_store_eval(TranspositionTable* tt, uint64_t key, int eval);
void tt_store_qs(TranspositionTable* tt, uint64_t key, int score, HashFlag flag, Move best_move, int ply);

extern TranspositionTable main_tt;

static inline void tt_prefetch(const TranspositionTable* tt, uint64_t key) {
    __builtin_prefetch(&tt->entries[(key & (uint64_t)tt->mask) & ~(uint64_t)(TT_BUCKET - 1)]);
}
#  define TT_PF(key) tt_prefetch(&main_tt, (key))

#endif 
