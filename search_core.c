/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT
 * Copyright (C) Dariusz Domagała
 * Copyright (C) Oliver Brausch (OliThink 5.10.5, MIT, see LICENSES/OLITHINK-MIT.txt)
 * Copyright (C) 2019-2025 Morgan Houppin (Stash 37)
 * Portions of this file are adapted from OliThink 5.10.5 and Stash 37. Modified by Dariusz Domagała, 2026-10-06.
 */
#include "search.h"
#include "board.h"
#include "movegen.h"
#include "eval.h"
#include "utils.h"
#include "tt.h"
#include "pawn_structure.h"
#include <string.h>
#include <stdbool.h>
#include "tune.h"

#define MAX_SEARCH_PLY (MAX_DEPTH + EXT_MAX + MAX_QUIESCENCE_DEPTH)
typedef char history_capacity_check[(HISTORY_GAME_LIMIT + 1 + MAX_SEARCH_PLY + 1 <= MAX_POSITION_HISTORY) ? 1 : -1];

#define QS_DELTA_BETA 125
#define QS_DELTA_ALPHA 85

enum { STAGE_HASH, STAGE_CAPTURES, STAGE_QUIETS, STAGE_END };
enum { NO_MOVE_YET, SOME_MOVE_SEARCHED, ALPHA_RAISED };

static const int piece_value_cp[7] = { 100, 500, 290, 310, 980, 0, 0 };
static const int piece_value_units[7] = {   0,   5,   2,   3,   9, 0, 0 };

#  define HIST_BONUS(d) (HB_A * (d) * (d) - HB_B * (d) - HB_C < 0 ? 0                               \
                      : (HB_A * (d) * (d) - HB_B * (d) - HB_C < 2461 ? HB_A * (d) * (d) - HB_B * (d) - HB_C : 2461) / 32)

#define FIFTY_MOVE_SCALE(p, e) (e)

#define DOUBLE_EXT_MARGIN 16
#define TRIPLE_EXT_MARGIN 96
#define DOUBLE_EXT_LIMIT 8
#  define QUIET_FLOOR_MARK(q) ((void)0)
#define PROBCUT_MARGIN 152
#define PROBCUT_D 6

static int  quiet_history[65536];
static Move killer_table[MAX_SEARCH_PLY];

#define CAPTURE_PICK_ARG
#define CAPTURE_PICK_PASS
static Move pv_triangle[MAX_SEARCH_PLY + 1][MAX_SEARCH_PLY + 1];
static int  eval_stack_ply[MAX_SEARCH_PLY + 1];
static int  null_at_ply[MAX_SEARCH_PLY + 2];

static int  cont_hist[2][2][6][64][6][64];
static Move move_stack[MAX_SEARCH_PLY + 2];
static int  double_ext_total[MAX_SEARCH_PLY + 2];

static long long* node_counter_ptr    = NULL;
static bool*      root_interrupted_ptr = NULL;

static Move empty_move(void) {
    Move m = {0};
    return m;
}
static bool is_no_move(Move m) {
    return m.from == m.to;
}

static bool same_move(Move a, Move b) {
    return a.from == b.from && a.to == b.to && a.promotion == b.promotion;
}

#define IS_KILLER_OR_COUNTER(m, ply) (same_move((m), killer_table[(ply)]))

static Move counter_table[2][6][64];
static Move counter_at_ply[MAX_SEARCH_PLY + 2];
static Move* counter_slot(int ply, Color us) {
    Move p;
    if (ply < 1 || null_at_ply[ply - 1]) return NULL;
    p = move_stack[ply - 1];
    if ((int)p.piece >= 6) return NULL;
    return &counter_table[us][p.piece][p.to];
}
#  undef IS_KILLER_OR_COUNTER
#    define IS_KILLER_OR_COUNTER(m, ply) (same_move((m), killer_table[(ply)]) || same_move((m), counter_at_ply[(ply)]))

static uint64_t       cuckoo_key[0x2000];
static unsigned short cuckoo_move[0x2000];
static int            cuckoo_ready = 0;

static void cuckoo_init(void) {
    static const PieceType fig[5] = { KNIGHT, BISHOP, ROOK, QUEEN, KING };
    int c, f, a, b, step;
    for (c = 0; c < 2; c++) for (f = 0; f < 5; f++) for (a = 0; a < 64; a++) for (b = a + 1; b < 64; b++) {
        int dx = (b & 7) - (a & 7), dy = (b >> 3) - (a >> 3);
        int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy, ok;
        uint64_t       k;
        unsigned short r;
        unsigned       ix;
        switch (fig[f]) {
            case KNIGHT: ok = (ax == 1 && ay == 2) || (ax == 2 && ay == 1); break;
            case BISHOP: ok = ax == ay;                                     break;
            case ROOK:   ok = ax == 0 || ay == 0;                           break;
            case QUEEN:  ok = ax == ay || ax == 0 || ay == 0;               break;
            default:     ok = ax <= 1 && ay <= 1;                           break;
        }
        if (!ok) continue;
        k  = zobrist_pieces[c][fig[f]][a] ^ zobrist_pieces[c][fig[f]][b] ^ zobrist_side_to_move;
        r  = (unsigned short)(a | b << 6);
        ix = (unsigned)(k & 0x1FFF);
        for (step = 0; r && step < 0x4000; step++) {
            uint64_t       tk = cuckoo_key[ix];
            unsigned short tr = cuckoo_move[ix];
            cuckoo_key[ix] = k; cuckoo_move[ix] = r;
            k = tk; r = tr;
            ix ^= (unsigned)(k & 0x1FFF) ^ (unsigned)((k >> 13) & 0x1FFF);
        }
    }
    cuckoo_ready = 1;
}

static int has_upcoming_cycle(Position* pos, int ply) {
    int lim = pos->halfmove_clock, h = pos->history_count - pos->history_base, k, i;
    if (h < lim) lim = h;
    for (k = 1; k <= ply && k <= lim; k++) if (null_at_ply[ply - k]) { lim = k - 1; break; }
    if (lim < 3) return 0;
    if (!cuckoo_ready) cuckoo_init();
    for (i = 3; i <= lim; i += 2) {
        uint64_t mk = pos->hash ^ pos->position_history[pos->history_count - i];
        unsigned ix = (unsigned)(mk & 0x1FFF);
        int      a, b, dx, dy;
        if (cuckoo_key[ix] != mk) {
            ix = (unsigned)((mk >> 13) & 0x1FFF);
            if (cuckoo_key[ix] != mk) continue;
        }
        a = cuckoo_move[ix] & 63; b = cuckoo_move[ix] >> 6;
        dx = (b & 7) - (a & 7); dy = (b >> 3) - (a >> 3);
        if (dx == 0 || dy == 0 || dx == dy || dx == -dy) {
            int st = ((dy > 0) - (dy < 0)) * 8 + ((dx > 0) - (dx < 0)), s;
            for (s = a + st; s != b; s += st) if ((pos->all_pieces >> s) & 1) break;
            if (s != b) continue;
        }
        if (ply > i) return 1;
        if ((pos->occupied[pos->to_move] >> (((pos->all_pieces >> a) & 1) ? a : b)) & 1) return 1;
    }
    return 0;
}

static int history_index(Color c, Move m) {
    return ((int)c << 15) | ((int)m.piece << 12) | (m.from << 6) | m.to;
}

static int* cont_hist_row(int ply, int k, Color us) {
    Move p;
    if (ply < k || null_at_ply[ply - k]) return NULL;
    p = move_stack[ply - k];
    if ((int)p.piece >= 6) return NULL;
    return &cont_hist[us][k & 1][p.piece][p.to][0][0];
}

static int cont_hist_sum(int ply, Color us, Move m, int z4) {
    int  s = 0, ix = (int)m.piece * 64 + m.to;
    int* w;
    if ((w = cont_hist_row(ply, 1, us))) s += w[ix];
    if ((w = cont_hist_row(ply, 2, us))) s += w[ix];
    if (z4 && (w = cont_hist_row(ply, 4, us))) s += w[ix];
    return s;
}

static void cont_hist_update(int ply, Color us, Move m, int b) {
    static const int cont_plies[3] = { 1, 2, 4 };
    int  k, ix = (int)m.piece * 64 + m.to, ab = b < 0 ? -b : b;
    int* w;
    if ((int)m.piece >= 6) return;
    for (k = 0; k < 3; k++)
        if ((w = cont_hist_row(ply, cont_plies[k], us))) w[ix] += b - w[ix] * ab / 512;
}

static int nmp_verify_ply = 0;

static Bitboard pinned_for_stm(const Position* pos) {
    Color    us   = pos->to_move;
    Color    them = (Color)(1 - (int)pos->to_move);
    int      k    = get_lsb(pos->pieces[us][KING]);
    Bitboard occ, king_ray_att, xray, b, pin = 0ULL;

    if (k < 0) return 0ULL;
    occ = pos->all_pieces;

    king_ray_att = get_rook_attacks_public(k, occ);
    xray = get_rook_attacks_public(k, occ ^ (king_ray_att & occ)) & ~king_ray_att;
    b    = xray & (pos->pieces[them][QUEEN] | pos->pieces[them][ROOK]);
    while (b) {
        int t = get_lsb(b);
        pop_lsb(&b);
        pin |= king_ray_att & get_rook_attacks_public(t, occ) & pos->occupied[us];
    }

    king_ray_att = get_bishop_attacks_public(k, occ);
    xray = get_bishop_attacks_public(k, occ ^ (king_ray_att & occ)) & ~king_ray_att;
    b    = xray & (pos->pieces[them][QUEEN] | pos->pieces[them][BISHOP]);
    while (b) {
        int t = get_lsb(b);
        pop_lsb(&b);
        pin |= king_ray_att & get_bishop_attacks_public(t, occ) & pos->occupied[us];
    }
    return pin;
}

static Move pick_capture(Move* mlist, int n, int s CAPTURE_PICK_ARG) {
    int i, t, pi = s, vmax = -9999;
    Move m;
    for (i = s; i < n; i++) {
        m = mlist[i];
        t = piece_value_cp[m.captured] - piece_value_units[m.piece];
        if (t > vmax) { vmax = t; pi = i; }
    }
    m = mlist[pi];
    if (pi != s) mlist[pi] = mlist[s];
    return m;
}

static Move pick_quiet(Move* mlist, int n, int s, int ply, Color us) {
    int i, pi = s, vmax = -(1 << 15);
    Move m;
    int pi3 = -1;
    const int* c1 = cont_hist_row(ply, 1, us);
    const int* c2 = cont_hist_row(ply, 2, us);
    for (i = s; i < n; i++) {
        m = mlist[i];
        if (same_move(m, killer_table[ply])) {
            pi = i;
            pi3 = -1;
            break;
        }

        if (pi3 < 0 && same_move(m, counter_at_ply[ply])) { pi3 = i; continue; }
        {

            int v = quiet_history[history_index(us, m)] / 2, ix = (int)m.piece * 64 + m.to;
            if (c1) v += c1[ix];
            if (c2) v += c2[ix];
            if (vmax < v) { vmax = v; pi = i; }
        }
    }
    if (pi3 >= 0) pi = pi3;
    m = mlist[pi];
    if (pi != s) mlist[pi] = mlist[s];
    return m;
}

#  define LMP_BUDGET(d, improving_flag, ply) (2 * (d) * ((improving_flag) + 1))

#define CUTNODE_ARG      , int cut
#define CUTNODE_PASS(x)  , (x)
#include <math.h>

static int lmr_table[2][256];
static int lmr_ready = 0;
static void lmr_init(void) {
    int i;
    lmr_table[0][0] = lmr_table[1][0] = 0;
    for (i = 1; i < 256; i++) {
        lmr_table[0][i] = (int)(log((double)i) * (LMR_N_A / 100.0) + (LMR_N_B / 100.0));
        lmr_table[1][i] = (int)(log((double)i) * (LMR_Q_A / 100.0) + (LMR_Q_B / 100.0));
    }
    lmr_ready = 1;
}
#define CUTNODE_LMR (lmr ? 1 : !cut)

static int quiet_history_score(Color us, Move m, int ply) {
    return quiet_history[history_index(us, m)] / 2 + cont_hist_sum(ply, us, m, 0);
}
static int root_depth = 1;

static int search_node(Position* pos, bool ch, int d, int ply, int alpha, int beta,
                     int null_ok, const Move* excl, SearchInfo* info CUTNODE_ARG);

static inline void qs_tt_store(Position* pos, int score, HashFlag flag, int ply) {
    int d0, f0, s0;
    if (probe_hash_entry(&main_tt, pos->hash, &d0, &f0, &s0, ply) && d0 > 0) return;
    store_hash(&main_tt, pos->hash, 0, score, flag, empty_move(), ply);
}
static int qsearch(Position* pos, bool ch, int ply, int alpha, int beta, SearchInfo* info) {
    const int qs_alfa0 = alpha;
    int  i, n, best = -MATE_SCORE;
    Move mlist[MAX_MOVES];

    LegalityBuffer qs_legal_ctx; int qs_legal_ready = 0;

    if (info->seldepth < ply) info->seldepth = ply;
    if (fast_check_stop()) return alpha;
    if (stop_requested || position_changed) return alpha;

    if (ply >= MAX_SEARCH_PLY) return FIFTY_MOVE_SCALE(pos, evaluate_position(pos));

    TTProbe    qts;
    const bool q_tt_found = tt_probe_entry(&main_tt, pos->hash, ply, &qts);
    if (beta - alpha == 1) {
        if (q_tt_found && qts.result_bb) {
            const int qf = qts.flag, qs = qts.score;
            if ((qf == HASH_BETA  || qf == HASH_EXACT) && qs >= beta)  return beta;
            if ((qf == HASH_ALPHA || qf == HASH_EXACT) && qs <= alpha) return alpha;
        }
    }

    if (!ch) do {

        const int mb = pos->mat_balance;
        int mat_balance_stm = (pos->to_move == WHITE) ? mb : -mb;

        {
            const int b = scale_material_balance(pos, mb);
            mat_balance_stm = (pos->to_move == WHITE) ? b : -b;
        }

        const int special_endgame = is_endgame_special(pos);
        if (!special_endgame && mat_balance_stm - QS_DELTA_BETA >= beta) return beta;
        if (!special_endgame && mat_balance_stm + QS_DELTA_ALPHA <= alpha) break;

        if (q_tt_found && qts.eval != TT_EVAL_NONE) best = qts.eval;
        else {
            best = evaluate_position(pos);
            tt_store_eval(&main_tt, pos->hash, best);
        }
        if (best > alpha) {
            if (best >= beta) return beta;
            alpha = best;
        }
    } while (0);

    n = ch ? generate_legal_evasions(pos, mlist) : generate_noisy_moves(pos, mlist);
    if (ch && n == 0) return -MATE_SCORE + ply;

    for (i = 0; i < n; i++) {
        Move     m = pick_capture(mlist, n, i CAPTURE_PICK_PASS);
        UndoInfo u;
        int      w;

        if (!ch && m.promotion == EMPTY
                && piece_value_cp[m.piece] > piece_value_cp[m.captured]
                && static_exchange_evaluation(pos, &m) < 0) continue;

        if (!ch) {
            if (!qs_legal_ready) { legality_context_init_pub(pos, &qs_legal_ctx); qs_legal_ready = 1; }
            if (!is_legal_fast_pub(pos, &m, &qs_legal_ctx)) continue;
        }
        make_move_full(pos, &m, &u);
        TT_PF(pos->hash);
        info->nodes++;
        (*node_counter_ptr)++;

        w = -qsearch(pos, is_in_check(pos, pos->to_move), ply + 1, -beta, -alpha, info);

        undo_move_full(pos, &m, &u);

        if (stop_requested || position_changed) return alpha;

        if (w > best) {
            if ((best = w) > alpha) {
                if ((alpha = w) >= beta) {
                    qs_tt_store(pos, beta, HASH_BETA, ply);
                    return beta;
                }
            }
        }
    }
    if (best > -MATE_SCORE)
        qs_tt_store(pos, best, best > qs_alfa0 ? HASH_EXACT : HASH_ALPHA, ply);
    return best > -MATE_SCORE ? best : FIFTY_MOVE_SCALE(pos, evaluate_position(pos));
}

static int search_node(Position* pos, bool ch, int d, int ply, int alpha, int beta,
                     int null_ok, const Move* excl, SearchInfo* info CUTNODE_ARG) {
    int  i, j, n, nall, nnoisy, first, hist_max, n_quiets, improving_flag, eval_adj;
    int  mc = 0;
    int  tt_noisy = 0;
    int  pvnode = beta > alpha + 1;
    int  eval_raw = 0;
    int  skip_quiets = 0;

    int  w = -MATE_SCORE;
    Color us = pos->to_move;
    int  tt_d = 0, tt_flag = 0, tt_s = 0;
    bool tt_hit;
    bool    tt_found = false;
    TTProbe tt_so;
    Move tt_pv_move, mlist[MAX_MOVES], quiets[MAX_MOVES];
    HashFlag flag;

    if (ply) pv_triangle[ply][ply] = empty_move();

    info->nodes++;
    (*node_counter_ptr)++;
    if (info->seldepth < ply) info->seldepth = ply;
    if (fast_check_stop()) {
        if (ply == 0 && root_interrupted_ptr) *root_interrupted_ptr = true;
        return alpha;
    }
    if (stop_requested || position_changed) {
        if (ply == 0 && root_interrupted_ptr) *root_interrupted_ptr = true;
        return alpha;
    }

    if (ply && (count_position_occurrences(pos, pos->hash) >= 2
             || pos->halfmove_clock >= 100
             || has_insufficient_material(pos))) {
        return 1 - (int)(info->nodes & 2);
    }

    if (ply && alpha < 0 && pos->halfmove_clock >= 3 && has_upcoming_cycle(pos, ply)) {
        alpha = 0;
        if (alpha >= beta) return beta;
    }

    if (ply >= MAX_SEARCH_PLY) return FIFTY_MOVE_SCALE(pos, evaluate_position(pos));
    double_ext_total[ply + 1] = double_ext_total[ply];

    if (ch) d++;
    if (d <= 0) return qsearch(pos, ch, ply, alpha, beta, info);

    if (alpha < -MATE_SCORE + ply)     alpha = -MATE_SCORE + ply;
    if (beta  >  MATE_SCORE - ply - 1) beta  =  MATE_SCORE - ply - 1;
    if (alpha >= beta) return alpha;

    tt_pv_move = (ply == 0) ? pv_triangle[0][0] : empty_move();

    tt_found = tt_probe_entry(&main_tt, pos->hash, ply, &tt_so);
    tt_hit   = tt_so.result_bb;
    if (tt_hit) { tt_d = tt_so.depth; tt_flag = tt_so.flag; tt_s = tt_so.score; }

    if (tt_hit && !excl && ply > 0 && tt_d >= d) {
        if ((tt_flag == HASH_BETA  || tt_flag == HASH_EXACT) && tt_s >= beta) return beta;
        if ((tt_flag == HASH_ALPHA || tt_flag == HASH_EXACT) && tt_s <= alpha) return alpha;
    }
    if (!excl && is_no_move(tt_pv_move)) {

        if (tt_found && tt_so.mv) tt_pv_move = unpack_move(tt_so.mv);
    }

    if (ch) eval_adj = -MATE_SCORE + ply;
    else {
        if (tt_found && tt_so.eval != TT_EVAL_NONE) eval_adj = tt_so.eval;
        else {
            eval_adj = evaluate_position(pos);
            if (!excl) tt_store_eval(&main_tt, pos->hash, eval_adj);
        }
        eval_raw = eval_adj;
        if (tt_hit && (tt_flag == HASH_EXACT
                       || (tt_s > eval_adj ? tt_flag == HASH_BETA : tt_flag == HASH_ALPHA)))
            eval_adj = tt_s;
    }
    eval_stack_ply[ply] = eval_adj;

    if (!ch && !pvnode && beta > -MATE_SCORE + 500) {

        if (d == 1 && eval_raw + RAZOR_MARGIN_CORE <= alpha) return qsearch(pos, false, ply, alpha, beta, info);
        if (d <= RFP_D && eval_adj - RFP_MULT * d > beta) return eval_adj;
    }

    null_ok = null_ok && !ch && !pvnode && !excl && ply >= nmp_verify_ply && beta > -MATE_SCORE + 500
                      && d >= 3 && eval_adj >= beta && eval_adj >= eval_raw
                      && (ply < 2 || !null_at_ply[ply - 2]);
    if (null_ok && pop_count(pos->occupied[us]
                             & ~pos->pieces[us][PAWN]
                             & ~pinned_for_stm(pos)) > 1) {
        int      R          = (NMP_R_BASE + NMP_R_DEPTH * d) / 256
                            + ((eval_adj - beta) / NMP_EVAL_DIV < NMP_EVAL_MAX ? (eval_adj - beta) / NMP_EVAL_DIV : NMP_EVAL_MAX);
        Color    org_side   = pos->to_move;
        int      org_ep     = pos->en_passant_square;
        uint64_t org_hash   = pos->hash;

        pos->to_move = (Color)(1 - (int)pos->to_move);
        pos->en_passant_square = -1;
        pos->hash ^= zobrist_side_to_move;
        if (org_ep != -1) pos->hash ^= zobrist_en_passant[org_ep];
        TT_PF(pos->hash);

        null_at_ply[ply] = 1;
        w = -search_node(pos, false, d - R, ply + 1, -beta, 1 - beta, 0, NULL, info CUTNODE_PASS(!cut));
        null_at_ply[ply] = 0;

        pos->to_move = org_side;
        pos->en_passant_square = org_ep;
        pos->hash = org_hash;

        if (!(stop_requested || position_changed) && w >= beta) {
            if (nmp_verify_ply || (d <= 12 && beta < MATE_SCORE - 500)) return beta;
            nmp_verify_ply = ply + (d - R) * 3 / 4;
            w = search_node(pos, false, d - R, ply, beta - 1, beta, 0, NULL, info CUTNODE_PASS(0));
            nmp_verify_ply = 0;
            if (stop_requested || position_changed) return alpha;
            if (w >= beta) return beta;
        }
    }
    null_at_ply[ply] = 0;

    if (!ch && !pvnode && !excl && ply > 0 && d >= PROBCUT_D
            && beta > -MATE_SCORE + 500 && beta < MATE_SCORE - 500 - PROBCUT_MARGIN) {
        int pcb = beta + PROBCUT_MARGIN;
        if (!(tt_hit && tt_d >= d - 4 && tt_s < pcb)) {
            int pn = generate_legal_noisy(pos, mlist), pi;
            for (pi = 0; pi < pn; pi++) {
                Move     pm = pick_capture(mlist, pn, pi CAPTURE_PICK_PASS);
                UndoInfo pu;
                bool     pch;
                int      pw;
                if (static_exchange_evaluation(pos, &pm) < pcb - eval_adj) continue;
                move_stack[ply] = pm;
                make_move_full(pos, &pm, &pu);
                TT_PF(pos->hash);
                pch = is_in_check(pos, pos->to_move);
                pw  = -search_node(pos, pch, 0, ply + 1, -pcb, -pcb + 1, 1, NULL, info CUTNODE_PASS(!cut));
                if (pw >= pcb)
                    pw = -search_node(pos, pch, d - 4, ply + 1, -pcb, -pcb + 1, 1, NULL, info CUTNODE_PASS(!cut));
                undo_move_full(pos, &pm, &pu);
                if (stop_requested || position_changed) return alpha;
                if (pw >= pcb) {
                    store_hash(&main_tt, pos->hash, d - 3, pw, HASH_BETA, pm, ply);
                    return beta;
                }
            }
        }
    }

    if (d >= 3 && is_no_move(tt_pv_move)) d--;

    LegalityBuffer legal_buf;
    improving_flag = !ch && ply >= 2 && eval_adj >= eval_stack_ply[ply - 2];
    first   = NO_MOVE_YET;
    {
        Move* cw = counter_slot(ply, us);
        if (cw) counter_at_ply[ply] = *cw;
        else    memset(&counter_at_ply[ply], 0, sizeof(Move));
    }
    hist_max  = -1;
    n_quiets  = 0;

    if (ch) {
        nall   = generate_legal_evasions(pos, mlist);
        nnoisy = nall;
    } else {

        nall   = generate_moves(pos, mlist);
        legality_context_init_pub(pos, &legal_buf);
        nnoisy = 0;
        for (i = 0; i < nall; i++) {
            if (mlist[i].captured != EMPTY || mlist[i].promotion != EMPTY) {
                Move t = mlist[nnoisy];
                mlist[nnoisy] = mlist[i];
                mlist[i] = t;
                nnoisy++;
            }
        }
    }

    if (!is_no_move(tt_pv_move)) {
        Move matched = empty_move();
        for (i = 0; i < nall; i++) {
            if (same_move(mlist[i], tt_pv_move)) { matched = mlist[i]; break; }
        }
        tt_pv_move = matched;
    }
    tt_noisy = !is_no_move(tt_pv_move) && (tt_pv_move.captured != EMPTY || tt_pv_move.promotion != EMPTY);

    for (n = STAGE_HASH; n <= (ch ? STAGE_CAPTURES : STAGE_QUIETS); n++) {
        int nd = d - 1;
        int lo, hi;
        int se = 0, sing = 0;

        if (n == STAGE_HASH) {
            if (is_no_move(tt_pv_move)) continue;
            lo = 0; hi = 1;

            if (ply && !excl && ply < 2 * root_depth && d >= SING_D
                    && tt_hit && (tt_flag == HASH_BETA || tt_flag == HASH_EXACT)
                    && tt_s > -MATE_SCORE + 500 && tt_s < MATE_SCORE - 500 && tt_d >= d - 3) {
                int sb = tt_s - SING_MARGIN * d / 16;
                sing = 1;
                if (search_node(pos, ch, d / 2 + 1, ply, sb - 1, sb, 0, &tt_pv_move, info CUTNODE_PASS(cut)) < sb) {
                    se = 1;

                    if (!pvnode && double_ext_total[ply] < DOUBLE_EXT_LIMIT && !(stop_requested || position_changed)) {
                        int sb2 = sb - DOUBLE_EXT_MARGIN;
                        if (search_node(pos, ch, d / 2 + 1, ply, sb2 - 1, sb2, 0, &tt_pv_move, info CUTNODE_PASS(cut)) < sb2) {
                            int sb3 = sb - TRIPLE_EXT_MARGIN;
                            se = 2;
                            if (tt_pv_move.captured == EMPTY && tt_pv_move.promotion == EMPTY
                                    && !(stop_requested || position_changed)
                                    && search_node(pos, ch, d / 2 + 1, ply, sb3 - 1, sb3, 0, &tt_pv_move, info CUTNODE_PASS(cut)) < sb3)
                                se = 3;
                        }
                    }
                } else if (sb >= beta) {
                    if (stop_requested || position_changed) return alpha;
                    return beta;
                } else if (tt_s >= beta)
                    se = -1;
                nd += se;
            }
        } else if (n == STAGE_CAPTURES) {
            lo = 0;      hi = nnoisy;
        } else {
            lo = nnoisy; hi = nall;
        }

        for (i = lo; i < hi; i++) {
            Move     m;
            UndoInfo u;
            int      quiet, ext = 0, see = 0, is_first_pv;
            int      escapes = 0;
            int      lmr;
            bool     gives_check_now;

            if      (n == STAGE_HASH)  m = tt_pv_move;
            else if (n == STAGE_CAPTURES) m = pick_capture(mlist, nnoisy, i CAPTURE_PICK_PASS);
            else                    m = pick_quiet(mlist, nall, i, ply, us);

            if ((n != STAGE_HASH && same_move(m, tt_pv_move)) || (excl && same_move(m, *excl))) continue;

            if (!ch && !is_legal_fast_pub(pos, &m, &legal_buf)) continue;

            quiet = (m.captured == EMPTY && m.promotion == EMPTY);

            if (ply == 0 && (is_root_excluded(&m) || !is_root_included(&m))) continue;
            mc++;

            if (skip_quiets && quiet) { QUIET_FLOOR_MARK(1); n = STAGE_END; break; }
            if (ply > 0 && !ch && first != NO_MOVE_YET && alpha > -MATE_SCORE + 500 && d <= LMP_D
                    && mc >= (improving_flag ? (LMP_I_A + LMP_I_B * d * d) / 16 : (LMP_N_A + LMP_N_B * d * d) / 16)) skip_quiets = 1;

            if (ply > 0 && first != NO_MOVE_YET && alpha > -MATE_SCORE + 500 && quiet && d <= 4
                    && cont_hist_sum(ply, us, m, 1) < CHP_A - CHP_B * (d - 1)) { QUIET_FLOOR_MARK(1); continue; }

            if (n != STAGE_HASH) see = static_exchange_evaluation(pos, &m);

            if (n != STAGE_HASH && ply > 0 && first != NO_MOVE_YET && alpha > -MATE_SCORE + 500
                    && d <= 12 && see < (quiet ? -SEE_Q * d : -SEE_B * d * d)) { QUIET_FLOOR_MARK(quiet); continue; }

            if (quiet && d >= 3 && !m.is_castling) {
                Move rev_move = m;
                rev_move.from = m.to; rev_move.to = m.from;
                escapes = static_exchange_evaluation(pos, &rev_move) < 0;
            }
            move_stack[ply] = m;
            make_move_full(pos, &m, &u);
            TT_PF(pos->hash);
            if (quiet) quiets[n_quiets++] = m;
            gives_check_now = is_in_check(pos, pos->to_move);

            if (sing && gives_check_now) nd--;
            double_ext_total[ply + 1] = double_ext_total[ply] + (n == STAGE_HASH && se >= 2);

            lmr = 0;
            if (!(gives_check_now || pvnode || ch || (m.piece == PAWN && is_passed_pawn(pos, m.to, us)))
                    && n == STAGE_QUIETS && !IS_KILLER_OR_COUNTER(m, ply)) {
                int his = quiet_history_score(us, m, ply);
                if (his > hist_max) hist_max = his;
                else if (d < 6 && (his < 0 || his * his < hist_max)) {
                    undo_move_full(pos, &m, &u);
                    QUIET_FLOOR_MARK(1); continue;
                }
            }
            if (d >= 3 && mc > 1 + 3 * pvnode) {
                int q  = quiet ? 1 : 0;
                int dk = d < 255 ? d : 255, mk = mc < 255 ? mc : 255;
                int r  = (LMR_K0 + lmr_table[q][dk] * lmr_table[q][mk] + !improving_flag * LMR_NIMP) / 1024;

                int hk = quiet ? (quiet_history[history_index(us, m)] + cont_hist_sum(ply, us, m, 1)) / LMR_HDIV : 0;
                r += !pvnode;
                r += cut;
                r += tt_noisy;
                r -= IS_KILLER_OR_COUNTER(m, ply);
                r -= escapes;
                if (hk < -3) hk = -3; else if (hk > 3) hk = 3;
                r -= hk;
                if (r > nd - 1) r = nd - 1;
                if (r < 0) r = 0;
                ext -= r;
                lmr = 1;
            }

            is_first_pv = (first == NO_MOVE_YET && pvnode);
            if (!is_first_pv)
                w = -search_node(pos, gives_check_now, nd + ext, ply + 1, -alpha - 1, -alpha, 1, NULL, info CUTNODE_PASS(CUTNODE_LMR));
            if (w > alpha && ext < 0) {
                w = -search_node(pos, gives_check_now, nd,       ply + 1, -alpha - 1, -alpha, 1, NULL, info CUTNODE_PASS(!cut));

                {
                    int b = HIST_BONUS(d);
                    cont_hist_update(ply, us, m, w > alpha ? b : -b);
                }
            }
            if ((w > alpha && w < beta) || is_first_pv)
                w = -search_node(pos, gives_check_now, nd,       ply + 1, -beta,      -alpha, 0, NULL, info CUTNODE_PASS(0));

            undo_move_full(pos, &m, &u);

            if (stop_requested || position_changed) {
                if (ply == 0 && root_interrupted_ptr) *root_interrupted_ptr = true;
                return alpha;
            }

            if (w > alpha) {
                alpha = w;
                first = ALPHA_RAISED;
                pv_triangle[ply][ply] = m;
                for (j = ply + 1; j <= MAX_SEARCH_PLY; j++) {
                    pv_triangle[ply][j] = pv_triangle[ply + 1][j];
                    if (is_no_move(pv_triangle[ply][j])) break;
                }

                if (w >= beta) {
                    if (quiet) {
                        int his = HIST_BONUS(d);
                        int ix  = history_index(us, m);
                        killer_table[ply] = m;
                        {
                            Move* cw = counter_slot(ply, us);
                            if (cw) *cw = m;
                        }
                        quiet_history[ix] += his - quiet_history[ix] * his / 512;
                        for (j = 0; j < n_quiets - 1; j++) {
                            int ix2 = history_index(us, quiets[j]);
                            quiet_history[ix2] += -his - quiet_history[ix2] * his / 512;
                        }
                        {

                            int k;
                            cont_hist_update(ply, us, m, his);
                            for (k = 0; k < n_quiets - 1; k++) cont_hist_update(ply, us, quiets[k], -his);
                        }
                    }
                    n = STAGE_END;
                    break;
                }
            } else if (first == NO_MOVE_YET) first = SOME_MOVE_SEARCHED;
        }
    }

    if (first == NO_MOVE_YET) alpha = (ch || excl) ? -MATE_SCORE + ply : 0;

    flag = HASH_ALPHA;
    if (first == ALPHA_RAISED) {
        flag  = (alpha >= beta) ? HASH_BETA : HASH_EXACT;
        tt_pv_move = pv_triangle[ply][ply];
    }

    if (!excl && !(ply == 0 && root_excluded_count > 0))
        store_hash(&main_tt, pos->hash, d, alpha, flag, tt_pv_move, ply);

    return alpha;
}

void search_new_game(void) {
    memset(quiet_history, 0, sizeof(quiet_history));
    memset(cont_hist,      0, sizeof(cont_hist));
    memset(counter_table, 0, sizeof(counter_table));
}

Move search_root(Position* pos, int depth, int alpha, int beta, SearchInfo* info,
                    long long* nodes_searched_ptr, bool* root_interrupted) {
    int  i;
    bool ch;
    char pv_str[1024] = "";

    node_counter_ptr    = nodes_searched_ptr;
    root_interrupted_ptr = root_interrupted;
    *root_interrupted = false;

    if (depth == 1) {
        memset(killer_table,  0, sizeof(killer_table));
        pv_triangle[0][0] = empty_move();
    }

    if (!lmr_ready) lmr_init();
    root_depth = depth;
    nmp_verify_ply = 0;
    ch = is_in_check(pos, pos->to_move);
    info->score = search_node(pos, ch, depth, 0, alpha, beta, 0, NULL, info CUTNODE_PASS(0));

    for (i = 0; i <= MAX_SEARCH_PLY && i < 10; i++) {
        char move_str[8];
        if (is_no_move(pv_triangle[0][i])) break;
        move_to_string_buf(&pv_triangle[0][i], move_str);
        if (i > 0) strncat(pv_str, " ", sizeof(pv_str) - strlen(pv_str) - 1);
        strncat(pv_str, move_str, sizeof(pv_str) - strlen(pv_str) - 1);
    }
    strncpy(info->pv, pv_str, sizeof(info->pv) - 1);
    info->pv[sizeof(info->pv) - 1] = '\0';

    return pv_triangle[0][0];
}

