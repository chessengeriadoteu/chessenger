/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#ifndef SEARCH_H
#define SEARCH_H

#include "board.h"
#include <stdbool.h>
#include <sys/time.h>

#define MAX_DEPTH 32
#define HISTORY_MAX_SCORE 10000

typedef struct {
    struct timeval start_time;
    long long nodes;
    int depth;
    int seldepth;
    int score;
    char pv[1024];
} SearchInfo;

void init_search(void);
void clear_killers(void);
void store_killer(int depth, Move move);
bool is_killer_move(int depth, Move move);
void init_history(void);
void clear_history(void);
void update_history(Move move, int depth, bool good_move);
int get_history_score(Move move);
void age_history(void);

bool fast_check_stop(void);
bool check_time(SearchInfo* info);
int negamax_with_pv(Position* pos, int depth, int alpha, int beta, int ply, SearchInfo* info);
Move search_single_depth_with_nodes(Position* pos, int depth, int alpha, SearchInfo* info);

void score_moves(Move* moves, int count, Move tt_move);
int piece_value(int piece);

int see_capture(Position* pos, Move* move);
bool gives_check(Position* pos, Move* move);

bool get_pending_position_command(char* buffer, int buffer_size);
void reset_search_state(void);

void set_search_time_limit(long long time_limit_ms);
void start_search_timer(void);
bool is_time_exceeded(void);

#endif
