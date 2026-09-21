/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT
 * Copyright (C) Dariusz Domagała
 * Copyright (C) Oliver Brausch (OliThink 5.10.5, MIT, see LICENSES/OLITHINK-MIT.txt)
 * Portions of this file are adapted from OliThink 5.10.5. Modified by Dariusz Domagała, 2026-09-21.
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

#define MAX_SEARCH_PLY 44
typedef char history_capacity_check[(HISTORY_GAME_LIMIT + 1 + MAX_SEARCH_PLY + 1 <= MAX_POSITION_HISTORY) ? 1 : -1];

#define QS_DELTA_BETA 125
#define QS_DELTA_ALPHA 85

enum { STAGE_HASH, STAGE_CAPTURES, STAGE_QUIETS, STAGE_END };
enum { NO_MOVE_YET, SOME_MOVE_SEARCHED, ALPHA_RAISED };

static const int piece_value_cp[7] = { 100, 500, 290, 310, 980, 0, 0 };
static const int piece_value_units[7] = {   0,   5,   2,   3,   9, 0, 0 };

static const int null_reduction_steps[7] = { 13, 43, 149, 519, 1809, 6311, 22027 };

static int  quiet_history[65536];
static Move killer_table[MAX_SEARCH_PLY];

static Move pv_triangle[MAX_SEARCH_PLY + 1][MAX_SEARCH_PLY + 1];
static int  eval_stack_ply[MAX_SEARCH_PLY + 1];
static int  null_at_ply[MAX_SEARCH_PLY + 2];

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

static int history_index(Color c, Move m) {
    return ((int)c << 15) | ((int)m.piece << 12) | (m.from << 6) | m.to;
}

static int null_reduction_from_margin(int delta) {
    int r = 0;
    if (delta >= 4) for (r = 1; r <= 7; r++) if (delta < null_reduction_steps[r - 1]) break;
    return r;
}

static Bitboard pinned_for_stm(const Position* pos) {
    Color    us   = pos->to_move;
    Color    them = (Color)(1 - (int)pos->to_move);
    int      k    = get_lsb(pos->pieces[us][KING]);
    Bitboard occ, katt, xray, b, pin = 0ULL;

    if (k < 0) return 0ULL;
    occ = pos->all_pieces;

    katt = get_rook_attacks_public(k, occ);
    xray = get_rook_attacks_public(k, occ ^ (katt & occ)) & ~katt;
    b    = xray & (pos->pieces[them][QUEEN] | pos->pieces[them][ROOK]);
    while (b) {
        int t = get_lsb(b);
        pop_lsb(&b);
        pin |= katt & get_rook_attacks_public(t, occ) & pos->occupied[us];
    }

    katt = get_bishop_attacks_public(k, occ);
    xray = get_bishop_attacks_public(k, occ ^ (katt & occ)) & ~katt;
    b    = xray & (pos->pieces[them][QUEEN] | pos->pieces[them][BISHOP]);
    while (b) {
        int t = get_lsb(b);
        pop_lsb(&b);
        pin |= katt & get_bishop_attacks_public(t, occ) & pos->occupied[us];
    }
    return pin;
}

static Move pick_capture(Move* mlist, int n, int s) {
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
    for (i = s; i < n; i++) {
        m = mlist[i];
        if (same_move(m, killer_table[ply])) {
            pi = i;
            break;
        }
        if (vmax < quiet_history[history_index(us, m)]) { vmax = quiet_history[history_index(us, m)]; pi = i; }
    }
    m = mlist[pi];
    if (pi != s) mlist[pi] = mlist[s];
    return m;
}

static int search_node(Position* pos, bool ch, int d, int ply, int alpha, int beta,
                     int null_ok, const Move* excl, SearchInfo* info);

static int qsearch(Position* pos, bool ch, int ply, int alpha, int beta, SearchInfo* info) {
    int  i, n, best = -MATE_SCORE;
    Move mlist[MAX_MOVES];

    LegalityBuffer qs_legal_ctx; int qs_legal_ready = 0;

    if (info->seldepth < ply) info->seldepth = ply;
    if (fast_check_stop()) return alpha;
    if (stop_requested || position_changed) return alpha;

    if (ply >= MAX_SEARCH_PLY) return evaluate_position(pos);

    if (!ch) do {

        int mat_balance_stm = (pos->to_move == WHITE) ? pos->mat_balance : -pos->mat_balance;
        if (mat_balance_stm - QS_DELTA_BETA >= beta) return beta;
        if (mat_balance_stm + QS_DELTA_ALPHA <= alpha) break;
        best = evaluate_position(pos);
        if (best > alpha) {
            if (best >= beta) return beta;
            alpha = best;
        }
    } while (0);

    n = ch ? generate_legal_evasions(pos, mlist) : generate_noisy_moves(pos, mlist);
    if (ch && n == 0) return -MATE_SCORE + ply;

    for (i = 0; i < n; i++) {
        Move     m = pick_capture(mlist, n, i);
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
        info->nodes++;
        (*node_counter_ptr)++;

        w = -qsearch(pos, is_in_check(pos, pos->to_move), ply + 1, -beta, -alpha, info);

        undo_move_full(pos, &m, &u);

        if (stop_requested || position_changed) return alpha;

        if (w > best) {
            if ((best = w) > alpha) {
                if ((alpha = w) >= beta) return beta;
            }
        }
    }
    return best > -MATE_SCORE ? best : evaluate_position(pos);
}

static int search_node(Position* pos, bool ch, int d, int ply, int alpha, int beta,
                     int null_ok, const Move* excl, SearchInfo* info) {
    int  i, j, n, nall, nnoisy, first, hist_max, n_quiets, improving_flag, eval_adj;
    int  pvnode = beta > alpha + 1;

    int  w = -MATE_SCORE;
    Color us = pos->to_move;
    int  tt_d = 0, tt_flag = 0, tt_s = 0;
    bool tt_hit;
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
             || has_insufficient_material(pos))) return 0;

    if (ply >= MAX_SEARCH_PLY) return evaluate_position(pos);

    if (ch) d++;
    if (d <= 0) return qsearch(pos, ch, ply, alpha, beta, info);

    if (alpha < -MATE_SCORE + ply)     alpha = -MATE_SCORE + ply;
    if (beta  >  MATE_SCORE - ply - 1) beta  =  MATE_SCORE - ply - 1;
    if (alpha >= beta) return alpha;

    tt_pv_move = (ply == 0) ? pv_triangle[0][0] : empty_move();

    tt_hit = probe_hash_entry(&main_tt, pos->hash, &tt_d, &tt_flag, &tt_s, ply);
    if (tt_hit && !excl && tt_d >= d) {
        if ((tt_flag == HASH_BETA  || tt_flag == HASH_EXACT) && tt_s >= beta) return beta;
        if ((tt_flag == HASH_ALPHA || tt_flag == HASH_EXACT) && tt_s <= alpha) return alpha;
    }
    if (!excl && is_no_move(tt_pv_move)) {

        int dummy = 0;
        (void)probe_hash(&main_tt, pos->hash, 0, -MATE_SCORE, MATE_SCORE, &dummy, &tt_pv_move, ply);
    }

    eval_adj = eval_stack_ply[ply] = ch ? -MATE_SCORE + ply
                                : (tt_hit ? tt_s : evaluate_position(pos));

    if (!ch && !pvnode && beta > -MATE_SCORE + 500) {
        if (d <= 3 && eval_adj + 400 < beta) {

            w = qsearch(pos, false, ply, alpha, beta, info);
            if (w < beta) return w;
        }
        if (d <= 8 && eval_adj - 88 * d > beta) return eval_adj;
    }

    null_ok = null_ok && !ch && beta > -MATE_SCORE + 500 && d > 1 && eval_adj > alpha
                      && (ply < 2 || !null_at_ply[ply - 2]);
    if (null_ok && pop_count(pos->occupied[us]
                             & ~pos->pieces[us][PAWN]
                             & ~pinned_for_stm(pos)) > 1) {
        int      R          = (10 + d + null_reduction_from_margin(eval_adj - alpha)) / 4;
        Color    org_side   = pos->to_move;
        int      org_ep     = pos->en_passant_square;
        uint64_t org_hash   = pos->hash;

        pos->to_move = (Color)(1 - (int)pos->to_move);
        pos->en_passant_square = -1;
        pos->hash ^= zobrist_side_to_move;
        if (org_ep != -1) pos->hash ^= zobrist_en_passant[org_ep];

        null_at_ply[ply] = 1;
        w = -search_node(pos, false, d - R, ply + 1, -beta, 1 - beta, 0, NULL, info);
        null_at_ply[ply] = 0;

        pos->to_move = org_side;
        pos->en_passant_square = org_ep;
        pos->hash = org_hash;

        if (!(stop_requested || position_changed) && w >= beta) return beta;
    }
    null_at_ply[ply] = 0;

    if (d >= 4 && is_no_move(tt_pv_move)) d--;

    LegalityBuffer legal_buf;
    improving_flag = !ch && ply >= 2 && eval_adj >= eval_stack_ply[ply - 2];
    first   = NO_MOVE_YET;
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

    for (n = STAGE_HASH; n <= (ch ? STAGE_CAPTURES : STAGE_QUIETS); n++) {
        int nd = d - 1;
        int lo, hi;

        if (n == STAGE_HASH) {
            if (is_no_move(tt_pv_move)) continue;
            lo = 0; hi = 1;

            if (d >= 8 && ply && alpha > -MATE_SCORE + 500
                    && tt_hit && tt_flag == HASH_BETA && tt_d >= d - 3) {
                int bc = tt_s - d;
                nd += (search_node(pos, ch, d >> 1, ply, bc - 1, bc, 0, &tt_pv_move, info) < bc);
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
            bool     gives_check_now;

            if      (n == STAGE_HASH)  m = tt_pv_move;
            else if (n == STAGE_CAPTURES) m = pick_capture(mlist, nnoisy, i);
            else                    m = pick_quiet(mlist, nall, i, ply, us);

            if ((n != STAGE_HASH && same_move(m, tt_pv_move)) || (excl && same_move(m, *excl))) continue;

            if (!ch && !is_legal_fast_pub(pos, &m, &legal_buf)) continue;

            quiet = (m.captured == EMPTY && m.promotion == EMPTY);

            if (ply == 0 && (is_root_excluded(&m) || !is_root_included(&m))) continue;

            if (!ch && quiet && alpha > -MATE_SCORE + 500 && n_quiets > (2 * d * (improving_flag + 1))
               ) {
                n = STAGE_END;
                break;
            }

            if (n != STAGE_HASH) see = static_exchange_evaluation(pos, &m);
            if (n != STAGE_HASH && first != NO_MOVE_YET && alpha > -MATE_SCORE + 500
                    && d <= 8 && see < -d * 60) continue;

            make_move_full(pos, &m, &u);
            if (quiet) quiets[n_quiets++] = m;
            gives_check_now = is_in_check(pos, pos->to_move);

            if (gives_check_now || pvnode || ch
                    || (m.piece == PAWN && is_passed_pawn(pos, m.to, us))) {

            } else if (n == STAGE_CAPTURES && d >= 2 && see < 0) {
                ext -= (d + 1) / (3 + improving_flag);
            } else if (n == STAGE_QUIETS && !IS_KILLER_OR_COUNTER(m, ply)) {
                int his = quiet_history[history_index(us, m)];
                if (his > hist_max) hist_max = his;
                else if (d < 6 && (his < 0 || his * his < hist_max)) {
                    undo_move_full(pos, &m, &u);
                    continue;
                } else if (d >= 2) ext -= (d + 1) / 3;
            }

            is_first_pv = (first == NO_MOVE_YET && pvnode);
            if (!is_first_pv)
                w = -search_node(pos, gives_check_now, nd + ext, ply + 1, -alpha - 1, -alpha, 1, NULL, info);
            if (w > alpha && ext < 0)
                w = -search_node(pos, gives_check_now, nd,       ply + 1, -alpha - 1, -alpha, 1, NULL, info);
            if ((w > alpha && w < beta) || is_first_pv)
                w = -search_node(pos, gives_check_now, nd,       ply + 1, -beta,      -alpha, 0, NULL, info);

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
                        int his = (d * d < 512) ? d * d : 512;
                        int ix  = history_index(us, m);
                        killer_table[ply] = m;
                        quiet_history[ix] += his - quiet_history[ix] * his / 512;
                        for (j = 0; j < n_quiets - 1; j++) {
                            int ix2 = history_index(us, quiets[j]);
                            quiet_history[ix2] += -his - quiet_history[ix2] * his / 512;
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

Move search_root(Position* pos, int depth, int alpha, int beta, SearchInfo* info,
                    long long* nodes_searched_ptr, bool* root_interrupted) {
    int  i;
    bool ch;
    char pv_str[1024] = "";

    node_counter_ptr    = nodes_searched_ptr;
    root_interrupted_ptr = root_interrupted;
    *root_interrupted = false;

    if (depth == 1) {
        memset(quiet_history, 0, sizeof(quiet_history));
        memset(killer_table,  0, sizeof(killer_table));
        pv_triangle[0][0] = empty_move();
    }

    ch = is_in_check(pos, pos->to_move);
    info->score = search_node(pos, ch, depth, 0, alpha, beta, 0, NULL, info);

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

