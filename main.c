/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>

#include "board.h"
#include "movegen.h"
#include "eval.h"
#include "tt.h"
#include "utils.h"
#include "search.h"

bool position_changed = false;
bool stop_requested = false;
bool quit_requested = false;

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
int calculate_search_time(int wtime, int btime, int winc, int binc, Color to_move, Position* pos);
float assess_position_complexity(Position* pos);
float detect_game_phase(Position* pos);
float detect_time_pressure(int my_time, int moves_played);

void print_uci_info(int depth, int seldepth, int score, long long time_ms, long long nodes, const char* pv) {

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
        printf("info depth %d seldepth %d score mate %d time %lld nodes %lld nps %lld pv %s\n",
               depth, seldepth, mate_moves, time_ms, nodes, nps, pv);
    } else {
        printf("info depth %d seldepth %d score cp %d time %lld nodes %lld nps %lld pv %s\n",
               depth, seldepth, score, time_ms, nodes, nps, pv);
    }
    fflush(stdout);
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

int calculate_search_time(int wtime, int btime, int winc, int binc, Color to_move, Position* pos) {
    int my_time = (to_move == WHITE) ? wtime : btime;
    int my_increment = (to_move == WHITE) ? winc : binc;

    if (my_time <= 0) return 100;

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

        print_uci_info(depth, info.seldepth, info.score, elapsed, nodes_total, info.pv);

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

int main() {

    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IOLBF, 0);

    init_masks();
    init_magic_bitboards();
    init_zobrist();
    init_tt(&main_tt, 64);
    init_search();

    Position pos;
    setup_starting_position(&pos);

    static char input[MAX_UCI_LINE];

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
            printf("id name Chessenger 0.3\n");
            printf("id author chessengeria.eu\n");
            printf("uciok\n");
            fflush(stdout);

        } else if (strncmp(input, "isready", 7) == 0) {
            printf("readyok\n");
            fflush(stdout);

        } else if (strncmp(input, "position", 8) == 0) {
            parse_position(&pos, input);
            position_changed = false;
            stop_requested = false;

        } else if (strncmp(input, "setoption", 9) == 0) {

            if (strstr(input, "MultiPV")) {

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

            stop_requested = false;
            position_changed = false;

            reset_search_state();

            main_tt.age++;

            if (strstr(input, "infinite")) {

                set_search_time_limit(0);
                start_search_timer();

                set_search_unbounded(true);

                SearchInfo info = {0};
                struct timeval search_start = get_wall_time();
                info.start_time = search_start;
                info.nodes = 0;

                Move best_move = {0};
                bool valid_move_found = false;
                int previous_score = 0;

                for (int depth = 1; depth <= MAX_DEPTH; depth++) {
                    if (stop_requested || position_changed) break;

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

                        print_uci_info(depth, info.seldepth, info.score, elapsed, info.nodes, info.pv);
                    }
                }

                while (!stop_requested && !position_changed && !quit_requested) {

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
                    } else {

                        queue_input(wait_line);
                        break;
                    }
                }

                print_safe_bestmove(valid_move_found ? &best_move : NULL, &pos);

            } else if (strstr(input, "movetime")) {

                int movetime = parse_movetime(input);
                printf("info string TIME_DEBUG: movetime=%dms\n", movetime);

                SearchInfo info = {0};
                Move best_move = search_with_time_limit(&pos, movetime, &info);

                print_safe_bestmove(&best_move, &pos);

            } else if (strstr(input, "wtime") || strstr(input, "btime")) {

                int wtime = parse_wtime(input);
                int btime = parse_btime(input);
                int winc = parse_winc(input);
                int binc = parse_binc(input);

                int search_time = calculate_search_time(wtime, btime, winc, binc, pos.to_move, &pos);

                SearchInfo info = {0};
                Move best_move = search_with_time_limit(&pos, search_time, &info);

                print_safe_bestmove(&best_move, &pos);

            } else {

                set_search_time_limit(0);
                start_search_timer();

                int depth_target = parse_depth(input);

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

                    print_uci_info(depth, info.seldepth, info.score, elapsed_iter,
                                   nodes_total, info.pv);

                    if (stop_requested) break;
                }

                long long elapsed = time_diff_ms(search_start, get_wall_time());
                if (elapsed < 1) elapsed = 1;

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

        } else if (strncmp(input, "stop", 4) == 0) {
            stop_requested = true;

        } else if (strncmp(input, "quit", 4) == 0) {
            break;
        }
    }

    free_tt(&main_tt);
    return 0;
}