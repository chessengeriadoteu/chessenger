/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#ifndef SEARCH_H
#define SEARCH_H

#include "board.h"
#include "smp.h"
#include "utils.h"
#include <stdbool.h>
#include <sys/time.h>

#define HISTORY_GRAVITY_MAX 512

#define RFP_MAX_DEPTH 4

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
void store_killer(int ply, Move move);
bool is_killer_move(int ply, Move move);
void init_history(void);
void clear_history(void);
void new_search_reset(void);
void update_history(Move move, int depth, bool good_move);
int get_history_score(Move move);
void age_history(void);

bool fast_check_stop(void);
bool check_time(SearchInfo* info);
int negamax_with_pv(Position* pos, int depth, int alpha, int beta, int ply, SearchInfo* info);

Move search_single_depth_with_nodes(Position* pos, int depth, int alpha, int beta, SearchInfo* info);

bool was_root_search_interrupted(void);

#define MAX_MULTIPV 218
extern Move root_excluded[MAX_MULTIPV];
extern int  root_excluded_count;
bool is_root_excluded(const Move* m);

extern Move root_included[MAX_MULTIPV];
extern int  root_included_count;
bool is_root_included(const Move* m);

void set_node_limit(long long limit);
long long get_nodes_searched(void);
bool node_limit_hit(void);

void score_moves(Move* moves, int count, Move tt_move);
int piece_value(int piece);

bool gives_check(Position* pos, Move* move);

extern bool stop_requested;
extern bool position_changed;
extern bool quit_requested;
extern bool bench_queues_input;

bool get_pending_input(char* buffer, int buffer_size);
bool take_partial_input(char* buffer, int buffer_size);
void reset_search_state(void);

void set_search_time_limit(long long time_limit_ms);

void set_search_unbounded(bool unbounded);
void start_search_timer(void);
bool is_time_exceeded(void);

void set_ponder_mode(bool on, long long budget_ms);
bool is_ponder_mode(void);
bool ponderhit_seen(void);

#endif
