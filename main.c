/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "board.h"
#include "movegen.h"
#include "eval.h"
#include "tt.h"
#include "utils.h"
#include "search.h"

#define ENGINE_VERSION "0.4"

bool position_changed = false;
bool stop_requested = false;
bool quit_requested = false;

#define MULTIPV_DEFAULT 1
#define MULTIPV_MIN     1
#define MULTIPV_MAX     MAX_MULTIPV
int multipv_setting = MULTIPV_DEFAULT;

#define THREADS_DEFAULT 1
#define THREADS_MIN     1
#define THREADS_MAX     1
int threads_setting = THREADS_DEFAULT;

static bool ponder_setting = false;

static char input_queue[MAX_UCI_LINE * 4] = "";
static size_t input_queue_pos = 0;

static void queue_input(const char* text) {
    size_t used = strlen(input_queue);
    const char* p = text;

    while (*p != '\0') {
        const char* nl = strchr(p, '\n');
        size_t line_len = nl ? (size_t)(nl - p) : strlen(p);
        size_t needed   = line_len + 2;

        if (used + needed > sizeof(input_queue)) {
            int shown = (int)(line_len < 40 ? line_len : 40);
            printf("info string ERROR: input queue full; %zu bytes dropped, "
                   "first dropped command: %.*s\n", strlen(p), shown, p);
            fflush(stdout);
            return;
        }
        memcpy(input_queue + used, p, line_len);
        used += line_len;
        input_queue[used++] = '\n';
        input_queue[used]   = '\0';
        p = nl ? nl + 1 : p + line_len;
    }
}

static bool drop_broken_line(char* out, size_t size) {
    size_t len = strlen(out);
    if (len > 0 && out[len - 1] == '\n') return false;

    const char* nl = memchr(out, '\n', size);

    if (nl != NULL) {
        printf("info string ERROR: NUL byte in UCI input line; line dropped\n");
    } else {
        if (feof(stdin) && len > 0) return false;
        printf("info string ERROR: UCI input line longer than %d bytes; line dropped\n",
               (int)size - 1);
        int c;
        while ((c = fgetc(stdin)) != EOF && c != '\n') {   }
    }
    fflush(stdout);
    out[0] = '\0';
    return true;
}

static bool take_stop_for_this_go(void) {
    size_t i = input_queue_pos;
    while (input_queue[i] != '\0') {
        size_t end_mark = i;
        while (input_queue[end_mark] != '\0' && input_queue[end_mark] != '\n') end_mark++;
        size_t next_line = (input_queue[end_mark] == '\n') ? end_mark + 1 : end_mark;

        const char* segment = input_queue + i;
        size_t cmd_len = end_mark - i;

        if (cmd_len >= 2 && strncmp(segment, "go", 2) == 0 &&
            (cmd_len == 2 || segment[2] == ' ' || segment[2] == '\t')) {
            return false;
        }
        if (cmd_len >= 4 && strncmp(segment, "stop", 4) == 0 &&
            (cmd_len == 4 || segment[4] == ' ' || segment[4] == '\t')) {
            memmove(input_queue + i, input_queue + next_line,
                    strlen(input_queue + next_line) + 1);
            return true;
        }
        i = next_line;
    }
    return false;
}

static bool read_uci_line(char* out, size_t size) {
    if (input_queue[input_queue_pos] != '\0') {
        size_t i = 0;
        while (input_queue[input_queue_pos] != '\0' &&
               input_queue[input_queue_pos] != '\n' && i + 1 < size) {
            out[i++] = input_queue[input_queue_pos++];
        }
        out[i] = '\0';
        if (input_queue[input_queue_pos] == '\n') input_queue_pos++;
        if (input_queue[input_queue_pos] == '\0') {
            input_queue[0] = '\0';
            input_queue_pos = 0;
        }
        return true;
    }

    char tail[MAX_UCI_LINE];
    if (take_partial_input(tail, sizeof(tail))) {
        snprintf(out, size, "%s", tail);
        size_t used = strlen(out);
        if (used + 1 >= size) return true;

        memset(out + used, 0, size - used);
        if (fgets(out + used, (int)(size - used), stdin) == NULL) {
            return used > 0;
        }
        drop_broken_line(out, size);
        return true;
    }

    memset(out, 0, size);
    if (fgets(out, (int)size, stdin) == NULL) return false;
    drop_broken_line(out, size);
    return true;
}

static void drain_search_input(void) {
    static char queued[MAX_UCI_LINE * 2];
    if (get_pending_input(queued, sizeof(queued))) {
        queue_input(queued);
    }
}

void parse_position(Position* pos, const char* input);
int parse_depth(const char* input);
int parse_movetime(const char* input);
int parse_wtime(const char* input);
int parse_btime(const char* input);
int parse_winc(const char* input);
int parse_binc(const char* input);
int parse_movestogo(const char* input);

long long parse_nodes_ll(const char* input);
int parse_nodes(const char* input);
int parse_mate(const char* input);
int calculate_search_time(int wtime, int btime, int winc, int binc, int movestogo, Color to_move, Position* pos);
float assess_position_complexity(Position* pos);
float detect_game_phase(Position* pos);
float detect_time_pressure(int my_time, int moves_played);

static bool parse_uci_int(const char* s, long* out) {
    if (s == NULL || s[0] == '\0') return false;
    errno = 0;
    char* end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s) return false;
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') end++;
    if (*end != '\0') return false;
    if (errno == ERANGE) return false;
    if (v > 2147483647L || v < -2147483648L) return false;
    *out = v;
    return true;
}

void print_uci_info_multipv(int multipv_idx, int depth, int seldepth, int score,
                            long long time_ms, long long nodes, const char* pv) {
    char mpv[24];
    if (multipv_idx > 0) snprintf(mpv, sizeof(mpv), " multipv %d", multipv_idx);
    else                 mpv[0] = '\0';

    long long nps = 0;
    if (time_ms > 0) {
        nps = (nodes * 1000LL) / time_ms;
    } else if (nodes > 0) {
        nps = nodes * 1000LL;
    }

    int mate_plies = MATE_SCORE - (score < 0 ? -score : score);
    if (mate_plies >= 0 && mate_plies < MAX_DEPTH * 2) {
        int mate_moves = (mate_plies + 1) / 2;
        if (score < 0) mate_moves = -mate_moves;
        printf("info depth %d seldepth %d%s score mate %d time %lld nodes %lld nps %lld pv %s\n",
               depth, seldepth, mpv, mate_moves, time_ms, nodes, nps, pv);
    } else {
        printf("info depth %d seldepth %d%s score cp %d time %lld nodes %lld nps %lld pv %s\n",
               depth, seldepth, mpv, score, time_ms, nodes, nps, pv);
    }
    fflush(stdout);
}

void print_uci_info(int depth, int seldepth, int score, long long time_ms,
                    long long nodes, const char* pv) {
    print_uci_info_multipv(multipv_setting > 1 ? 1 : 0,
                           depth, seldepth, score, time_ms, nodes, pv);
}

void print_safe_bestmove(Move* best_move, Position* pos) {

    if (best_move && best_move->from >= 0 && best_move->to >= 0 &&
        best_move->from < 64 && best_move->to < 64 &&
        best_move->from != best_move->to) {
        printf("bestmove %s\n", move_to_string(best_move));
    } else if (pos) {
        Move legal[256];
        int n = generate_legal_moves(pos, legal);
        if (n > 0) {
            printf("bestmove %s\n", move_to_string(&legal[0]));
        } else {
            printf("bestmove 0000\n");
        }
    } else {
        printf("bestmove 0000\n");
    }
    fflush(stdout);
}

static char last_completed_pv[1024] = "";

static bool extract_ponder_from_pv(const char* pv, char* out, size_t out_size) {
    if (!pv || !out || out_size < 6) return false;
    const char* p = pv;
    while (*p == ' ') p++;
    while (*p != '\0' && *p != ' ') p++;
    while (*p == ' ') p++;
    if (*p == '\0') return false;
    size_t n = 0;
    while (p[n] != '\0' && p[n] != ' ' && n + 1 < out_size) { out[n] = p[n]; n++; }
    out[n] = '\0';
    return n >= 4;
}

static void emit_bestmove(Move* best_move, Position* pos, const char* pv) {
    (void)pv;
    char ponder_move[16];
    if (ponder_setting && best_move &&
        best_move->from >= 0 && best_move->to >= 0 &&
        best_move->from < 64 && best_move->to < 64 &&
        best_move->from != best_move->to &&
        extract_ponder_from_pv(last_completed_pv, ponder_move, sizeof(ponder_move))) {
        printf("bestmove %s ponder %s\n", move_to_string(best_move), ponder_move);
        fflush(stdout);
        return;
    }
    print_safe_bestmove(best_move, pos);
}

static bool go_has_token(const char* input, const char* token) {
    size_t tlen = strlen(token);
    char buf_copy[MAX_UCI_LINE];
    snprintf(buf_copy, sizeof(buf_copy), "%s", input);
    char* save_ptr = NULL;
    for (char* tok = strtok_r(buf_copy, " \t\r\n", &save_ptr); tok != NULL;
         tok = strtok_r(NULL, " \t\r\n", &save_ptr)) {
        if (strlen(tok) == tlen && strcmp(tok, token) == 0) return true;
    }
    return false;
}

float assess_position_complexity(Position* pos) {
    float complexity = 1.0f;

    int total_pieces = 0;
    for (int color = 0; color < 2; color++) {
        for (int piece = 0; piece < 6; piece++) {
            total_pieces += pop_count(pos->pieces[color][piece]);
        }
    }
    complexity += (total_pieces - 16) * 0.05f;

    int pawn_count = pop_count(pos->pieces[WHITE][PAWN]) + pop_count(pos->pieces[BLACK][PAWN]);
    if (pawn_count < 12) complexity += 0.3f;

    if (is_in_check(pos, WHITE) || is_in_check(pos, BLACK)) {
        complexity += 0.5f;
    }

    return complexity > 2.0f ? 2.0f : complexity;
}

int calculate_search_time(int wtime, int btime, int winc, int binc, int movestogo, Color to_move, Position* pos) {
    int my_time = (to_move == WHITE) ? wtime : btime;
    int my_increment = (to_move == WHITE) ? winc : binc;

    if (my_time <= 0) return 100;

    if (movestogo > 0) {
        const int MOVE_OVERHEAD = 30;
        int safe = my_time - MOVE_OVERHEAD;
        if (safe < 1) return 1;
        int n = movestogo > 100 ? 100 : movestogo;
        long long alloc = (long long)safe / n;
        if (my_increment > 0) alloc += ((long long)my_increment * 7) / 10;
        long long cap = ((long long)safe * 4) / 5;
        if (alloc > cap) alloc = cap;
        if (alloc < 1) alloc = 1;
        return (int)alloc;
    }

    if (my_increment <= 0) {
        const int MOVE_OVERHEAD = 30;
        const int MOVE_HORIZON  = 25;
        int safe = my_time - MOVE_OVERHEAD;
        if (safe < 1) return 1;
        int alloc = safe / MOVE_HORIZON;
        return alloc < 1 ? 1 : alloc;
    }

    float total_time_left = my_time / 1000.0f;
    float increment = my_increment / 1000.0f;

    int total_pieces = 0;
    for (int color = 0; color < 2; color++) {
        for (int piece = 0; piece < 6; piece++) {
            total_pieces += pop_count(pos->pieces[color][piece]);
        }
    }
    int move_number = 1 + (32 - total_pieces) * 2;
    if (move_number < 1) move_number = 1;
    if (move_number > 60) move_number = 60;

    float time_per_move;
    const char* phase_name;
    const char* algorithm_type;

    if (total_time_left < 180.0f) {

        algorithm_type = "ADAPTIVE";

        float base_divisor = 12.0f;

        if (move_number <= 4) {
            phase_name = "OPENING";
            base_divisor = 15.0f;
        } else if (move_number <= 25) {
            phase_name = "MIDDLEGAME";
            base_divisor = 10.0f;
        } else {
            phase_name = "ENDGAME";
            base_divisor = 14.0f;
        }

        time_per_move = total_time_left / base_divisor;
        time_per_move += increment * 0.7f;

    } else {

        algorithm_type = "PHASE_BASED";

        float phase_time_ratio;
        int moves_in_phase;

        if (move_number <= 4) {
            phase_name = "OPENING";
            phase_time_ratio = 0.25f;
            moves_in_phase = 4;
        } else if (move_number <= 25) {
            phase_name = "MIDDLEGAME";
            phase_time_ratio = 0.60f;
            moves_in_phase = 21;
        } else {
            phase_name = "ENDGAME";
            phase_time_ratio = 0.15f;
            moves_in_phase = 15;
        }

        float phase_time_left = total_time_left * phase_time_ratio;
        time_per_move = phase_time_left / moves_in_phase;
        time_per_move += increment * 0.5f;
    }

    float max_time_per_move = total_time_left * 0.4f;
    if (time_per_move > max_time_per_move) {
        time_per_move = max_time_per_move;
    }

    int final_time = (int)(time_per_move * 1000.0f);

    int min_time = 100;

    long long sustainable = (long long)my_increment + my_time / 40;
    if (min_time > sustainable) min_time = (int)sustainable;
    if (min_time > my_time / 4) min_time = my_time / 4;
    if (min_time < 10) min_time = 10;

    if (final_time < min_time) final_time = min_time;
    if (final_time > 60000) final_time = 60000;

    if (my_time < 10000) {
        final_time = my_time / 8;
        if (final_time < min_time) final_time = min_time;
    }

    printf("info string TIME_MGMT: calculated=%dms phase=%s algorithm=%s pieces=%d move_est=%d total_time=%.1fs\n",
           final_time, phase_name, algorithm_type, total_pieces, move_number, total_time_left);

    return final_time;
}

float detect_game_phase(Position* pos) {
    int material_count = 0;

    for (int color = 0; color < 2; color++) {
        material_count += pop_count(pos->pieces[color][QUEEN]) * 9;
        material_count += pop_count(pos->pieces[color][ROOK]) * 5;
        material_count += pop_count(pos->pieces[color][BISHOP]) * 3;
        material_count += pop_count(pos->pieces[color][KNIGHT]) * 3;
    }

    float phase = (float)material_count / 78.0f;
    return phase > 1.0f ? 1.0f : phase;
}

float detect_time_pressure(int my_time, int moves_played) {
    if (my_time <= 0) return 2.0f;

    int expected_moves_left = 40 - moves_played;
    if (expected_moves_left < 10) expected_moves_left = 10;

    int time_per_move = my_time / expected_moves_left;

    if (time_per_move < 1000) return 2.0f;
    if (time_per_move < 5000) return 1.5f;
    if (time_per_move < 15000) return 1.2f;

    return 1.0f;
}

int parse_wtime(const char* input) {
    const char* ptr = strstr(input, "wtime");
    return ptr ? atoi(ptr + 5) : 0;
}

int parse_btime(const char* input) {
    const char* ptr = strstr(input, "btime");
    return ptr ? atoi(ptr + 5) : 0;
}

int parse_winc(const char* input) {
    const char* ptr = strstr(input, "winc");
    return ptr ? atoi(ptr + 4) : 0;
}

int parse_binc(const char* input) {
    const char* ptr = strstr(input, "binc");
    return ptr ? atoi(ptr + 4) : 0;
}

int parse_movestogo(const char* input) {
    const char* ptr = strstr(input, "movestogo");
    return ptr ? atoi(ptr + 9) : 0;
}

static long long parse_go_value(const char* input, const char* param_name, long long default_value) {
    char buf_copy[MAX_UCI_LINE];
    snprintf(buf_copy, sizeof(buf_copy), "%s", input);
    char* save_ptr = NULL;
    for (char* tok = strtok_r(buf_copy, " \t\r\n", &save_ptr); tok != NULL;
         tok = strtok_r(NULL, " \t\r\n", &save_ptr)) {
        if (strcmp(tok, param_name) != 0) continue;
        char* value_str = strtok_r(NULL, " \t\r\n", &save_ptr);
        if (value_str == NULL) return default_value;
        return atoll(value_str);
    }
    return default_value;
}

long long parse_nodes_ll(const char* input) {
    long long v = parse_go_value(input, "nodes", 0);
    return v > 0 ? v : 0;
}

int parse_nodes(const char* input) {
    long long v = parse_nodes_ll(input);
    return v > 2147483647LL ? 2147483647 : (int)v;
}

int parse_mate(const char* input) {
    long long v = parse_go_value(input, "mate", 0);
    if (v <= 0) return 0;

    if (v > MAX_DEPTH) return MAX_DEPTH;
    return (int)v;
}

int parse_movetime(const char* input) {
    const char* ptr = strstr(input, "movetime");
    return ptr ? atoi(ptr + 8) : 1000;
}

static bool match_legal_move(Position* pos, const char* move_str, Move* out) {
    Move parsed = parse_move(pos, move_str);
    if (parsed.from < 0) return false;

    Move legal[MAX_MOVES];
    int legal_count = generate_legal_moves(pos, legal);

    for (int i = 0; i < legal_count; i++) {
        if (legal[i].from == parsed.from &&
            legal[i].to == parsed.to &&
            legal[i].promotion == parsed.promotion) {
            *out = legal[i];
            return true;
        }
    }
    return false;
}

static void apply_searchmoves(const char* input, Position* pos) {
    if (!go_has_token(input, "searchmoves")) return;

    static const char* SEARCHMOVES_TERMINATORS[] = {
        "ponder", "wtime", "btime", "winc", "binc", "movestogo",
        "depth", "nodes", "mate", "movetime", "infinite", NULL
    };

    Move mlist[MAX_MULTIPV];
    int  n_moves = 0, n_rejected = 0;
    bool collecting = false;

    char buf_copy[MAX_UCI_LINE];
    snprintf(buf_copy, sizeof(buf_copy), "%s", input);
    char* save_ptr = NULL;
    for (char* tok = strtok_r(buf_copy, " \t\r\n", &save_ptr); tok != NULL;
         tok = strtok_r(NULL, " \t\r\n", &save_ptr)) {
        if (!collecting) {
            if (strcmp(tok, "searchmoves") == 0) collecting = true;
            continue;
        }
        bool end_mark = false;
        for (int i = 0; SEARCHMOVES_TERMINATORS[i] != NULL; i++) {
            if (strcmp(tok, SEARCHMOVES_TERMINATORS[i]) == 0) { end_mark = true; break; }
        }
        if (end_mark) break;

        Move m;
        if (!match_legal_move(pos, tok, &m)) {
            printf("info string go searchmoves: `%s` is not a legal move in this "
                   "position, skipped\n", tok);
            fflush(stdout);
            n_rejected++;
            continue;
        }

        bool already = false;
        for (int i = 0; i < n_moves; i++) {
            if (mlist[i].from == m.from && mlist[i].to == m.to &&
                mlist[i].promotion == m.promotion) { already = true; break; }
        }
        if (already) continue;

        if (n_moves >= MAX_MULTIPV) {
            printf("info string go searchmoves: list longer than %d moves, rest skipped\n",
                   MAX_MULTIPV);
            fflush(stdout);
            break;
        }
        mlist[n_moves++] = m;
    }

    if (n_moves == 0) {
        printf("info string go searchmoves: NONE of the given moves is legal "
               "in this position; NOT restricting, searching all moves\n");
        fflush(stdout);
        return;
    }

    for (int i = 0; i < n_moves; i++) root_included[i] = mlist[i];
    root_included_count = n_moves;
    printf("info string go searchmoves: root restricted to %d move(s)%s\n",
           n_moves, n_rejected > 0 ? ", some skipped as illegal" : "");
    fflush(stdout);
}

static bool position_desync = false;
static char position_desync_reason[96] = "";

static void apply_moves_from_command(Position* pos, const char* input) {
    const char* moves_ptr = strstr(input, "moves");
    if (!moves_ptr) return;
    moves_ptr += 5;

    char move_str[6];
    int move_index = 0;
    while (*moves_ptr) {

        while (*moves_ptr == ' ') moves_ptr++;
        if (!*moves_ptr) break;

        int i = 0;
        while (*moves_ptr && *moves_ptr != ' ' && i < 5) {
            move_str[i++] = *moves_ptr++;
        }
        move_str[i] = '\0';

        bool overlong = (*moves_ptr != '\0' && *moves_ptr != ' ');
        while (*moves_ptr != '\0' && *moves_ptr != ' ') moves_ptr++;

        move_index++;

        Move move;
        if (overlong || !match_legal_move(pos, move_str, &move)) {
            printf("info string ERROR: illegal or unparsable move '%s' (#%d) in position command; "
                   "aborting move sequence\n", move_str, move_index);
            fflush(stdout);

            position_desync = true;
            snprintf(position_desync_reason, sizeof(position_desync_reason),
                     "move '%s' (#%d)", move_str, move_index);
            return;
        }

        if (pos->history_count >= HISTORY_GAME_LIMIT) {
            compact_position_history(pos);
        }

        UndoInfo undo;
        make_move_full(pos, &move, &undo);
    }
}

void parse_position(Position* pos, const char* input) {
    if (strstr(input, "startpos")) {
        setup_starting_position(pos);
        position_desync = false;
        apply_moves_from_command(pos, input);
    } else if (strstr(input, "fen")) {
        const char* fen_start = strstr(input, "fen") + 4;
        while (*fen_start == ' ') fen_start++;

        const char* fen_end = strstr(fen_start, " moves");
        size_t fen_len = fen_end ? (size_t)(fen_end - fen_start) : strlen(fen_start);

        char fen[256];
        if (fen_len >= sizeof(fen)) {
            printf("info string ERROR: bad FEN in position command: FEN too long "
                   "(%zu chars, max %zu); position unchanged\n",
                   fen_len, sizeof(fen) - 1);
            fflush(stdout);
            return;
        }
        memcpy(fen, fen_start, fen_len);
        fen[fen_len] = '\0';

        if (!parse_fen(pos, fen)) {
            printf("info string ERROR: bad FEN in position command: %s; "
                   "position unchanged\n", fen_last_error());
            fflush(stdout);
            return;
        }
        position_desync = false;
        apply_moves_from_command(pos, input);
    }
}

int parse_depth(const char* input) {
    const char* depth_ptr = strstr(input, "depth");
    if (depth_ptr) {
        int depth = atoi(depth_ptr + 5);

        if (depth > MAX_DEPTH) {
            depth = MAX_DEPTH;
        }
        if (depth < 1) {
            depth = 1;
        }
        return depth;
    }
    return 6;
}

#define ROOT_FULL_ALPHA (-31000)
#define ROOT_FULL_BETA  ( 31000)

_Static_assert(ROOT_FULL_BETA > MATE_SCORE, "root sentinel must lie outside the mate score band");
_Static_assert(-(ROOT_FULL_ALPHA) > MATE_SCORE, "root sentinel must lie outside the mate score band");
#define ASPIRATION_WINDOW   30
#define MIN_ASPIRATION_DEPTH 3

static Move search_root_with_aspiration(Position* pos, int depth, int previous_score,
                                        bool use_aspiration, SearchInfo* info,
                                        bool* window_open) {
    int delta = ASPIRATION_WINDOW;
    int alpha = ROOT_FULL_ALPHA;
    int beta  = ROOT_FULL_BETA;

    if (use_aspiration) {
        alpha = previous_score - delta;
        beta  = previous_score + delta;

        if (alpha < ROOT_FULL_ALPHA) alpha = ROOT_FULL_ALPHA;
        if (beta  > ROOT_FULL_BETA)  beta  = ROOT_FULL_BETA;
    }

    Move best = {0};
    *window_open = false;

    while (true) {
        best = search_single_depth_with_nodes(pos, depth, alpha, beta, info);

        if (stop_requested || position_changed || is_time_exceeded()) {

            *window_open = (info->score <= alpha && alpha > ROOT_FULL_ALPHA) ||
                           (info->score >= beta  && beta  < ROOT_FULL_BETA);
            break;
        }

        if (info->score <= alpha && alpha > ROOT_FULL_ALPHA) {
            delta *= 4;
            alpha = previous_score - delta;
            if (alpha < ROOT_FULL_ALPHA) alpha = ROOT_FULL_ALPHA;
        } else if (info->score >= beta && beta < ROOT_FULL_BETA) {
            delta *= 4;
            beta = previous_score + delta;
            if (beta > ROOT_FULL_BETA) beta = ROOT_FULL_BETA;
        } else {
            break;
        }
    }

    return best;
}

typedef struct {
    int  score;
    int  seldepth;
    char pv[1024];
} PvLine;

static PvLine pv_lines[MULTIPV_MAX];

static void report_hashfull(void) {
    printf("info hashfull %d\n", hashfull_tt(&main_tt));
    fflush(stdout);
}

static void report_pv_lines(Position* pos, int depth, Move first_best,
                            int first_seldepth, int first_score, const char* first_pv,
                            long long elapsed, struct timeval start_time,
                            long long* nodes_total) {
    snprintf(last_completed_pv, sizeof(last_completed_pv), "%s",
             first_pv ? first_pv : "");
    if (multipv_setting <= 1 || first_best.from == first_best.to) {
        print_uci_info(depth, first_seldepth, first_score, elapsed, *nodes_total, first_pv);
        report_hashfull();
        return;
    }

    int count = 0;
    pv_lines[0].score    = first_score;
    pv_lines[0].seldepth = first_seldepth;
    snprintf(pv_lines[0].pv, sizeof(pv_lines[0].pv), "%s", first_pv ? first_pv : "");
    count = 1;

    root_excluded_count = 0;
    root_excluded[root_excluded_count++] = first_best;

    for (int idx = 2; idx <= multipv_setting && idx <= MULTIPV_MAX; idx++) {
        if (stop_requested || position_changed || is_time_exceeded()) break;

        SearchInfo mi = {0};
        mi.start_time = get_wall_time();
        mi.depth = depth;
        bool window_open = false;
        Move segment = search_root_with_aspiration(pos, depth, 0, false, &mi, &window_open);

        if (was_root_search_interrupted() || window_open) break;
        if (position_changed) break;
        if (segment.from == segment.to) break;

        *nodes_total += mi.nodes;

        pv_lines[count].score    = mi.score;
        pv_lines[count].seldepth = mi.seldepth;
        snprintf(pv_lines[count].pv, sizeof(pv_lines[count].pv), "%s", mi.pv);
        count++;

        if (root_excluded_count >= MULTIPV_MAX) break;
        root_excluded[root_excluded_count++] = segment;
    }

    for (int i = 2; i < count; i++) {
        PvLine key = pv_lines[i];
        int j = i - 1;
        while (j >= 1 && pv_lines[j].score < key.score) {
            pv_lines[j + 1] = pv_lines[j];
            j--;
        }
        pv_lines[j + 1] = key;
    }

    long long elapsed_final = time_diff_ms(start_time, get_wall_time());
    if (elapsed_final < 1) elapsed_final = 1;

    for (int i = 0; i < count; i++) {
        print_uci_info_multipv(i + 1, depth, pv_lines[i].seldepth, pv_lines[i].score,
                               elapsed_final, *nodes_total, pv_lines[i].pv);
    }

    report_hashfull();

    root_excluded_count = 0;
}

Move search_with_time_limit(Position* pos, int time_limit_ms, SearchInfo* final_info) {

    set_search_time_limit(time_limit_ms);
    start_search_timer();

    struct timeval start_time = get_wall_time();
    Move best_move = {0};
    bool valid_move_found = false;
    int previous_score = 0;

    long long nodes_total = 0;

    for (int depth = 1; depth <= MAX_DEPTH; depth++) {

        if (depth > 1) {
            long long elapsed_before = time_diff_ms(start_time, get_wall_time());
            if (elapsed_before > time_limit_ms * 0.9) {
                break;
            }
        }

        SearchInfo info = {0};
        info.start_time = start_time;
        info.nodes = 0;
        info.depth = depth;

        bool use_aspiration = (depth >= MIN_ASPIRATION_DEPTH && valid_move_found);
        bool window_open = false;
        Move current_best = search_root_with_aspiration(pos, depth, previous_score,
                                                        use_aspiration, &info, &window_open);

        nodes_total += info.nodes;

        bool iteration_interrupted = was_root_search_interrupted();

        if (position_changed) break;

        if (iteration_interrupted || window_open) {

            if (!valid_move_found && current_best.from != current_best.to) {
                best_move = current_best;
                valid_move_found = true;
            }
            break;
        }

        best_move = current_best;
        valid_move_found = true;
        previous_score = info.score;
        *final_info = info;
        final_info->nodes = nodes_total;

        long long elapsed = time_diff_ms(start_time, get_wall_time());

        report_pv_lines(pos, depth, current_best, info.seldepth, info.score, info.pv,
                        elapsed, start_time, &nodes_total);
        final_info->nodes = nodes_total;

        if (stop_requested || is_time_exceeded()) {
            break;
        }

        if (depth >= 6) {
            if (info.score > 800) break;
            if (info.score < -800) break;
        }
    }

    return valid_move_found ? best_move : (Move){0};
}

static bool parse_setoption(const char* input,
                            char* name,  size_t name_sz,
                            char* value, size_t value_sz) {
    name[0] = '\0';
    value[0] = '\0';

    const char* n = strstr(input, " name ");
    if (!n) return false;
    n += 6;
    while (*n == ' ') n++;

    const char* v = strstr(n, " value ");
    const char* n_end = v ? v : (n + strlen(n));
    while (n_end > n && n_end[-1] == ' ') n_end--;

    size_t n_len = (size_t)(n_end - n);
    if (n_len >= name_sz) n_len = name_sz - 1;
    memcpy(name, n, n_len);
    name[n_len] = '\0';

    if (v) {
        v += 7;
        while (*v == ' ') v++;
        size_t v_len = strlen(v);
        while (v_len > 0 && v[v_len - 1] == ' ') v_len--;
        if (v_len >= value_sz) v_len = value_sz - 1;
        memcpy(value, v, v_len);
        value[v_len] = '\0';
    }
    return true;
}

static void report_unsupported_go(const char* input) {
    static const char* known_params[] = {

        "go", "infinite", "movetime", "wtime", "btime", "winc", "binc", "depth",
        "movestogo", "ponder", "nodes", "mate", "searchmoves", NULL
    };
    char buf_copy[MAX_UCI_LINE];
    snprintf(buf_copy, sizeof(buf_copy), "%s", input);
    char* save_ptr = NULL;
    for (char* tok = strtok_r(buf_copy, " \t\r\n", &save_ptr); tok != NULL;
         tok = strtok_r(NULL, " \t\r\n", &save_ptr)) {
        if (tok[0] == '\0') continue;
        bool is_number = true;
        for (const char* p = tok; *p != '\0'; p++) {
            if (*p != '-' && (*p < '0' || *p > '9')) { is_number = false; break; }
        }
        if (is_number) continue;

        if (strcmp(tok, "searchmoves") == 0) break;

        bool is_known = false;
        for (int i = 0; known_params[i] != NULL; i++) {
            if (strcmp(tok, known_params[i]) == 0) { is_known = true; break; }
        }
        if (is_known) continue;
        printf("info string go %s: parameter NOT SUPPORTED, ignored\n", tok);
        fflush(stdout);
    }
}

static void report_default_value(const char* param_name, int fallback_value) {
    printf("info string go %s: NUMERIC VALUE MISSING, parser assumes %d\n", param_name, fallback_value);
    fflush(stdout);
}

static void report_missing_go_values(const char* input) {
    static const struct { const char* param_name; int (*parser)(const char*); } GO_PARAMS_WITH_VALUE[] = {
        { "depth",     parse_depth     },
        { "movetime",  parse_movetime  },
        { "wtime",     parse_wtime     },
        { "btime",     parse_btime     },
        { "winc",      parse_winc      },
        { "binc",      parse_binc      },
        { "movestogo", parse_movestogo },

        { "nodes",     parse_nodes     },
        { "mate",      parse_mate      },
        { NULL, NULL }
    };
    char buf_copy[MAX_UCI_LINE];
    snprintf(buf_copy, sizeof(buf_copy), "%s", input);
    char* save_ptr = NULL;
    int pending_idx = -1;
    for (char* tok = strtok_r(buf_copy, " \t\r\n", &save_ptr); tok != NULL;
         tok = strtok_r(NULL, " \t\r\n", &save_ptr)) {
        if (tok[0] == '\0') continue;

        bool is_number = true;
        int n_digits = 0;
        for (const char* p = tok; *p != '\0'; p++) {
            if (*p >= '0' && *p <= '9') { n_digits++; continue; }
            if (*p == '-' && p == tok) continue;
            is_number = false;
            break;
        }
        if (n_digits == 0) is_number = false;

        if (pending_idx >= 0 && !is_number) {
            report_default_value(GO_PARAMS_WITH_VALUE[pending_idx].param_name, GO_PARAMS_WITH_VALUE[pending_idx].parser(input));
        }
        pending_idx = -1;
        if (is_number) continue;
        for (int i = 0; GO_PARAMS_WITH_VALUE[i].param_name != NULL; i++) {
            if (strcmp(tok, GO_PARAMS_WITH_VALUE[i].param_name) == 0) { pending_idx = i; break; }
        }
    }

    if (pending_idx >= 0) {
        report_default_value(GO_PARAMS_WITH_VALUE[pending_idx].param_name, GO_PARAMS_WITH_VALUE[pending_idx].parser(input));
    }
}

int main() {

    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IOLBF, 0);

    init_masks();
    init_magic_bitboards();
    init_zobrist();
    init_tt(&main_tt, TT_HASH_MB_DEFAULT);
    init_search();

    Position pos;
    setup_starting_position(&pos);

    static char input[MAX_UCI_LINE];

    bool uci_announced = false;

    while (read_uci_line(input, sizeof(input))) {

        trim_uci_line(input);

        if (strcmp(input, "ucinewgame") == 0) {
            setup_starting_position(&pos);
            position_changed = false;
            stop_requested = false;

            reset_search_state();
            clear_tt(&main_tt);
            clear_killers();
            clear_history();
            position_desync = false; position_desync_reason[0] = '\0';
        } else if (strcmp(input, "uci") == 0) {

            printf("id name Chessenger %s\n", ENGINE_VERSION);
            printf("id author Dariusz Domagala (chessengeria.eu)\n");

            printf("option name Hash type spin default %d min %d max %d\n",
                   TT_HASH_MB_DEFAULT, TT_HASH_MB_MIN, TT_HASH_MB_MAX);

            printf("option name MultiPV type spin default %d min %d max %d\n",
                   MULTIPV_DEFAULT, MULTIPV_MIN, MULTIPV_MAX);

            printf("option name Clear Hash type button\n");

            printf("option name Threads type spin default %d min %d max %d\n",
                   THREADS_DEFAULT, THREADS_MIN, THREADS_MAX);

            printf("option name Ponder type check default %s\n",
                   ponder_setting ? "true" : "false");
            printf("uciok\n");
            fflush(stdout);
            uci_announced = true;

        } else if (strncmp(input, "isready", 7) == 0) {
            printf("readyok\n");
            fflush(stdout);

        } else if (strncmp(input, "position", 8) == 0) {
            parse_position(&pos, input);
            position_changed = false;
            stop_requested = false;

        } else if (strncmp(input, "setoption", 9) == 0) {

            char opt_name[MAX_UCI_LINE];
            char opt_value[MAX_UCI_LINE];
            bool opt_ok = parse_setoption(input, opt_name, sizeof(opt_name),
                                                 opt_value, sizeof(opt_value));
            long want_l = 0;
            if (opt_ok && strcmp(opt_name, "Hash") == 0) {

                if (opt_value[0] == '\0') {
                    printf("info string setoption Hash: missing value, option ignored\n");
                    fflush(stdout);
                } else if (!parse_uci_int(opt_value, &want_l)) {

                    printf("info string setoption Hash: value `%s` is not a number, option ignored\n",
                           opt_value);
                    fflush(stdout);
                } else {
                    if (want_l < TT_HASH_MB_MIN) want_l = TT_HASH_MB_MIN;
                    if (want_l > TT_HASH_MB_MAX) want_l = TT_HASH_MB_MAX;
                    int want_mb = (int)want_l;
                    size_t got_b = resize_tt(&main_tt, want_mb);
                    if (got_b == 0) {

                        printf("info string setoption Hash %d MB: ALLOCATION FAILED, table empty\n",
                               want_mb);
                    } else {

                        printf("info string Hash %d MB REQUESTED -> %.3f MiB APPLIED, %zu entries, %zu B\n",
                               want_mb, (double)got_b / (1024.0 * 1024.0),
                               (size_t)main_tt.size, got_b);
                    }
                    fflush(stdout);
                }
            } else if (opt_ok && strcmp(opt_name, "MultiPV") == 0) {

                if (opt_value[0] == '\0') {
                    printf("info string setoption MultiPV: missing value, option ignored\n");
                } else if (!parse_uci_int(opt_value, &want_l)) {
                    printf("info string setoption MultiPV: value `%s` is not a number, option ignored\n",
                           opt_value);
                } else {
                    long want = want_l;
                    if (want < MULTIPV_MIN) want = MULTIPV_MIN;
                    if (want > MULTIPV_MAX) want = MULTIPV_MAX;
                    multipv_setting = (int)want;
                    printf("info string MultiPV %d\n", multipv_setting);
                }
                fflush(stdout);
            } else if (opt_ok && strcmp(opt_name, "Clear Hash") == 0) {

                clear_tt(&main_tt);
                clear_killers();
                clear_history();
                printf("info string Clear Hash: table, killers and history cleared\n");
                fflush(stdout);
            } else if (opt_ok && strcmp(opt_name, "Threads") == 0) {

                if (opt_value[0] == '\0') {
                    printf("info string setoption Threads: missing value, option ignored\n");
                } else if (!parse_uci_int(opt_value, &want_l)) {
                    printf("info string setoption Threads: value `%s` is not a number, option ignored\n",
                           opt_value);
                } else {
                    long want = want_l;
                    if (want < THREADS_MIN) want = THREADS_MIN;
                    if (want > THREADS_MAX) want = THREADS_MAX;
                    threads_setting = (int)want;
                    if (want != want_l) {

                        printf("info string Threads %ld REQUESTED -> %d APPLIED (single-threaded engine, range %d..%d)\n",
                               want_l, threads_setting, THREADS_MIN, THREADS_MAX);
                    } else {
                        printf("info string Threads %d\n", threads_setting);
                    }
                }
                fflush(stdout);
            } else if (opt_ok && strcmp(opt_name, "Ponder") == 0) {

                if (opt_value[0] == '\0') {
                    printf("info string setoption Ponder: missing value, option ignored\n");
                } else if (strcmp(opt_value, "true") == 0) {
                    ponder_setting = true;
                    printf("info string Ponder true\n");
                } else if (strcmp(opt_value, "false") == 0) {
                    ponder_setting = false;
                    printf("info string Ponder false\n");
                } else {
                    printf("info string setoption Ponder: value `%s` is neither "
                           "`true` nor `false`, option ignored\n", opt_value);
                }
                fflush(stdout);
            } else if (opt_ok) {

                printf("info string setoption %s: UNKNOWN option, ignored\n", opt_name);
                fflush(stdout);
            } else {

                printf("info string setoption: invalid syntax (missing `name`), ignored\n");
                fflush(stdout);
            }

        } else if (strncmp(input, "perft", 5) == 0) {

            int depth = 1;
            if (strlen(input) > 6) {
                depth = atoi(input + 6);
            }
            if (depth < 1) depth = 1;
            if (depth > 8) depth = 8;

            struct timeval start = get_wall_time();
            uint64_t nodes = perft(&pos, depth);
            struct timeval end = get_wall_time();
            long long time_ms = time_diff_ms(start, end);
            if (time_ms < 1) time_ms = 1;

            printf("info string perft %d nodes %llu time %lld nps %lld\n",
                   depth, (unsigned long long)nodes, time_ms,
                   (unsigned long long)((nodes * 1000LL) / time_ms));
            fflush(stdout);

        } else if (strncmp(input, "divide", 6) == 0) {

            int depth = 1;
            if (strlen(input) > 7) {
                depth = atoi(input + 7);
            }
            if (depth < 1) depth = 1;
            if (depth > 6) depth = 6;

            printf("info string Starting divide %d analysis...\n", depth);

            Move moves[256];
            int move_count = generate_legal_moves(&pos, moves);

            printf("info string Generated %d legal moves:\n", move_count);

            uint64_t total_nodes = 0;
            for (int i = 0; i < move_count; i++) {
                UndoInfo undo;
                make_move_full(&pos, &moves[i], &undo);

                uint64_t nodes = (depth > 1) ? perft(&pos, depth - 1) : 1;
                total_nodes += nodes;

                printf("info string %s: %llu\n",
                       move_to_string(&moves[i]), (unsigned long long)nodes);

                undo_move_full(&pos, &moves[i], &undo);
            }

            printf("info string Total: %llu nodes\n", (unsigned long long)total_nodes);
            fflush(stdout);

        } else if (strncmp(input, "d", 1) == 0 && strlen(input) == 1) {

            printf("info string Current position:\n");
            printf("info string FEN: %s\n", position_to_fen(&pos));
            printf("info string To move: %s\n", pos.to_move == WHITE ? "White" : "Black");
            printf("info string Castling: %s%s%s%s\n",
                   (pos.castling_rights & WHITE_KINGSIDE) ? "K" : "",
                   (pos.castling_rights & WHITE_QUEENSIDE) ? "Q" : "",
                   (pos.castling_rights & BLACK_KINGSIDE) ? "k" : "",
                   (pos.castling_rights & BLACK_QUEENSIDE) ? "q" : "");
            printf("info string En passant: %s\n",
                   pos.en_passant_square >= 0 ? square_to_string(pos.en_passant_square) : "-");
            fflush(stdout);

        } else if (strcmp(input, "eval") == 0) {

            int eval_stm = evaluate_position(&pos);
            int eval_white = (pos.to_move == WHITE) ? eval_stm : -eval_stm;
            printf("info string eval stm %d white %d\n", eval_stm, eval_white);
            fflush(stdout);

        } else if (strcmp(input, "hash") == 0) {

            printf("info string hash 0x%016llx\n",
                   (unsigned long long)compute_hash(&pos));
            fflush(stdout);

        } else if (strncmp(input, "go", 2) == 0) {

            if (position_desync) {
                printf("info string ERROR: position invalid (%s); refusing to move "
                       "until a valid position command arrives\n", position_desync_reason);
                printf("bestmove 0000\n");
                fflush(stdout);
                continue;
            }

            report_unsupported_go(input);
            report_missing_go_values(input);

            stop_requested = false;
            position_changed = false;

            root_excluded_count = 0;

            last_completed_pv[0] = '\0';

            reset_search_state();

            main_tt.age++;

            bool ponder_req = go_has_token(input, "ponder");

            apply_searchmoves(input, &pos);
            set_node_limit(parse_nodes_ll(input));

            if (go_has_token(input, "mate") &&
                (strstr(input, "infinite") || ponder_req || strstr(input, "movetime") ||
                 strstr(input, "wtime") || strstr(input, "btime"))) {
                printf("info string go mate: the mate limit works in this engine ONLY "
                       "without time control and without `infinite`; ignored here\n");
                fflush(stdout);
            }

            if (strstr(input, "infinite") || ponder_req) {

                set_search_time_limit(0);
                start_search_timer();

                set_search_unbounded(true);

                if (ponder_req) {

                    long long go_budget_ms = 0;
                    if (strstr(input, "movetime")) {
                        go_budget_ms = parse_movetime(input);
                    } else if (strstr(input, "wtime") || strstr(input, "btime")) {
                        go_budget_ms = calculate_search_time(parse_wtime(input), parse_btime(input),
                                                       parse_winc(input), parse_binc(input),
                                                       parse_movestogo(input), pos.to_move, &pos);
                    }
                    set_ponder_mode(true, go_budget_ms);
                    printf("info string ponder: searching on the opponent's time; "
                           "budget after `ponderhit` = %lld ms\n", go_budget_ms);
                    fflush(stdout);
                }

                bool stop_in_same_batch = take_stop_for_this_go();

                SearchInfo info = {0};
                struct timeval search_start = get_wall_time();
                info.start_time = search_start;
                info.nodes = 0;

                Move best_move = {0};
                bool valid_move_found = false;
                int previous_score = 0;

                for (int depth = 1; depth <= MAX_DEPTH; depth++) {
                    if (stop_requested || position_changed) break;

                    if (stop_in_same_batch && depth > 1) {
                        stop_requested = true;
                        break;
                    }

                    info.depth = depth;

                    bool use_aspiration = (depth >= MIN_ASPIRATION_DEPTH && valid_move_found);
                    bool window_open = false;
                    Move current_best = search_root_with_aspiration(&pos, depth, previous_score,
                                                                    use_aspiration, &info,
                                                                    &window_open);

                    if (!stop_requested && !position_changed && !window_open) {
                        best_move = current_best;
                        valid_move_found = true;
                        previous_score = info.score;

                        long long elapsed = time_diff_ms(search_start, get_wall_time());
                        if (elapsed < 1) elapsed = 1;

                        report_pv_lines(&pos, depth, current_best, info.seldepth,
                                        info.score, info.pv, elapsed, search_start,
                                        &info.nodes);
                    }
                }

                while (!stop_requested && !position_changed && !quit_requested
                       && !ponderhit_seen()) {

                    static char wait_line[MAX_UCI_LINE];
                    if (!read_uci_line(wait_line, sizeof(wait_line))) {
                        quit_requested = true;
                        break;
                    }
                    trim_uci_line(wait_line);

                    if (wait_line[0] == '\0') continue;

                    if (strncmp(wait_line, "stop", 4) == 0) {
                        stop_requested = true;
                    } else if (strncmp(wait_line, "quit", 4) == 0) {
                        quit_requested = true;
                    } else if (strncmp(wait_line, "isready", 7) == 0) {
                        printf("readyok\n");
                        fflush(stdout);
                    } else if (strncmp(wait_line, "ponderhit", 9) == 0) {

                        if (is_ponder_mode()) {
                            printf("info string ponderhit after the depth limit was reached; "
                                   "returning the move\n");
                            fflush(stdout);
                            break;
                        }
                        printf("info string ponderhit ignored: engine is not "
                               "in ponder mode\n");
                        fflush(stdout);
                    } else {

                        queue_input(wait_line);
                        break;
                    }
                }

                emit_bestmove(valid_move_found ? &best_move : NULL, &pos, info.pv);

                set_ponder_mode(false, 0);

            } else if (strstr(input, "movetime")) {

                int movetime = parse_movetime(input);
                printf("info string TIME_DEBUG: movetime=%dms\n", movetime);

                SearchInfo info = {0};
                Move best_move = search_with_time_limit(&pos, movetime, &info);

                emit_bestmove(&best_move, &pos, info.pv);

            } else if (strstr(input, "wtime") || strstr(input, "btime")) {

                int wtime = parse_wtime(input);
                int btime = parse_btime(input);
                int winc = parse_winc(input);
                int binc = parse_binc(input);

                int movestogo = parse_movestogo(input);

                int search_time = calculate_search_time(wtime, btime, winc, binc, movestogo, pos.to_move, &pos);

                SearchInfo info = {0};
                Move best_move = search_with_time_limit(&pos, search_time, &info);

                emit_bestmove(&best_move, &pos, info.pv);

            } else {

                set_search_time_limit(0);
                start_search_timer();

                int depth_target = parse_depth(input);

                const bool has_depth   = go_has_token(input, "depth");
                const int  mate_target   = parse_mate(input);
                const long long nodes_target = parse_nodes_ll(input);
                bool mate_found  = false;

                if (!has_depth && mate_target > 0) {

                    int target_depth = 2 * mate_target - 1;
                    if (target_depth > MAX_DEPTH) target_depth = MAX_DEPTH;
                    if (target_depth < 1) target_depth = 1;
                    depth_target = target_depth;
                    printf("info string go mate %d: searching to depth %d plies\n",
                           mate_target, depth_target);
                    fflush(stdout);
                } else if (!has_depth && nodes_target > 0) {

                    depth_target = MAX_DEPTH;
                    printf("info string go nodes %lld: deepening to %d, the node budget is the limit\n",
                           nodes_target, MAX_DEPTH);
                    fflush(stdout);
                }

                struct timeval search_start = get_wall_time();
                Move best_move = {0};
                bool valid_move_found = false;
                int previous_score = 0;
                int last_completed_depth = 0;

                long long nodes_total = 0;

                for (int depth = 1; depth <= depth_target; depth++) {
                    SearchInfo info = {0};
                    info.start_time = search_start;
                    info.nodes = 0;
                    info.depth = depth;

                    bool use_aspiration = (depth >= MIN_ASPIRATION_DEPTH && valid_move_found);
                    bool window_open = false;
                    Move current_best = search_root_with_aspiration(&pos, depth, previous_score,
                                                                    use_aspiration, &info,
                                                                    &window_open);

                    nodes_total += info.nodes;

                    bool iteration_interrupted = was_root_search_interrupted();

                    if (position_changed) break;

                    if (iteration_interrupted || window_open) {
                        if (!valid_move_found && current_best.from != current_best.to) {
                            best_move = current_best;
                            valid_move_found = true;
                        }
                        break;
                    }

                    best_move = current_best;
                    valid_move_found = true;
                    previous_score = info.score;
                    last_completed_depth = depth;

                    long long elapsed_iter = time_diff_ms(search_start, get_wall_time());
                    if (elapsed_iter < 1) elapsed_iter = 1;

                    report_pv_lines(&pos, depth, current_best, info.seldepth,
                                    info.score, info.pv, elapsed_iter, search_start,
                                    &nodes_total);

                    if (mate_target > 0 && info.score > 0) {
                        int mate_plies = MATE_SCORE - info.score;
                        if (mate_plies >= 0 && mate_plies <= 2 * mate_target - 1) {
                            printf("info string go mate %d: mate found, %d moves, "
                                   "depth %d\n", mate_target, (mate_plies + 1) / 2, depth);
                            fflush(stdout);
                            mate_found = true;
                            break;
                        }
                    }

                    if (stop_requested) break;
                }

                long long elapsed = time_diff_ms(search_start, get_wall_time());
                if (elapsed < 1) elapsed = 1;

                if (nodes_target > 0 && node_limit_hit()) {
                    printf("info string go nodes: budget %lld, searched %lld, "
                           "deepest completed depth %d\n",
                           nodes_target, get_nodes_searched(), last_completed_depth);
                    fflush(stdout);
                }

                if (mate_target > 0 && !mate_found) {
                    printf("info string go mate %d: no mate found up to depth %d; "
                           "this is NOT a proof that no mate exists (the search prunes)\n",
                           mate_target, depth_target);
                    fflush(stdout);
                }

                bool search_aborted = stop_requested || position_changed || quit_requested;

                if (search_aborted) {
                    if (last_completed_depth == 0) {
                        printf("info string search aborted at depth %d after %lld ms; "
                               "no depth completed, result not reported\n",
                               depth_target, elapsed);
                    } else {
                        printf("info string search aborted after %lld ms; "
                               "deepest completed depth %d of %d requested\n",
                               elapsed, last_completed_depth, depth_target);
                    }
                    fflush(stdout);
                }

                print_safe_bestmove(&best_move, &pos);
            }

            drain_search_input();
            if (quit_requested) break;

        } else if (strncmp(input, "ponderhit", 9) == 0) {

            printf("info string ponderhit ignored: engine is not in ponder mode "
                   "(no `go ponder` in progress)\n");
            fflush(stdout);

        } else if (strncmp(input, "stop", 4) == 0) {
            stop_requested = true;

        } else if (strncmp(input, "quit", 4) == 0) {
            break;
        } else if (input[0] != '\0' && uci_announced) {

            printf("info string %s: command NOT SUPPORTED\n", input);
            fflush(stdout);
        }
    }

    free_tt(&main_tt);
    return 0;
}