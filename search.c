/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
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

bool fast_gives_check(Position* pos, Move* move) {
    if (!pos || !move) return false;

    Color us = pos->to_move;
    Color them = 1 - us;
    PieceType piece = move->piece;
    int to = move->to;

    Bitboard enemy_king_bb = pos->pieces[them][KING];
    if (enemy_king_bb == 0) return false;

    int king_square = get_lsb(enemy_king_bb);
    if (king_square < 0 || king_square > 63) return false;

    Bitboard occupied_after = pos->all_pieces;
    occupied_after &= ~(1ULL << move->from);
    occupied_after |= (1ULL << to);

    if (move->is_en_passant) {
        int captured_pawn_square = (us == WHITE) ? to - 8 : to + 8;
        occupied_after &= ~(1ULL << captured_pawn_square);
    }

    switch (piece) {
        case PAWN: {
            int rank_diff = (king_square / 8) - (to / 8);
            int file_diff = (king_square % 8) - (to % 8);
            if (file_diff < 0) file_diff = -file_diff;
            if ((rank_diff == 1 || rank_diff == -1) && file_diff == 1) {
                if ((us == WHITE && rank_diff == 1) || (us == BLACK && rank_diff == -1)) {
                    return true;
                }
            }
            break;
        }
        case KNIGHT:
            return (knight_attacks[to] & (1ULL << king_square)) != 0;
        case BISHOP:
            return (get_bishop_attacks_public(to, occupied_after) & (1ULL << king_square)) != 0;
        case ROOK:
            return (get_rook_attacks_public(to, occupied_after) & (1ULL << king_square)) != 0;
        case QUEEN:
            return ((get_rook_attacks_public(to, occupied_after) | get_bishop_attacks_public(to, occupied_after)) & (1ULL << king_square)) != 0;
        case KING:
            return (king_attacks[to] & (1ULL << king_square)) != 0;
        default:
            return false;
    }
    return false;
}

#define FULL_MOVES_TO_SEARCH 3
#define MIN_LMR_DEPTH 2
#define CHECK_STOP_FREQUENCY 4096

#define NULL_MOVE_DEPTH_THRESHOLD 2
#define NULL_MOVE_REDUCTION 3

#define FUTILITY_MARGIN_1 150
#define FUTILITY_MARGIN_2 250
#define FUTILITY_MARGIN_3 400

#define RAZOR_MARGIN 300
#define RAZOR_DEPTH_LIMIT 3

#define DELTA_MARGIN 900

#define MAX_QUIESCENCE_DEPTH 8

static const int futility_margin[4] = {0, FUTILITY_MARGIN_1, FUTILITY_MARGIN_2, FUTILITY_MARGIN_3};

static bool stop_requested = false;
static bool position_changed = false;
static char pending_position_command[512] = "";
static bool has_pending_position = false;
static int check_counter = 0;

static long long search_time_limit_ms = 0;
static struct timeval search_start_time;

static int history_table[64][64];

static Move killer_moves[MAX_DEPTH][2];

static Move pv_table[MAX_DEPTH][MAX_DEPTH];
static int pv_length[MAX_DEPTH];

void reset_search_state(void) {
    stop_requested = false;
    position_changed = false;
    has_pending_position = false;
    pending_position_command[0] = '\0';

}

bool get_pending_position_command(char* buffer, int buffer_size) {
    if (has_pending_position) {
        strncpy(buffer, pending_position_command, buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
        has_pending_position = false;
        return true;
    }
    return false;
}

void set_search_time_limit(long long time_limit_ms) {
    search_time_limit_ms = time_limit_ms;
}

void start_search_timer(void) {
    search_start_time = get_wall_time();
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

    for (int i = 0; i < MAX_DEPTH; i++) {
        pv_length[i] = 0;
        for (int j = 0; j < MAX_DEPTH; j++) {
            pv_table[i][j] = (Move){0};
        }
    }
}

void clear_killers(void) {
    for (int i = 0; i < MAX_DEPTH; i++) {
        killer_moves[i][0] = (Move){0};
        killer_moves[i][1] = (Move){0};
    }
}

void store_killer(int depth, Move move) {
    if (depth >= 0 && depth < MAX_DEPTH) {

        if (killer_moves[depth][0].from == move.from &&
            killer_moves[depth][0].to == move.to) {
            return;
        }

        killer_moves[depth][1] = killer_moves[depth][0];
        killer_moves[depth][0] = move;
    }
}

bool is_killer_move(int depth, Move move) {
    if (depth >= 0 && depth < MAX_DEPTH) {
        return (killer_moves[depth][0].from == move.from &&
                killer_moves[depth][0].to == move.to) ||
               (killer_moves[depth][1].from == move.from &&
                killer_moves[depth][1].to == move.to);
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
        if (good_move) {
            history_table[move.from][move.to] += depth * depth;
            if (history_table[move.from][move.to] > HISTORY_MAX_SCORE) {
                history_table[move.from][move.to] = HISTORY_MAX_SCORE;
            }
        } else {
            history_table[move.from][move.to] -= depth;
            if (history_table[move.from][move.to] < 0) {
                history_table[move.from][move.to] = 0;
            }
        }
    }
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

int see_capture(Position* pos, Move* move) {
    if (move->captured == EMPTY) {
        return 0;
    }

    int captured_value = piece_value(move->captured);

    int attacking_piece_value = piece_value(move->piece);

    int see_score = captured_value - attacking_piece_value;

    if (move->piece == PAWN && see_score >= 0) {
        see_score += 50;
    }

    return see_score;
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

int quiescence_search(Position* pos, int alpha, int beta, int ply, SearchInfo* info) {
    info->nodes++;

    if (ply >= MAX_QUIESCENCE_DEPTH) {
        return evaluate_position(pos);
    }

    if (ply > info->seldepth) {
        info->seldepth = ply;
    }

    if (fast_check_stop()) {
        return alpha;
    }

    int stand_pat = evaluate_position(pos);

    if (stand_pat >= beta) {
        return beta;
    }

    if (stand_pat + DELTA_MARGIN < alpha) {
        return alpha;
    }

    if (stand_pat > alpha) {
        alpha = stand_pat;
    }

    Move tactical_moves[256];
    int tactical_count = 0;

    Move all_moves[256];
    int all_count = generate_legal_moves(pos, all_moves);

    bool is_check = false;
    for (int i = 0; i < all_count; i++) {
        bool is_capture = (all_moves[i].captured != EMPTY || all_moves[i].promotion != EMPTY);

        if (ply <= 4) {
            is_check = fast_gives_check(pos, &all_moves[i]);
        } else if (ply <= 6 && stand_pat + 200 >= alpha) {

            is_check = fast_gives_check(pos, &all_moves[i]);
        }

        if (ply <= 4) {
            is_check = fast_gives_check(pos, &all_moves[i]);
        } else if (ply <= 6 && stand_pat + 200 >= alpha) {

            is_check = fast_gives_check(pos, &all_moves[i]);
        }

        if (ply <= 4) {
            is_check = fast_gives_check(pos, &all_moves[i]);
        }

                if (is_capture || is_check) {
            tactical_moves[tactical_count++] = all_moves[i];
        }
        if (all_moves[i].captured != EMPTY || all_moves[i].promotion != EMPTY) {
            tactical_moves[tactical_count++] = all_moves[i];
        }
    }

    for (int i = 0; i < tactical_count; i++) {
        if (tactical_moves[i].captured != EMPTY) {

            tactical_moves[i].score = see_capture(pos, &tactical_moves[i]);

            if (tactical_moves[i].score > 0) {
                tactical_moves[i].score += 1000;
            }
        } else {

            if (is_check) {

                tactical_moves[i].score = 200 + (ply <= 2 ? 50 : ply <= 4 ? 30 : 10);
            }
        }

        if (tactical_moves[i].promotion != EMPTY) {
            tactical_moves[i].score += piece_value(tactical_moves[i].promotion);
        }
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

        int score = -quiescence_search(pos, -beta, -alpha, ply + 1, info);

        undo_move_full(pos, &tactical_moves[i], &undo);

        if (fast_check_stop()) {
            return alpha;
        }

        if (score >= beta) {
            return beta;
        }

        if (score > alpha) {
            alpha = score;
        }
    }

    if (tactical_count == 0 && is_in_check(pos, pos->to_move)) {

        if (all_count == 0) {

            return -MATE_SCORE + ply;
        }
    }

    return alpha;
}

int negamax_with_pv(Position* pos, int depth, int alpha, int beta, int ply, SearchInfo* info) {
    info->nodes++;

    pv_length[ply] = ply;

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

    if (depth <= 0) {
        return quiescence_search(pos, alpha, beta, ply, info);
    }

    if (depth <= RAZOR_DEPTH_LIMIT && !in_check && ply > 0) {
        int static_eval = evaluate_position(pos);
        if (static_eval + RAZOR_MARGIN <= alpha) {
            int razor_score = quiescence_search(pos, alpha, beta, ply, info);
            if (razor_score <= alpha) {
                return razor_score;
            }
        }
    }

    if (depth >= NULL_MOVE_DEPTH_THRESHOLD && !in_check && ply > 0) {

        bool has_pieces = (pos->pieces[pos->to_move][QUEEN] |
                          pos->pieces[pos->to_move][ROOK] |
                          pos->pieces[pos->to_move][BISHOP] |
                          pos->pieces[pos->to_move][KNIGHT]) != 0;

        if (has_pieces) {

            Color original_side = pos->to_move;
            pos->to_move = 1 - pos->to_move;

            int null_score = -negamax_with_pv(pos, depth - 1 - NULL_MOVE_REDUCTION,
                                             -beta, -beta + 1, ply + 1, info);

            pos->to_move = original_side;

            if (fast_check_stop()) {
                return alpha;
            }

            if (null_score >= beta) {
                return beta;
            }
        }
    }

    if (depth <= 3 && depth > 0 && !in_check && ply > 0) {
        int static_eval = evaluate_position(pos);
        int margin = futility_margin[depth];
        if (static_eval + margin <= alpha) {
            return static_eval;
        }
    }

    if (depth <= 2 && !in_check && ply > 0) {
        int static_eval = evaluate_position(pos);
        int reverse_margin = 200 * depth;
        if (static_eval - reverse_margin >= beta) {
            return static_eval - reverse_margin;
        }
    }
    Move moves[256];
    int move_count = generate_legal_moves(pos, moves);

    if (move_count == 0) {
        if (in_check) {
            return -30000 + ply;
        } else {
            return 0;
        }
    }

    Move null_move = {0};
    score_moves_with_history_enhanced(pos, moves, move_count, null_move, depth);

    int best_score = -31000;
    bool found_pv = false;

    for (int i = 0; i < move_count; i++) {
        Move* move = &moves[i];

                if (move->captured != EMPTY && depth <= 3) {
                    int see_value = see_capture(pos, move);
                    if (see_value < -100) {
                        continue;
                    }
                }

        UndoInfo undo;
        make_move_full(pos, move, &undo);

        int score;
        bool gives_check = is_in_check(pos, pos->to_move);

        if (i > FULL_MOVES_TO_SEARCH && depth >= MIN_LMR_DEPTH &&
            !in_check && !gives_check && !found_pv &&
            move->captured == EMPTY && move->promotion == EMPTY &&
            !is_killer_move(ply, *move)) {

            int reduction = 1 + (i / 4);
            if (i > 8) reduction++;

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

                if (score > alpha && score < beta && found_pv) {
                    score = -negamax_with_pv(pos, depth - 1, -beta, -alpha, ply + 1, info);

                    if (fast_check_stop()) {
                        undo_move_full(pos, move, &undo);
                        return alpha;
                    }
                }
            }
        } else {

            if (found_pv) {
                score = -negamax_with_pv(pos, depth - 1, -alpha - 1, -alpha, ply + 1, info);
                if (score > alpha && score < beta) {
                    score = -negamax_with_pv(pos, depth - 1, -beta, -alpha, ply + 1, info);
                }
        } else {
                score = -negamax_with_pv(pos, depth - 1, -beta, -alpha, ply + 1, info);
            }

            if (fast_check_stop()) {
                undo_move_full(pos, move, &undo);
                return alpha;
            }
        }

        undo_move_full(pos, move, &undo);

        if (score > best_score) {
            best_score = score;

            pv_table[ply][ply] = *move;
            for (int j = ply + 1; j < pv_length[ply + 1]; j++) {
                pv_table[ply][j] = pv_table[ply + 1][j];
            }
            pv_length[ply] = pv_length[ply + 1];

            if (score >= beta) {
                if (move->captured == EMPTY && move->promotion == EMPTY) {
                    store_killer(ply, *move);
                    update_history(*move, depth, true);
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

    return best_score;
}

Move search_single_depth_with_nodes(Position* pos, int depth, int alpha, SearchInfo* info) {
    int beta = (alpha > -500) ? alpha + 100 : 1000;

    Move moves[256];
    int move_count = generate_legal_moves(pos, moves);

    if (move_count == 0) {
        Move null_move = {0};
        return null_move;
    }

    Move null_move = {0};
    score_moves_with_history_enhanced(pos, moves, move_count, null_move, depth);

    Move best_move = moves[0];
    int best_score = -31000;

    pv_length[0] = 0;

    for (int i = 0; i < move_count; i++) {
        Move* move = &moves[i];

                if (move->captured != EMPTY && depth <= 3) {
                    int see_value = see_capture(pos, move);
                    if (see_value < -100) {
                        continue;
                    }
                }

        UndoInfo undo;
        make_move_full(pos, move, &undo);

        int score = -negamax_with_pv(pos, depth - 1, -beta, -alpha, 1, info);

        undo_move_full(pos, move, &undo);

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

bool fast_check_stop(void) {
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

        char buffer[256];
#ifdef _WIN32
        ssize_t bytes_read = win_stdin_read(STDIN_FILENO, buffer, sizeof(buffer) - 1);
#else
        ssize_t bytes_read = read(STDIN_FILENO, buffer, sizeof(buffer) - 1);
#endif

        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';

            if (strstr(buffer, "stop") || strstr(buffer, "quit")) {
                stop_requested = true;
                abort_now = true;
            }
            else if (strstr(buffer, "position")) {
                position_changed = true;
                stop_requested = true;
                abort_now = true;

                strncpy(pending_position_command, buffer, sizeof(pending_position_command) - 1);
                pending_position_command[sizeof(pending_position_command) - 1] = '\0';
                has_pending_position = true;
            }
        }

#ifndef _WIN32
        fcntl(STDIN_FILENO, F_SETFL, flags);
#endif
    }

    return abort_now;
}

bool check_time(SearchInfo* info) {
    (void)info;
    return fast_check_stop();
}

