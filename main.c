/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#ifndef _WIN32
#include <fcntl.h>
#endif
#ifndef _WIN32
#include <sys/select.h>
#endif
#include "board.h"
#include "movegen.h"
#include "eval.h"
#include "tt.h"
#include "utils.h"
#include "search.h"
#ifdef _WIN32
int win_stdin_ready(void);
int win_stdin_read(int fd, void* buf, unsigned int count);
#endif

bool position_changed = false;
bool stop_requested = false;

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

int check_stop(void) {
#ifndef _WIN32
    fd_set fds;
    struct timeval tv = {0, 0};
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
#endif

#ifdef _WIN32
    if (win_stdin_ready()) {
#else
    if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
#endif
        char buf[256];
        if (fgets(buf, sizeof(buf), stdin)) {
            if (strstr(buf, "stop")) {
                stop_requested = true;
                return 1;
            }
            if (strstr(buf, "position") || strstr(buf, "ucinewgame")) {
                position_changed = true;
                return 1;
            }
        }
    }
    return 0;
}

void print_uci_info(int depth, int seldepth, int score, long long time_ms, long long nodes, const char* pv) {

    long long nps = 0;
    if (time_ms > 0) {
        nps = (nodes * 1000LL) / time_ms;
    } else if (nodes > 0) {
        nps = nodes * 1000LL;
    }

    printf("info depth %d seldepth %d score cp %d time %lld nodes %lld nps %lld pv %s\n",
           depth, seldepth, score, time_ms, nodes, nps, pv);
    fflush(stdout);
}

void print_safe_bestmove(Move* best_move) {

    if (best_move && best_move->from >= 0 && best_move->to >= 0 &&
        best_move->from < 64 && best_move->to < 64 &&
        best_move->from != best_move->to) {
        printf("bestmove %s\n", move_to_string(best_move));
    } else {

        printf("bestmove e2e4\n");
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
    if (my_time < 5000 && my_increment <= 0) return my_time / 10;

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

    if (final_time < 100) final_time = 100;
    if (final_time > 60000) final_time = 60000;

    if (my_time < 10000) {
        final_time = my_time / 8;
        if (final_time < 100) final_time = 100;
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

void parse_position(Position* pos, const char* input) {
    if (strstr(input, "startpos")) {
        setup_starting_position(pos);

        const char* moves_ptr = strstr(input, "moves");
        if (moves_ptr) {
            moves_ptr += 5;

            char move_str[6];
            while (*moves_ptr) {

                while (*moves_ptr == ' ') moves_ptr++;
                if (!*moves_ptr) break;

                int i = 0;
                while (*moves_ptr && *moves_ptr != ' ' && i < 5) {
                    move_str[i++] = *moves_ptr++;
                }
                move_str[i] = '\0';

                Move move = parse_move(pos, move_str);
                if (move.from >= 0) {
                    UndoInfo undo;
                    make_move_full(pos, &move, &undo);
                }
            }
        }
    } else if (strstr(input, "fen")) {
        const char* fen_start = strstr(input, "fen") + 4;
        while (*fen_start == ' ') fen_start++;

        char fen[256];
        strncpy(fen, fen_start, sizeof(fen) - 1);
        fen[sizeof(fen) - 1] = '\0';

        char* moves_ptr = strstr(fen, " moves");
        if (moves_ptr) *moves_ptr = '\0';

        parse_fen(pos, fen);
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

Move search_with_time_limit(Position* pos, int time_limit_ms, SearchInfo* final_info) {

    set_search_time_limit(time_limit_ms);
    start_search_timer();

    struct timeval start_time = get_wall_time();
    Move best_move = {0};
    bool valid_move_found = false;
    int previous_score = 0;

    const int ASPIRATION_WINDOW = 30;
    const int MIN_ASPIRATION_DEPTH = 3;

    for (int depth = 1; depth <= MAX_DEPTH; depth++) {

        long long elapsed_before = time_diff_ms(start_time, get_wall_time());
        if (elapsed_before > time_limit_ms * 0.9) {
            break;
        }

        SearchInfo info = {0};
        info.start_time = start_time;
        info.nodes = 0;
        info.depth = depth;

        Move current_best;

        if (depth >= MIN_ASPIRATION_DEPTH && previous_score != 0 && valid_move_found) {

            int alpha = previous_score - ASPIRATION_WINDOW;
            int beta = previous_score + ASPIRATION_WINDOW;

            current_best = search_single_depth_with_nodes(pos, depth, alpha, &info);

            if (info.score <= alpha) {

                info.nodes = 0;
                current_best = search_single_depth_with_nodes(pos, depth, previous_score - 150, &info);
            } else if (info.score >= beta) {

                info.nodes = 0;
                current_best = search_single_depth_with_nodes(pos, depth, previous_score + 150, &info);
            }

        } else {

            current_best = search_single_depth_with_nodes(pos, depth, 0, &info);
        }

        if (stop_requested || position_changed || is_time_exceeded()) {
    		break;
        }

        best_move = current_best;
        valid_move_found = true;
        previous_score = info.score;
        *final_info = info;

        long long elapsed = time_diff_ms(start_time, get_wall_time());

        print_uci_info(depth, info.seldepth, info.score, elapsed, info.nodes, info.pv);

        if (depth >= 6) {
            if (info.score > 800) break;
            if (info.score < -800) break;
        }
    }

    return valid_move_found ? best_move : (Move){0};
}

int main() {

    setvbuf(stdin, NULL, _IOLBF, 0);
    setvbuf(stdout, NULL, _IOLBF, 0);

    init_masks();
    init_magic_bitboards();
    init_tt(&main_tt, 64);
    init_search();

    Position pos;
    setup_starting_position(&pos);

    char input[1024];

    while (fgets(input, sizeof(input), stdin)) {

        input[strcspn(input, "\n")] = 0;

        if (strncmp(input, "uci", 3) == 0) {
            printf("id name Chessenger 0.2\n");
            printf("id author chessengeria.eu\n");
            printf("uciok\n");
            fflush(stdout);

        } else if (strncmp(input, "isready", 7) == 0) {
            printf("readyok\n");
            fflush(stdout);

        } else if (strncmp(input, "ucinewgame", 10) == 0) {
            setup_starting_position(&pos);
            position_changed = false;
            stop_requested = false;

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

        } else if (strncmp(input, "go", 2) == 0) {

            stop_requested = false;
            position_changed = false;

            reset_search_state();

            if (strstr(input, "infinite")) {

                SearchInfo info = {0};
                struct timeval search_start = get_wall_time();
                info.start_time = search_start;
                info.nodes = 0;

                Move best_move = {0};
                bool valid_move_found = false;
                int previous_score = 0;

                const int ASPIRATION_WINDOW = 30;
                const int MIN_ASPIRATION_DEPTH = 3;

                for (int depth = 1; depth <= MAX_DEPTH; depth++) {
                    if (stop_requested || position_changed) break;

                    info.depth = depth;
                    info.nodes = 0;

                    Move current_best;

                    if (depth >= MIN_ASPIRATION_DEPTH && previous_score != 0 && valid_move_found) {

                        int alpha = previous_score - ASPIRATION_WINDOW;
                        int beta = previous_score + ASPIRATION_WINDOW;

                        current_best = search_single_depth_with_nodes(&pos, depth, alpha, &info);

                        if (info.score <= alpha) {

                            info.nodes = 0;
                            current_best = search_single_depth_with_nodes(&pos, depth, previous_score - 150, &info);
                        } else if (info.score >= beta) {

                            info.nodes = 0;
                            current_best = search_single_depth_with_nodes(&pos, depth, previous_score + 150, &info);
                        }
                    } else {

                        current_best = search_single_depth_with_nodes(&pos, depth, 0, &info);
                    }

                    if (!stop_requested && !position_changed) {
                        best_move = current_best;
                        valid_move_found = true;
                        previous_score = info.score;

                        long long elapsed = time_diff_ms(search_start, get_wall_time());
                        if (elapsed < 1) elapsed = 1;

                        print_uci_info(depth, info.seldepth, info.score, elapsed, info.nodes, info.pv);
                    }
                }

                if (!position_changed && valid_move_found) {
                    print_safe_bestmove(&best_move);
                }

            } else if (strstr(input, "movetime")) {

                int movetime = parse_movetime(input);
                printf("info string TIME_DEBUG: movetime=%dms\n", movetime);

                SearchInfo info = {0};
                Move best_move = search_with_time_limit(&pos, movetime, &info);

                print_safe_bestmove(&best_move);

            } else if (strstr(input, "wtime") || strstr(input, "btime")) {

                int wtime = parse_wtime(input);
                int btime = parse_btime(input);
                int winc = parse_winc(input);
                int binc = parse_binc(input);

                int search_time = calculate_search_time(wtime, btime, winc, binc, pos.to_move, &pos);

                SearchInfo info = {0};
                Move best_move = search_with_time_limit(&pos, search_time, &info);

                print_safe_bestmove(&best_move);

            } else {

                int depth = parse_depth(input);

                SearchInfo info = {0};
                struct timeval search_start = get_wall_time();
                info.start_time = search_start;
                info.nodes = 0;
                info.depth = depth;

                Move best_move = search_single_depth_with_nodes(&pos, depth, 0, &info);

                long long elapsed = time_diff_ms(search_start, get_wall_time());
                if (elapsed < 1) elapsed = 1;

                print_uci_info(depth, info.seldepth, info.score, elapsed, info.nodes, info.pv);

                print_safe_bestmove(&best_move);
            }

        } else if (strncmp(input, "stop", 4) == 0) {
            stop_requested = true;

        } else if (strncmp(input, "quit", 4) == 0) {
            break;
        }
    }

    free_tt(&main_tt);
    return 0;
}