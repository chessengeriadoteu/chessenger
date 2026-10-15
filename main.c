/* SPDX-License-Identifier: GPL-3.0-or-later AND GPL-3.0-only
 * Copyright (C) Dariusz Domagała
 * Copyright (C) 2019-2025 Morgan Houppin (Stash 37)
 * Copyright (C) Sam Moore (SimpleChess 3.5.0)
 * Portions of this file are adapted from Stash 37 and SimpleChess 3.5.0. Modified by Dariusz Domagała, 2026-10-15.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "board.h"
#include "movegen.h"
#include "eval.h"
#include "tt.h"
#include "utils.h"
#include "syzygy.h"
#include "search.h"
#include "tune.h"

#ifdef _WIN32
#define NULL_DEVICE "NUL"
#else
#define NULL_DEVICE "/dev/null"
#endif

#define ENGINE_VERSION "2.9.5"

bool position_changed = false;
bool stop_requested = false;
bool quit_requested = false;

#define MULTIPV_DEFAULT 1
#define MULTIPV_MIN     1
#define MULTIPV_MAX     MAX_MULTIPV
int multipv_setting = MULTIPV_DEFAULT;

#define THREADS_DEFAULT 1
#define THREADS_MIN     1

#define THREADS_MAX     64
int threads_setting = THREADS_DEFAULT;

static bool ponder_setting = false;

#define MOVE_OVERHEAD_MAX 5000
static int move_overhead_ms = 0;

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

#define BENCH_DEPTH 15
static const char* const BENCH_FEN[] = {
    "r2qk2r/1pp1nppp/pn2p3/4P3/2PP4/P1Q1BP1P/1P6/2KR1B1R b kq - 0 6",
    "8/8/8/4K3/8/4R3/8/5k2 b - - 18 79",
    "8/6p1/8/5k2/3K1P1b/8/8/8 w - - 6 65",
    "8/4k3/1K6/4bBp1/PP4P1/8/8/8 b - - 2 49",
    "4R3/8/7p/8/3p1r2/P7/7P/3k2K1 w - - 13 41",
    "6k1/7p/6p1/p2P4/5r1P/1P2R3/4K3/4B1q1 w - - 0 42",
    "r7/P7/1k1p4/Rnp5/8/4K3/8/4B3 w - - 7 63",
    "8/2k5/7p/5Kp1/6P1/3R1P1P/8/r7 b - - 5 43",
    "r2r2k1/pq1n1ppb/4p2p/2p4P/4P1P1/3N1Pb1/1PQ1B1K1/R4R2 w - - 0 25",
    "r4nk1/p4pp1/1p5p/bBp5/8/P4PB1/1P4PP/3R2K1 b - - 4 17",
    "1r2r1k1/Qp1q1pp1/2p2n2/N2pR3/1P4p1/P5P1/2R1P1BP/6K1 b - - 1 26",
    "2q1r1k1/3r1pp1/1p1Pp2p/p1p5/4Q2N/P1B4P/KP4P1/3R4 b - - 3 23",
    "8/5pkp/p4bp1/3P4/1RQ1P2P/2P2KP1/2r1NP2/3q4 w - - 2 35",
    "4n1k1/2b2ppp/r1P1b3/p7/PpN5/1P1Pq1PP/2P2PBK/4R3 w - - 0 18",
    "8/1R1nrpk1/2R3p1/2p1b2p/2P5/P4BPP/5P2/6K1 b - - 4 28",
    "1r6/4R1p1/5nkp/1pB5/1P3PP1/7P/7K/8 b - - 0 41",
    "1r6/5pk1/6p1/2p1p1P1/p1P1q3/6P1/P4QK1/5R2 w - - 1 39",
    "1B4k1/p4p2/1p5p/1r3P2/8/8/5K1P/8 w - - 0 38",
    "8/6k1/2R1bp2/4q1p1/PP1pP1Pp/3Q3P/8/5RK1 b - - 2 49",
    "rn1q1rk1/5pbp/pp3np1/2pPp1B1/P7/1QN2B2/1P3PPP/R4RK1 b - - 3 4",
    "R2q1rk1/1b1nbppp/2pppn2/8/1pPP4/1P2NNP1/1B2PPBP/3Q1RK1 b - - 0 9",
    "8/3K4/8/2Q2P2/3Pq3/8/8/1k6 b - - 14 82",
    "7k/1p2Q1pb/7p/p1Pr1P2/3p1qP1/1P2p2P/PB6/6RK w - - 2 41",
    "8/6pp/4pk2/2Kp4/8/q7/1r6/8 w - - 6 57",
    "8/8/4K3/8/8/p5k1/8/5q2 w - - 0 72",
    "8/6p1/6P1/5b2/4kP2/6Kp/8/8 w - - 1 70",
    "8/8/8/r2kpb2/4N3/p4KP1/5P2/R7 w - - 10 63",
    "6rk/p1qp3n/4b2p/4Pp1B/2pB2P1/QP6/P4PP1/2R3K1 b - - 0 30",
    "1R6/6k1/3rp3/1p3p2/1P3P1P/1B4P1/P2b2K1/8 b - - 4 41",
    "r1r2nk1/4pp1p/1p1b2p1/3p1P2/pP1P4/Pq2P3/R2QBP1P/4NRK1 w - - 1 23",
    "3qr1k1/n2rbpp1/p4n1p/1p1pB3/1P1P4/PBN2Q1P/2R2PP1/2R3K1 b - - 11 21",
    "8/1pp3kp/3pBqp1/3P4/2P4P/rP4P1/P2QR2K/r7 w - - 7 42",
    "8/8/2K1n2p/5pkP/3B4/1P2P3/5P2/8 b - - 4 42",
    "3r4/P1R1n3/2n1pk1p/1B4p1/1p1P4/1N4P1/1P3P2/6K1 b - - 4 40",
    "5r1k/pb6/1p4pp/nB2R1N1/8/P7/6PP/6K1 w - - 0 25",
    "4q1k1/Q2Nbp2/1p2p1p1/3nN2p/1P1Pb3/Pn2B1P1/5PBP/6K1 b - - 3 20",
    "r1q1r3/6kp/p2bp1p1/Pbp5/3pRP2/1P1B3Q/2PB2PP/5R1K b - - 4 18",
    "r6r/p4k1p/b1p1p3/2Bn1p2/Pp6/2P3P1/4PPBP/2RR2K1 b - - 0 14",
    "8/6R1/8/4P3/1p2K3/7p/5k2/5r2 w - - 11 56",
    "3R4/1p6/1Pk4p/3p4/3Kp3/8/1P4r1/8 w - - 12 45",
    "6k1/1R5p/2r2p2/1p3K2/p3NP2/6P1/2r5/R7 b - - 16 41",
    "8/1p3k2/p2N4/P1Bn4/1b6/7P/4NP2/5K2 b - - 1 36",
    "8/r5k1/4p3/3n1p1p/6pP/6P1/5R1K/1B6 w - - 0 52",
    "5k2/6p1/4B3/5R2/3r2PP/5P2/4n2K/8 b - - 2 39",
    "8/1b1nk1p1/1p5p/p1r2P2/P4KP1/PB6/2R3P1/4B3 b - - 0 42",
    "r4rk1/pp2ppbp/1qn3p1/4Pb2/2BP4/5N1P/3B1PP1/2RQK2R w K - 4 16",
    "5rk1/6p1/b3ppnp/p2q4/1p1N2P1/1P2BP1P/P4Q1K/3R4 w - - 1 20",
    "7k/1b1n4/1p6/pP1B4/P4P2/7P/5PP1/5RK1 b - - 0 34",
    "rn1qk2r/pbpp2pp/1p2p3/5p2/1bPP4/2nBPN2/PPQ2PPP/R1B2RK1 w kq - 0 2",
    "1R6/8/8/8/2pb4/3q4/6K1/2k5 w - - 2 75",
};
#define BENCH_N ((int)(sizeof(BENCH_FEN) / sizeof(BENCH_FEN[0])))

static long long go_depth_nodes_sum = 0;
static bool bench_active = false;
static int bench_depth = BENCH_DEPTH;
static bool bench_from_cmdline = false;
static long long bench_nodes_start = 0;
static struct timeval bench_t0;
static int bench_done = 0;
static double bench_sec_total = 0.0;
static int bench_stdout_copy = -1;

static bool queue_push_front(const char* text) {
    size_t n = strlen(text);
    size_t remaining = strlen(input_queue + input_queue_pos);
    if (n + remaining + 1 > sizeof(input_queue)) {
        printf("info string ERROR: bench: input queue too small (%zu + %zu B)\n", n, remaining);
        fflush(stdout);
        return false;
    }
    memmove(input_queue + n, input_queue + input_queue_pos, remaining + 1);
    memcpy(input_queue, text, n);
    input_queue_pos = 0;
    return true;
}

static void banner(FILE* f);

static void bench_start(const char* input) {
    int depth_arg = BENCH_DEPTH;
    const char* arg = input + 5;
    while (*arg == ' ') arg++;
    if (*arg != '\0') {
        int g = atoi(arg);
        if (g >= 1 && g <= MAX_DEPTH) depth_arg = g;
        else {
            printf("info string bench: depth '%s' outside 1..%d, using %d\n",
                   arg, MAX_DEPTH, BENCH_DEPTH);
            fflush(stdout);
        }
    }
    static char script[MAX_UCI_LINE * 2];
    int u = snprintf(script, sizeof(script), "setoption name Hash value 128\nucinewgame\n");
    for (int i = 0; i < BENCH_N && u > 0 && (size_t)u < sizeof(script); i++) {

        u += snprintf(script + u, sizeof(script) - (size_t)u,
                      "bench_update %d\nposition fen %s\ngo depth %d\n", i, BENCH_FEN[i], depth_arg);
    }
    if (u <= 0 || (size_t)u + sizeof("bench_end\n") > sizeof(script)) {
        printf("info string ERROR: bench: command script does not fit the buffer\n");
        fflush(stdout);
        return;
    }
    snprintf(script + u, sizeof(script) - (size_t)u, "bench_end\n");
    if (!queue_push_front(script)) return;
    bench_active = true;
    bench_queues_input = true;
    bench_depth = depth_arg;
    bench_nodes_start = go_depth_nodes_sum;
    bench_t0 = get_wall_time();
    bench_done = 0;
    bench_sec_total = 0.0;

    if (!bench_from_cmdline && bench_stdout_copy < 0) {
        fflush(stdout);
        int nul = open(NULL_DEVICE, O_WRONLY);
        if (nul >= 0) {
            bench_stdout_copy = dup(STDOUT_FILENO);
            if (bench_stdout_copy >= 0) dup2(nul, STDOUT_FILENO);
            close(nul);
        }
    }

    if (bench_from_cmdline) banner(stderr);
    if (isatty(STDERR_FILENO)) {
        fprintf(stderr, "\n  \033[1mChessenger %s - bench\033[0m   %d positions - depth %d - Hash 128 MB - 1 thread\n\n",
                ENGINE_VERSION, BENCH_N, depth_arg);
        fflush(stderr);
    }
}

static void bench_update(int done_count) {
    bench_done = done_count;
    long long ms = time_diff_ms(bench_t0, get_wall_time());
    if (!isatty(STDERR_FILENO)) {
        if (done_count < BENCH_N)
            fprintf(stderr, "Position: %d/%d (%s)\n", done_count + 1, BENCH_N, BENCH_FEN[done_count]);
        fflush(stderr);
        return;
    }
    enum { BAR_WIDTH = 30 };
    int filled = done_count * BAR_WIDTH / BENCH_N;
    fprintf(stderr, "\r  \033[32m");
    for (int i = 0; i < BAR_WIDTH; i++) {
        if (i == filled) fputs("\033[90m", stderr);
        fputs(i < filled ? "#" : ".", stderr);
    }
    if (done_count < BENCH_N)
        fprintf(stderr, "\033[0m  position \033[1m%2d/%d\033[0m  %3d %%  %5.1f s ",
                done_count + 1, BENCH_N, done_count * 100 / BENCH_N, ms / 1000.0);
    else
        fprintf(stderr, "\033[0m  \033[1;32mdone %d/%d\033[0m  100 %%  %5.1f s \n",
                BENCH_N, BENCH_N, ms / 1000.0);
    fflush(stderr);
}

#ifndef BUILD_FLAGS
#define BUILD_FLAGS "?"
#endif
#ifndef BUILD_DATE
#define BUILD_DATE "?"
#endif
#if defined(__APPLE__)
#  if defined(__aarch64__) || defined(__arm64__)
#    define BUILD_SYSTEM "macOS - Apple Silicon (arm64)"
#  elif defined(__x86_64__)
#    define BUILD_SYSTEM "macOS - Intel (x86_64)"
#  else
#    define BUILD_SYSTEM "macOS"
#  endif
#elif defined(_WIN32)
#  if defined(_M_ARM64) || defined(__aarch64__)
#    define BUILD_ARCH "arm64"
#  elif defined(_M_X64) || defined(__x86_64__)
#    define BUILD_ARCH "x64"
#  else
#    define BUILD_ARCH "x86"
#  endif
#  if defined(_WIN32_WINNT) && _WIN32_WINNT >= 0x0A00
#    define BUILD_SYSTEM "Windows 10+ - " BUILD_ARCH
#  elif defined(_WIN32_WINNT) && _WIN32_WINNT >= 0x0603
#    define BUILD_SYSTEM "Windows 8.1+ - " BUILD_ARCH
#  elif defined(_WIN32_WINNT) && _WIN32_WINNT >= 0x0601
#    define BUILD_SYSTEM "Windows 7+ - " BUILD_ARCH
#  else
#    define BUILD_SYSTEM "Windows - " BUILD_ARCH
#  endif
#elif defined(__linux__)
#  if defined(__x86_64__)
#    define BUILD_SYSTEM "Linux - x86_64"
#  elif defined(__aarch64__)
#    define BUILD_SYSTEM "Linux - aarch64"
#  else
#    define BUILD_SYSTEM "Linux"
#  endif
#else
#  define BUILD_SYSTEM "unknown system"
#endif
#define BUILD_STR_(x) #x
#define BUILD_STR(x) BUILD_STR_(x)
#if defined(__clang__) && defined(__apple_build_version__)
#  define BUILD_COMPILER "Apple clang " BUILD_STR(__clang_major__) "." BUILD_STR(__clang_minor__) "." BUILD_STR(__clang_patchlevel__)
#elif defined(__clang__)
#  define BUILD_COMPILER "clang " BUILD_STR(__clang_major__) "." BUILD_STR(__clang_minor__) "." BUILD_STR(__clang_patchlevel__)
#elif defined(__GNUC__)
#  define BUILD_COMPILER "gcc " BUILD_STR(__GNUC__) "." BUILD_STR(__GNUC_MINOR__) "." BUILD_STR(__GNUC_PATCHLEVEL__)
#elif defined(_MSC_VER)
#  define BUILD_COMPILER "MSVC " BUILD_STR(_MSC_FULL_VER)
#else
#  define BUILD_COMPILER "unknown compiler"
#endif
#if defined(BUILD_PGO) && BUILD_PGO
#  define BUILD_PGO_STR "PGO"
#else
#  define BUILD_PGO_STR "no PGO"
#endif

static void banner(FILE* f) {
    fprintf(f, "===========================================================================\n"
               "Chessenger %s\n"
               "Build  : " BUILD_PGO_STR " - " BUILD_SYSTEM " - " BUILD_COMPILER " - " BUILD_FLAGS "\n"
               "Built  : " BUILD_DATE "\n"
               "Author : Dariusz Domagala (chessengeria.eu) - UCI chess engine\n"
               "===========================================================================\n",
            ENGINE_VERSION);
    fflush(f);
}

static void bench_row(Move* mv, Position* pos, int result_bb, int depth_arg,
                         struct timeval t0, long long node_count) {
    struct timeval t1 = get_wall_time();
    double secs = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_usec - t0.tv_usec) / 1e6;
    if (secs < 0) secs = 0;
    bench_sec_total += secs;
    char r[16];

    if (mv && mv->from >= 0 && mv->to >= 0 && mv->from < 64 && mv->to < 64 &&
        mv->from != mv->to) {
        snprintf(r, sizeof(r), "%s", move_to_string(mv));
    } else {
        Move legal[256];
        int n = generate_legal_moves(pos, legal);
        snprintf(r, sizeof(r), "%s", n > 0 ? move_to_string(&legal[0]) : "0000");
    }
    char w[16];
    int mate_plies = MATE_SCORE - (result_bb < 0 ? -result_bb : result_bb);
    if (depth_arg == 0) {
        snprintf(w, sizeof(w), "%8s", "-");
    } else if (mate_plies >= 0 && mate_plies < MAX_DEPTH * 2) {
        char m[12];
        snprintf(m, sizeof(m), "%sM%d", result_bb < 0 ? "-" : "", (mate_plies + 1) / 2);
        snprintf(w, sizeof(w), "%8s", m);
    } else {
        snprintf(w, sizeof(w), "%5d cp", result_bb);
    }
    long long nps = secs > 0 ? (long long)(node_count / secs) : 0;

    if (isatty(STDERR_FILENO)) fputs("\r\033[K", stderr);
    fprintf(stderr, "Bench #%3d: %-6s %s %3d ply %11.6f sec. %11lld nodes %10lld nps\n",
            bench_done + 1, r, w, depth_arg, secs, node_count, nps);
    fflush(stderr);
}

static void bench_end(void) {
    long long ms = time_diff_ms(bench_t0, get_wall_time());
    if (ms < 1) ms = 1;
    long long node_count = go_depth_nodes_sum - bench_nodes_start;
    long long nps = node_count * 1000 / ms;
    bench_active = false;
    bench_queues_input = false;
    if (bench_stdout_copy >= 0) {
        fflush(stdout);
        dup2(bench_stdout_copy, STDOUT_FILENO);
        close(bench_stdout_copy);
        bench_stdout_copy = -1;
    }
    printf("info string bench: positions %d nodes %lld time %lld nps %lld\n", BENCH_N, node_count, ms, nps);
    fflush(stdout);
    bench_update(BENCH_N);

    fprintf(stderr, "Overall:   %d positions %22.6f sec. %11lld nodes %10lld nps\n",
            BENCH_N, bench_sec_total, node_count,
            bench_sec_total > 0 ? (long long)(node_count / bench_sec_total) : 0);

    fprintf(stderr, "\n===========================================================================\n"
                    "Positions       : %d (depth %d, Hash 128 MB, 1 thread)\n"
                    "Total time (ms) : %lld\n"
                    "Nodes searched  : %lld\n"
                    "Nodes/second    : %lld  (%.2f Mnps)\n"
                    "===========================================================================\n",
            BENCH_N, bench_depth, ms, node_count, nps, nps / 1e6);
    fflush(stderr);
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

    char tbh[32];
    if (syzygy_limit > 0) snprintf(tbh, sizeof(tbh), " tbhits %lld", SYZYGY_HITS());
    else                  tbh[0] = '\0';

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
        printf("info depth %d seldepth %d%s score mate %d time %lld nodes %lld nps %lld%s pv %s\n",
               depth, seldepth, mpv, mate_moves, time_ms, nodes, nps, tbh, pv);
    } else {
        printf("info depth %d seldepth %d%s score cp %d time %lld nodes %lld nps %lld%s pv %s\n",
               depth, seldepth, mpv, score, time_ms, nodes, nps, tbh, pv);
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

#include <math.h>

#define EFFORT_SCORE_BAND 10000

#define TM_HARD_CAP(mt) ((long long)((mt) * 0.40f))

static int tm_flexible = 0;

static int tm_hard_ms = 0;

static int tm_set_hard(int go_budget_ms, int my_time) {
    long long hard_ms = (long long)go_budget_ms * 5 / 2;
    long long cap_ms  = TM_HARD_CAP(my_time);
    if (hard_ms > cap_ms)  hard_ms = cap_ms;
    if (hard_ms < go_budget_ms) hard_ms = go_budget_ms;
    tm_hard_ms = (int)hard_ms;
    return go_budget_ms;
}

int calculate_search_time(int wtime, int btime, int winc, int binc, int movestogo, Color to_move, Position* pos) {
    int my_time = (to_move == WHITE) ? wtime : btime;
    int my_increment = (to_move == WHITE) ? winc : binc;

    if (my_time <= 0) return tm_set_hard(100, my_time);

    if (move_overhead_ms > 0) {
        my_time -= move_overhead_ms;
        if (my_time < 1) my_time = 1;
    }

    if (movestogo > 0) {
        const int MOVE_OVERHEAD = 30;
        int safe = my_time - MOVE_OVERHEAD;
        if (safe < 1) return tm_set_hard(1, my_time);
        int n = movestogo > 100 ? 100 : movestogo;
        long long alloc = (long long)safe / n;
        if (my_increment > 0) alloc += ((long long)my_increment * 7) / 10;
        long long cap = ((long long)safe * 4) / 5;
        if (alloc > cap) alloc = cap;
        if (alloc < 1) alloc = 1;
        return tm_set_hard((int)alloc, my_time);
    }

    if (my_increment <= 0) {
        const int MOVE_OVERHEAD = 30;
        const int MOVE_HORIZON  = 25;
        int safe = my_time - MOVE_OVERHEAD;
        if (safe < 1) return tm_set_hard(1, my_time);
        int alloc = safe / MOVE_HORIZON;
        return tm_set_hard(alloc < 1 ? 1 : alloc, my_time);
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

        time_per_move = total_time_left / 30.0f + increment * 0.8f;

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

    float max_time_per_move = total_time_left * 0.16f;
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

    {
        long long hard_ms = (long long)final_time * 5 / 2;
        long long cap_ms  = TM_HARD_CAP(my_time);
        if (hard_ms > cap_ms)      hard_ms = cap_ms;
        if (hard_ms < final_time) hard_ms = final_time;
        tm_hard_ms = (int)hard_ms;
    }

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

static long long uci_nodes_before    = 0;
static int       uci_iter_depth = 0;
static long long uci_nodes_now(const SearchInfo* info) { return SMP_NODES(uci_nodes_before + info->nodes); }

static void uci_root_change_main(const SearchInfo* info, int score, const char* pv) {
    if (multipv_setting > 1 || uci_iter_depth <= 0) return;
    long long t = uci_gate_ms();
    if (t < 0) return;
    print_uci_info(uci_iter_depth, info->seldepth, score, t, uci_nodes_now(info), pv);
}

static void uci_report_window_bound(int depth, const SearchInfo* info, Move mv, bool is_lower_bound) {
    if (multipv_setting > 1) return;
    long long t = uci_gate_ms();
    if (t < 0) return;
    char result_bb[32];
    int mate_plies = MATE_SCORE - (info->score < 0 ? -info->score : info->score);
    if (mate_plies >= 0 && mate_plies < MAX_DEPTH * 2) {
        int mate_moves = (mate_plies + 1) / 2;
        snprintf(result_bb, sizeof(result_bb), "mate %d", info->score < 0 ? -mate_moves : mate_moves);
    } else {
        snprintf(result_bb, sizeof(result_bb), "cp %d", info->score);
    }
    char pv[16] = "";
    if (mv.from >= 0 && mv.to >= 0 && mv.from < 64 && mv.to < 64 && mv.from != mv.to)
        snprintf(pv, sizeof(pv), " pv %s", move_to_string(&mv));
    long long n = uci_nodes_now(info);
    long long nps = t > 0 ? n * 1000LL / t : 0;
    printf("info depth %d seldepth %d score %s %s time %lld nodes %lld nps %lld%s\n", depth, info->seldepth, result_bb,
           is_lower_bound ? "lowerbound" : "upperbound", t, n, nps, pv);
    fflush(stdout);
}

static Move search_root_with_aspiration(Position* pos, int depth, int previous_score,
                                        bool use_aspiration, SearchInfo* info,
                                        bool* window_open) {
    int delta = ASPIRATION_WINDOW;
    int alpha = ROOT_FULL_ALPHA;
    int beta  = ROOT_FULL_BETA;
    uci_iter_depth  = depth;
    uci_root_change_hook = uci_root_change_main;

    {
        Move best_found = {0};

        int search_depth_cur = depth;

        delta = 0;
        *window_open = false;

        if (use_aspiration && depth > 5) {
            delta = ASP_DELTA0 + (previous_score < 0 ? -previous_score : previous_score) / ASP_DIV;
            alpha = previous_score - delta;
            beta  = previous_score + delta;
        }

        while (true) {
            if (alpha < -2 * QUEEN_VALUE) alpha = ROOT_FULL_ALPHA;
            if (beta  >  2 * QUEEN_VALUE) beta  = ROOT_FULL_BETA;

            best_found = search_single_depth_with_nodes(pos, search_depth_cur, alpha, beta, info);

            if (stop_requested || position_changed || is_time_exceeded()) {
                *window_open = (info->score <= alpha && alpha > ROOT_FULL_ALPHA) ||
                               (info->score >= beta  && beta  < ROOT_FULL_BETA);
                break;
            }

            if (info->score <= alpha && alpha > ROOT_FULL_ALPHA) {
                uci_report_window_bound(depth, info, best_found, false);
                search_depth_cur = depth;
                beta   = (alpha + beta) / 2;
                alpha  = info->score - delta;
            } else if (info->score >= beta && beta < ROOT_FULL_BETA) {
                uci_report_window_bound(depth, info, best_found, true);
                search_depth_cur -= (search_depth_cur - 1 > (depth - 1) / 2);
                beta   = info->score + delta;
            } else {
                break;
            }
            delta += delta * ASP_GROW / 256;
        }
        return best_found;
    }

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
            uci_report_window_bound(depth, info, best, false);
            delta *= 4;
            alpha = previous_score - delta;
            if (alpha < ROOT_FULL_ALPHA) alpha = ROOT_FULL_ALPHA;
        } else if (info->score >= beta && beta < ROOT_FULL_BETA) {
            uci_report_window_bound(depth, info, best, true);
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
        print_uci_info(depth, first_seldepth, first_score, elapsed, SMP_NODES(*nodes_total), first_pv);
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

        if (is_root_excluded(&segment) || !is_root_included(&segment)) break;

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
                               elapsed_final, SMP_NODES(*nodes_total), pv_lines[i].pv);
    }

    report_hashfull();

    root_excluded_count = 0;
}

Move search_with_time_limit(Position* pos, int time_limit_ms, SearchInfo* final_info) {

    const long long tm_soft = (tm_flexible && time_limit_ms > 0) ? time_limit_ms : 0;
    const int  tm_hard = tm_soft ? (tm_hard_ms > 0 ? tm_hard_ms
                                                      : (int)(tm_soft * 5 / 2))
                                   : time_limit_ms;
    bool tm_move_changed = false;

    static const double cs_stability_scale[5] = {2.50, 1.20, 0.90, 0.80, 0.75};
    int  cs_stab = 0;
    long long cs_optimal = tm_hard;
    bool cs_only_move = false;
    if (tm_soft) {
        Move only_move_buf[256];
        cs_only_move = (generate_legal_moves(pos, only_move_buf) == 1);
    }
    set_search_time_limit(tm_hard);
    start_search_timer();
    new_search_reset();

    struct timeval start_time = get_wall_time();
    Move best_move = {0};
    bool valid_move_found = false;
    int previous_score = 0;

    long long nodes_total = 0;

    int  decisive_last_score = 0;
    int  decisive_last_depth = 0;
    bool decisive_have_iter  = false;

    for (int depth = 1; depth <= MAX_DEPTH; depth++) {

        if (depth > 1) {
            long long elapsed_before = time_diff_ms(start_time, get_wall_time());
            if (tm_soft) {

                if (elapsed_before >= cs_optimal) break;

                if ((double)elapsed_before * 1.4 > (double)tm_hard) break;
            } else
            if (tm_soft) {

                bool past_soft = elapsed_before > tm_soft * 0.9;
                bool may_extend = tm_move_changed
                                     && elapsed_before < 2 * tm_soft;
                if (past_soft && !may_extend) {
                    break;
                }
            } else
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
        uci_nodes_before = nodes_total;
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

        tm_move_changed = valid_move_found
                          && (current_best.from != best_move.from
                              || current_best.to != best_move.to
                              || current_best.promotion != best_move.promotion);

        if (tm_soft) {
            cs_stab = (!valid_move_found || tm_move_changed) ? 0
                    : (cs_stab < 4 ? cs_stab + 1 : 4);
            double time_scale = cs_only_move ? 0.2 : 1.0;
            time_scale *= cs_stability_scale[cs_stab];
            if (valid_move_found) {
                int score_drop = previous_score - info.score;
                if (score_drop >  100) score_drop =  100;
                if (score_drop < -100) score_drop = -100;
                time_scale *= pow(2.0, score_drop / 100.0);
            }
            long long opt = (long long)(tm_soft * time_scale);
            cs_optimal = opt < tm_hard ? opt : tm_hard;
            if (valid_move_found && depth >= 12 && cs_stab >= 4 && !cs_only_move && root_excluded_count == 0
                    && abs(info.score) < EFFORT_SCORE_BAND && effort_total > 0) {
                const long long effort_share_pct = 100 * effort_best / effort_total;
                if (effort_share_pct > 95) cs_optimal = cs_optimal * (100 - 60 * (effort_share_pct - 95) / 5) / 100;
            }
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

        if (depth >= DECISIVE_STOP_MIN_DEPTH && (info.score > 800 || info.score < -800)) {
            if (decisive_have_iter && decisive_last_depth == depth - 1
                                      && decisive_last_score == info.score) {
                break;
            }
        }
        decisive_last_score = info.score;
        decisive_last_depth = depth;
        decisive_have_iter  = true;
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

int main(int argc, char** argv) {

    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IOLBF, 0);

    init_masks();
    init_magic_bitboards();
    init_zobrist();
    init_tt(&main_tt, TT_HASH_MB_DEFAULT);
    init_search();
    endgame_init();

    Position pos;
    setup_starting_position(&pos);

    static char input[MAX_UCI_LINE];

    bool uci_announced = false;

    if (argc > 1 && strcmp(argv[1], "bench") == 0) {
        snprintf(input, sizeof(input), "bench %s\n", argc > 2 ? argv[2] : "");
        queue_input(input);
        bench_from_cmdline = true;

        if (!freopen(NULL_DEVICE, "w", stdout)) return 1;
    } else if (isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) {

        banner(stdout);
    }

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
            {
                extern void search_new_game(void);
                search_new_game();
            }
            smp_new_game();
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
            printf("option name Move Overhead type spin default 0 min 0 max %d\n", MOVE_OVERHEAD_MAX);

            printf("option name SyzygyPath type string default <empty>\n");
            printf("option name SyzygyProbeLimit type spin default 7 min 0 max 7\n");
            printf("option name SyzygyProbeDepth type spin default 1 min 1 max 100\n");
            printf("option name Syzygy50MoveRule type check default true\n");

            printf("option name UCI_Chess960 type check default false\n");
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

            if (opt_ok && strcasecmp(opt_name, "Hash") == 0) {

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
            } else if (opt_ok && strcasecmp(opt_name, "MultiPV") == 0) {

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
            } else if (opt_ok && strcasecmp(opt_name, "Clear Hash") == 0) {

                clear_tt(&main_tt);
                clear_killers();
                clear_history();
                printf("info string Clear Hash: table, killers and history cleared\n");
                fflush(stdout);
            } else if (opt_ok && strcasecmp(opt_name, "Threads") == 0) {

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
                    {
                        const int alive_threads = smp_set_threads(threads_setting);
                        if (alive_threads != threads_setting) {
                            printf("info string Threads %d: created only %d threads\n", threads_setting, alive_threads);
                            threads_setting = alive_threads;
                        }
                    }
                    if (want != want_l) {
                        printf("info string Threads %ld REQUESTED -> %d APPLIED (range %d..%d)\n",
                               want_l, threads_setting, THREADS_MIN, THREADS_MAX);
                    } else {
                        printf("info string Threads %d\n", threads_setting);
                    }
                }
                fflush(stdout);
            } else if (opt_ok && strcasecmp(opt_name, "Ponder") == 0) {

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
            } else if (opt_ok && strcasecmp(opt_name, "Move Overhead") == 0) {

                if (opt_value[0] == '\0') {
                    printf("info string setoption Move Overhead: missing value, option ignored\n");
                } else if (!parse_uci_int(opt_value, &want_l)) {
                    printf("info string setoption Move Overhead: value `%s` is not a number, option ignored\n",
                           opt_value);
                } else {
                    long want = want_l;
                    if (want < 0) want = 0;
                    if (want > MOVE_OVERHEAD_MAX) want = MOVE_OVERHEAD_MAX;
                    move_overhead_ms = (int)want;
                    printf("info string Move Overhead %d ms\n", move_overhead_ms);
                }
                fflush(stdout);
            } else if (opt_ok && strcasecmp(opt_name, "UCI_Chess960") == 0) {

                if (strcmp(opt_value, "true") == 0) {
                    uci_chess960 = true;
                    printf("info string UCI_Chess960 true\n");
                } else if (strcmp(opt_value, "false") == 0) {
                    uci_chess960 = false;
                    printf("info string UCI_Chess960 false\n");
                } else {
                    printf("info string setoption UCI_Chess960: value `%s` is neither "
                           "`true` nor `false`, option ignored\n", opt_value);
                }
                fflush(stdout);
            } else if (opt_ok && strcasecmp(opt_name, "SyzygyPath") == 0) {

                syzygy_set_path(opt_value);
                printf("info string SyzygyPath: probing up to %d pieces\n", syzygy_limit);
                fflush(stdout);
            } else if (opt_ok && strcasecmp(opt_name, "SyzygyProbeLimit") == 0) {
                if (opt_value[0] == '\0' || !parse_uci_int(opt_value, &want_l)) {
                    printf("info string setoption SyzygyProbeLimit: value `%s` is not a number, option ignored\n",
                           opt_value);
                } else {
                    syzygy_set_limit((int)want_l);
                    printf("info string SyzygyProbeLimit %ld: probing up to %d pieces\n", want_l, syzygy_limit);
                }
                fflush(stdout);
            } else if (opt_ok && strcasecmp(opt_name, "SyzygyProbeDepth") == 0) {

                if (opt_value[0] == '\0' || !parse_uci_int(opt_value, &want_l)) {
                    printf("info string setoption SyzygyProbeDepth: value `%s` is not a number, option ignored\n",
                           opt_value);
                } else {
                    long g = want_l < 1 ? 1 : (want_l > 100 ? 100 : want_l);
                    syzygy_set_depth((int)g);
                    printf("info string SyzygyProbeDepth %ld\n", g);
                }
                fflush(stdout);
            } else if (opt_ok && strcasecmp(opt_name, "Syzygy50MoveRule") == 0) {

                if (strcmp(opt_value, "true") == 0 || strcmp(opt_value, "false") == 0) {
                    syzygy_set_rule50(opt_value[0] == 't');
                    printf("info string Syzygy50MoveRule %s\n", opt_value);
                } else {
                    printf("info string setoption Syzygy50MoveRule: value `%s` is neither `true` nor `false`, "
                           "option ignored\n", opt_value);
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

            syzygy_hits = 0;

            if (root_included_count == 0 && syzygy_limit > 0) {
                Move tb_list[MAX_MULTIPV];
                int tb_wdl = 0;
                int tb_count = syzygy_root(&pos, tb_list, MAX_MULTIPV, &tb_wdl);
                if (tb_count > 0) {
                    for (int i = 0; i < tb_count; i++) root_included[i] = tb_list[i];
                    root_included_count = tb_count;
                    printf("info string syzygy root: %s, %d move(s)\n",
                           tb_wdl > 0 ? "win" : tb_wdl < 0 ? "loss" : "draw", tb_count);
                    fflush(stdout);
                }
            }
            set_node_limit(parse_nodes_ll(input));

            smp_start(&pos, multipv_setting);

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
    new_search_reset();

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
                    uci_nodes_before = 0;
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

                smp_halt();
                emit_bestmove(valid_move_found ? &best_move : NULL, &pos, info.pv);

                set_ponder_mode(false, 0);

            } else if (strstr(input, "movetime")) {

                int movetime = parse_movetime(input);
                printf("info string TIME_DEBUG: movetime=%dms\n", movetime);

                SearchInfo info = {0};
                tm_flexible = 0;
                Move best_move = search_with_time_limit(&pos, movetime, &info);
                smp_halt();

                emit_bestmove(&best_move, &pos, info.pv);

            } else if (strstr(input, "wtime") || strstr(input, "btime")) {

                int wtime = parse_wtime(input);
                int btime = parse_btime(input);
                int winc = parse_winc(input);
                int binc = parse_binc(input);

                int movestogo = parse_movestogo(input);

                int search_time = calculate_search_time(wtime, btime, winc, binc, movestogo, pos.to_move, &pos);

                SearchInfo info = {0};
                tm_flexible = 1;
                Move best_move = search_with_time_limit(&pos, search_time, &info);
                smp_halt();

                emit_bestmove(&best_move, &pos, info.pv);

            } else {

                set_search_time_limit(0);
                start_search_timer();
    new_search_reset();

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
                    uci_nodes_before = nodes_total;
                    Move current_best = search_root_with_aspiration(&pos, depth, previous_score,
                                                                    use_aspiration, &info,
                                                                    &window_open);

                    nodes_total += info.nodes;
                    go_depth_nodes_sum += info.nodes;

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

                smp_halt();
                {
                    const long long helper_nodes = smp_helper_nodes();
                    nodes_total          += helper_nodes;
                    go_depth_nodes_sum  += helper_nodes;
                }

                long long elapsed = time_diff_ms(search_start, get_wall_time());
                if (elapsed < 1) elapsed = 1;

                if (nodes_target > 0 && node_limit_hit()) {
                    printf("info string go nodes: budget %lld, searched %lld, "
                           "deepest completed depth %d\n",
                           nodes_target, SMP_NODES(get_nodes_searched()), last_completed_depth);
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

                if (bench_active)
                    bench_row(&best_move, &pos, previous_score, last_completed_depth,
                                 search_start, nodes_total);

                print_safe_bestmove(&best_move, &pos);
            }

            drain_search_input();
            if (quit_requested) break;

        } else if (strcmp(input, "bench") == 0 || strncmp(input, "bench ", 6) == 0) {
            bench_start(input);

        } else if (strncmp(input, "bench_update ", 13) == 0 && bench_active) {
            bench_update(atoi(input + 13));

        } else if (strcmp(input, "bench_end") == 0 && bench_active) {
            bench_end();
            if (bench_from_cmdline) break;

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

    smp_shutdown();
    free_tt(&main_tt);
    return 0;
}