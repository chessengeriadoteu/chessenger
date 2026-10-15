/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 * Copyright (C) 2019-2025 Morgan Houppin (Stash 37)
 * Portions of this file are adapted from Stash 37. Modified by Dariusz Domagała, 2026-10-15.
 */
#include "tt.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <malloc.h>
#endif

uint64_t zobrist_pieces[2][7][64];
uint64_t zobrist_castling[16];
uint64_t zobrist_en_passant[64];
uint64_t zobrist_side_to_move;

TranspositionTable main_tt = {0};

static uint64_t random_seed = 1;

static uint64_t random_uint64(void) {
    random_seed ^= random_seed >> 12;
    random_seed ^= random_seed << 25;
    random_seed ^= random_seed >> 27;
    return random_seed * 0x2545F4914F6CDD1DULL;
}

void init_zobrist(void) {

    random_seed = 0x9E3779B97F4A7C15ULL;

    for (int color = 0; color < 2; color++) {
        for (int piece = 0; piece < 7; piece++) {
            for (int square = 0; square < 64; square++) {
                zobrist_pieces[color][piece][square] = random_uint64();
            }
        }
    }

    for (int i = 0; i < 16; i++) {
        zobrist_castling[i] = random_uint64();
    }

    for (int i = 0; i < 64; i++) {
        zobrist_en_passant[i] = random_uint64();
    }

    zobrist_side_to_move = random_uint64();
}

uint64_t compute_hash(Position* pos) {
    uint64_t hash = 0;

    for (int color = 0; color < 2; color++) {
        for (int piece = 0; piece < 7; piece++) {
            Bitboard pieces = pos->pieces[color][piece];
            while (pieces) {
                int square = get_lsb(pieces);
                hash ^= zobrist_pieces[color][piece][square];
                pop_lsb(&pieces);
            }
        }
    }

    hash ^= zobrist_castling[pos->castling_rights];

    if (pos->en_passant_square != -1) {
        hash ^= zobrist_en_passant[pos->en_passant_square];
    }

    if (pos->to_move == BLACK) {
        hash ^= zobrist_side_to_move;
    }

    return hash;
}

_Static_assert((uint64_t)TT_HASH_MB_MAX * 1024 * 1024 / 16 <= 0x7FFFFFFF,
               "TT_HASH_MB_MAX: the number of entries must fit in `int` (`size`, `mask`)");

void init_tt(TranspositionTable* tt, int size_mb) {

    int entries = (int)(((uint64_t)size_mb * 1024 * 1024) / sizeof(HashEntry));

    int size = 1;
    while (size <= entries / 2) {
        size *= 2;
    }

    {
        void* p = NULL;
#ifdef _WIN32
        p = _aligned_malloc((size_t)size * sizeof(HashEntry), 64);
#else
        if (posix_memalign(&p, 64, (size_t)size * sizeof(HashEntry)) != 0) p = NULL;
#endif
        if (p) memset(p, 0, (size_t)size * sizeof(HashEntry));
        tt->entries = p;
    }
    if (!tt->entries) {
        tt->size = 0;
        tt->mask = 0;
        return;
    }

    tt->size = size;
    tt->mask = size - 1;
    tt->age = 0;
}

void clear_tt(TranspositionTable* tt) {
    if (tt->entries) {
        memset(tt->entries, 0, tt->size * sizeof(HashEntry));
    }
    tt->age = 0;
}

int hashfull_tt(const TranspositionTable* tt) {
    if (!tt || !tt->entries || tt->size <= 0) return 0;
    int sample_n = tt->size < 1000 ? tt->size : 1000;
    int occ_all = 0;
    for (int i = 0; i < sample_n; i++) {

        if (__atomic_load_n(&tt->entries[i].key, __ATOMIC_RELAXED) != 0) occ_all++;
    }
    return (int)(((long long)occ_all * 1000) / sample_n);
}

size_t resize_tt(TranspositionTable* tt, int size_mb) {
    if (size_mb < TT_HASH_MB_MIN) size_mb = TT_HASH_MB_MIN;
    if (size_mb > TT_HASH_MB_MAX) size_mb = TT_HASH_MB_MAX;

    free_tt(tt);
    init_tt(tt, size_mb);

    if (!tt->entries) return 0;
    return (size_t)tt->size * sizeof(HashEntry);
}

void free_tt(TranspositionTable* tt) {
    if (tt->entries) {
#ifdef _WIN32
        _aligned_free(tt->entries);
#else
        free(tt->entries);
#endif
        tt->entries = NULL;
    }
    tt->size = 0;
    tt->mask = 0;
    tt->age = 0;
}

#define MATE_DISTANCE_BOUND (MAX_DEPTH * 4)

static int score_to_tt(int score, int ply) {
    if (score >= MATE_SCORE - MATE_DISTANCE_BOUND)  return score + ply;
    if (score <= -MATE_SCORE + MATE_DISTANCE_BOUND) return score - ply;
    return score;
}

static int score_from_tt(int score, int ply) {
    if (score >= MATE_SCORE - MATE_DISTANCE_BOUND)  return score - ply;
    if (score <= -MATE_SCORE + MATE_DISTANCE_BOUND) return score + ply;
    return score;
}

uint16_t pack_move(Move m) {
    if (m.from == m.to) return 0;
    return (uint16_t)(((unsigned)m.from & 0x3Fu)
                    | (((unsigned)m.to & 0x3Fu) << 6)
                    | (((unsigned)m.promotion & 0x7u) << 12));
}

Move unpack_move(uint16_t packed) {
    Move m = {0};
    m.from      = (int)(packed & 0x3Fu);
    m.to        = (int)((packed >> 6) & 0x3Fu);
    m.promotion = (PieceType)((packed >> 12) & 0x7u);
    return m;
}

_Static_assert(sizeof(HashEntry) == 16, "HashEntry must be 16 B - changing the field layout costs table entries");

static inline HashEntry* tt_bucket(const TranspositionTable* tt, uint64_t key) {
    return &tt->entries[(key & (uint64_t)tt->mask) & ~(uint64_t)(TT_BUCKET - 1)];
}
#define TT_AGE_MASK 63
static inline int tt_age(const TranspositionTable* tt) { return tt->age & TT_AGE_MASK; }
static inline int tt_bound(const HashEntry* e)         { return e->genflag & 3; }
static inline int tt_victim_score(const HashEntry* e, int cur_age) {
    return (int)e->depth - 4 * ((cur_age - ((e->genflag >> 2) & TT_AGE_MASK)) & TT_AGE_MASK);
}

#define TT_KEEP_PLIES 3
static inline int tt_keep_old(const TranspositionTable* tt, const HashEntry* old_entry, uint64_t key, int depth, HashFlag flag) {
    return old_entry->key == key && tt_bound(old_entry) != HASH_NONE
        && ((old_entry->genflag >> 2) & TT_AGE_MASK) == tt_age(tt)
        && flag != HASH_EXACT && depth + TT_KEEP_PLIES < (int)old_entry->depth;
}

typedef uint64_t __attribute__((may_alias)) tt_word64;
_Static_assert(offsetof(HashEntry, score) == 8, "TT entry data starts at byte 8");

static inline void tt_read(HashEntry* e, HashEntry* buf_copy) {
    const uint64_t k = __atomic_load_n((tt_word64*)(void*)e,     __ATOMIC_RELAXED);
    const uint64_t d = __atomic_load_n((tt_word64*)(void*)e + 1, __ATOMIC_RELAXED);
    memcpy((char*)buf_copy + 8, &d, 8);
    buf_copy->key = k ^ d;
}

static inline void tt_write(HashEntry* e, const HashEntry* new_entry) {
    uint64_t d;
    memcpy(&d, (const char*)new_entry + 8, 8);
    __atomic_store_n((tt_word64*)(void*)e + 1, d,             __ATOMIC_RELAXED);
    __atomic_store_n((tt_word64*)(void*)e,     new_entry->key ^ d, __ATOMIC_RELAXED);
}

static bool tt_find(TranspositionTable* tt, uint64_t key, HashEntry* w) {
    if (!tt->entries || tt->size == 0) return false;
    HashEntry* k = tt_bucket(tt, key);
    for (int i = 0; i < TT_BUCKET; i++) {
        tt_read(&k[i], w);
        if (w->key == key) {
            const uint8_t g = (uint8_t)((w->genflag & 0) | (tt_age(tt) << 2) | tt_bound(w));
            if (g != w->genflag) { w->genflag = g; tt_write(&k[i], w); }
            return true;
        }
        if (w->key == 0) return false;
    }
    return false;
}

static HashEntry* tt_slot(TranspositionTable* tt, uint64_t key, HashEntry* old_entry) {
    HashEntry* k = tt_bucket(tt, key);
    HashEntry w[TT_BUCKET];
    for (int i = 0; i < TT_BUCKET; i++) {
        tt_read(&k[i], &w[i]);
        if (w[i].key == key || w[i].key == 0) { *old_entry = w[i]; return &k[i]; }
    }
    const int cur_age = tt_age(tt);
    int o = 0;
    for (int i = 1; i < TT_BUCKET; i++)
        if (tt_victim_score(&w[i], cur_age) < tt_victim_score(&w[o], cur_age)) o = i;
    *old_entry = w[o];
    return &k[o];
}

void store_hash(TranspositionTable* tt, uint64_t key, int depth, int score, HashFlag flag, Move best_move, int ply) {
    store_hash_eval(tt, key, depth, score, flag, best_move, ply, TT_EVAL_NONE);
}

void store_hash_eval(TranspositionTable* tt, uint64_t key, int depth, int score, HashFlag flag, Move best_move, int ply, int eval) {
    if (!tt->entries || tt->size == 0) return;
    if (score > MATE_SCORE || score < -MATE_SCORE) return;
    score = score_to_tt(score, ply);
    if (depth < 0)   depth = 0;
    if (depth > 255) depth = 255;

    HashEntry old_entry, new_entry;
    HashEntry* e = tt_slot(tt, key, &old_entry);
    if (tt_keep_old(tt, &old_entry, key, depth, flag)) return;
    if (eval == TT_EVAL_NONE && old_entry.key == key) eval = old_entry.eval;
    new_entry.key       = key;
    new_entry.score     = (int16_t)score;
    new_entry.eval      = (int16_t)eval;
    new_entry.best_move = pack_move(best_move);
    if (new_entry.best_move == 0 && old_entry.key == key) new_entry.best_move = old_entry.best_move;
    new_entry.depth     = (uint8_t)depth;
    new_entry.genflag   = (uint8_t)((tt_age(tt) << 2) | (int)flag);
    tt_write(e, &new_entry);
}

void tt_store_eval(TranspositionTable* tt, uint64_t key, int eval) {
    if (!tt->entries || tt->size == 0) return;
    HashEntry old_entry;
    HashEntry* e = tt_slot(tt, key, &old_entry);
    if (old_entry.key == key) { old_entry.eval = (int16_t)eval; tt_write(e, &old_entry); return; }
    HashEntry new_entry;
    new_entry.key       = key;
    new_entry.score     = 0;
    new_entry.eval      = (int16_t)eval;
    new_entry.best_move = 0;
    new_entry.depth     = 0;
    new_entry.genflag   = (uint8_t)((tt_age(tt) << 2) | HASH_NONE);
    tt_write(e, &new_entry);
}

bool tt_probe_entry(TranspositionTable* tt, uint64_t key, int ply, TTProbe* out) {
    HashEntry w;
    if (!tt_find(tt, key, &w)) { out->result_bb = false; out->eval = TT_EVAL_NONE; out->mv = 0; return false; }
    out->flag  = tt_bound(&w);
    out->result_bb = out->flag != HASH_NONE;
    out->depth = w.depth;
    out->score = score_from_tt(w.score, ply);
    out->eval  = w.eval;
    out->mv  = w.best_move;
    return true;
}

bool probe_hash(TranspositionTable* tt, uint64_t key, int depth, int alpha, int beta, int* score, Move* best_move, int ply) {
    HashEntry w;
    if (!tt_find(tt, key, &w)) return false;
    if (best_move && w.best_move != 0) *best_move = unpack_move(w.best_move);
    const int flag = tt_bound(&w);
    if (flag == HASH_NONE) return false;
    if (w.depth >= depth) {
        int s = score_from_tt(w.score, ply);
        if (flag == HASH_EXACT)                   { *score = s;     return true; }
        if (flag == HASH_ALPHA && s <= alpha)     { *score = alpha; return true; }
        if (flag == HASH_BETA  && s >= beta)      { *score = beta;  return true; }
    }
    return false;
}

bool probe_hash_entry(TranspositionTable* tt, uint64_t key, int* depth, int* flag, int* score, int ply) {
    HashEntry w;
    if (!tt_find(tt, key, &w) || tt_bound(&w) == HASH_NONE) return false;
    *depth = w.depth;
    *flag  = tt_bound(&w);
    *score = score_from_tt(w.score, ply);
    return true;
}

void tt_store_qs(TranspositionTable* tt, uint64_t key, int score, HashFlag flag, Move best_move, int ply) {
    if (!tt->entries || tt->size == 0) return;
    HashEntry* k = tt_bucket(tt, key);
    HashEntry w[TT_BUCKET];
    int o = -1;
    for (int i = 0; i < TT_BUCKET; i++) {
        tt_read(&k[i], &w[i]);
        if (w[i].key == key) {
            const uint8_t g = (uint8_t)((w[i].genflag & 0) | (tt_age(tt) << 2) | tt_bound(&w[i]));
            if (g != w[i].genflag) { w[i].genflag = g; tt_write(&k[i], &w[i]); }
            if (tt_bound(&w[i]) != HASH_NONE && w[i].depth > 0) return;
            o = i;
            break;
        }
        if (w[i].key == 0) { o = i; break; }
    }
    if (score > MATE_SCORE || score < -MATE_SCORE) return;
    if (o < 0) {
        const int cur_age = tt_age(tt);
        o = 0;
        for (int i = 1; i < TT_BUCKET; i++)
            if (tt_victim_score(&w[i], cur_age) < tt_victim_score(&w[o], cur_age)) o = i;
    }
    const HashEntry old_entry = w[o];
    HashEntry new_entry;
    score = score_to_tt(score, ply);
    if (tt_keep_old(tt, &old_entry, key, 0, flag)) return;
    new_entry.key       = key;
    new_entry.score     = (int16_t)score;
    new_entry.eval      = (int16_t)(old_entry.key == key ? old_entry.eval : TT_EVAL_NONE);
    new_entry.best_move = pack_move(best_move);
    if (new_entry.best_move == 0 && old_entry.key == key) new_entry.best_move = old_entry.best_move;
    new_entry.depth     = 0;
    new_entry.genflag   = (uint8_t)((tt_age(tt) << 2) | (int)flag);
    tt_write(&k[o], &new_entry);
}

bool tt_node_value(TranspositionTable* tt, uint64_t key, int* score, int ply) {
    HashEntry w;
    if (!tt_find(tt, key, &w) || tt_bound(&w) != HASH_EXACT) return false;
    int s = score_from_tt(w.score, ply);
    if (s >=  MATE_SCORE - MATE_DISTANCE_BOUND) return false;
    if (s <= -MATE_SCORE + MATE_DISTANCE_BOUND) return false;
    *score = s;
    return true;
}
