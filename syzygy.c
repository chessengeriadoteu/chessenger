/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#include <stdio.h>
#include <string.h>
#include "syzygy.h"
#include "movegen.h"
#include "utils.h"
#include "fathom/tbprobe.h"

int syzygy_limit = 0;
long long syzygy_hits = 0;
static int limit_uci = 7;
static int probe_depth_uci = 1;
static bool rule50_enabled = true;
static bool loaded = false;
#if TB_EMBEDDED || TB_NEXT_TO_ENGINE

static bool embedded_on = false;
static bool embedded_paused = false;
#endif

static void recompute_limit(void) {
    int n = loaded ? (int)TB_LARGEST : 0;
#if TB_EMBEDDED || TB_NEXT_TO_ENGINE
    if (embedded_on && embedded_paused) n = 0;
#endif
    syzygy_limit = n < limit_uci ? n : limit_uci;
}

#if TB_EMBEDDED

static void embedded_enable(void) {
    tb_from_memory = true;
    loaded = tb_init("(embedded)");
    embedded_on = loaded && TB_LARGEST > 0;
    embedded_paused = false;
}
#elif TB_NEXT_TO_ENGINE

static char beside[1024] = "";
static int dir_pieces = 0;

void syzygy_dir_set(const char* dir_path) { snprintf(beside, sizeof(beside), "%s", dir_path ? dir_path : ""); }
const char* syzygy_dir_info(void) { return beside; }
int syzygy_dir_pieces(void) { return dir_pieces; }

static void embedded_enable(void) {
    loaded = tb_init(beside);
    embedded_on = loaded && TB_LARGEST > 0;
    dir_pieces = embedded_on ? (int)TB_LARGEST : 0;
    embedded_paused = false;
}
#endif
#if TB_EMBEDDED || TB_NEXT_TO_ENGINE

void syzygy_embedded_start(void) {
    embedded_enable();
    recompute_limit();
}

void syzygy_embedded_pause(bool paused) {
    embedded_paused = paused;
    recompute_limit();
}
#endif

void syzygy_set_path(const char* tb_path) {

    if (tb_path == NULL || tb_path[0] == '\0' || strcmp(tb_path, "<empty>") == 0) {

#if TB_EMBEDDED

        embedded_enable();
        printf("info string SyzygyPath: built-in tablebases up to %u pieces\n", loaded ? TB_LARGEST : 0);
        fflush(stdout);
#elif TB_NEXT_TO_ENGINE

        embedded_enable();
        if (embedded_on)
            printf("info string SyzygyPath: tablebases next to the engine (%s) up to %u pieces\n", beside, TB_LARGEST);
        else
            printf("info string SyzygyPath: no tablebases next to the engine (%s), tablebases off\n", beside);
        fflush(stdout);
#else
        loaded = false;
#endif
    } else {
#if TB_EMBEDDED
        tb_from_memory = false;
        embedded_on = false;
#elif TB_NEXT_TO_ENGINE
        embedded_on = false;
#endif
        loaded = tb_init(tb_path);
        if (!loaded) {
            printf("info string SyzygyPath: tb_init failed for `%s`, tablebases off\n", tb_path);
        } else if (TB_LARGEST == 0) {
            printf("info string SyzygyPath: no tablebase files found in `%s`\n", tb_path);
        } else {
            printf("info string SyzygyPath: tablebases up to %u pieces loaded\n", TB_LARGEST);
        }
        fflush(stdout);
    }
    recompute_limit();
}

void syzygy_set_limit(int limit) {
    limit_uci = limit < 0 ? 0 : (limit > 7 ? 7 : limit);
#if TB_EMBEDDED || TB_NEXT_TO_ENGINE
    embedded_paused = false;
#endif
    recompute_limit();
}

void syzygy_set_depth(int depth_arg) {
    probe_depth_uci = depth_arg < 1 ? 1 : (depth_arg > 100 ? 100 : depth_arg);
}

void syzygy_set_rule50(bool enabled) { rule50_enabled = enabled; }

int syzygy_largest(void) { return loaded ? (int)TB_LARGEST : 0; }

#define SYZ_ARGS(pos)                                                                   \
    (pos)->occupied[WHITE], (pos)->occupied[BLACK],                                     \
    (pos)->pieces[WHITE][KING]   | (pos)->pieces[BLACK][KING],                          \
    (pos)->pieces[WHITE][QUEEN]  | (pos)->pieces[BLACK][QUEEN],                         \
    (pos)->pieces[WHITE][ROOK]   | (pos)->pieces[BLACK][ROOK],                          \
    (pos)->pieces[WHITE][BISHOP] | (pos)->pieces[BLACK][BISHOP],                        \
    (pos)->pieces[WHITE][KNIGHT] | (pos)->pieces[BLACK][KNIGHT],                        \
    (pos)->pieces[WHITE][PAWN]   | (pos)->pieces[BLACK][PAWN]

static int wdl_class(unsigned wdl) {
    if (wdl == TB_WIN  || (!rule50_enabled && wdl == TB_CURSED_WIN))   return 1;
    if (wdl == TB_LOSS || (!rule50_enabled && wdl == TB_BLESSED_LOSS)) return -1;
    return 0;
}

static bool can_probe(const Position* pos) {
    if (__builtin_popcountll(pos->pieces[WHITE][KING]) != 1 ||
        __builtin_popcountll(pos->pieces[BLACK][KING]) != 1) return false;
    return !is_in_check((Position*)pos, pos->to_move == WHITE ? BLACK : WHITE);
}

bool syzygy_probe_wdl(const Position* pos, int depth, int* wdl) {
    if (pos->castling_rights != 0 || pos->halfmove_clock != 0) return false;
    int piece_count = __builtin_popcountll(pos->all_pieces);
    if (piece_count > syzygy_limit) return false;
    if (piece_count == syzygy_limit && depth < probe_depth_uci) return false;
    if (!can_probe(pos)) return false;
    unsigned r = tb_probe_wdl(SYZ_ARGS(pos), 0, 0,
                              pos->en_passant_square < 0 ? 0 : (unsigned)pos->en_passant_square,
                              pos->to_move == WHITE);
    if (r == TB_RESULT_FAILED) return false;
    __atomic_fetch_add(&syzygy_hits, 1, __ATOMIC_RELAXED);
    *wdl = wdl_class(r);
    return true;
}

static PieceType promo_piece(unsigned p) {
    switch (p) {
        case TB_PROMOTES_QUEEN:  return QUEEN;
        case TB_PROMOTES_ROOK:   return ROOK;
        case TB_PROMOTES_BISHOP: return BISHOP;
        case TB_PROMOTES_KNIGHT: return KNIGHT;
        default:                 return EMPTY;
    }
}

static bool result_to_move(unsigned r, const Move* tb_legal_moves, int n, Move* out) {
    int z = (int)TB_GET_FROM(r), to_sq = (int)TB_GET_TO(r);
    PieceType pr = promo_piece(TB_GET_PROMOTES(r));
    for (int i = 0; i < n; i++) {
        if (tb_legal_moves[i].from == z && tb_legal_moves[i].to == to_sq && tb_legal_moves[i].promotion == (int)pr) {
            *out = tb_legal_moves[i];
            return true;
        }
    }
    return false;
}

int syzygy_root(Position* pos, Move* mlist, int max, int* wdl) {
    if (syzygy_limit == 0 || pos->castling_rights != 0) return 0;
    if (__builtin_popcountll(pos->all_pieces) > syzygy_limit) return 0;
    if (!can_probe(pos)) return 0;
    static unsigned tb_results[TB_MAX_MOVES];
    unsigned best = tb_probe_root(SYZ_ARGS(pos), (unsigned)pos->halfmove_clock, 0,
                                  pos->en_passant_square < 0 ? 0 : (unsigned)pos->en_passant_square,
                                  pos->to_move == WHITE, tb_results);
    if (best == TB_RESULT_FAILED || best == TB_RESULT_CHECKMATE || best == TB_RESULT_STALEMATE) return 0;
    syzygy_hits++;

    Move tb_legal_moves[MAX_MOVES];
    int n = generate_legal_moves(pos, tb_legal_moves);
    int k = wdl_class(TB_GET_WDL(best));
    *wdl = k;
    int n_moves = 0;
    if (k != 0) {

        if (max > 0 && result_to_move(best, tb_legal_moves, n, &mlist[0])) n_moves = 1;
    } else {

        for (int i = 0; i < TB_MAX_MOVES && tb_results[i] != TB_RESULT_FAILED && n_moves < max; i++) {
            if (wdl_class(TB_GET_WDL(tb_results[i])) == 0 && result_to_move(tb_results[i], tb_legal_moves, n, &mlist[n_moves])) n_moves++;
        }
    }
    return n_moves;
}
