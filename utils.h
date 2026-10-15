/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#ifndef UTILS_H
#define UTILS_H

#include "board.h"
#include <stdint.h>
#include <stdbool.h>
#include <sys/time.h>

#define MAX_MOVES 256

#define MAX_DEPTH 128
#define INFINITY_SCORE 50000
#define MATE_SCORE 30000

#define MAX_QUIESCENCE_DEPTH 8

#define EXT_MAX 4

#define PV_MAX_PLY (MAX_DEPTH + EXT_MAX + 2)

#define MAX_UCI_LINE 8192

struct timeval get_wall_time(void);
long long time_diff_ms(struct timeval start, struct timeval end);

static inline int get_lsb(Bitboard bb) {
    if (bb == 0) return -1;

    return __builtin_ctzll(bb);
}

static inline void pop_lsb(Bitboard* bb) {
    if (*bb != 0) {
        *bb &= (*bb - 1);
    }
}

int pop_count(Bitboard bb);

void init_masks(void);

extern Bitboard rank_masks[8];
extern Bitboard file_masks[8];

void print_position(Position* pos);
void print_bitboard(Bitboard bb);
char* move_to_string(Move* move);
void move_to_string_buf(Move* move, char* buffer);
char* position_to_fen(Position* pos);

uint64_t perft(Position* pos, int depth);
uint64_t perft_debug(Position* pos, int depth, int max_depth);
uint64_t perft_verbose(Position* pos, int depth);
char* square_to_string(int square);

bool validate_move_before_send(Position* pos, Move* move);

int static_exchange_evaluation(Position* pos, Move* move);

bool see_is_good_capture(Position* pos, Move* move);

void trim_uci_line(char* line);

#endif
