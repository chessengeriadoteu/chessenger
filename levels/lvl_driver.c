/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT AND GPL-3.0-only
 * Copyright (C) Dariusz Domagała
 * Copyright (C) Oliver Brausch (OliThink 5.10.5, MIT, see LICENSES/OLITHINK-MIT.txt)
 * Copyright (C) 2019-2025 Morgan Houppin (Stash 37)
 * Copyright (C) Sam Moore (SimpleChess 3.5.0)
 * Portions of this file are adapted from OliThink 5.10.5, Stash 37 and SimpleChess 3.5.0. Modified by Dariusz Domagała, 2026-10-21.
 */
void print_uci_info(int depth, int seldepth, int score, long long time_ms, long long nodes, const char* pv);
void print_uci_info_multipv(int multipv_idx, int depth, int seldepth, int score,
                            long long time_ms, long long nodes, const char* pv);

#define LVL_ROOT_FULL_ALPHA      (-31000)
#define LVL_ROOT_FULL_BETA       ( 31000)
#define LVL_ASPIRATION_WINDOW    30
#define LVL_MIN_ASPIRATION_DEPTH 3
#define LVL_EFFORT_SCORE_BAND            10000
#define LVL_BEST_MOVE_RATIO            1.4
#define LVL_DECISIVE_STOP_MIN_DEPTH    10
#define LVL_TM_HARD_CAP(mt)        ((long long)((mt) * 0.40f))
#define LVL_MULTIPV_MAX          MAX_MULTIPV

#define LVL_SKILL_MAX   20
#define LVL_SKILL_TOP   10
#define LVL_ELO_MIN     1200
#define LVL_ELO_STEP    200
#define LVL_TOL_0       120.0
#define LVL_TOL_STEP    6.0
#define LVL_NOISE_CP    100.0
#define LVL_LINES       4

static const int LVL_TABLE[] = { 939, 951, 969, 997, 1041, 1107, 1206, 1345, 1529, 1754, 2004, 2252, 2474, 2653,
                                 2788 };
#define LVL_TABLE_N ((int)(sizeof(LVL_TABLE) / sizeof(LVL_TABLE[0])))

static double lvl_level_elo(int elo) {
    if (elo <= LVL_TABLE[0]) return 0.0;
    for (int i = 1; i < LVL_TABLE_N; i++) {
        if (elo > LVL_TABLE[i]) continue;
        return (double)(i - 1) + (double)(elo - LVL_TABLE[i - 1]) / (double)(LVL_TABLE[i] - LVL_TABLE[i - 1]);
    }
    return (double)(LVL_TABLE_N - 1);
}

static double lvl_level(const LevelStrength* s) {
    if (s->env) return s->env_level;
    if (s->limit) return lvl_level_elo(s->elo);
    return s->skill >= LVL_SKILL_TOP ? (double)LVL_SKILL_MAX : lvl_level_elo(LVL_ELO_MIN + LVL_ELO_STEP * (s->skill - 1));
}

static uint64_t lvl_rng = 0;

static void lvl_seed(uint64_t seed) {
    seed += 0x9E3779B97F4A7C15ULL;
    seed = (seed ^ (seed >> 30)) * 0xBF58476D1CE4E5B9ULL;
    seed = (seed ^ (seed >> 27)) * 0x94D049BB133111EBULL;
    lvl_rng = (seed ^ (seed >> 31)) | 1;
}

static double lvl_random(void) {
    if (lvl_rng == 0) {
        const char* env = getenv("CHESSENGER_SEED");
        uint64_t seed = 0;
        if (env && env[0]) seed = strtoull(env, NULL, 10);
        else {
            const struct timeval tv = get_wall_time();
            seed = ((uint64_t)tv.tv_sec << 20) ^ (uint64_t)tv.tv_usec ^ ((uint64_t)(uintptr_t)&seed << 9);
        }
        lvl_seed(seed);
    }
    uint64_t x = lvl_rng;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    lvl_rng = x;
    return (double)((x * 2685821657736338717ULL) >> 11) * (1.0 / 9007199254740992.0);
}

static bool      lvl_ready = false;
static bool      lvl_own_buffer = false;
static bool      lvl_level_go = false;
static double    lvl_l = 20.0;
static int       lvl_lines = 0;
static int       lvl_pick_d = 0;
static bool      lvl_level_done = false;
static int       lvl_multipv = 1;
static char      lvl_completed_pv[1024] = "";

static int       lvl_pick_count = 0, lvl_pick_depth = 0;
static int       lvl_pick_score[MAX_MULTIPV], lvl_pick_seldepth[MAX_MULTIPV];
static char      lvl_pick_pv[MAX_MULTIPV][1024];
static long long lvl_pick_time = 0, lvl_pick_nodes = 0;

typedef struct {
    int  score;
    int  seldepth;
    char pv[1024];
} LevelLine;
static LevelLine lvl_lines_tab[LVL_MULTIPV_MAX];

static long long lvl_uci_nodes_before    = 0;
static int       lvl_uci_iter_depth = 0;
static long long lvl_uci_nodes_now(const SearchInfo* info) { return lvl_uci_nodes_before + info->nodes; }

static void lvl_uci_root_change_hook(const SearchInfo* info, int score, const char* pv) {
    if (lvl_multipv > 1 || lvl_uci_iter_depth <= 0) return;
    if (lvl_lines > 0) return;
    long long t = uci_gate_ms();
    if (t < 0) return;
    print_uci_info(lvl_uci_iter_depth, info->seldepth, score, t, lvl_uci_nodes_now(info), pv);
}

static void lvl_window_bound(int depth, const SearchInfo* info, Move this_move, bool is_lower_bound) {
    if (lvl_multipv > 1) return;
    if (lvl_lines > 0) return;
    if (console_takes_search_output()) {
        const int plies = MATE_SCORE - (info->score < 0 ? -info->score : info->score);
        const bool mate = plies >= 0 && plies < MAX_DEPTH * 2;
        const bool ok = this_move.from >= 0 && this_move.to >= 0 && this_move.from < 64 && this_move.to < 64 && this_move.from != this_move.to;
        console_search_bound(depth, info->seldepth, info->score, mate,
                             mate ? ((plies + 1) / 2) * (info->score < 0 ? -1 : 1) : 0, lvl_uci_nodes_now(info),
                             ok ? &this_move : NULL, is_lower_bound);
        return;
    }
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
    if (this_move.from >= 0 && this_move.to >= 0 && this_move.from < 64 && this_move.to < 64 && this_move.from != this_move.to)
        snprintf(pv, sizeof(pv), " pv %s", move_to_string(&this_move));
    long long n = lvl_uci_nodes_now(info);
    long long nps = t > 0 ? n * 1000LL / t : 0;
    printf("info depth %d seldepth %d score %s %s time %lld nodes %lld nps %lld%s\n", depth, info->seldepth, result_bb,
           is_lower_bound ? "lowerbound" : "upperbound", t, n, nps, pv);
    fflush(stdout);
}

static Move lvl_root(Position* pos, int depth, int alpha, int beta, SearchInfo* info) {
    return lvl_search_root(pos, depth, alpha, beta, info, &lvl_node_count, &lvl_aborted);
}

static Move lvl_aspiration(Position* pos, int depth, int previous_score, bool use_aspiration, SearchInfo* info,
                         bool* window_open) {
    int delta = LVL_ASPIRATION_WINDOW;
    int alpha = LVL_ROOT_FULL_ALPHA;
    int beta  = LVL_ROOT_FULL_BETA;
    lvl_uci_iter_depth = depth;
    uci_root_change_hook   = lvl_uci_root_change_hook;
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
        if (alpha < -2 * QUEEN_VALUE) alpha = LVL_ROOT_FULL_ALPHA;
        if (beta  >  2 * QUEEN_VALUE) beta  = LVL_ROOT_FULL_BETA;

        best_found = lvl_root(pos, search_depth_cur, alpha, beta, info);

        if (stop_requested || position_changed || is_time_exceeded()) {
            *window_open = (info->score <= alpha && alpha > LVL_ROOT_FULL_ALPHA) ||
                           (info->score >= beta  && beta  < LVL_ROOT_FULL_BETA);
            break;
        }

        if (info->score <= alpha && alpha > LVL_ROOT_FULL_ALPHA) {
            lvl_window_bound(depth, info, best_found, false);
            search_depth_cur = depth;
            beta   = (alpha + beta) / 2;
            alpha  = info->score - delta;
        } else if (info->score >= beta && beta < LVL_ROOT_FULL_BETA) {
            lvl_window_bound(depth, info, best_found, true);
            search_depth_cur -= (search_depth_cur - 1 > (depth - 1) / 2);
            beta   = info->score + delta;
        } else {
            break;
        }
        delta += delta * ASP_GROW / 256;
    }
    return best_found;
}

static void lvl_hashfull(void) {
    if (console_takes_search_output()) return;
    printf("info hashfull %d\n", lvl_hashfull_tt(&lvl_tt));
    fflush(stdout);
}

static void lvl_pick_line(int i, int score, int seldepth, const char* pv) {
    if (i < 0 || i >= MAX_MULTIPV) return;
    lvl_pick_score[i] = score;
    lvl_pick_seldepth[i] = seldepth;
    snprintf(lvl_pick_pv[i], sizeof(lvl_pick_pv[i]), "%s", pv ? pv : "");
}

static bool lvl_pick_at_depth(int depth, int count, bool complete, long long time_ms, long long nodes) {
    lvl_pick_time = time_ms;
    lvl_pick_nodes = nodes;
    if (!complete || count <= 0) return false;
    lvl_pick_count = count < MAX_MULTIPV ? count : MAX_MULTIPV;
    lvl_pick_depth = depth;
    return lvl_level_go && depth >= lvl_pick_d;
}

static void lvl_report_level(Position* pos, int depth, Move first_best, int first_seldepth, int first_score,
                              const char* first_pv, long long elapsed, struct timeval start_time,
                              long long* nodes_total) {
    if (first_best.from == first_best.to) {
        print_uci_info(depth, first_seldepth, first_score, elapsed, *nodes_total, first_pv);
        lvl_hashfull();
        lvl_pick_at_depth(depth, 0, false, elapsed, *nodes_total);
        return;
    }
    int count = 1;
    bool complete = true;
    lvl_lines_tab[0].score    = first_score;
    lvl_lines_tab[0].seldepth = first_seldepth;
    snprintf(lvl_lines_tab[0].pv, sizeof(lvl_lines_tab[0].pv), "%s", first_pv ? first_pv : "");
    root_excluded_count = 0;
    root_excluded[root_excluded_count++] = first_best;
    for (int idx = 2; idx <= lvl_lines && idx <= LVL_MULTIPV_MAX; idx++) {
        if (stop_requested || position_changed || is_time_exceeded()) { complete = false; break; }
        SearchInfo mi = {0};
        mi.start_time = get_wall_time();
        mi.depth = depth;
        bool window_open = false;
        Move segment = lvl_aspiration(pos, depth, 0, false, &mi, &window_open);
        if (lvl_aborted || window_open || position_changed) { complete = false; break; }
        if (segment.from == segment.to) break;
        *nodes_total += mi.nodes;
        if (is_root_excluded(&segment) || !is_root_included(&segment)) break;
        lvl_lines_tab[count].score    = mi.score;
        lvl_lines_tab[count].seldepth = mi.seldepth;
        snprintf(lvl_lines_tab[count].pv, sizeof(lvl_lines_tab[count].pv), "%s", mi.pv);
        count++;
        if (root_excluded_count >= LVL_MULTIPV_MAX) break;
        root_excluded[root_excluded_count++] = segment;
    }
    for (int i = 2; i < count; i++) {
        LevelLine key = lvl_lines_tab[i];
        int j = i - 1;
        while (j >= 1 && lvl_lines_tab[j].score < key.score) {
            lvl_lines_tab[j + 1] = lvl_lines_tab[j];
            j--;
        }
        lvl_lines_tab[j + 1] = key;
    }
    long long elapsed_final = time_diff_ms(start_time, get_wall_time());
    if (elapsed_final < 1) elapsed_final = 1;
    if (lvl_multipv <= 1) {
        print_uci_info(depth, lvl_lines_tab[0].seldepth, lvl_lines_tab[0].score, elapsed_final, *nodes_total,
                       lvl_lines_tab[0].pv);
    } else {
        for (int i = 0; i < count && i < lvl_multipv; i++)
            print_uci_info_multipv(i + 1, depth, lvl_lines_tab[i].seldepth, lvl_lines_tab[i].score,
                                   elapsed_final, *nodes_total, lvl_lines_tab[i].pv);
    }
    lvl_hashfull();
    root_excluded_count = 0;
    if (complete)
        for (int i = 0; i < count; i++)
            lvl_pick_line(i, lvl_lines_tab[i].score, lvl_lines_tab[i].seldepth, lvl_lines_tab[i].pv);
    if (lvl_pick_at_depth(depth, count, complete, elapsed_final, *nodes_total) &&
        !stop_requested && !position_changed && !quit_requested) {
        lvl_level_done = true;
        stop_requested = true;
    }
}

static void lvl_report(Position* pos, int depth, Move first_best, int first_seldepth, int first_score,
                      const char* first_pv, long long elapsed, struct timeval start_time, long long* nodes_total) {
    snprintf(lvl_completed_pv, sizeof(lvl_completed_pv), "%s", first_pv ? first_pv : "");
    if (lvl_lines > 0) {
        lvl_report_level(pos, depth, first_best, first_seldepth, first_score, first_pv, elapsed, start_time, nodes_total);
        return;
    }
    if (lvl_multipv <= 1 || first_best.from == first_best.to) {
        print_uci_info(depth, first_seldepth, first_score, elapsed, *nodes_total, first_pv);
        lvl_hashfull();
        return;
    }
    int count = 0;
    lvl_lines_tab[0].score    = first_score;
    lvl_lines_tab[0].seldepth = first_seldepth;
    snprintf(lvl_lines_tab[0].pv, sizeof(lvl_lines_tab[0].pv), "%s", first_pv ? first_pv : "");
    count = 1;
    root_excluded_count = 0;
    root_excluded[root_excluded_count++] = first_best;
    for (int idx = 2; idx <= lvl_multipv && idx <= LVL_MULTIPV_MAX; idx++) {
        if (stop_requested || position_changed || is_time_exceeded()) break;
        SearchInfo mi = {0};
        mi.start_time = get_wall_time();
        mi.depth = depth;
        bool window_open = false;
        Move segment = lvl_aspiration(pos, depth, 0, false, &mi, &window_open);
        if (lvl_aborted || window_open) break;
        if (position_changed) break;
        if (segment.from == segment.to) break;
        *nodes_total += mi.nodes;
        if (is_root_excluded(&segment) || !is_root_included(&segment)) break;
        lvl_lines_tab[count].score    = mi.score;
        lvl_lines_tab[count].seldepth = mi.seldepth;
        snprintf(lvl_lines_tab[count].pv, sizeof(lvl_lines_tab[count].pv), "%s", mi.pv);
        count++;
        if (root_excluded_count >= LVL_MULTIPV_MAX) break;
        root_excluded[root_excluded_count++] = segment;
    }
    for (int i = 2; i < count; i++) {
        LevelLine key = lvl_lines_tab[i];
        int j = i - 1;
        while (j >= 1 && lvl_lines_tab[j].score < key.score) {
            lvl_lines_tab[j + 1] = lvl_lines_tab[j];
            j--;
        }
        lvl_lines_tab[j + 1] = key;
    }
    long long elapsed_final = time_diff_ms(start_time, get_wall_time());
    if (elapsed_final < 1) elapsed_final = 1;
    for (int i = 0; i < count; i++)
        print_uci_info_multipv(i + 1, depth, lvl_lines_tab[i].seldepth, lvl_lines_tab[i].score,
                               elapsed_final, *nodes_total, lvl_lines_tab[i].pv);
    lvl_hashfull();
    root_excluded_count = 0;
}

static int lvl_tm_flexible = 0;
static int lvl_tm_hard_ms  = 0;

static int lvl_tm_set_hard(int go_budget_ms, int my_time) {
    long long hard_ms = (long long)go_budget_ms * 5 / 2;
    long long cap_ms  = LVL_TM_HARD_CAP(my_time);
    if (hard_ms > cap_ms)  hard_ms = cap_ms;
    if (hard_ms < go_budget_ms) hard_ms = go_budget_ms;
    lvl_tm_hard_ms = (int)hard_ms;
    return go_budget_ms;
}

static int lvl_move_time(int wtime, int btime, int winc, int binc, int movestogo, Color to_move, Position* pos,
                         int overhead_ms) {
    int my_time = (to_move == WHITE) ? wtime : btime;
    int my_increment = (to_move == WHITE) ? winc : binc;

    if (my_time <= 0) return lvl_tm_set_hard(100, my_time);

    if (overhead_ms > 0) {
        my_time -= overhead_ms;
        if (my_time < 1) my_time = 1;
    }

    if (movestogo > 0) {
        const int MOVE_OVERHEAD = 30;
        int safe = my_time - MOVE_OVERHEAD;
        if (safe < 1) return lvl_tm_set_hard(1, my_time);
        int n = movestogo > 100 ? 100 : movestogo;
        long long alloc = (long long)safe / n;
        if (my_increment > 0) alloc += ((long long)my_increment * 7) / 10;
        long long cap = ((long long)safe * 4) / 5;
        if (alloc > cap) alloc = cap;
        if (alloc < 1) alloc = 1;
        return lvl_tm_set_hard((int)alloc, my_time);
    }

    if (my_increment <= 0) {
        const int MOVE_OVERHEAD = 30;
        const int MOVE_HORIZON  = 25;
        int safe = my_time - MOVE_OVERHEAD;
        if (safe < 1) return lvl_tm_set_hard(1, my_time);
        int alloc = safe / MOVE_HORIZON;
        return lvl_tm_set_hard(alloc < 1 ? 1 : alloc, my_time);
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

    if (!console_takes_search_output())
        printf("info string TIME_MGMT: calculated=%dms phase=%s algorithm=%s pieces=%d move_est=%d total_time=%.1fs\n",
               final_time, phase_name, algorithm_type, total_pieces, move_number, total_time_left);

    {
        long long hard_ms = (long long)final_time * 5 / 2;
        long long cap_ms  = LVL_TM_HARD_CAP(my_time);
        if (hard_ms > cap_ms)      hard_ms = cap_ms;
        if (hard_ms < final_time) hard_ms = final_time;
        lvl_tm_hard_ms = (int)hard_ms;
    }

    return final_time;
}

static Move lvl_search_timed(Position* pos, int time_limit_ms, SearchInfo* final_info, LevelResult* w) {
    const long long tm_soft = (lvl_tm_flexible && time_limit_ms > 0) ? time_limit_ms : 0;
    const int  tm_hard = tm_soft ? (lvl_tm_hard_ms > 0 ? lvl_tm_hard_ms
                                                         : (int)(tm_soft * 5 / 2))
                                   : time_limit_ms;
    bool tm_move_changed = false;
    static const double cs_stability_scale[5] = {2.50, 1.20, 0.90, 0.80, 0.75};
    int  cs_stab = 0;
    long long cs_optimal = tm_hard;
    bool cs_only_move = false;
    if (tm_soft) {
        Move only_move_buf[256];
        cs_only_move = (lvl_generate_legal_moves(pos, only_move_buf) == 1);
    }
    set_search_time_limit(tm_hard);
    start_search_timer();
    new_search_reset();
    struct timeval start_time = get_wall_time();
    w->start = start_time;
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
                if ((double)elapsed_before * LVL_BEST_MOVE_RATIO > (double)tm_hard) break;
            } else
            if (elapsed_before > time_limit_ms * 0.9) {
                break;
            }
        }
        SearchInfo info = {0};
        info.start_time = start_time;
        info.nodes = 0;
        info.depth = depth;
        bool use_aspiration = (depth >= LVL_MIN_ASPIRATION_DEPTH && valid_move_found);
        bool window_open = false;
        lvl_uci_nodes_before = nodes_total;
        Move current_best = lvl_aspiration(pos, depth, previous_score, use_aspiration, &info, &window_open);
        nodes_total += info.nodes;
        bool iteration_interrupted = lvl_aborted;
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
                    && abs(info.score) < LVL_EFFORT_SCORE_BAND && lvl_effort_total > 0) {
                const long long effort_share_pct = 100 * lvl_effort_best / lvl_effort_total;
                if (effort_share_pct > 95) cs_optimal = cs_optimal * (100 - 60 * (effort_share_pct - 95) / 5) / 100;
            }
        }
        best_move = current_best;
        valid_move_found = true;
        previous_score = info.score;
        *final_info = info;
        final_info->nodes = nodes_total;
        w->depth_arg = depth;
        w->eval_value = info.score;
        long long elapsed = time_diff_ms(start_time, get_wall_time());
        lvl_report(pos, depth, current_best, info.seldepth, info.score, info.pv, elapsed, start_time, &nodes_total);
        final_info->nodes = nodes_total;
        if (stop_requested || is_time_exceeded()) {
            break;
        }
        if (depth >= LVL_DECISIVE_STOP_MIN_DEPTH && (info.score > 800 || info.score < -800)) {
            if (decisive_have_iter && decisive_last_depth == depth - 1
                                      && decisive_last_score == info.score) {
                break;
            }
        }
        decisive_last_score = info.score;
        decisive_last_depth = depth;
        decisive_have_iter  = true;
    }
    w->go_nodes = nodes_total;
    return valid_move_found ? best_move : (Move){0};
}

static void lvl_tt_take_over(void) {
    if (lvl_tt.entries != main_tt.entries || lvl_tt.size != main_tt.size || lvl_tt.mask != main_tt.mask) {
        lvl_tt.entries = main_tt.entries;
        lvl_tt.size    = main_tt.size;
        lvl_tt.mask    = main_tt.mask;
        lvl_own_buffer  = false;
    }
    if (!lvl_own_buffer) {
        lvl_clear_tt(&lvl_tt);
        lvl_own_buffer = true;
    }
    lvl_tt.age++;
}

void lvl_main_buffer(void) {
    if (!lvl_own_buffer) return;
    clear_tt(&main_tt);
    lvl_own_buffer = false;
}

void lvl_new_game(void) {
    lvl_search_new_game();
    lvl_own_buffer = false;
    lvl_tt.age = 0;
}

bool lvl_active(const LevelStrength* strength_cfg, bool use_nnue) {
    lvl_level_go = false;
    lvl_l = (double)LVL_SKILL_MAX;
    if (strength_cfg) {
        const double l = lvl_level(strength_cfg);
        if (l < LVL_SKILL_MAX) {
            lvl_level_go = true;
            lvl_l = l;
        }
    }
    return lvl_level_go || !use_nnue;
}

void lvl_go(Position* pos, const LevelGo* g, LevelResult* w) {
    memset(w, 0, sizeof(*w));
    if (!lvl_ready) {
        lvl_endgame_init();
        lvl_generator_init();
        lvl_ready = true;
    }
    lvl_tt_take_over();
    lvl_node_count = 0;
    lvl_aborted = false;
    lvl_node_limit = g->nodes > 0 ? g->nodes : 0;
    lvl_limit_hit = false;
    lvl_multipv = g->multipv;
    lvl_completed_pv[0] = '\0';
    lvl_level_done = false;
    lvl_pick_count = lvl_pick_depth = 0;
    lvl_pick_time = lvl_pick_nodes = 0;
    lvl_lines = 0;
    if (lvl_level_go) {
        const int n = (int)lvl_l;
        const double f = lvl_l - (double)n;
        lvl_pick_d = 1 + n + ((f > 0.0 && lvl_random() < f) ? 1 : 0);
        lvl_lines = g->multipv > LVL_LINES ? g->multipv : LVL_LINES;
    }

    if (g->infinite || g->ponder) {

        set_search_time_limit(0);
        start_search_timer();
        new_search_reset();
        set_search_unbounded(true);
        if (g->ponder) {
            long long go_budget_ms = 0;
            if (g->has_movetime) {
                go_budget_ms = g->movetime;
            } else if (g->has_clock) {
                go_budget_ms = lvl_move_time(g->wtime, g->btime, g->winc, g->binc, g->movestogo, pos->to_move, pos,
                                       g->overhead_ms);
            }
            set_ponder_mode(true, go_budget_ms);
            if (!console_takes_search_output())
                printf("info string ponder: searching on the opponent's time; "
                       "budget after `ponderhit` = %lld ms\n", go_budget_ms);
            fflush(stdout);
        }
        SearchInfo info = {0};
        struct timeval search_start = get_wall_time();
        info.start_time = search_start;
        info.nodes = 0;
        w->start = search_start;
        Move best_move = {0};
        bool valid_move_found = false;
        int previous_score = 0;
        for (int depth = 1; depth <= MAX_DEPTH; depth++) {
            if (stop_requested || position_changed) break;
            if (g->stop_pending && depth > 1) {
                stop_requested = true;
                break;
            }
            info.depth = depth;
            bool use_aspiration = (depth >= LVL_MIN_ASPIRATION_DEPTH && valid_move_found);
            bool window_open = false;
            lvl_uci_nodes_before = 0;
            Move current_best = lvl_aspiration(pos, depth, previous_score, use_aspiration, &info, &window_open);
            if (!stop_requested && !position_changed && !window_open) {
                best_move = current_best;
                valid_move_found = true;
                previous_score = info.score;
                w->depth_arg = depth;
                w->eval_value = info.score;
                long long elapsed = time_diff_ms(search_start, get_wall_time());
                if (elapsed < 1) elapsed = 1;
                lvl_report(pos, depth, current_best, info.seldepth, info.score, info.pv, elapsed, search_start,
                          &info.nodes);
            }
        }
        w->this_move = valid_move_found ? best_move : (Move){0};
        w->go_nodes = info.nodes;
        w->must_wait = true;
        w->level_done = lvl_level_done;
    } else if (g->has_movetime) {

        if (!console_takes_search_output())
            printf("info string TIME_DEBUG: movetime=%dms\n", g->movetime);
        SearchInfo info = {0};
        lvl_tm_flexible = 0;
        w->this_move = lvl_search_timed(pos, g->movetime, &info, w);
    } else if (g->has_clock) {

        int search_time = lvl_move_time(g->wtime, g->btime, g->winc, g->binc, g->movestogo, pos->to_move, pos,
                                        g->overhead_ms);
        SearchInfo info = {0};
        lvl_tm_flexible = 1;
        w->this_move = lvl_search_timed(pos, search_time, &info, w);
    } else {

        set_search_time_limit(0);
        start_search_timer();
        new_search_reset();
        int depth_target = g->depth;
        const bool has_depth = g->has_depth;
        const int  mate_goal = g->mate;
        const long long nodes_target = g->nodes;
        bool mate_found = false;
        if (!has_depth && mate_goal > 0) {
            int target_depth = 2 * mate_goal - 1;
            if (target_depth > MAX_DEPTH) target_depth = MAX_DEPTH;
            if (target_depth < 1) target_depth = 1;
            depth_target = target_depth;
            if (!console_takes_search_output())
                printf("info string go mate %d: searching to depth %d plies\n", mate_goal, depth_target);
            fflush(stdout);
        } else if (!has_depth && nodes_target > 0) {
            depth_target = MAX_DEPTH;
            if (!console_takes_search_output())
                printf("info string go nodes %lld: deepening to %d, the node budget is the limit\n",
                       nodes_target, MAX_DEPTH);
            fflush(stdout);
        }
        struct timeval search_start = get_wall_time();
        w->start = search_start;
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
            bool use_aspiration = (depth >= LVL_MIN_ASPIRATION_DEPTH && valid_move_found);
            bool window_open = false;
            lvl_uci_nodes_before = nodes_total;
            Move current_best = lvl_aspiration(pos, depth, previous_score, use_aspiration, &info, &window_open);
            nodes_total += info.nodes;
            w->node_count += info.nodes;
            bool iteration_interrupted = lvl_aborted;
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
            lvl_report(pos, depth, current_best, info.seldepth, info.score, info.pv, elapsed_iter, search_start,
                      &nodes_total);
            if (mate_goal > 0 && info.score > 0) {
                int mate_plies = MATE_SCORE - info.score;
                if (mate_plies >= 0 && mate_plies <= 2 * mate_goal - 1) {
                    if (!console_takes_search_output())
                        printf("info string go mate %d: mate found, %d moves, depth %d\n", mate_goal,
                               (mate_plies + 1) / 2, depth);
                    fflush(stdout);
                    mate_found = true;
                    break;
                }
            }
            if (stop_requested) break;
        }
        long long elapsed = time_diff_ms(search_start, get_wall_time());
        if (elapsed < 1) elapsed = 1;
        if (nodes_target > 0 && lvl_limit_hit) {
            if (!console_takes_search_output())
                printf("info string go nodes: budget %lld, searched %lld, deepest completed depth %d\n",
                       nodes_target, lvl_node_count, last_completed_depth);
            fflush(stdout);
        }
        if (mate_goal > 0 && !mate_found) {
            if (!console_takes_search_output())
                printf("info string go mate %d: no mate found up to depth %d; "
                       "this is NOT a proof that no mate exists (the search prunes)\n", mate_goal, depth_target);
            fflush(stdout);
        }
        bool search_aborted = stop_requested || position_changed || quit_requested;
        if (search_aborted && !console_takes_search_output()) {
            if (last_completed_depth == 0) {
                printf("info string search aborted at depth %d after %lld ms; "
                       "no depth completed, result not reported\n", depth_target, elapsed);
            } else {
                printf("info string search aborted after %lld ms; "
                       "deepest completed depth %d of %d requested\n", elapsed, last_completed_depth, depth_target);
            }
            fflush(stdout);
        }
        w->this_move = best_move;
        w->depth_arg = last_completed_depth;
        w->eval_value = previous_score;
        w->go_nodes = nodes_total;
        w->depth_mode = true;
    }
    snprintf(w->pv, sizeof(w->pv), "%s", lvl_completed_pv);
}

static bool lvl_legal_move(Position* pos, const char* uci_move_str, Move* out) {
    Move legal[MAX_MOVES];
    int n = lvl_generate_legal_moves(pos, legal);
    char buf[16];
    for (int i = 0; i < n; i++) {
        move_to_string_buf(&legal[i], buf);
        if (strcmp(buf, uci_move_str) == 0) {
            *out = legal[i];
            return true;
        }
    }
    return false;
}

void lvl_pick_move(Position* pos, LevelResult* w) {
    const bool go = lvl_level_go;
    lvl_level_go = false;
    lvl_lines = 0;
    lvl_level_done = false;
    if (!go || lvl_pick_count <= 0) return;
    double t = (LVL_TOL_0 - LVL_TOL_STEP * lvl_l) / 128.0;
    if (t < 0.0) t = 0.0;
    int top = lvl_pick_score[0], low = lvl_pick_score[0];
    for (int i = 1; i < lvl_pick_count; i++) {
        if (lvl_pick_score[i] > top) top = lvl_pick_score[i];
        if (lvl_pick_score[i] < low) low = lvl_pick_score[i];
    }
    double spread = (double)(top - low);
    if (spread > LVL_NOISE_CP) spread = LVL_NOISE_CP;
    int best = 0;
    double best_value = 0.0;
    for (int i = 0; i < lvl_pick_count; i++) {
        const double v = lvl_pick_score[i] + t * (double)(top - lvl_pick_score[i]) + t * spread * lvl_random();
        if (i == 0 || v > best_value) {
            best_value = v;
            best = i;
        }
    }
    const char* pv = lvl_pick_pv[best];
    char first[16];
    size_t k = 0;
    while (pv[k] && pv[k] != ' ' && k + 1 < sizeof(first)) { first[k] = pv[k]; k++; }
    first[k] = '\0';
    Move m;
    if (k == 0 || !lvl_legal_move(pos, first, &m)) return;
    snprintf(w->pv, sizeof(w->pv), "%s", pv);
    print_uci_info_multipv(lvl_multipv > 1 ? 1 : 0, lvl_pick_depth, lvl_pick_seldepth[best], lvl_pick_score[best], lvl_pick_time,
                           lvl_pick_nodes, pv);
    w->this_move = m;
}

#define LVL_BENCH_DEPTH       15
#define LVL_BENCH_POSITIONS  10

void lvl_bench(const char* const* fen, int n) {
    if (n > LVL_BENCH_POSITIONS) n = LVL_BENCH_POSITIONS;
    static const int skill[9] = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    const int n_moves = 20 + 9;
    long long combined = 0;
    uint64_t digest_all = 1469598103934665603ULL;
    for (int p = 0; p < n_moves; p++) {
        LevelStrength s = { LVL_SKILL_TOP, false, LVL_ELO_MIN, false, 0.0 };
        if (p < 20) { s.env = true; s.env_level = (double)p; }
        else s.skill = skill[p - 20];
        lvl_seed(798 + (uint64_t)p);
        clear_tt(&main_tt);
        lvl_new_game();
        long long node_count = 0;
        uint64_t digest = 1469598103934665603ULL;
        char move_list[n * 8 + 8];
        move_list[0] = '\0';
        for (int i = 0; i < n; i++) {
            Position pos;
            if (!parse_fen(&pos, fen[i])) continue;
            reset_search_state();
            root_excluded_count = 0;
            lvl_active(&s, true);
            const double l = lvl_l;
            LevelGo g = {0};
            g.has_depth = true;
            g.depth = LVL_BENCH_DEPTH;
            g.multipv = 1;
            LevelResult w;

            lvl_go(&pos, &g, &w);
            lvl_pick_move(&pos, &w);
            node_count += lvl_node_count;
            char buf[16] = "0000";
            if (w.this_move.from != w.this_move.to) move_to_string_buf(&w.this_move, buf);
            for (const char* c = buf; *c; c++) { digest = (digest ^ (uint8_t)*c) * 1099511628211ULL; }
            digest = (digest ^ ' ') * 1099511628211ULL;
            strcat(move_list, buf);
            strcat(move_list, " ");
            (void)l;
        }
        combined += node_count;
        digest_all = (digest_all ^ digest) * 1099511628211ULL;
        if (p < 20) fprintf(stderr, "level l=%-2d        nodes %10lld  moves %016llx  %s\n", p, node_count,
                            (unsigned long long)digest, move_list);
        else fprintf(stderr, "Skill %d (l=%5.2f) nodes %10lld  moves %016llx  %s\n", s.skill, lvl_level(&s), node_count,
                     (unsigned long long)digest, move_list);
    }
    fprintf(stderr, "bench levels: %d levels x %d positions, go depth %d, Hash %lld MB, total nodes %lld, digest %016llx\n",
            n_moves, n, LVL_BENCH_DEPTH, (long long)main_tt.size * (long long)sizeof(HashEntry) / (1024 * 1024), combined,
            (unsigned long long)digest_all);
    clear_tt(&main_tt);
    lvl_new_game();
    lvl_rng = 0;
}
