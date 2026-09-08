/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT
 * Copyright (C) Dariusz Domagała
 * Copyright (C) Oliver Brausch (OliThink 5.10.5, MIT, see LICENSES/OLITHINK-MIT.txt)
 * Portions of this file are adapted from OliThink 5.10.5. Modified by Dariusz Domagała, 2026-09-08.
 */
#include "search.h"
#include "board.h"
#include "eval.h"
#include "movegen.h"
#include "utils.h"
#include "tt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifndef _WIN32
#include <fcntl.h>
#endif
#include <errno.h>
#include <sys/time.h>
#ifdef _WIN32
int win_stdin_ready(void);
int win_stdin_read(int fd, void* buf, unsigned int count);
#endif

#define FULL_MOVES_TO_SEARCH 3
#define MIN_LMR_DEPTH 2

#define CHECK_STOP_FREQUENCY 4096

#define MAX_TRIED_QUIETS 64

#define NULL_MOVE_DEPTH_THRESHOLD 2

#define RAZOR_MARGIN 300
#define RAZOR_DEPTH_LIMIT 3

#define DELTA_MARGIN 900

#define DELTA_MOVE_MARGIN 200

_Static_assert(MAX_MOVES == 256, "Move[256] literals in search.c assume MAX_MOVES==256");

static char pending_input[MAX_UCI_LINE * 2] = "";
static bool has_pending_input = false;
static int check_counter = 0;

static bool root_search_interrupted = false;

bool was_root_search_interrupted(void) {
    return root_search_interrupted;
}

static char partial_line[MAX_UCI_LINE] = "";

static bool partial_overflow = false;

static long long search_time_limit_ms = 0;
static struct timeval search_start_time;

#define CURRMOVE_GATE_MS 3000

static bool search_timer_started = false;

static bool search_unbounded = false;

void set_search_unbounded(bool unbounded) {
    search_unbounded = unbounded;
}

static int history_table[64][64];

static Move killer_moves[PV_MAX_PLY][2];

static Move pv_table[PV_MAX_PLY][PV_MAX_PLY];
static int pv_length[PV_MAX_PLY];

static int  eval_stack[PV_MAX_PLY];
static bool eval_stack_valid[PV_MAX_PLY];

static bool null_move_at[PV_MAX_PLY];

static uint16_t se_excluded[PV_MAX_PLY];

static const int nullvar_table[] = {13, 43, 149, 519, 1809, 6311, 22027};
static int null_variance(int delta) {
    int r = 0;
    if (delta >= 4) for (r = 1; r <= 7; r++) if (delta < nullvar_table[r - 1]) break;
    return r;
}

static bool ponder_mode = false;
static long long ponder_budget_ms = 0;
static bool ponderhit_received = false;

static bool      node_limit_active = false;
static long long node_limit = 0;
static long long nodes_searched = 0;
static bool      node_limit_reached = false;

void reset_search_state(void) {
    stop_requested = false;
    position_changed = false;
    has_pending_input = false;
    pending_input[0] = '\0';

    search_unbounded = false;

    partial_overflow = false;

    ponder_mode = false;
    ponder_budget_ms = 0;
    ponderhit_received = false;

    node_limit = 0;
    node_limit_active = false;
    nodes_searched = 0;
    node_limit_reached = false;
    root_included_count = 0;

}

bool get_pending_input(char* buffer, int buffer_size) {
    if (has_pending_input) {
        strncpy(buffer, pending_input, buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
        has_pending_input = false;
        pending_input[0] = '\0';
        return true;
    }
    return false;
}

bool take_partial_input(char* buffer, int buffer_size) {
    if (partial_line[0] == '\0' || buffer_size <= 0) return false;
    strncpy(buffer, partial_line, buffer_size - 1);
    buffer[buffer_size - 1] = '\0';
    partial_line[0] = '\0';
    return true;
}

static bool queue_pending_line(const char* line) {
    size_t used = strlen(pending_input);
    size_t len  = strlen(line);

    if (used + len + 2 > sizeof(pending_input)) {

        printf("info string ERROR: pending input queue full; command dropped: %.40s\n", line);
        fflush(stdout);
        return false;
    }

    memcpy(pending_input + used, line, len);
    pending_input[used + len] = '\n';
    pending_input[used + len + 1] = '\0';
    has_pending_input = true;
    return true;
}

void set_node_limit(long long limit) {
    node_limit = limit > 0 ? limit : 0;
    node_limit_active = node_limit > 0;
}

long long get_nodes_searched(void) {
    return nodes_searched;
}

bool node_limit_hit(void) {
    return node_limit_reached;
}

void set_search_time_limit(long long time_limit_ms) {
    search_time_limit_ms = time_limit_ms;
}

void start_search_timer(void) {
    search_start_time = get_wall_time();
    search_timer_started = true;
}

static bool currmove_gate_open(void) {
    if (!search_timer_started) return false;
    return time_diff_ms(search_start_time, get_wall_time()) >= CURRMOVE_GATE_MS;
}

bool is_time_exceeded(void) {
    if (search_time_limit_ms <= 0) return false;

    long long elapsed = time_diff_ms(search_start_time, get_wall_time());

    return elapsed >= search_time_limit_ms;
}

void init_search(void) {
    stop_requested = false;
    position_changed = false;
    init_history();
    clear_killers();

    for (int i = 0; i < PV_MAX_PLY; i++) {
        eval_stack[i]       = 0;
        eval_stack_valid[i] = false;
        null_move_at[i]      = false;
        pv_length[i] = 0;
        for (int j = 0; j < PV_MAX_PLY; j++) {
            pv_table[i][j] = (Move){0};
        }
    }
}

void clear_killers(void) {

    for (int i = 0; i < PV_MAX_PLY; i++) {
        killer_moves[i][0] = (Move){0};
        killer_moves[i][1] = (Move){0};
    }
}

void store_killer(int ply, Move move) {

    if (ply >= 0 && ply < PV_MAX_PLY) {

        if (killer_moves[ply][0].from == move.from &&
            killer_moves[ply][0].to == move.to) {
            return;
        }

        killer_moves[ply][1] = killer_moves[ply][0];
        killer_moves[ply][0] = move;
    }
}

bool is_killer_move(int ply, Move move) {

    if (ply >= 0 && ply < PV_MAX_PLY) {
        return (killer_moves[ply][0].from == move.from &&
                killer_moves[ply][0].to == move.to) ||
               (killer_moves[ply][1].from == move.from &&
                killer_moves[ply][1].to == move.to);
    }
    return false;
}

void init_history(void) {
    for (int i = 0; i < 64; i++) {
        for (int j = 0; j < 64; j++) {
            history_table[i][j] = 0;
        }
    }
}

void clear_history(void) {
    init_history();
}

void update_history(Move move, int depth, bool good_move) {
    if (move.from >= 0 && move.from < 64 && move.to >= 0 && move.to < 64) {

        int b = depth * depth;
        if (b > HISTORY_GRAVITY_MAX) b = HISTORY_GRAVITY_MAX;
        if (!good_move) b = -b;
        int h = history_table[move.from][move.to];
        h += b - h * (b < 0 ? -b : b) / HISTORY_GRAVITY_MAX;
        if (h >  HISTORY_GRAVITY_MAX) h =  HISTORY_GRAVITY_MAX;
        if (h < -HISTORY_GRAVITY_MAX) h = -HISTORY_GRAVITY_MAX;
        history_table[move.from][move.to] = h;
    }
}

void new_search_reset(void) {
    init_history();

    clear_killers();
}

int get_history_score(Move move) {
    if (move.from >= 0 && move.from < 64 && move.to >= 0 && move.to < 64) {
        return history_table[move.from][move.to];
    }
    return 0;
}

void age_history(void) {
    for (int i = 0; i < 64; i++) {
        for (int j = 0; j < 64; j++) {
            history_table[i][j] /= 2;
        }
    }
}

bool gives_check(Position* pos, Move* move) {

    UndoInfo undo;
    make_move_full(pos, move, &undo);
    bool is_check = is_in_check(pos, pos->to_move);
    undo_move_full(pos, move, &undo);
    return is_check;
}

int piece_value(int piece) {
    switch (piece) {
        case PAWN: return 100;
        case KNIGHT: return 325;
        case BISHOP: return 325;
        case ROOK: return 500;
        case QUEEN: return 975;
        case KING: return 0;
        default: return 0;
    }
}

#define QS_CHECK_PLIES 0

int quiescence_search(Position* pos, int alpha, int beta, int ply, int qply, SearchInfo* info) {
    info->nodes++;

    if (node_limit_active) nodes_searched++;

    if (ply > info->seldepth) {
        info->seldepth = ply;
    }

    if (fast_check_stop()) {
        return alpha;
    }

    if (qply >= MAX_QUIESCENCE_DEPTH) {
        return evaluate_position(pos);
    }

    uint64_t tt_key = pos->hash;
    {
        int tt_score;
        if (probe_hash(&main_tt, tt_key, 0, alpha, beta, &tt_score, NULL, ply)) {
            return tt_score;
        }
    }
    int alpha_orig = alpha;

    bool in_check = is_in_check(pos, pos->to_move);

    int stand_pat = 0;
    bool have_stand_pat = false;

    int hung_recapture = 0;

    if (!in_check) {
        stand_pat = evaluate_position(pos);
        have_stand_pat = true;

        hung_recapture = hung_penalty_stm;

        if (stand_pat >= beta) {

            store_hash(&main_tt, tt_key, 0, beta, HASH_BETA, (Move){0}, ply);
            return beta;
        }

        if (stand_pat + DELTA_MARGIN < alpha) {
            return alpha;
        }

        if (stand_pat > alpha) {
            alpha = stand_pat;
        }
    }

    Move all_moves[256];
    int all_count;

    const bool qs_noisy_only = (!in_check && qply >= QS_CHECK_PLIES);
    if (qs_noisy_only) {
        all_count = generate_legal_noisy(pos, all_moves);
    } else if (in_check) {

        all_count = generate_legal_evasions(pos, all_moves);
    } else {
        all_count = generate_legal_moves(pos, all_moves);
    }

    if (all_count == 0 && !qs_noisy_only) {

        int terminal_score = in_check ? (-MATE_SCORE + ply) : 0;
        store_hash(&main_tt, tt_key, 0, terminal_score, HASH_EXACT, (Move){0}, ply);
        return terminal_score;
    }

    Move tactical_moves[256];
    int tactical_count = 0;

    for (int i = 0; i < all_count; i++) {
        bool is_capture   = (all_moves[i].captured != EMPTY);
        bool is_promotion = (all_moves[i].promotion != EMPTY);

        bool is_check = false;
        if (!in_check && !is_capture && !is_promotion && qply < QS_CHECK_PLIES) {

            is_check = gives_check(pos, &all_moves[i]);
        }

        if (!in_check && !is_capture && !is_promotion && !is_check) {
            continue;
        }

        if (have_stand_pat && is_capture && !is_promotion &&
            stand_pat + piece_value(all_moves[i].captured) + hung_recapture
                + DELTA_MOVE_MARGIN < alpha) {
            continue;
        }

        Move m = all_moves[i];

        if (is_capture) {

            m.score = static_exchange_evaluation(pos, &m);

            if (!in_check && m.score < 0) {
                continue;
            }

            if (m.score > 0) {
                m.score += 1000;
            }
        } else if (is_check) {
            m.score = 200 + (qply == 0 ? 50 : 30);
        } else {
            m.score = 0;
        }

        if (is_promotion && !is_capture) {
            m.score += piece_value(m.promotion);
        }

        tactical_moves[tactical_count++] = m;
    }

    for (int i = 0; i < tactical_count - 1; i++) {
        for (int j = i + 1; j < tactical_count; j++) {
            if (tactical_moves[i].score < tactical_moves[j].score) {
                Move temp = tactical_moves[i];
                tactical_moves[i] = tactical_moves[j];
                tactical_moves[j] = temp;
            }
        }
    }

    for (int i = 0; i < tactical_count; i++) {
        UndoInfo undo;
        make_move_full(pos, &tactical_moves[i], &undo);

        int score = -quiescence_search(pos, -beta, -alpha, ply + 1, qply + 1, info);

        undo_move_full(pos, &tactical_moves[i], &undo);

        if (fast_check_stop()) {
            return alpha;
        }

        if (score >= beta) {

            store_hash(&main_tt, tt_key, 0, beta, HASH_BETA, tactical_moves[i], ply);
            return beta;
        }

        if (score > alpha) {
            alpha = score;
        }
    }

    store_hash(&main_tt, tt_key, 0, alpha,
               (alpha > alpha_orig) ? HASH_EXACT : HASH_ALPHA, (Move){0}, ply);
    return alpha;
}

static void pick_best_move(Move* moves, int move_count, int s) {
    int pi = s;
    int vmax = moves[s].score;
    for (int i = s + 1; i < move_count; i++) {
        if (moves[i].score > vmax) {
            vmax = moves[i].score;
            pi = i;
        }
    }
    if (pi != s) {
        Move tmp = moves[s];
        moves[s]  = moves[pi];
        moves[pi] = tmp;
    }
}
int negamax_with_pv(Position* pos, int depth, int alpha, int beta, int ply, SearchInfo* info) {
    info->nodes++;

    if (node_limit_active) nodes_searched++;

    if (ply < PV_MAX_PLY) pv_length[ply] = ply;

    if (ply > info->seldepth) {
        info->seldepth = ply;
    }

    if (is_threefold_repetition(pos) || pos->halfmove_clock >= 100) {
        return 0;
    }

    if (fast_check_stop()) {
        return alpha;
    }

    bool in_check = is_in_check(pos, pos->to_move);

    if (in_check && ply + depth < MAX_DEPTH + EXT_MAX) {
        depth++;
    }

    if (depth <= 0) {
        return quiescence_search(pos, alpha, beta, ply, 0, info);
    }

    uint64_t tt_key = pos->hash;

    const uint16_t se_excluded_move = (ply < PV_MAX_PLY) ? se_excluded[ply] : 0;

    Move tt_move = {0};

    if (se_excluded_move == 0) {
        int tt_score;

        if (probe_hash(&main_tt, tt_key, depth, alpha, beta, &tt_score, &tt_move, ply)
            && beta - alpha == 1) {
            return tt_score;
        }
    }

    int  static_eval      = 0;
    bool have_static_eval = false;

    bool improving = true;
    if (ply >= 0 && ply < PV_MAX_PLY) {
        if (in_check) {
            eval_stack_valid[ply] = false;
        } else {

            if (!have_static_eval) {
                int tt_eval;
                if (tt_node_value(&main_tt, tt_key, &tt_eval, ply)) {
                    static_eval      = tt_eval;
                    have_static_eval = true;
                }
            }
            if (!have_static_eval) {
                static_eval      = evaluate_position(pos);
                have_static_eval = true;
            }
            eval_stack[ply]       = static_eval;
            eval_stack_valid[ply] = true;
            if (ply >= 2 && eval_stack_valid[ply - 2]) {
                improving = (eval_stack[ply] >= eval_stack[ply - 2]);
            }
        }
    }

    if (depth <= RAZOR_DEPTH_LIMIT && !in_check && ply > 0) {
        if (!have_static_eval) {
            static_eval = evaluate_position(pos);
            have_static_eval = true;
        }
        if (static_eval + RAZOR_MARGIN <= alpha) {
            int razor_score = quiescence_search(pos, alpha, beta, ply, 0, info);
            if (razor_score <= alpha) {
                return razor_score;
            }
        }
    }

    if (depth >= NULL_MOVE_DEPTH_THRESHOLD && !in_check && ply > 0
        && se_excluded_move == 0
        && !(ply < PV_MAX_PLY && null_move_at[ply])) {

        bool has_pieces = (pos->pieces[pos->to_move][QUEEN] |
                          pos->pieces[pos->to_move][ROOK] |
                          pos->pieces[pos->to_move][BISHOP] |
                          pos->pieces[pos->to_move][KNIGHT]) != 0;

        bool null_entry_ok = false;
        if (has_pieces) {
            if (!have_static_eval) {
                static_eval      = evaluate_position(pos);
                have_static_eval = true;
            }
            null_entry_ok = (static_eval >= beta);
        }

        if (has_pieces && null_entry_ok) {

            Color original_side = pos->to_move;
            int   original_ep   = pos->en_passant_square;

            uint64_t original_hash = pos->hash;
            pos->to_move = 1 - pos->to_move;
            pos->en_passant_square = -1;
            pos->hash ^= zobrist_side_to_move;
            if (original_ep != -1) pos->hash ^= zobrist_en_passant[original_ep];

            int null_r = (10 + depth + null_variance(static_eval - alpha)) / 4;
            if (ply + 1 < PV_MAX_PLY) null_move_at[ply + 1] = true;
            int null_score = -negamax_with_pv(pos, depth - 1 - null_r,
                                             -beta, -beta + 1, ply + 1, info);
            if (ply + 1 < PV_MAX_PLY) null_move_at[ply + 1] = false;

            pos->to_move = original_side;
            pos->en_passant_square = original_ep;
            pos->hash = original_hash;

            if (fast_check_stop()) {
                return alpha;
            }

            if (null_score >= beta) {
                return beta;
            }
        }
    }

    if (depth <= RFP_MAX_DEPTH && !in_check && ply > 0 &&
        alpha > -MATE_SCORE + PV_MAX_PLY &&
        beta  <  MATE_SCORE - PV_MAX_PLY) {

        if (!have_static_eval) {
            static_eval = evaluate_position(pos);
            have_static_eval = true;
        }
        int reverse_margin = 150 * depth;
        if (static_eval - reverse_margin >= beta) {
            return static_eval - reverse_margin;
        }
    }

    if (depth >= 4 && tt_move.from == tt_move.to) {
        depth--;
    }
    Move moves[256];

    int move_count;
    if (in_check) {
        move_count = generate_legal_evasions(pos, moves);
    } else {
        move_count = generate_legal_moves(pos, moves);
    }

    if (move_count == 0) {

        Move no_move = {0};
        if (in_check) {

            int mate_score = -MATE_SCORE + ply;
            store_hash(&main_tt, tt_key, depth, mate_score, HASH_EXACT, no_move, ply);
            return mate_score;
        } else {
            store_hash(&main_tt, tt_key, depth, 0, HASH_EXACT, no_move, ply);
            return 0;
        }
    }

    int noisy_end = move_count;
    if (!in_check && move_count > 1) {
        Move noisy_buf[256];
        int n_noisy = 0;
        for (int i = 0; i < move_count; i++) {
            if (moves[i].captured != EMPTY || moves[i].promotion != EMPTY) noisy_buf[n_noisy++] = moves[i];
        }
        noisy_end = n_noisy;
        for (int i = 0; i < move_count; i++) {
            if (moves[i].captured == EMPTY && moves[i].promotion == EMPTY) noisy_buf[n_noisy++] = moves[i];
        }
        for (int i = 0; i < move_count; i++) moves[i] = noisy_buf[i];
    }

    score_moves_with_history_enhanced(pos, moves, noisy_end, tt_move, ply);

    bool singular = false;
    if (depth >= 8 && ply > 0 && ply < PV_MAX_PLY && se_excluded_move == 0 &&
        alpha > -MATE_SCORE + 500 &&
        !(tt_move.from == tt_move.to)) {
        int se_tt_depth, se_tt_type, se_tt_score;
        if (probe_hash_entry(&main_tt, tt_key, &se_tt_depth, &se_tt_type,
                             &se_tt_score, ply)
            && se_tt_type == HASH_BETA && se_tt_depth >= depth - 3) {
            int bc = se_tt_score - depth;
            se_excluded[ply] = pack_move(tt_move);
            int se_score = negamax_with_pv(pos, depth / 2, bc - 1, bc, ply, info);
            se_excluded[ply] = 0;
            singular = (se_score < bc);
        }
    }

    int best_score = -31000;
    bool found_pv = false;

    Move tt_best_move = {0};

    bool lmp_ok = (!in_check && (beta - alpha == 1) &&
                   alpha > -MATE_SCORE + PV_MAX_PLY &&
                   beta  <  MATE_SCORE - PV_MAX_PLY);
    int lmp_threshold = 2 * depth * (improving ? 2 : 1);
    int quiets_searched = 0;

    int hist_max = -1;

    short tried_quiets[MAX_TRIED_QUIETS];
    int   num_tried_quiets = 0;

    int searched_moves = 0;

    for (int stage = 0; stage < 2; stage++) {
        const int stage_begin = (stage == 0) ? 0 : noisy_end;
        const int stage_end = (stage == 0) ? noisy_end : move_count;
        if (stage_begin >= stage_end) continue;
        if (stage == 1) {

            score_moves_with_history_enhanced(pos, moves + stage_begin, stage_end - stage_begin,
                                              tt_move, ply);
        }
    for (int i = stage_begin; i < stage_end; i++) {

        pick_best_move(moves, stage_end, i);
        Move* move = &moves[i];

        if (se_excluded_move != 0 && pack_move(*move) == se_excluded_move) continue;

        int se_ext = (singular && ply + depth < MAX_DEPTH + EXT_MAX &&
                      tt_move.from == move->from && tt_move.to == move->to &&
                      tt_move.promotion == move->promotion) ? 1 : 0;

        bool is_quiet = (move->captured == EMPTY && move->promotion == EMPTY);
        if (lmp_ok && is_quiet && quiets_searched > lmp_threshold) {
            continue;
        }
        if (is_quiet) quiets_searched++;

        int  see_val    = 0;
        bool see_done = false;

        bool see_quiet_candidate = (move->promotion == EMPTY) && (beta - alpha == 1);
        if (i > 0 && depth <= 8 && !in_check &&
            alpha > -MATE_SCORE + PV_MAX_PLY &&
            beta  <  MATE_SCORE - PV_MAX_PLY &&
            (move->captured != EMPTY || see_quiet_candidate) &&
            !(tt_move.from == move->from && tt_move.to == move->to)) {
            see_val    = static_exchange_evaluation(pos, move);
            see_done = true;
            if (see_val < -60 * depth) {
                continue;
            }
        }

        bool losing_capture = false;
        if (depth >= 2 && i > FULL_MOVES_TO_SEARCH && !in_check && !found_pv &&
            beta - alpha == 1 &&
            alpha > -MATE_SCORE + PV_MAX_PLY &&
            beta  <  MATE_SCORE - PV_MAX_PLY &&
            move->captured != EMPTY && move->promotion == EMPTY &&
            !(tt_move.from == move->from && tt_move.to == move->to)) {
            if (!see_done) {
                see_val    = static_exchange_evaluation(pos, move);
                see_done = true;
            }
            losing_capture = (see_val < 0);
        }

        UndoInfo undo;
        make_move_full(pos, move, &undo);

        if (is_quiet && num_tried_quiets < MAX_TRIED_QUIETS) {
            tried_quiets[num_tried_quiets++] = (short)i;
        }

        int score;
        bool gives_check = is_in_check(pos, pos->to_move);

        int reduction = 0;
        {

            bool prune_ok = (beta - alpha == 1) &&
                             alpha > -MATE_SCORE + PV_MAX_PLY &&
                             beta  <  MATE_SCORE - PV_MAX_PLY;
            bool history_prune = false;

            if (i > FULL_MOVES_TO_SEARCH && depth >= MIN_LMR_DEPTH &&
                !in_check && !gives_check && !found_pv &&
                move->captured == EMPTY && move->promotion == EMPTY &&
                !is_killer_move(ply, *move)) {
                int his = get_history_score(*move);
                if (his > hist_max) {
                    hist_max = his;

                } else if (prune_ok && depth < 6 && (his < 0 || his * his < hist_max)) {
                    history_prune = true;
                } else {
                    reduction = (depth + 1) / 3;
                }
            }

            if (reduction == 0 && losing_capture && !gives_check) {
                reduction = 1;
            }

            if (history_prune) {
                undo_move_full(pos, move, &undo);
                continue;
            }
        }

        if (reduction > 0) {

            score = -negamax_with_pv(pos, depth - 1 - reduction, -alpha - 1, -alpha, ply + 1, info);

            if (fast_check_stop()) {
                undo_move_full(pos, move, &undo);
                return alpha;
            }

            if (score > alpha) {
                score = -negamax_with_pv(pos, depth - 1, -alpha - 1, -alpha, ply + 1, info);

                if (fast_check_stop()) {
                    undo_move_full(pos, move, &undo);
                    return alpha;
                }

                if (score > alpha && score < beta) {
                    score = -negamax_with_pv(pos, depth - 1, -beta, -alpha, ply + 1, info);

                    if (fast_check_stop()) {
                        undo_move_full(pos, move, &undo);
                        return alpha;
                    }
                }
            }
        } else {

            if (searched_moves > 0) {
                score = -negamax_with_pv(pos, depth - 1 + se_ext, -alpha - 1, -alpha, ply + 1, info);
                if (score > alpha && score < beta) {
                    score = -negamax_with_pv(pos, depth - 1 + se_ext, -beta, -alpha, ply + 1, info);
                }
        } else {
                score = -negamax_with_pv(pos, depth - 1 + se_ext, -beta, -alpha, ply + 1, info);
            }

            if (fast_check_stop()) {
                undo_move_full(pos, move, &undo);
                return alpha;
            }
        }

        searched_moves++;

        undo_move_full(pos, move, &undo);

        if (score > best_score) {
            best_score = score;
            tt_best_move = *move;

            if (ply + 1 < PV_MAX_PLY) {
                pv_table[ply][ply] = *move;
                for (int j = ply + 1; j < pv_length[ply + 1] && j < PV_MAX_PLY; j++) {
                    pv_table[ply][j] = pv_table[ply + 1][j];
                }
                pv_length[ply] = pv_length[ply + 1];
            }

            if (score >= beta) {
                if (move->captured == EMPTY && move->promotion == EMPTY) {
                    store_killer(ply, *move);
                    update_history(*move, depth, true);

                    for (int q = 0; q < num_tried_quiets; q++) {
                        if (tried_quiets[q] == (short)i) continue;
                        update_history(moves[tried_quiets[q]], depth, false);
                    }
                }

                if (se_excluded_move == 0) {
                    store_hash(&main_tt, tt_key, depth, beta, HASH_BETA, *move, ply);
                }

                return beta;
            }
            if (score > alpha) {
                alpha = score;
                found_pv = true;

                if (move->captured == EMPTY && move->promotion == EMPTY) {
                    update_history(*move, depth, true);
                }
            }
        } else {
            if (move->captured == EMPTY && move->promotion == EMPTY) {
                update_history(*move, depth, false);
            }
        }
    }
    }

    if (se_excluded_move != 0) return (searched_moves == 0) ? (-MATE_SCORE + ply) : best_score;

    store_hash(&main_tt, tt_key, depth, found_pv ? best_score : alpha,
               found_pv ? HASH_EXACT : HASH_ALPHA, tt_best_move, ply);

    return best_score;
}

Move root_excluded[MAX_MULTIPV];
int  root_excluded_count = 0;

bool is_root_excluded(const Move* m) {
    for (int i = 0; i < root_excluded_count; i++) {
        if (root_excluded[i].from == m->from &&
            root_excluded[i].to   == m->to   &&
            root_excluded[i].promotion == m->promotion) return true;
    }
    return false;
}

Move root_included[MAX_MULTIPV];
int  root_included_count = 0;

bool is_root_included(const Move* m) {
    if (root_included_count == 0) return true;
    for (int i = 0; i < root_included_count; i++) {
        if (root_included[i].from == m->from &&
            root_included[i].to   == m->to   &&
            root_included[i].promotion == m->promotion) return true;
    }
    return false;
}

Move search_single_depth_with_nodes(Position* pos, int depth, int alpha, int beta, SearchInfo* info) {
    Move moves[256];
    int move_count = generate_legal_moves(pos, moves);

    root_search_interrupted = false;

    if (move_count == 0) {
        Move null_move = {0};

        info->score = is_in_check(pos, pos->to_move) ? -30000 : 0;
        return null_move;
    }

    Move tt_move = {0};
    {
        int tt_score_root;
        uint64_t root_key = pos->hash;
        (void)probe_hash(&main_tt, root_key, depth, alpha, beta, &tt_score_root, &tt_move, 0);
    }

    score_moves_with_history_enhanced(pos, moves, move_count, tt_move, 0);
    qsort(moves, move_count, sizeof(Move), compare_moves);

    Move best_move = moves[0];
    if (root_excluded_count > 0 || root_included_count > 0) {
        int first_free = -1;
        for (int i = 0; i < move_count; i++) {
            if (!is_root_excluded(&moves[i]) && is_root_included(&moves[i])) { first_free = i; break; }
        }
        if (first_free < 0) {

            Move none_move = {0};
            info->score = -31000;
            info->pv[0] = '\0';
            return none_move;
        }
        best_move = moves[first_free];
    }
    int best_score = -31000;

    int currmove_number = 0;

    int searched_moves = 0;

    pv_length[0] = 0;

    for (int i = 0; i < move_count; i++) {
        Move* move = &moves[i];

        if (root_excluded_count > 0 && is_root_excluded(move)) continue;

        if (root_included_count > 0 && !is_root_included(move)) continue;

        currmove_number++;

        if (currmove_gate_open()) {
            char currmove_str[8];
            move_to_string_buf(move, currmove_str);

            printf("info currmove %s currmovenumber %d\n", currmove_str,
                   currmove_number + root_excluded_count);
            fflush(stdout);
        }

        UndoInfo undo;
        make_move_full(pos, move, &undo);

        int score;
        if (searched_moves > 0) {
            score = -negamax_with_pv(pos, depth - 1, -alpha - 1, -alpha, 1, info);
            if (score > alpha && score < beta) {
                score = -negamax_with_pv(pos, depth - 1, -beta, -alpha, 1, info);
            }
        } else {
            score = -negamax_with_pv(pos, depth - 1, -beta, -alpha, 1, info);
        }

        undo_move_full(pos, move, &undo);

        if (fast_check_stop()) {
            root_search_interrupted = true;
            break;
        }

        searched_moves++;

        if (score > best_score) {
            best_score = score;
            best_move = *move;

            pv_table[0][0] = *move;
            for (int j = 1; j < pv_length[1]; j++) {
                pv_table[0][j] = pv_table[1][j];
            }
            pv_length[0] = pv_length[1];

            if (score >= beta) break;
            if (score > alpha) alpha = score;
        }
    }

    info->score = best_score;

    char pv_str[1024] = "";
    for (int i = 0; i < pv_length[0] && i < 10; i++) {
        char move_str[8];
        move_to_string_buf(&pv_table[0][i], move_str);
        if (i > 0) strcat(pv_str, " ");
        strcat(pv_str, move_str);
    }
    strncpy(info->pv, pv_str, sizeof(info->pv) - 1);
    info->pv[sizeof(info->pv) - 1] = '\0';

    return best_move;
}

static bool pending_has_go(void) {
    const char* p = pending_input;
    while (*p != '\0') {
        if (strncmp(p, "go", 2) == 0) {
            char po = p[2];
            if (po == '\0' || po == '\n' || po == ' ' || po == '\t') return true;
        }
        const char* nl = strchr(p, '\n');
        if (nl == NULL) break;
        p = nl + 1;
    }
    return false;
}

void set_ponder_mode(bool on, long long budget_ms) {
    ponder_mode = on;
    ponder_budget_ms = on ? budget_ms : 0;
    ponderhit_received = false;
}

bool is_ponder_mode(void) { return ponder_mode; }
bool ponderhit_seen(void) { return ponderhit_received; }

static void handle_line_during_search(char* line, bool* abort_now) {
    while (*line == ' ' || *line == '\t' || *line == '\r') line++;
    size_t len = strlen(line);
    while (len > 0 && (line[len-1] == ' ' || line[len-1] == '\r')) line[--len] = '\0';
    if (len == 0) return;

    if (strncmp(line, "isready", 7) == 0) {

        printf("readyok\n");
        fflush(stdout);
    } else if (strncmp(line, "quit", 4) == 0) {

        printf("info string quit received during search; search aborted\n");
        fflush(stdout);
        stop_requested = true;
        *abort_now = true;
        if (!queue_pending_line(line)) {
            quit_requested = true;
        }
    } else if (strncmp(line, "stop", 4) == 0) {
        stop_requested = true;
        *abort_now = true;

        if (pending_has_go()) {
            queue_pending_line(line);
        }
    } else if (strncmp(line, "ponderhit", 9) == 0) {

        if (ponder_mode && !ponderhit_received) {
            ponderhit_received = true;
            if (ponder_budget_ms > 0) {
                set_search_unbounded(false);
                set_search_time_limit(ponder_budget_ms);
                start_search_timer();
                printf("info string ponderhit: own clock, budget %lld ms\n",
                       ponder_budget_ms);
            } else {

                stop_requested = true;
                *abort_now = true;
                printf("info string ponderhit without a time budget (no wtime/btime); "
                       "returning the move from the last completed iteration\n");
            }
            fflush(stdout);
        } else {

            printf("info string ponderhit ignored: engine is not in ponder mode\n");
            fflush(stdout);
        }
    } else if (strncmp(line, "position", 8) == 0) {
        position_changed = true;
        stop_requested = true;
        *abort_now = true;
        queue_pending_line(line);
    } else {

        queue_pending_line(line);
    }
}

static void consume_input_chunk(const char* chunk, bool* abort_now) {
    const char* p = chunk;

    if (partial_overflow) {
        const char* nl = strchr(p, '\n');
        if (!nl) return;
        partial_overflow = false;
        p = nl + 1;
    }

    while (*p) {
        const char* nl = strchr(p, '\n');
        size_t seg = nl ? (size_t)(nl - p) + 1 : strlen(p);
        size_t used = strlen(partial_line);

        if (used + seg >= sizeof(partial_line)) {
            printf("info string ERROR: UCI input line longer than %d bytes; line dropped\n",
                   (int)sizeof(partial_line) - 1);
            fflush(stdout);
            partial_line[0] = '\0';
            partial_overflow = (nl == NULL);
            p += seg;
            continue;
        }

        memcpy(partial_line + used, p, seg);
        partial_line[used + seg] = '\0';
        p += seg;

        if (nl) {
            partial_line[used + seg - 1] = '\0';
            handle_line_during_search(partial_line, abort_now);
            partial_line[0] = '\0';
        }
    }
}

static size_t drop_nul_lines(char* buf, size_t len, size_t* dropped) {
    size_t out = 0, i = 0;
    *dropped = 0;

    while (i < len) {
        size_t start = i;
        bool has_nul = false;
        while (i < len && buf[i] != '\n') {
            if (buf[i] == '\0') has_nul = true;
            i++;
        }
        size_t seg_end = (i < len) ? i + 1 : i;
        if (has_nul) {
            (*dropped)++;
        } else {
            if (out != start) memmove(buf + out, buf + start, seg_end - start);
            out += seg_end - start;
        }
        i = seg_end;
    }
    buf[out] = '\0';
    return out;
}

bool fast_check_stop(void) {

    if (node_limit_active && nodes_searched >= node_limit) {
        node_limit_reached = true;
        stop_requested = true;
        return true;
    }

    check_counter++;
    if (check_counter < CHECK_STOP_FREQUENCY) {
        return stop_requested;
    }
    check_counter = 0;

    bool abort_now = false;

    if (is_time_exceeded()) {
        stop_requested = true;
        abort_now = true;
    }

    if (!abort_now) {
#ifndef _WIN32
        int flags = fcntl(STDIN_FILENO, F_GETFL);
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
#endif

        static char chunk[MAX_UCI_LINE];
        ssize_t bytes_read;
#ifdef _WIN32
        while ((bytes_read = win_stdin_read(STDIN_FILENO, chunk, sizeof(chunk) - 1)) > 0) {
#else
        while ((bytes_read = read(STDIN_FILENO, chunk, sizeof(chunk) - 1)) > 0) {
#endif
            chunk[bytes_read] = '\0';

            if (memchr(chunk, '\0', (size_t)bytes_read) != NULL) {
                size_t dropped = 0;
                drop_nul_lines(chunk, (size_t)bytes_read, &dropped);
                printf("info string ERROR: NUL byte in UCI input; %zu line(s) dropped\n",
                       dropped);
                fflush(stdout);
            }

            consume_input_chunk(chunk, &abort_now);

            if (abort_now) break;
        }

        if (bytes_read == 0 && search_unbounded) {
            stop_requested = true;
            abort_now = true;
        }

#ifndef _WIN32
        fcntl(STDIN_FILENO, F_SETFL, flags);
#endif
    }

    return abort_now || stop_requested;
}

bool check_time(SearchInfo* info) {
    (void)info;
    return fast_check_stop();
}

