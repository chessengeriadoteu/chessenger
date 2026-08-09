/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#ifndef TT_H
#define TT_H

#include "board.h"
#include <stdint.h>

typedef enum {
    HASH_ALPHA,
    HASH_BETA,
    HASH_EXACT
} HashFlag;

typedef struct {
    uint64_t key;
    int depth;
    int score;
    HashFlag flag;
    Move best_move;
    int age;
} HashEntry;

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
void free_tt(TranspositionTable* tt);

uint64_t compute_hash(Position* pos);
void store_hash(TranspositionTable* tt, uint64_t key, int depth, int score, HashFlag flag, Move best_move);
bool probe_hash(TranspositionTable* tt, uint64_t key, int depth, int alpha, int beta, int* score, Move* best_move);

extern TranspositionTable main_tt;

#endif 
