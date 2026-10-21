/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT
 * Copyright (C) Dariusz Domagała
 * Copyright (C) Oliver Brausch (OliThink 5.10.5, MIT, see LICENSES/OLITHINK-MIT.txt)
 * Copyright (C) 2019-2025 Morgan Houppin (Stash 37)
 * Copyright (C) 2004-2026 The Stockfish developers (Stockfish 19, see the AUTHORS file of Stockfish)
 * Portions of this file are adapted from OliThink 5.10.5, Stash 37 and Stockfish 19. Modified by Dariusz Domagała, 2026-10-21.
 */
#include "syzygy.h"
#include "search.h"
#include "board.h"
#include "movegen.h"
#include "eval.h"
#include "utils.h"
#include "tt.h"
#include "pawn_structure.h"
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include "tune.h"

#include <stdio.h>
#include <stdlib.h>
#include "nnue.h"

#define MAX_SEARCH_PLY (MAX_DEPTH + EXT_MAX + MAX_QUIESCENCE_DEPTH)
typedef char history_capacity_check[(HISTORY_GAME_LIMIT + 1 + MAX_SEARCH_PLY + 1 <= MAX_POSITION_HISTORY) ? 1 : -1];

#define QS_DELTA_BETA QS_MARGIN_BETA
#define QS_DELTA_ALPHA QS_MARGIN_ALPHA

#define QS_FUT_MARGIN QS_FUT_SEE
#define BAD_CAPTURES_MAX 32

#define TB_WIN_ZONE (SYZYGY_WIN_SCORE - 1000)

#define RED_ST_OFF T_RED_ST_OFF
#define RED_NMP_ST T_RED_NMP_ST
#define RED_EV_DIVISOR T_RED_EV_DIVISOR
#define CHECK_SEE_MARGIN T_CHECK_SEE_MARGIN

#  define FIRST_MOVE_DONE (first != NO_MOVE_YET)

#define LMR_EXTEND_THRESHOLD T_LMR_EXTEND_THRESHOLD
#define LMR_SKIP_RED_THRESHOLD T_LMR_SKIP_RED_THRESHOLD
#define LMR_SKIP_NO_TT_ADD T_LMR_SKIP_NO_TT_ADD

#  include "magic.h"
#define EXT_PHASE_MAX T_EXT_PHASE_MAX

static int ext_direct_check(const Position *pos, const Move *m) {
    const Color    on = pos->to_move;
    const Bitboard kb = pos->pieces[on][KING];
    const int      t  = m->to;
    const int      pt = m->promotion != EMPTY ? (int)m->promotion : (int)m->piece;
    if (m->is_castling || !kb) return 0;
    switch (pt) {
    case PAWN: {
        const int k = get_lsb(kb), fr = (t & 7) - (k & 7);
        return (k >> 3) == (t >> 3) + (on == BLACK ? 1 : -1) && (fr == 1 || fr == -1);
    }
    case KNIGHT: return (knight_attacks[t] & kb) != 0;
    case BISHOP: return (magic_bishop_attacks(t, pos->all_pieces) & kb) != 0;
    case ROOK:   return (magic_rook_attacks(t, pos->all_pieces) & kb) != 0;
    case QUEEN:  return ((magic_bishop_attacks(t, pos->all_pieces) | magic_rook_attacks(t, pos->all_pieces)) & kb) != 0;
    default:     return 0;
    }
}

enum { STAGE_HASH, STAGE_CAPTURES, STAGE_QUIETS, STAGE_BAD_CAPTURES, STAGE_END };
enum { NO_MOVE_YET, SOME_MOVE_SEARCHED, ALPHA_RAISED };

static const int piece_value_cp[7] = { 100, 500, 290, 310, 980, 0, 0 };
static const int piece_value_units[7] = {   0,   5,   2,   3,   9, 0, 0 };

#define REFINE_K3_THRESHOLD (piece_value_cp[QUEEN] + piece_value_cp[ROOK])
#define REFINE_K4_THRESHOLD (piece_value_cp[QUEEN])
#define REFINE_K5_THRESHOLD (piece_value_cp[QUEEN] + 2 * piece_value_cp[BISHOP])

static inline int side_piece_value_cp(const Position* pos, Color c) {
    return pop_count(pos->pieces[c][ROOK])   * piece_value_cp[ROOK]
         + pop_count(pos->pieces[c][KNIGHT]) * piece_value_cp[KNIGHT]
         + pop_count(pos->pieces[c][BISHOP]) * piece_value_cp[BISHOP]
         + pop_count(pos->pieces[c][QUEEN])  * piece_value_cp[QUEEN];
}

static inline int refine_k5_good_capture(Position* pos, const Move* m, int see, Color us) {
    const Color them = (Color)(1 - (int)us);
    if (side_piece_value_cp(pos, us) >= REFINE_K5_THRESHOLD) return 0;
    if (see >= -piece_value_cp[PAWN]) return 1;
    if (see > -piece_value_cp[KNIGHT] && m->captured == PAWN) {
        const int row_idx = m->to >> 3;
        if ((them == WHITE ? row_idx >= 5 : row_idx <= 2) && is_passed_pawn(pos, m->to, them)) return 1;
    }
    return 0;
}

#  define HIST_BONUS_RAW(d) (HB_A * (d) * (d) - HB_B * (d) - HB_C < 0 ? 0                               \
                      : (HB_A * (d) * (d) - HB_B * (d) - HB_C < 2461 ? HB_A * (d) * (d) - HB_B * (d) - HB_C : 2461) / 32)
#    define HIST_BONUS(d) (HIST_BONUS_RAW(d) > (d) ? HIST_BONUS_RAW(d) : ((d) > 0 ? (d) : 0))

#  define S2_HS       16384
#  define S2_HB(d)    (32 * HIST_BONUS(d))
#  define S2_HR(x)    ((x) / 32)

#define FB_A T_FB_A
#define FB_B T_FB_B
#define FB_LD T_FB_LD
#define FB_CAPHIST_WEIGHT 1468
#define FB_CH_FUT 1030
#define FB_SEE_H 284
#define FB_LMR_H 2291
#define FB_SPLIT_H 8552
#define FB_PENALTY_MULT 3

#define S2_CH(mm) S2_HR(capture_history[(int)us][(int)(mm).piece][(mm).to][(int)(mm).captured])

static const int see_victim_value[7] = { 100, 500, 325, 325, 975, 0, 0 };

#define PS_DEEPER_MARGIN T_PS_DEEPER_MARGIN
#define PS_SHALLOWER_MARGIN T_PS_SHALLOWER_MARGIN
#define PS_PV_DEPTH_MAX T_PS_PV_DEPTH_MAX
#define PS_REDUCTION_MARGIN T_PS_REDUCTION_MARGIN
#define PS_NO_EVAL (-(1 << 28))

#  define PAWN_HIST_ARG
#  define PAWN_HIST_PASS

#define FIFTY_MOVE_SCALE(p, e) (e)

#define FF2_A T_FF2_A
#define FF2_B T_FF2_B
#define FF2_C T_FF2_C
#define FF2_LD T_FF2_LD

#  define LMR_BASE(q) ((LMR_K0 + lmr_table[(q)][d < 255 ? d : 255] * lmr_table[(q)][mc < 255 ? mc : 255]                  \
                          + !improving_flag * LMR_NIMP) > 0                                                              \
                          ? (LMR_K0 + lmr_table[(q)][d < 255 ? d : 255] * lmr_table[(q)][mc < 255 ? mc : 255]                \
                             + !improving_flag * LMR_NIMP) / 1024 : 0)

#  define US_HAS_PIECES() ((pos->occupied[us] & ~pos->pieces[us][PAWN] & ~pos->pieces[us][KING]) != 0)

#    define FB_VICTIM_VALUE(t) (material_values_ptr[(int)(t)])

#define DOUBLE_EXT_MARGIN T_DOUBLE_EXT_MARGIN
#define TRIPLE_EXT_MARGIN T_TRIPLE_EXT_MARGIN
#define DOUBLE_EXT_LIMIT T_DOUBLE_EXT_LIMIT

#  define QUIET_FLOOR_MARK(q) ((void)0)
#define PROBCUT_MARGIN PC_MARGIN
#define PROBCUT_D T_PROBCUT_D

#define CORR_TABLE_SIZE  16384
#define CORR_LIMIT    1024
#define CORR_WEIGHT_PAWN T_CORR_WEIGHT_PAWN
#define CORR_WEIGHT_NONPAWN T_CORR_WEIGHT_NONPAWN
#define CORR_WEIGHT_CONT T_CORR_WEIGHT_CONT
#define CORR_DIVISOR    355767
#define CORR_UPDATE_NONPAWN T_CORR_UPDATE_NONPAWN
#define CORR_UPDATE_CONT2 T_CORR_UPDATE_CONT2
#define CORR_UPDATE_CONT4 T_CORR_UPDATE_CONT4
#define CORR_INPUT_NUM 19
#define CORR_INPUT_DEN 896
#define CORR_BONUS_MOVE T_CORR_BONUS_MOVE
#define CORR_BONUS_NO_MOVE T_CORR_BONUS_NO_MOVE

#  define LAZY_SEE()(see_ready ? see : (see_ready = 1, see = static_exchange_evaluation(pos, &m)))

static const int see_gain_value[7] = { 100, 500, 325, 325, 975, 10000, 0 };
#  define GAIN_LOWER_BOUND(mm) (((mm).promotion != EMPTY || (mm).is_en_passant || (mm).is_castling) ? -INFINITY_SCORE \
                           : see_gain_value[(int)(mm).captured] - see_gain_value[(int)(mm).piece])
typedef struct {
    int  quiet_history[65536];
    Move killer_table[MAX_SEARCH_PLY];
    int  capture_history[2][6][64][7];
    Move pv_triangle[MAX_SEARCH_PLY + 1][MAX_SEARCH_PLY + 1];
    int  eval_stack_ply[MAX_SEARCH_PLY + 1];
    int  null_at_ply[MAX_SEARCH_PLY + 2];
    int  cont_hist[2][2][6][64][6][64];
    Move move_stack[MAX_SEARCH_PLY + 2];
    int  double_ext_total[MAX_SEARCH_PLY + 2];
    int  stat_score_at_ply[MAX_SEARCH_PLY + 2];
    int  move_count_at_ply[MAX_SEARCH_PLY + 2];
    short corr_hist_pawn[2][CORR_TABLE_SIZE];
    short corr_hist_nonpawn[2][2][CORR_TABLE_SIZE];
    short corr_hist_cont[2][6][64][6][64];
    long long* node_counter_ptr;
    bool*      root_interrupted_ptr;
    Move counter_table[2][6][64];
    Move counter_at_ply[MAX_SEARCH_PLY + 2];
    int  nmp_verify_ply;
    int  root_depth;
    int  reduced_child_at_ply[MAX_SEARCH_PLY + 2];
    int  parent_lmr_reduction[MAX_SEARCH_PLY + 2];
    int  raw_eval_at_ply[MAX_SEARCH_PLY + 2];
    NnueThread* nnue_thread;
} SearchThreadState;

static SMP_TLS SearchThreadState thread_state;
#define quiet_history     (thread_state.quiet_history)
#define killer_table       (thread_state.killer_table)
#define killer2_table      (thread_state.killer2_table)
#define capture_history      (thread_state.capture_history)
#define capture_from_to_hist         (thread_state.capture_from_to_hist)
#define pv_triangle           (thread_state.pv_triangle)
#define eval_stack_ply   (thread_state.eval_stack_ply)
#define null_at_ply          (thread_state.null_at_ply)
#define cont_hist_basic         (thread_state.cont_hist_basic)
#define cont_hist           (thread_state.cont_hist)
#define move_stack    (thread_state.move_stack)
#define gives_check_at_ply        (thread_state.gives_check_at_ply)
#define double_ext_total         (thread_state.double_ext_total)
#define nnue_thread           (thread_state.nnue_thread)
#define cutoff_count_at_ply       (thread_state.cutoff_count_at_ply)
#define tt_pv_at_ply         (thread_state.tt_pv_at_ply)
#define stat_score_at_ply         (thread_state.stat_score_at_ply)
#define move_count_at_ply          (thread_state.move_count_at_ply)
#define corr_pawn_table    (thread_state.corr_pawn_table)
#define corr_hist_pawn     (thread_state.corr_hist_pawn)
#define corr_hist_nonpawn       (thread_state.corr_hist_nonpawn)
#define corr_hist_cont     (thread_state.corr_hist_cont)
#define node_counter_ptr    (thread_state.node_counter_ptr)
#define root_interrupted_ptr (thread_state.root_interrupted_ptr)
#define contempt_limit_side    (thread_state.contempt_limit_side)
#define counter_table      (thread_state.counter_table)
#define counter_at_ply           (thread_state.counter_at_ply)
#define nmp_verify_ply    (thread_state.nmp_verify_ply)
#define root_depth       (thread_state.root_depth)
#define qs_entry_ply       (thread_state.qs_entry_ply)
#define qs_last_to_sq        (thread_state.qs_last_to_sq)
#define reduced_child_at_ply         (thread_state.reduced_child_at_ply)
#define parent_lmr_reduction       (thread_state.parent_lmr_reduction)
#define raw_eval_at_ply       (thread_state.raw_eval_at_ply)
#define pawn_history        (thread_state.pawn_history)
#define low_ply_history        (thread_state.low_ply_history)

static inline uint64_t key_after_move(const Position* pos, const Move* m) {
    const int us = pos->to_move, them = 1 - us;
    uint64_t h = pos->hash ^ zobrist_side_to_move ^ zobrist_pieces[us][m->piece][m->from];
    if (m->is_en_passant) {
        h ^= zobrist_pieces[them][PAWN][us == WHITE ? m->to - 8 : m->to + 8];
        h ^= zobrist_pieces[us][PAWN][m->to];
    } else if (!m->is_castling) {
        if (m->captured != EMPTY) h ^= zobrist_pieces[them][m->captured][m->to];
        h ^= zobrist_pieces[us][m->promotion != EMPTY ? m->promotion : m->piece][m->to];
    }
    if (pos->en_passant_square != -1) h ^= zobrist_en_passant[pos->en_passant_square];
    if (m->piece == PAWN && (m->to - m->from == 16 || m->from - m->to == 16)) h ^= zobrist_en_passant[(m->from + m->to) / 2];
    return h;
}
#  define PREFETCH_BEFORE_MOVE(m) const uint64_t prefetch_key = key_after_move(pos, &(m)); TT_PF(prefetch_key)
#  define PREFETCH_AFTER_MOVE()     do { if (pos->hash != prefetch_key) TT_PF(pos->hash); } while (0)

#define CAPTURE_PICK_ARG  , Color us
#define CAPTURE_PICK_PASS , us

#define BASIC_EG_EVAL_CAP (SYZYGY_WIN_SCORE - 1000 - 1)
static inline int node_eval_net(Position* pos, int ply) {
    int w;
    if ((pos->occupied[WHITE] == pos->pieces[WHITE][KING] || pos->occupied[BLACK] == pos->pieces[BLACK][KING])
        && eval_basic_endgame(pos, &w))
        return w > BASIC_EG_EVAL_CAP ? BASIC_EG_EVAL_CAP : (w < -BASIC_EG_EVAL_CAP ? -BASIC_EG_EVAL_CAP : w);
    return nnue_eval_node(nnue_thread, ply, pos);
}
#define NODE_EVAL(p, ply) node_eval_net((p), (ply))
_Static_assert(MAX_SEARCH_PLY < NNUE_STACK_SIZE, "accumulator stack must have an entry for every search ply");
_Static_assert(TB_WIN_ZONE == SYZYGY_WIN_SCORE - 1000, "nnue_to_search clamps to SYZYGY_WIN_SCORE - 1000 - 1 = TB_WIN_ZONE - 1");

static inline uint64_t corr_pawn_key(const Position* pos) {
    uint64_t x = pos->pieces[WHITE][PAWN] ^ (pos->pieces[BLACK][PAWN] * 0x9E3779B97F4A7C15ULL);
    x ^= x >> 30; x *= 0xBF58476D1CE4E5B9ULL;
    x ^= x >> 27; x *= 0x94D049BB133111EBULL;
    x ^= x >> 31;
    return x;
}
#  define PAWN_HIST_PRUNE_TERM(m)
#  define PAWN_HIST_ORDER_TERM(m)
#  define LOW_PLY_HIST_ORDER_TERM(m)

typedef struct { short *pawn_bit, *npb, *npc, *k2, *k4; } CorrSlots;

static inline uint64_t corr_nonpawn_key(const Position* pos, int c) {
    uint64_t x = pos->pieces[c][KNIGHT] * 0x9E3779B97F4A7C15ULL
               + pos->pieces[c][BISHOP] * 0xC2B2AE3D27D4EB4FULL
               + pos->pieces[c][ROOK]   * 0x165667B19E3779F9ULL
               + pos->pieces[c][QUEEN]  * 0xD6E8FEB86659FD93ULL
               + pos->pieces[c][KING]   * 0xFF51AFD7ED558CCDULL;
    x ^= x >> 30; x *= 0xBF58476D1CE4E5B9ULL;
    x ^= x >> 27; x *= 0x94D049BB133111EBULL;
    x ^= x >> 31;
    return x;
}

static inline short* corr_cont_slot(int ply, int k, Color us, int f1, int t1) {
    Move p;
    if (ply < k || null_at_ply[ply - k]) return NULL;
    p = move_stack[ply - k];
    if ((int)p.piece >= 6) return NULL;
    return &corr_hist_cont[us][p.promotion != EMPTY ? (int)p.promotion : (int)p.piece][(int)p.to][f1][t1];
}

static inline int corr_compute(const Position* pos, int ply, Color us, CorrSlots* k) {
    int cv = 0;
    (void)pos; (void)ply;
    k->pawn_bit = &corr_hist_pawn[us][corr_pawn_key(pos) & (CORR_TABLE_SIZE - 1)];
    cv += CORR_WEIGHT_PAWN * (int)*k->pawn_bit;
    k->npb = &corr_hist_nonpawn[WHITE][us][corr_nonpawn_key(pos, WHITE) & (CORR_TABLE_SIZE - 1)];
    k->npc = &corr_hist_nonpawn[BLACK][us][corr_nonpawn_key(pos, BLACK) & (CORR_TABLE_SIZE - 1)];
    cv += CORR_WEIGHT_NONPAWN * ((int)*k->npb + (int)*k->npc);
    k->k2 = k->k4 = NULL;
    if (ply >= 2 && !null_at_ply[ply - 1] && (int)move_stack[ply - 1].piece < 6) {
        const Move prev_move = move_stack[ply - 1];
        const int  f1 = prev_move.promotion != EMPTY ? (int)prev_move.promotion : (int)prev_move.piece, t1 = prev_move.to;
        if ((k->k2 = corr_cont_slot(ply, 2, us, f1, t1))) cv += CORR_WEIGHT_CONT * (int)*k->k2;
        if ((k->k4 = corr_cont_slot(ply, 4, us, f1, t1))) cv += CORR_WEIGHT_CONT * (int)*k->k4;
    }
    return cv / CORR_DIVISOR;
}

static inline int corr_clamp(int v) {
    return v < -TB_WIN_ZONE + 1 ? -TB_WIN_ZONE + 1 : v > TB_WIN_ZONE - 1 ? TB_WIN_ZONE - 1 : v;
}

static inline void corr_update_entry(short* e, int b) {
    if (b >  CORR_LIMIT) b =  CORR_LIMIT;
    if (b < -CORR_LIMIT) b = -CORR_LIMIT;
    *e = (short)((int)*e + b - (int)*e * (b < 0 ? -b : b) / CORR_LIMIT);
}

static inline void corr_learn(const CorrSlots* k, int bonus) {
    (void)k; (void)bonus;
    corr_update_entry(k->pawn_bit, bonus);
    corr_update_entry(k->npb, bonus * CORR_UPDATE_NONPAWN / 128);
    corr_update_entry(k->npc, bonus * CORR_UPDATE_NONPAWN / 128);
    if (k->k2) corr_update_entry(k->k2, bonus * CORR_UPDATE_CONT2 / 128);
    if (k->k4) corr_update_entry(k->k4, bonus * CORR_UPDATE_CONT4 / 128);
}

#define S2_STOP (smp_helper ? smp_stop_read() : (stop_requested || position_changed))

long long effort_total = 0, effort_best = 0;
#define EFFORT_AT_ROOT (!smp_helper && root_excluded_count == 0)

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
    int c, f, a, b, stride;
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
        for (stride = 0; r && stride < 0x4000; stride++) {
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

#define S2_C3_INL inline __attribute__((always_inline))

static S2_C3_INL int* cont_hist_row(int ply, int k, Color us) {
    Move p;
    if (ply < k || null_at_ply[ply - k]) return NULL;
    p = move_stack[ply - k];
    if ((int)p.piece >= 6) return NULL;
    return &cont_hist[us][k & 1][p.piece][p.to][0][0];
}

static S2_C3_INL int cont_hist_sum(int ply, Color us, Move m, int with_ply4) {
    int  s = 0, ix = (int)m.piece * 64 + m.to;
    int* w;
    if ((w = cont_hist_row(ply, 1, us))) s += S2_HR(w[ix]);
    if ((w = cont_hist_row(ply, 2, us))) s += S2_HR(w[ix]);
    if (with_ply4 && (w = cont_hist_row(ply, 4, us))) s += S2_HR(w[ix]);
    return s;
}
#  define CONT_HIST_UPDATE_PLIES 3

static void cont_hist_update(int ply, Color us, Move m, int b) {
    static const int cont_plies[4] = { 1, 2, 4, 6 };
    int  k, ix = (int)m.piece * 64 + m.to, ab = b < 0 ? -b : b;
    int* w;
    if ((int)m.piece >= 6) return;
    for (k = 0; k < CONT_HIST_UPDATE_PLIES; k++)
        if ((w = cont_hist_row(ply, cont_plies[k], us))) w[ix] += b - w[ix] * ab / S2_HS;
}

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

        t += S2_HR(capture_history[(int)us][(int)m.piece][m.to][(int)m.captured]) * FB_CAPHIST_WEIGHT / 1024;
        if (t > vmax) { vmax = t; pi = i; }
    }
    m = mlist[pi];
    if (pi != s) mlist[pi] = mlist[s];
    return m;
}

static Move pick_quiet(Move* mlist, int n, int s, int ply, Color us PAWN_HIST_ARG) {
    int i, pi = s, vmax = -(1 << 15);
    Move m;
    int pi3 = -1;
    const int* c1 = cont_hist_row(ply, 1, us);
    const int* c2 = cont_hist_row(ply, 2, us);
    {

        int k1 = -1, k2 = -1, k3 = -1;
        for (i = s; i < n; i++) {
            if (same_move(mlist[i], killer_table[ply])) { k1 = i; break; }
            if (k3 < 0 && same_move(mlist[i], counter_at_ply[ply])) k3 = i;
        }
        pi = k1 >= 0 ? k1 : k2 >= 0 ? k2 : k3;
        if (pi >= 0) {
            m = mlist[pi];
            if (pi != s) mlist[pi] = mlist[s];
            return m;
        }
        pi = s;
    }
    for (i = s; i < n; i++) {
        m = mlist[i];
        {

            int v = S2_HR(quiet_history[history_index(us, m)]) / 2, ix = (int)m.piece * 64 + m.to;
            if (c1) v += S2_HR(c1[ix]);
            if (c2) v += S2_HR(c2[ix]);
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
    return S2_HR(quiet_history[history_index(us, m)]) / 2 + cont_hist_sum(ply, us, m, 0);
}

static Move pick_evasion(Move* mlist, int n, int s, int ply, Color us, int cont_plies) {
    int  i, t, pi = s, vmax = -(1 << 30);
    Move m;
    for (i = s; i < n; i++) {
        m = mlist[i];
        if (m.captured == EMPTY && m.promotion == EMPTY)
            t = -(1 << 20) + (cont_plies ? quiet_history_score(us, m, ply) : S2_HR(quiet_history[history_index(us, m)]) / 2);
        else t = piece_value_cp[m.captured] - piece_value_units[m.piece];
        if (t > vmax) { vmax = t; pi = i; }
    }
    m = mlist[pi];
    if (pi != s) mlist[pi] = mlist[s];
    return m;
}

static int search_node(Position* pos, bool ch, int d, int ply, int alpha, int beta,
                     int null_ok, const Move* excl, SearchInfo* info CUTNODE_ARG);

#  include "magic.h"

static int qs_may_give_check(const Move *m, int ksq, Bitboard king_diag, Bitboard king_orth) {
    const Bitboard from_bb = 1ULL << m->from, to_bb = 1ULL << m->to;
    if ((king_diag | king_orth) & from_bb) return 1;
    switch (m->piece) {
    case PAWN: {
        const int fr = (m->to & 7) - (ksq & 7), rr = (ksq >> 3) - (m->to >> 3);
        return (fr == 1 || fr == -1) && (rr == 1 || rr == -1);
    }
    case KNIGHT: return (knight_attacks[ksq] & to_bb) != 0;
    case BISHOP: return (king_diag & to_bb) != 0;
    case ROOK:   return (king_orth & to_bb) != 0;
    case QUEEN:  return ((king_diag | king_orth) & to_bb) != 0;
    default:     return 0;
    }
}

static int lazy_gives_check(Position* pos, Move* m, int* ksq, Bitboard* king_diag, Bitboard* king_orth) {
    if (m->is_castling) return gives_check(pos, m);
    if (*ksq < 0) {
        const Bitboard kb = pos->pieces[pos->to_move == WHITE ? BLACK : WHITE][KING];
        if (!kb) return 0;
        *ksq = get_lsb(kb);
        *king_diag  = magic_bishop_attacks(*ksq, pos->all_pieces);
        *king_orth  = magic_rook_attacks(*ksq, pos->all_pieces);
    }
    if (!qs_may_give_check(m, *ksq, *king_diag, *king_orth)) return 0;
    return gives_check(pos, m);
}
#  define LAZY_GIVES_CHECK() (gives_check_cache >= 0 ? gives_check_cache : (gives_check_cache = lazy_gives_check(pos, &m, &lazy_king_sq, &lazy_king_diag, &lazy_king_orth)))
#  define LMP_EXEMPT_MOVE(m) IS_KILLER_OR_COUNTER((m), ply)
static inline void qs_tt_store(Position* pos, int score, HashFlag flag, int ply) {
    int d0, f0, s0;
    (void)d0; (void)f0; (void)s0;
    tt_store_qs(&main_tt, pos->hash, score, flag, empty_move(), ply);
}
static int qsearch(Position* pos, bool ch, int ply, int alpha, int beta, SearchInfo* info) {
    const int qs_alpha0 = alpha;
    int  qg_br = 0;
    int  i, n, best = -MATE_SCORE;
    Move mlist[MAX_MOVES];

    LegalityBuffer qs_legal_ctx; int qs_legal_ready = 0;
    Color us = pos->to_move;
    int  qs_base_score = 0, qs_have_base = 0;

    if (info->seldepth < ply) info->seldepth = ply;
    if (fast_check_stop()) return alpha;
    if (S2_STOP) return alpha;

    if (ply >= MAX_SEARCH_PLY) return FIFTY_MOVE_SCALE(pos, NODE_EVAL(pos, ply));

    TTProbe    qts;
    const bool q_tt_found = tt_probe_entry(&main_tt, pos->hash, ply, &qts);
    if (beta - alpha == 1) {
        if (q_tt_found && qts.result_bb) {
            const int qf = qts.flag, qs = qts.score;

            if ((qf == HASH_BETA  || qf == HASH_EXACT) && qs >= beta)  return qs;
            if ((qf == HASH_ALPHA || qf == HASH_EXACT) && qs <= alpha) return qs;
        }
    }

    if (!ch) do {
        int beta_filter_done = 0;
        int eval_is_fresh = 0, raw_eval_for_tt = 0;

        const int mb = pos->mat_balance;
        int mat_balance_stm = (pos->to_move == WHITE) ? mb : -mb;

        {
            const int b = scale_material_balance(pos, mb);
            mat_balance_stm = (pos->to_move == WHITE) ? b : -b;
        }

        if (mat_balance_stm - QS_DELTA_BETA >= beta && !is_endgame_special(pos)) {
            int beta_filter_val;
            if (q_tt_found && qts.eval != TT_EVAL_NONE) best = qts.eval;
            else { best = NODE_EVAL(pos, ply); eval_is_fresh = 1; }
            raw_eval_for_tt = best;
            { CorrSlots qk; best = corr_clamp(best + corr_compute(pos, ply, pos->to_move, &qk)); }
            beta_filter_val = best;
            if (q_tt_found && qts.result_bb && (qts.flag == HASH_EXACT
                                        || (qts.score > beta_filter_val ? qts.flag == HASH_BETA : qts.flag == HASH_ALPHA)))
                beta_filter_val = qts.score;
            if (beta_filter_val >= beta) return beta;
            beta_filter_done = 1;
        }
        if (mat_balance_stm + QS_DELTA_ALPHA <= alpha && !is_endgame_special(pos)) {
            qg_br = 1;
            break;
        }
        if (beta_filter_done) {
            if (eval_is_fresh) tt_store_eval(&main_tt, pos->hash, raw_eval_for_tt);
        } else {

        if (q_tt_found && qts.eval != TT_EVAL_NONE) best = qts.eval;
        else {
            best = NODE_EVAL(pos, ply);
            tt_store_eval(&main_tt, pos->hash, best);
        }

        { CorrSlots qk; best = corr_clamp(best + corr_compute(pos, ply, pos->to_move, &qk)); }
        }
        qs_base_score = best; qs_have_base = 1;

        if (q_tt_found && qts.result_bb && (qts.flag == HASH_EXACT
                                    || (qts.score > best ? qts.flag == HASH_BETA : qts.flag == HASH_ALPHA)))
            best = qts.score;
        if (best > alpha) {
            if (best >= beta) return best;
            alpha = best;
        }
    } while (0);

    n = ch ? generate_legal_evasions(pos, mlist) : generate_noisy_moves(pos, mlist);
    if (ch && n == 0) return -MATE_SCORE + ply;

    for (i = 0; i < n; i++) {
        Move     m = ch ? pick_evasion(mlist, n, i, ply, pos->to_move, 0) : pick_capture(mlist, n, i CAPTURE_PICK_PASS);
        UndoInfo u;
        int      w;

        if (ch && best > -TB_WIN_ZONE && m.captured == EMPTY && m.promotion == EMPTY) continue;

        if (!ch && m.promotion == EMPTY) {
            const int qf = qs_have_base && qs_base_score + QS_FUT_MARGIN <= alpha;
            if (qf || piece_value_cp[m.piece] > piece_value_cp[m.captured]) {

                static const int qs_see_value[7] = { 100, 500, 325, 325, 975, 10000, 0 };
                if (m.is_en_passant || qs_see_value[(int)m.captured] - qs_see_value[(int)m.piece] < (qf ? 1 : 0)) {
                const int s = static_exchange_evaluation(pos, &m);
                if (s < 0 || (qf && s <= 0)) continue;
                }
            }
        }
        PREFETCH_BEFORE_MOVE(m);

        if (!ch) {
            if (!qs_legal_ready) { legality_context_init_pub(pos, &qs_legal_ctx); qs_legal_ready = 1; }
            if (!is_legal_fast_pub(pos, &m, &qs_legal_ctx)) continue;
        }
        make_move_full(pos, &m, &u);
        nnue_after_move(nnue_thread, ply + 1, pos);
        PREFETCH_AFTER_MOVE();
        info->nodes++;
        (*node_counter_ptr)++;

        move_stack[ply] = m; null_at_ply[ply] = 0;
        w = -qsearch(pos, is_in_check(pos, pos->to_move), ply + 1, -beta, -alpha, info);

        undo_move_full(pos, &m, &u);

        if (S2_STOP) return alpha;

        if (w > best) {
            if ((best = w) > alpha) {
                if ((alpha = w) >= beta) {

                    qs_tt_store(pos, w, HASH_BETA, ply);
                    return w;
                }
            }
        }
    }

    if (qg_br && best > -MATE_SCORE && best <= qs_alpha0) {
        int qg_e;
        if (q_tt_found && qts.eval != TT_EVAL_NONE) qg_e = qts.eval;
        else {
            qg_e = NODE_EVAL(pos, ply);
            tt_store_eval(&main_tt, pos->hash, qg_e);
        }
        { CorrSlots qk; qg_e = corr_clamp(qg_e + corr_compute(pos, ply, pos->to_move, &qk)); }
        if (qg_e > best) best = qg_e;
    }
    if (best > -MATE_SCORE)
        qs_tt_store(pos, best, best >= beta ? HASH_BETA : best > qs_alpha0 ? HASH_EXACT : HASH_ALPHA, ply);
    return best > -MATE_SCORE ? best : FIFTY_MOVE_SCALE(pos, NODE_EVAL(pos, ply));
}

#include <stdio.h>
static void uci_report_currmove(Move* m, int nr) {
    if (smp_helper) return;
    if (uci_gate_ms() < 0) return;
    char buf_area[8];
    move_to_string_buf(m, buf_area);
    printf("info currmove %s currmovenumber %d\n", buf_area, nr + root_excluded_count);
    fflush(stdout);
}

static void pv_to_string(const Position* pos, char* out_str, size_t str_cap) {
    Position buf_copy = *pos;
    Move mlist[256];
    UndoInfo u;
    char ms[8];
    int  i, k, n;
    out_str[0] = '\0';
    for (i = 0; i <= MAX_SEARCH_PLY && i < 64; i++) {
        const Move m = pv_triangle[0][i];
        if (is_no_move(m)) break;
        n = generate_legal_moves(&buf_copy, mlist);
        for (k = 0; k < n; k++)
            if (mlist[k].from == m.from && mlist[k].to == m.to && mlist[k].promotion == m.promotion) break;
        if (k == n) break;
        move_to_string_buf(&mlist[k], ms);
        if (i > 0) strncat(out_str, " ", str_cap - strlen(out_str) - 1);
        strncat(out_str, ms, str_cap - strlen(out_str) - 1);
        make_move_full(&buf_copy, &mlist[k], &u);
    }
}

static void uci_report_pv(Position* pos, SearchInfo* info, int w) {
    char pv_str[1024];
    if (smp_helper) return;
    if (!uci_root_change_hook || uci_gate_ms() < 0) return;
    pv_to_string(pos, pv_str, sizeof(pv_str));
    uci_root_change_hook(info, w, pv_str);
}

static int search_node(Position* pos, bool ch, int d, int ply, int alpha, int beta,
                     int null_ok, const Move* excl, SearchInfo* info CUTNODE_ARG) {
    int  i, j, n, nall, nnoisy, first, hist_max, n_quiets, improving_flag, eval_adj;
    int  uci_nr = 0;
    int  n_hist_pruned = 0;

    int  mc = 0;
    int  tt_noisy = 0;
    Move bad_captures[BAD_CAPTURES_MAX];
    int  bad_captures_see[BAD_CAPTURES_MAX], bad_count = 0;
    int  pvnode = beta > alpha + 1;
    int  eval_raw = 0;
    int  skip_quiets = 0;
    int      lazy_king_sq = -1;
    Bitboard lazy_king_diag = 0, lazy_king_orth = 0;

    int  w = -MATE_SCORE;
    int  tb_max = MATE_SCORE;
    Color us = pos->to_move;
    int  tt_d = 0, tt_flag = 0, tt_s = 0;
    bool tt_hit;
    bool    tt_found = false;
    TTProbe tt_so;
    Move tt_pv_move, mlist[MAX_MOVES], quiets[MAX_MOVES];
    Move tried_captures[MAX_MOVES];
    int  n_tried_captures = 0;
    bool   corr_active    = false;
    int    corr_eval = 0;
    CorrSlots  corr_slots;
    HashFlag flag;

    int  fs_best = -MATE_SCORE - 1, fs_alpha0 = alpha, fs_beta0 = beta;

    if (ply) pv_triangle[ply][ply] = empty_move();

    info->nodes++;
    (*node_counter_ptr)++;
    if (info->seldepth < ply) info->seldepth = ply;
    if (fast_check_stop()) {
        if (ply == 0 && root_interrupted_ptr) *root_interrupted_ptr = true;
        return alpha;
    }
    if (S2_STOP) {
        if (ply == 0 && root_interrupted_ptr) *root_interrupted_ptr = true;
        return alpha;
    }

    if (ply && (count_position_occurrences_max2(pos, pos->hash) >= 2
             || pos->halfmove_clock >= 100
             || has_insufficient_material(pos))) {
        return 1 - (int)(info->nodes & 2);
    }

    if (ply && alpha < 0 && pos->halfmove_clock >= 3 && has_upcoming_cycle(pos, ply)) {
        alpha = 0;
        if (alpha >= beta) return beta;
    }

    if (ply >= MAX_SEARCH_PLY) return FIFTY_MOVE_SCALE(pos, NODE_EVAL(pos, ply));
    double_ext_total[ply + 1] = double_ext_total[ply];
    stat_score_at_ply[ply] = 0;
    move_count_at_ply[ply]  = 0;

    if (d <= 0) return qsearch(pos, ch, ply, alpha, beta, info);

    if (alpha < -MATE_SCORE + ply)     alpha = -MATE_SCORE + ply;
    if (beta  >  MATE_SCORE - ply - 1) beta  =  MATE_SCORE - ply - 1;
    if (alpha >= beta) return alpha;

    tt_pv_move = (ply == 0) ? pv_triangle[0][0] : empty_move();

    tt_found = tt_probe_entry(&main_tt, pos->hash, ply, &tt_so);
    tt_hit   = tt_so.result_bb;
    if (tt_hit) { tt_d = tt_so.depth; tt_flag = tt_so.flag; tt_s = tt_so.score; }

    if (tt_hit && !excl && ply > 0 && tt_d >= d) {

        if ((tt_flag == HASH_BETA  || tt_flag == HASH_EXACT) && tt_s >= beta) return tt_s;
        if ((tt_flag == HASH_ALPHA || tt_flag == HASH_EXACT) && tt_s <= alpha) return tt_s;
    }

    if (ply && !excl && syzygy_limit > 0) {
        int tb_wdl;
        if (syzygy_probe_wdl(pos, d, &tb_wdl)) {
            int tb_s = tb_wdl > 0 ? SYZYGY_WIN_SCORE - ply : tb_wdl < 0 ? -SYZYGY_WIN_SCORE + ply : 0;
            HashFlag tb_f = tb_wdl > 0 ? HASH_BETA : tb_wdl < 0 ? HASH_ALPHA : HASH_EXACT;
            if (tb_f == HASH_EXACT || (tb_f == HASH_BETA ? tb_s >= beta : tb_s <= alpha)) {
                store_hash(&main_tt, pos->hash, d + 6 < MAX_DEPTH ? d + 6 : MAX_DEPTH, tb_s, tb_f, empty_move(), ply);
                return tb_s >= beta ? beta : tb_s <= alpha ? alpha : tb_s;
            }

            if (pvnode) {

                if (tb_f == HASH_BETA) { if (tb_s > alpha) alpha = fs_best = tb_s; }
                else tb_max = tb_s;
            }
        }
    }
    fs_alpha0 = alpha;
    fs_beta0 = beta;
    if (!excl && is_no_move(tt_pv_move)) {

        if (tt_found && tt_so.this_move) tt_pv_move = unpack_move(tt_so.this_move);
    }

    if (ch) eval_adj = -MATE_SCORE + ply;
    else {
        if (tt_found && tt_so.eval != TT_EVAL_NONE) eval_adj = tt_so.eval;
        else {
            eval_adj = NODE_EVAL(pos, ply);
            if (ply && !null_at_ply[ply - 1]) eval_adj -= stat_score_at_ply[ply - 1] / RED_EV_DIVISOR;
            if (!excl) tt_store_eval(&main_tt, pos->hash, eval_adj);
        }

        corr_active     = true;
        eval_adj = corr_eval = corr_clamp(eval_adj + corr_compute(pos, ply, us, &corr_slots));
        eval_raw = eval_adj;
        if (tt_hit && (tt_flag == HASH_EXACT
                       || (tt_s > eval_adj ? tt_flag == HASH_BETA : tt_flag == HASH_ALPHA)))
            eval_adj = tt_s;
    }
    eval_stack_ply[ply] = eval_adj;

    if (!excl) raw_eval_at_ply[ply] = !ch ? eval_raw : (ply >= 2 ? raw_eval_at_ply[ply - 2] : PS_NO_EVAL);
    if (ply && parent_lmr_reduction[ply - 1]) {
        const int ps_pr = parent_lmr_reduction[ply - 1];
        parent_lmr_reduction[ply - 1] = 0;
        if (!excl && raw_eval_at_ply[ply] != PS_NO_EVAL && raw_eval_at_ply[ply - 1] != PS_NO_EVAL) {
            const int raw_eval_sum = raw_eval_at_ply[ply] + raw_eval_at_ply[ply - 1];
            if (ps_pr >= 3 && raw_eval_sum <= 0) d++;
            if (ps_pr >= 2 && d >= 2 && raw_eval_sum > PS_REDUCTION_MARGIN) d--;
        }
    }

    if (!ch && !pvnode && beta > -MATE_SCORE + 500) {

        if (d == 1 && eval_raw + RAZOR_MARGIN_CORE <= alpha) return qsearch(pos, false, ply, alpha, beta, info);

        if (d <= RFP_D && eval_adj - RFP_MULT * d > beta)
            return eval_adj < TB_WIN_ZONE ? eval_adj - RFP_MULT * d : beta;
    }

    null_ok = null_ok && !ch && !pvnode && !excl && ply >= nmp_verify_ply && beta > -MATE_SCORE + 500
                      && d >= 3 && eval_adj >= beta && eval_adj >= eval_raw
                      && (ply < 2 || !null_at_ply[ply - 2]);
    null_ok = null_ok && (!ply || stat_score_at_ply[ply - 1] < RED_NMP_ST);
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
        nnue_after_null(nnue_thread, ply + 1);
        w = -search_node(pos, false, d - R, ply + 1, -beta, 1 - beta, 0, NULL, info CUTNODE_PASS(!cut));
        null_at_ply[ply] = 0;

        pos->to_move = org_side;
        pos->en_passant_square = org_ep;
        pos->hash = org_hash;

        if (!(S2_STOP) && w < -MATE_SCORE + 500 && ply && reduced_child_at_ply[ply - 1]
                && side_piece_value_cp(pos, (Color)(1 - (int)us)) < REFINE_K3_THRESHOLD) return alpha;

        if (!(S2_STOP) && w >= beta) {
            {

            if (nmp_verify_ply || (d <= 12 && beta < MATE_SCORE - 500)) return w < TB_WIN_ZONE ? w : beta;
            nmp_verify_ply = ply + (d - R) * 3 / 4;
            w = search_node(pos, false, d - R, ply, beta - 1, beta, 0, NULL, info CUTNODE_PASS(0));
            nmp_verify_ply = 0;
            if (S2_STOP) return alpha;
            if (w >= beta) return beta;
            }
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
                PREFETCH_BEFORE_MOVE(pm);
                make_move_full(pos, &pm, &pu);
                nnue_after_move(nnue_thread, ply + 1, pos);
                PREFETCH_AFTER_MOVE();
                pch = is_in_check(pos, pos->to_move);
                pw  = -search_node(pos, pch, 0, ply + 1, -pcb, -pcb + 1, 1, NULL, info CUTNODE_PASS(!cut));
                if (pw >= pcb)
                    pw = -search_node(pos, pch, d - 4, ply + 1, -pcb, -pcb + 1, 1, NULL, info CUTNODE_PASS(!cut));
                undo_move_full(pos, &pm, &pu);
                if (S2_STOP) return alpha;
                if (pw >= pcb) {
                    store_hash(&main_tt, pos->hash, d - 3, pw, HASH_BETA, pm, ply);
                    return pw;
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
    Bitboard escape_known_bb = 0, escape_flag_bb = 0;
    n_quiets  = 0;
    (void)n_hist_pruned;

    if (ch) {
        nall   = generate_legal_evasions(pos, mlist);
        nnoisy = nall;
    } else
    {

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

    for (n = STAGE_HASH; n <= (ch ? STAGE_CAPTURES : STAGE_BAD_CAPTURES); n++) {
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
                const int sv = search_node(pos, ch, d / 2 + 1, ply, sb - 1, sb, 0, &tt_pv_move, info CUTNODE_PASS(cut));
                if (sv < sb) {
                    se = 1;

                    if (!pvnode && double_ext_total[ply] < DOUBLE_EXT_LIMIT && !(S2_STOP)) {
                        int sb2 = sb - DOUBLE_EXT_MARGIN;
                        if (search_node(pos, ch, d / 2 + 1, ply, sb2 - 1, sb2, 0, &tt_pv_move, info CUTNODE_PASS(cut)) < sb2) {
                            int sb3 = sb - TRIPLE_EXT_MARGIN;
                            se = 2;
                            if (tt_pv_move.captured == EMPTY && tt_pv_move.promotion == EMPTY
                                    && !(S2_STOP)
                                    && search_node(pos, ch, d / 2 + 1, ply, sb3 - 1, sb3, 0, &tt_pv_move, info CUTNODE_PASS(cut)) < sb3)
                                se = 3;
                        }
                    }
                } else if (sb >= beta) {
                    if (S2_STOP) return alpha;
                    return sv < MATE_SCORE - 500 ? sv : beta;
                } else if (tt_s >= beta)
                    se = (beta < MATE_SCORE - 500 && beta > -MATE_SCORE + 500) ? -2 : -1;
                nd += se;
            }
        } else if (n == STAGE_CAPTURES) {
            lo = 0;      hi = nnoisy;
        } else if (n == STAGE_BAD_CAPTURES) {
            lo = 0;      hi = bad_count;
        } else {
            lo = nnoisy; hi = nall;
        }

        for (i = lo; i < hi; i++) {
            Move     m;
            UndoInfo u;
            int      quiet, ext = 0, see = 0, is_first_pv;
            int      escapes = 0;
            int      lmr;
            int      lmr_skip_reduce = 0;
            int      see_ready = 0;

            int      cont_sum_cache = 0, cont_sum_ready = 0;
#  define LAZY_CONT_SUM() (cont_sum_ready ? cont_sum_cache : (cont_sum_ready = 1, cont_sum_cache = cont_hist_sum(ply, us, m, 1)))
            bool     gives_check_now;
            int      gives_check_cache = -1;

            if      (n == STAGE_HASH)  m = tt_pv_move;
            else if (n == STAGE_CAPTURES && ch) m = pick_evasion(mlist, nnoisy, i, ply, us, 1);
            else if (n == STAGE_CAPTURES) m = pick_capture(mlist, nnoisy, i CAPTURE_PICK_PASS);
            else if (n == STAGE_BAD_CAPTURES)   { m = bad_captures[i]; see = bad_captures_see[i]; see_ready = 1; }
            else                    m = pick_quiet(mlist, nall, i, ply, us PAWN_HIST_PASS);

            if ((n != STAGE_HASH && same_move(m, tt_pv_move)) || (excl && same_move(m, *excl))) continue;

            if (n == STAGE_CAPTURES && !ch && m.promotion == EMPTY && piece_value_cp[m.piece] >= piece_value_cp[m.captured]) {
                int bad_capture_threshold = -(7 * see_victim_value[m.captured] + S2_CH(m) * FB_SPLIT_H / 1024) / 18;
                see = static_exchange_evaluation(pos, &m);
                see_ready = 1;
                if (see < bad_capture_threshold && bad_count < BAD_CAPTURES_MAX && !refine_k5_good_capture(pos, &m, see, us)) { bad_captures[bad_count] = m; bad_captures_see[bad_count++] = see; continue; }
            }

            if (!ch && !is_legal_fast_pub(pos, &m, &legal_buf)) continue;

            quiet = (m.captured == EMPTY && m.promotion == EMPTY);

            if (ply == 0 && (is_root_excluded(&m) || !is_root_included(&m))) continue;
            mc++;

            if (skip_quiets && quiet && !LMP_EXEMPT_MOVE(m)) { QUIET_FLOOR_MARK(1); n = STAGE_QUIETS; break; }
            if (ply > 0 && !ch && first != NO_MOVE_YET && alpha > -MATE_SCORE + 500 && d <= LMP_D
                    && mc >= (improving_flag ? (LMP_I_A + LMP_I_B * d * d) / 16 : (LMP_N_A + LMP_N_B * d * d) / 16)
               ) skip_quiets = 1;

            if (ply > 0 && !ch && quiet && first != NO_MOVE_YET
                    && alpha > -MATE_SCORE + 500 && alpha < MATE_SCORE - 500 && US_HAS_PIECES()) {
                int ff_hk = (S2_HR(quiet_history[history_index(us, m)]) + LAZY_CONT_SUM()) / LMR_HDIV;
                int ff_ld, ff_score;
                if (ff_hk < -3) ff_hk = -3; else if (ff_hk > 3) ff_hk = 3;
                ff_ld = d - 1 - LMR_BASE(1) + ff_hk;
                ff_score  = eval_adj + FF2_A + FF2_B * ff_ld + FF2_C * (eval_adj > alpha);
                if (ff_ld < FF2_LD && ff_score <= alpha && !LAZY_GIVES_CHECK()) {
                    if (fs_best <= ff_score && fs_best > -TB_WIN_ZONE && fs_best < TB_WIN_ZONE && ff_score < TB_WIN_ZONE) fs_best = ff_score;
                    QUIET_FLOOR_MARK(1); continue;
                }
            }

            if (ply > 0 && !ch && m.captured != EMPTY && m.promotion == EMPTY && first != NO_MOVE_YET
                    && alpha > -MATE_SCORE + 500 && alpha < MATE_SCORE - 500 && US_HAS_PIECES()) {
                const int fb_ld = d - 1 - LMR_BASE(0);
                int fb_score = eval_adj + FB_A + FB_B * fb_ld + FB_VICTIM_VALUE(m.captured);
                fb_score += S2_HR(capture_history[(int)us][(int)m.piece][m.to][(int)m.captured]) * FB_CH_FUT / 1024;
                if (fb_ld < FB_LD && fb_score <= alpha && !(m.is_en_passant ? gives_check(pos, &m) : LAZY_GIVES_CHECK())) continue;
            }

            if (ply > 0 && first != NO_MOVE_YET && alpha > -MATE_SCORE + 500 && quiet && d <= 4
                    && LAZY_CONT_SUM() PAWN_HIST_PRUNE_TERM(m) < CHP_A - CHP_B * (d - 1)
                    && !LAZY_GIVES_CHECK()
               ) { QUIET_FLOOR_MARK(1); continue; }

            if (n != STAGE_HASH && ply > 0 && first != NO_MOVE_YET && alpha > -MATE_SCORE + 500

                    && d <= 12 && GAIN_LOWER_BOUND(m) < (quiet ? -SEE_Q * d : -SEE_B * d * d - S2_CH(m) * FB_SEE_H / 1024)
                    && LAZY_SEE() < (quiet ? -SEE_Q * d : -SEE_B * d * d - S2_CH(m) * FB_SEE_H / 1024)
                    && !(quiet && see >= -CHECK_SEE_MARGIN * d && LAZY_GIVES_CHECK())
               ) { QUIET_FLOOR_MARK(quiet); continue; }

            const int hist_prune_ok = !(pvnode || ch || (m.piece == PAWN && is_passed_pawn(pos, m.to, us)))
                             && n == STAGE_QUIETS && !IS_KILLER_OR_COUNTER(m, ply);
            const int hist_prune_score = hist_prune_ok ? quiet_history_score(us, m, ply) PAWN_HIST_ORDER_TERM(m) LOW_PLY_HIST_ORDER_TERM(m) : 0;
            if (hist_prune_ok && hist_prune_score <= hist_max && d < 6 && FIRST_MOVE_DONE && (hist_prune_score < 0 || hist_prune_score * hist_prune_score < hist_max)
                    && !LAZY_GIVES_CHECK()) {
                n_hist_pruned++;
                QUIET_FLOOR_MARK(1); continue;
            }

            if (quiet && d >= 3 && !m.is_castling) {
                Move rev_move = m;
                rev_move.from = m.to; rev_move.to = m.from;
                {
                    const Bitboard from_bit_bb = 1ULL << m.from;
                    if (!(escape_known_bb & from_bit_bb)) { escape_known_bb |= from_bit_bb; if (static_exchange_evaluation(pos, &rev_move) < 0) escape_flag_bb |= from_bit_bb; }
                    escapes = (escape_flag_bb & from_bit_bb) != 0;
                }
            }

            if (n != STAGE_HASH && m.piece == PAWN && m.promotion == EMPTY && (m.to >> 3) == (us == WHITE ? 6 : 1))
                (void)LAZY_SEE();
            PREFETCH_BEFORE_MOVE(m);

            const int pawn_push_seventh = n == STAGE_HASH && !sing && m.piece == PAWN && m.promotion == EMPTY
                                 && (m.to >> 3) == (us == WHITE ? 6 : 1);
            int ext_see = (n == STAGE_HASH) ? (pawn_push_seventh ? static_exchange_evaluation(pos, &m) : 0) : see;
            move_stack[ply] = m;

            stat_score_at_ply[ply] = quiet ? S2_HR(quiet_history[history_index(us, m)]) + LAZY_CONT_SUM() - RED_ST_OFF : 0;
            move_count_at_ply[ply]  = mc;
            if (ply == 0 && !excl) uci_report_currmove(&m, ++uci_nr);
            const long long effort_nodes_before = info->nodes;
            make_move_full(pos, &m, &u);
            nnue_after_move(nnue_thread, ply + 1, pos);
            PREFETCH_AFTER_MOVE();
            if (quiet) quiets[n_quiets++] = m;
            else tried_captures[n_tried_captures++] = m;
            gives_check_now = is_in_check(pos, pos->to_move);

            if (gives_check_now && !sing && side_piece_value_cp(pos, us) - piece_value_cp[m.promotion] > REFINE_K4_THRESHOLD
                    && (n == STAGE_HASH ? !pawn_push_seventh : !see_ready)) {
                undo_move_full(pos, &m, &u);
                ext_see = (n == STAGE_HASH) ? static_exchange_evaluation(pos, &m) : LAZY_SEE();
                make_move_full(pos, &m, &u);
                nnue_after_move(nnue_thread, ply + 1, pos);
            }

            int ext_extra = 0;
            if (!sing) {

                if (gives_check_now && side_piece_value_cp(pos, us) - piece_value_cp[m.promotion] > REFINE_K4_THRESHOLD
                        && (ext_see >= 0 || !ext_direct_check(pos, &m))) ext_extra = 1;
                if (!ext_extra && m.captured != EMPTY && m.captured != PAWN && pos->mat_phase <= EXT_PHASE_MAX) ext_extra = 1;
                if (!ext_extra && m.piece == PAWN && m.promotion == EMPTY && (m.to >> 3) == (us == WHITE ? 6 : 1)
                        && ext_see >= 0 && is_passed_pawn(pos, m.to, us)) ext_extra = 1;
            }
            double_ext_total[ply + 1] = double_ext_total[ply] + (n == STAGE_HASH && se >= 2);

            lmr = 0;
            if (!(gives_check_now || pvnode || ch || (m.piece == PAWN && is_passed_pawn(pos, m.to, us)))
                    && n == STAGE_QUIETS && !IS_KILLER_OR_COUNTER(m, ply)) {
                const int his = hist_prune_score;
                if (his > hist_max) hist_max = his;
            }
            nd += ext_extra;
            const int lmr_skipped = !pvnode && mc == 1 && d >= 2;
            if ((d >= 3 && mc > 1 + 3 * pvnode) || lmr_skipped) {
                int q  = quiet ? 1 : 0;
                int dk = d < 255 ? d : 255, mk = mc < 255 ? mc : 255;

                int r  = (LMR_K0 + lmr_table[q][dk] * lmr_table[q][mk] + !improving_flag * LMR_NIMP
                          - (quiet ? 0 : S2_CH(m) * FB_LMR_H / 1024)) / 1024;

                int hk = quiet ? (S2_HR(quiet_history[history_index(us, m)]) + LAZY_CONT_SUM()) / LMR_HDIV : 0;
                r += !pvnode;
                r += cut;
                r += tt_noisy;
                r -= IS_KILLER_OR_COUNTER(m, ply);
                r -= escapes;
                if (hk < -3) hk = -3; else if (hk > 3) hk = 3;
                r -= hk;
                if (quiet && ply) {
                    if (stat_score_at_ply[ply] >= 0 && stat_score_at_ply[ply - 1] < 0) r--;
                    else if (stat_score_at_ply[ply - 1] >= 0 && stat_score_at_ply[ply] < 0) r++;
                }
                if (cut && d <= 10 && mc <= 2 && !ch) r--;
                if (ply && move_count_at_ply[ply - 1] > 13) r--;
                if (skip_quiets) r++;
                if (!quiet && d < 8 && mc > 2) r++;
                if (lmr_skipped) {
                    r += cut && d <= 10 && !ch;
                    r += LMR_SKIP_NO_TT_ADD * is_no_move(tt_pv_move);
                    lmr_skip_reduce = (r > LMR_SKIP_RED_THRESHOLD);
                } else {
                const int lmr_raw_r = r;
                if (r > nd - 1) r = nd - 1;
                if (r < 0) r = 0;
                ext -= r;
                if (cut && lmr_raw_r <= LMR_EXTEND_THRESHOLD) ext += 1;
                lmr = 1;
                }
            }

            is_first_pv = (first == NO_MOVE_YET && pvnode);
            if (!is_first_pv) {
                reduced_child_at_ply[ply] = ext < 0;
                parent_lmr_reduction[ply] = ext < 0 ? -ext : 0;
                w = -search_node(pos, gives_check_now, nd + ext - lmr_skip_reduce, ply + 1, -alpha - 1, -alpha, 1, NULL, info CUTNODE_PASS(CUTNODE_LMR));
                parent_lmr_reduction[ply] = 0;
                reduced_child_at_ply[ply] = 0;
            }
            int ps_pvd = nd;
            if (w > alpha && ext < 0) {

                {
                    const int ps_nd = ps_pvd = nd + (w > fs_best + PS_DEEPER_MARGIN) - (w < fs_best + PS_SHALLOWER_MARGIN);
                    if (ps_nd > nd + ext)
                        w = -search_node(pos, gives_check_now, ps_nd, ply + 1, -alpha - 1, -alpha, 1, NULL, info CUTNODE_PASS(!cut));
                }

                {
                    int b = S2_HB(d);
                    cont_hist_update(ply, us, m, w > alpha ? b : -b);
                }
            }
            if ((w > alpha && w < beta) || is_first_pv)
                w = -search_node(pos, gives_check_now, ps_pvd,   ply + 1, -beta,      -alpha, 0, NULL, info CUTNODE_PASS(0));

            undo_move_full(pos, &m, &u);
            const long long move_effort = info->nodes - effort_nodes_before;
            if (ply == 0 && !excl && EFFORT_AT_ROOT) effort_total += move_effort;
            nd -= ext_extra;

            if (S2_STOP) {
                if (ply == 0 && root_interrupted_ptr) *root_interrupted_ptr = true;
                return alpha;
            }
            if (w > fs_best) fs_best = w;

            if (w > alpha) {
                alpha = w;
                first = ALPHA_RAISED;
                pv_triangle[ply][ply] = m;
                for (j = ply + 1; j <= MAX_SEARCH_PLY; j++) {
                    pv_triangle[ply][j] = pv_triangle[ply + 1][j];
                    if (is_no_move(pv_triangle[ply][j])) break;
                }
                if (ply == 0 && !excl && uci_nr > 1 && w < beta) uci_report_pv(pos, info, w);
                if (ply == 0 && !excl && EFFORT_AT_ROOT) effort_best = move_effort;

                if (w < beta && d > 2 && d < PS_PV_DEPTH_MAX && w > -TB_WIN_ZONE && w < TB_WIN_ZONE) { d -= 2; nd -= 2; }

                if (w >= beta) {

                    if (!quiet) {
                        int cb = S2_HB(d);
                        int kb = FB_PENALTY_MULT * cb < S2_HS ? FB_PENALTY_MULT * cb : S2_HS;
                        int* c = &capture_history[(int)us][(int)m.piece][m.to][(int)m.captured];
                        int  k;
                        *c += cb - *c * cb / S2_HS;
                        for (k = 0; k < n_tried_captures - 1; k++) {
                            int* c2 = &capture_history[(int)us][(int)tried_captures[k].piece]
                                                 [tried_captures[k].to][(int)tried_captures[k].captured];
                            *c2 += -kb - *c2 * kb / S2_HS;
                        }
                    }
                    else {
                        int cb = S2_HB(d);
                        int kb = FB_PENALTY_MULT * cb < S2_HS ? FB_PENALTY_MULT * cb : S2_HS;
                        int k;
                        for (k = 0; k < n_tried_captures; k++) {
                            int* c2 = &capture_history[(int)us][(int)tried_captures[k].piece]
                                                 [tried_captures[k].to][(int)tried_captures[k].captured];
                            *c2 += -kb - *c2 * kb / S2_HS;
                        }
                    }
                    if (quiet) {
                        int his = S2_HB(d);
                        int ix  = history_index(us, m);
                        killer_table[ply] = m;
                        {
                            Move* cw = counter_slot(ply, us);
                            if (cw) *cw = m;
                        }
                        quiet_history[ix] += his - quiet_history[ix] * his / S2_HS;
                        if ((int)m.piece != PAWN && !m.is_castling) {
                            Move r = m;
                            int  ixr;
                            r.from = m.to; r.to = m.from;
                            ixr = history_index(us, r);
                            quiet_history[ixr] += -his - quiet_history[ixr] * his / S2_HS;
                        }
                        for (j = 0; j < n_quiets - 1; j++) {
                            int ix2 = history_index(us, quiets[j]);
                            quiet_history[ix2] += -his - quiet_history[ix2] * his / S2_HS;
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
    if (first == NO_MOVE_YET) fs_best = alpha;

    if (pvnode && fs_best > tb_max) fs_best = tb_max;
    flag = HASH_ALPHA;
    if (first != NO_MOVE_YET)
        flag = fs_best >= fs_beta0 ? HASH_BETA : fs_best > fs_alpha0 ? HASH_EXACT : HASH_ALPHA;
    if (first == ALPHA_RAISED) tt_pv_move = pv_triangle[ply][ply];
    alpha = fs_best;

    if (ply && flag == HASH_BETA && first != NO_MOVE_YET && fs_best < MATE_SCORE - 500
            && fs_beta0 > -MATE_SCORE + 500 && fs_beta0 < MATE_SCORE - 500)
        alpha = (fs_best * d + fs_beta0) / (d + 1);

    {
        const bool corr_had_move = (first == ALPHA_RAISED);
        if (corr_active && alpha > -TB_WIN_ZONE && alpha < TB_WIN_ZONE
            && !(corr_had_move && (pv_triangle[ply][ply].captured != EMPTY || pv_triangle[ply][ply].is_en_passant))
            && (corr_had_move ? alpha > corr_eval : (alpha < corr_eval))) {
            long long b = (long long)(alpha - corr_eval) * d * (corr_had_move ? CORR_BONUS_MOVE : CORR_BONUS_NO_MOVE)
                          * CORR_INPUT_NUM / CORR_INPUT_DEN;
            if (b >  CORR_LIMIT / 4) b =  CORR_LIMIT / 4;
            if (b < -CORR_LIMIT / 4) b = -CORR_LIMIT / 4;
            corr_learn(&corr_slots, 1061 * (int)b / 1024);
        }
    }
    if (!excl && !(ply == 0 && root_excluded_count > 0))
        store_hash(&main_tt, pos->hash, d, alpha, flag, tt_pv_move, ply);

    return alpha;
}

void search_new_game(void) {
    memset(quiet_history, 0, sizeof(quiet_history));
    memset(capture_history, 0, sizeof(capture_history));
    memset(cont_hist,      0, sizeof(cont_hist));
    memset(counter_table, 0, sizeof(counter_table));

    memset(corr_hist_pawn, 0, sizeof(corr_hist_pawn));
    memset(corr_hist_nonpawn,   0, sizeof(corr_hist_nonpawn));
    memset(corr_hist_cont, 0, sizeof(corr_hist_cont));
}

Move search_root(Position* pos, int depth, int alpha, int beta, SearchInfo* info,
                    long long* nodes_searched_ptr, bool* root_interrupted) {
    bool ch;
    char pv_str[1024] = "";

    node_counter_ptr    = nodes_searched_ptr;
    root_interrupted_ptr = root_interrupted;
    *root_interrupted = false;

    if (!nnue_thread && !(nnue_thread = nnue_thread_new())) {
        fprintf(stderr, "nnue: out of memory for thread accumulators\n");
        exit(1);
    }
    nnue_root(nnue_thread, pos);
    if (EFFORT_AT_ROOT) { effort_total = 0; effort_best = 0; }

    if (depth == 1) {
        memset(killer_table,  0, sizeof(killer_table));
        pv_triangle[0][0] = empty_move();
    }

    if (!lmr_ready) lmr_init();
    root_depth = depth;
    nmp_verify_ply = 0;
    ch = is_in_check(pos, pos->to_move);
    info->score = search_node(pos, ch, depth, 0, alpha, beta, 0, NULL, info CUTNODE_PASS(0));

    pv_to_string(pos, pv_str, sizeof(pv_str));
    strncpy(info->pv, pv_str, sizeof(info->pv) - 1);
    info->pv[sizeof(info->pv) - 1] = '\0';

    return pv_triangle[0][0];
}

void smp_init_shared_search(void) {
    if (!lmr_ready) lmr_init();
    if (!cuckoo_ready) cuckoo_init();
}

