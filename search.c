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

#define FULL_MOVES_TO_SEARCH 3
#define MIN_LMR_DEPTH 2
#define CHECK_STOP_FREQUENCY 4096

#define NULL_MOVE_DEPTH_THRESHOLD 2
#define NULL_MOVE_REDUCTION 3

#define RAZOR_MARGIN 300
#define RAZOR_DEPTH_LIMIT 3

#define DELTA_MARGIN 900

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

static bool search_unbounded = false;

void set_search_unbounded(bool unbounded) {
    search_unbounded = unbounded;
}

static int history_table[64][64];

static Move killer_moves[MAX_DEPTH][2];

#define PV_MAX_PLY (MAX_DEPTH + 2)
static Move pv_table[PV_MAX_PLY][PV_MAX_PLY];
static int pv_length[PV_MAX_PLY];

void reset_search_state(void) {
    stop_requested = false;
    position_changed = false;
    has_pending_input = false;
    pending_input[0] = '\0';

    search_unbounded = false;

    partial_overflow = false;

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

    for (int i = 0; i < PV_MAX_PLY; i++) {
        pv_length[i] = 0;
        for (int j = 0; j < PV_MAX_PLY; j++) {
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

void store_killer(int ply, Move move) {
    if (ply >= 0 && ply < MAX_DEPTH) {

        if (killer_moves[ply][0].from == move.from &&
            killer_moves[ply][0].to == move.to) {
            return;
        }

        killer_moves[ply][1] = killer_moves[ply][0];
        killer_moves[ply][0] = move;
    }
}

bool is_killer_move(int ply, Move move) {
    if (ply >= 0 && ply < MAX_DEPTH) {
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

#define QS_CHECK_PLIES 2

int quiescence_search(Position* pos, int alpha, int beta, int ply, int qply, SearchInfo* info) {
    info->nodes++;

    if (ply > info->seldepth) {
        info->seldepth = ply;
    }

    if (fast_check_stop()) {
        return alpha;
    }

    if (qply >= MAX_QUIESCENCE_DEPTH) {
        return evaluate_position(pos);
    }

    bool in_check = is_in_check(pos, pos->to_move);

    if (!in_check) {
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
    }

    Move all_moves[256];
    int all_count = generate_legal_moves(pos, all_moves);

    if (all_count == 0) {

        return in_check ? (-MATE_SCORE + ply) : 0;
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
            return beta;
        }

        if (score > alpha) {
            alpha = score;
        }
    }

    return alpha;
}

int negamax_with_pv(Position* pos, int depth, int alpha, int beta, int ply, SearchInfo* info) {
    info->nodes++;

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

    if (depth <= 0) {
        return quiescence_search(pos, alpha, beta, ply, 0, info);
    }

    uint64_t tt_key = compute_hash(pos);

    Move tt_move = {0};
    {
        int tt_score;

        if (probe_hash(&main_tt, tt_key, depth, alpha, beta, &tt_score, &tt_move, ply)
            && beta - alpha == 1) {
            return tt_score;
        }
    }

    if (depth <= RAZOR_DEPTH_LIMIT && !in_check && ply > 0) {
        int static_eval = evaluate_position(pos);
        if (static_eval + RAZOR_MARGIN <= alpha) {
            int razor_score = quiescence_search(pos, alpha, beta, ply, 0, info);
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

    score_moves_with_history_enhanced(pos, moves, move_count, tt_move, ply);
    qsort(moves, move_count, sizeof(Move), compare_moves);

    int best_score = -31000;
    bool found_pv = false;

    Move tt_best_move = {0};

    for (int i = 0; i < move_count; i++) {
        Move* move = &moves[i];

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

                if (score > alpha && score < beta) {
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
                }

                store_hash(&main_tt, tt_key, depth, beta, HASH_BETA, *move, ply);
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

    store_hash(&main_tt, tt_key, depth, found_pv ? best_score : alpha,
               found_pv ? HASH_EXACT : HASH_ALPHA, tt_best_move, ply);

    return best_score;
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
        uint64_t root_key = compute_hash(pos);
        (void)probe_hash(&main_tt, root_key, depth, alpha, beta, &tt_score_root, &tt_move, 0);
    }

    score_moves_with_history_enhanced(pos, moves, move_count, tt_move, 0);
    qsort(moves, move_count, sizeof(Move), compare_moves);

    Move best_move = moves[0];
    int best_score = -31000;

    pv_length[0] = 0;

    for (int i = 0; i < move_count; i++) {
        Move* move = &moves[i];

        UndoInfo undo;
        make_move_full(pos, move, &undo);

        int score = -negamax_with_pv(pos, depth - 1, -beta, -alpha, 1, info);

        undo_move_full(pos, move, &undo);

        if (fast_check_stop()) {
            root_search_interrupted = true;
            break;
        }

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

