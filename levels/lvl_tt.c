/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 * Copyright (C) 2019-2025 Morgan Houppin (Stash 37)
 * Portions of this file are adapted from Stash 37. Modified by Dariusz Domagała, 2026-10-21.
 */
static void lvl_clear_tt(TranspositionTable* tt);
static int lvl_hashfull_tt(const TranspositionTable* tt);
static uint16_t lvl_pack_move(Move m);
static Move lvl_unpack_move(uint16_t packed);
static void lvl_store_hash(TranspositionTable* tt, uint64_t key, int depth, int score, HashFlag flag, Move best_move, int ply);
static void lvl_store_hash_eval(TranspositionTable* tt, uint64_t key, int depth, int score, HashFlag flag, Move best_move, int ply, int eval);
static void lvl_tt_store_eval(TranspositionTable* tt, uint64_t key, int eval);
static bool lvl_tt_probe_entry(TranspositionTable* tt, uint64_t key, int ply, TTProbe* out);
static void lvl_tt_store_qs(TranspositionTable* tt, uint64_t key, int score, HashFlag flag, Move best_move, int ply);

#define MATE_DISTANCE_BOUND 512

static TranspositionTable lvl_tt;
_Static_assert(sizeof(HashEntry) == 16, "HashEntry must be 16 B - changing the field layout costs table entries");
_Static_assert(offsetof(HashEntry, score) == 8, "TT entry data starts at byte 8");

static void lvl_clear_tt(TranspositionTable* tt) {
    if (tt->entries) {
        memset(tt->entries, 0, tt->size * sizeof(HashEntry));
    }
    tt->age = 0;
}

static int lvl_hashfull_tt(const TranspositionTable* tt) {
    if (!tt || !tt->entries || tt->size <= 0) return 0;
    int sample_n = tt->size < 1000 ? tt->size : 1000;
    int occ_all = 0;
    for (int i = 0; i < sample_n; i++) {

        if (__atomic_load_n(&tt->entries[i].key, __ATOMIC_RELAXED) != 0) occ_all++;
    }
    return (int)(((long long)occ_all * 1000) / sample_n);
}

static int lvl_score_to_tt(int score, int ply) {
    if (score >= MATE_SCORE - MATE_DISTANCE_BOUND)  return score + ply;
    if (score <= -MATE_SCORE + MATE_DISTANCE_BOUND) return score - ply;
    return score;
}

static int lvl_score_from_tt(int score, int ply) {
    if (score >= MATE_SCORE - MATE_DISTANCE_BOUND)  return score - ply;
    if (score <= -MATE_SCORE + MATE_DISTANCE_BOUND) return score + ply;
    return score;
}

static uint16_t lvl_pack_move(Move m) {
    if (m.from == m.to) return 0;
    return (uint16_t)(((unsigned)m.from & 0x3Fu)
                    | (((unsigned)m.to & 0x3Fu) << 6)
                    | (((unsigned)m.promotion & 0x7u) << 12));
}

static Move lvl_unpack_move(uint16_t packed) {
    Move m = {0};
    m.from      = (int)(packed & 0x3Fu);
    m.to        = (int)((packed >> 6) & 0x3Fu);
    m.promotion = (PieceType)((packed >> 12) & 0x7u);
    return m;
}

static inline HashEntry* lvl_tt_bucket(const TranspositionTable* tt, uint64_t key) {
    return &tt->entries[(key & (uint64_t)tt->mask) & ~(uint64_t)(TT_BUCKET - 1)];
}
#define TT_AGE_MASK 63
#define TT_PV_BIT     0
static inline int lvl_tt_age(const TranspositionTable* tt) { return tt->age & TT_AGE_MASK; }
static inline int lvl_tt_bound(const HashEntry* e)         { return e->genflag & 3; }
static inline int lvl_tt_victim_score(const HashEntry* e, int cur_age) {
    return (int)e->depth - 4 * ((cur_age - ((e->genflag >> 2) & TT_AGE_MASK)) & TT_AGE_MASK);
}

#define TT_KEEP_PLIES 3
static inline int lvl_tt_keep_old(const TranspositionTable* tt, const HashEntry* old_entry, uint64_t key, int depth, HashFlag flag) {
    return old_entry->key == key && lvl_tt_bound(old_entry) != HASH_NONE
        && ((old_entry->genflag >> 2) & TT_AGE_MASK) == lvl_tt_age(tt)
        && flag != HASH_EXACT && depth + TT_KEEP_PLIES < (int)old_entry->depth;
}

typedef uint64_t __attribute__((may_alias)) tt_word64;

static inline void lvl_tt_read(HashEntry* e, HashEntry* buf_copy) {
    const uint64_t k = __atomic_load_n((tt_word64*)(void*)e,     __ATOMIC_RELAXED);
    const uint64_t d = __atomic_load_n((tt_word64*)(void*)e + 1, __ATOMIC_RELAXED);
    memcpy((char*)buf_copy + 8, &d, 8);
    buf_copy->key = k ^ d;
}

static inline void lvl_tt_write(HashEntry* e, const HashEntry* new_entry) {
    uint64_t d;
    memcpy(&d, (const char*)new_entry + 8, 8);
    __atomic_store_n((tt_word64*)(void*)e + 1, d,             __ATOMIC_RELAXED);
    __atomic_store_n((tt_word64*)(void*)e,     new_entry->key ^ d, __ATOMIC_RELAXED);
}

static bool lvl_tt_find(TranspositionTable* tt, uint64_t key, HashEntry* w) {
    if (!tt->entries || tt->size == 0) return false;
    HashEntry* k = lvl_tt_bucket(tt, key);
    for (int i = 0; i < TT_BUCKET; i++) {
        lvl_tt_read(&k[i], w);
        if (w->key == key) {
            const uint8_t g = (uint8_t)((w->genflag & TT_PV_BIT) | (lvl_tt_age(tt) << 2) | lvl_tt_bound(w));
            if (g != w->genflag) { w->genflag = g; lvl_tt_write(&k[i], w); }
            return true;
        }
        if (w->key == 0) return false;
    }
    return false;
}

static HashEntry* lvl_tt_slot(TranspositionTable* tt, uint64_t key, HashEntry* old_entry) {
    HashEntry* k = lvl_tt_bucket(tt, key);
    HashEntry w[TT_BUCKET];
    for (int i = 0; i < TT_BUCKET; i++) {
        lvl_tt_read(&k[i], &w[i]);
        if (w[i].key == key || w[i].key == 0) { *old_entry = w[i]; return &k[i]; }
    }
    const int cur_age = lvl_tt_age(tt);
    int o = 0;
    for (int i = 1; i < TT_BUCKET; i++)
        if (lvl_tt_victim_score(&w[i], cur_age) < lvl_tt_victim_score(&w[o], cur_age)) o = i;
    *old_entry = w[o];
    return &k[o];
}

static void lvl_store_hash(TranspositionTable* tt, uint64_t key, int depth, int score, HashFlag flag, Move best_move, int ply) {
    lvl_store_hash_eval(tt, key, depth, score, flag, best_move, ply, TT_EVAL_NONE);
}

static void lvl_store_hash_eval(TranspositionTable* tt, uint64_t key, int depth, int score, HashFlag flag, Move best_move, int ply, int eval) {
    if (!tt->entries || tt->size == 0) return;
    if (score > MATE_SCORE || score < -MATE_SCORE) return;
    score = lvl_score_to_tt(score, ply);
    if (depth < 0)   depth = 0;
    if (depth > 255) depth = 255;

    HashEntry old_entry, new_entry;
    HashEntry* e = lvl_tt_slot(tt, key, &old_entry);
    if (lvl_tt_keep_old(tt, &old_entry, key, depth, flag)) return;
    if (eval == TT_EVAL_NONE && old_entry.key == key) eval = old_entry.eval;
    new_entry.key       = key;
    new_entry.score     = (int16_t)score;
    new_entry.eval      = (int16_t)eval;
    new_entry.best_move = lvl_pack_move(best_move);
    if (new_entry.best_move == 0 && old_entry.key == key) new_entry.best_move = old_entry.best_move;
    new_entry.depth     = (uint8_t)depth;
    new_entry.genflag   = (uint8_t)((lvl_tt_age(tt) << 2) | (int)flag);
    lvl_tt_write(e, &new_entry);
}

static void lvl_tt_store_eval(TranspositionTable* tt, uint64_t key, int eval) {
    if (!tt->entries || tt->size == 0) return;
    HashEntry old_entry;
    HashEntry* e = lvl_tt_slot(tt, key, &old_entry);
    if (old_entry.key == key) { old_entry.eval = (int16_t)eval; lvl_tt_write(e, &old_entry); return; }
    HashEntry new_entry;
    new_entry.key       = key;
    new_entry.score     = 0;
    new_entry.eval      = (int16_t)eval;
    new_entry.best_move = 0;
    new_entry.depth     = 0;
    new_entry.genflag   = (uint8_t)((lvl_tt_age(tt) << 2) | HASH_NONE);
    lvl_tt_write(e, &new_entry);
}

static bool lvl_tt_probe_entry(TranspositionTable* tt, uint64_t key, int ply, TTProbe* out) {
    HashEntry w;
    if (!lvl_tt_find(tt, key, &w)) { out->result_bb = false; out->eval = TT_EVAL_NONE; out->this_move = 0; return false; }
    out->flag  = lvl_tt_bound(&w);
    out->result_bb = out->flag != HASH_NONE;
    out->depth = w.depth;
    out->score = lvl_score_from_tt(w.score, ply);
    out->eval  = w.eval;
    out->this_move  = w.best_move;
    return true;
}

static void lvl_tt_store_qs(TranspositionTable* tt, uint64_t key, int score, HashFlag flag, Move best_move, int ply) {
    if (!tt->entries || tt->size == 0) return;
    HashEntry* k = lvl_tt_bucket(tt, key);
    HashEntry w[TT_BUCKET];
    int o = -1;
    for (int i = 0; i < TT_BUCKET; i++) {
        lvl_tt_read(&k[i], &w[i]);
        if (w[i].key == key) {
            const uint8_t g = (uint8_t)((w[i].genflag & TT_PV_BIT) | (lvl_tt_age(tt) << 2) | lvl_tt_bound(&w[i]));
            if (g != w[i].genflag) { w[i].genflag = g; lvl_tt_write(&k[i], &w[i]); }
            if (lvl_tt_bound(&w[i]) != HASH_NONE && w[i].depth > 0) return;
            o = i;
            break;
        }
        if (w[i].key == 0) { o = i; break; }
    }
    if (score > MATE_SCORE || score < -MATE_SCORE) return;
    if (o < 0) {
        const int cur_age = lvl_tt_age(tt);
        o = 0;
        for (int i = 1; i < TT_BUCKET; i++)
            if (lvl_tt_victim_score(&w[i], cur_age) < lvl_tt_victim_score(&w[o], cur_age)) o = i;
    }
    const HashEntry old_entry = w[o];
    HashEntry new_entry;
    score = lvl_score_to_tt(score, ply);
    if (lvl_tt_keep_old(tt, &old_entry, key, 0, flag)) return;
    new_entry.key       = key;
    new_entry.score     = (int16_t)score;
    new_entry.eval      = (int16_t)(old_entry.key == key ? old_entry.eval : TT_EVAL_NONE);
    new_entry.best_move = lvl_pack_move(best_move);
    if (new_entry.best_move == 0 && old_entry.key == key) new_entry.best_move = old_entry.best_move;
    new_entry.depth     = 0;
    new_entry.genflag   = (uint8_t)((lvl_tt_age(tt) << 2) | (int)flag);
    lvl_tt_write(&k[o], &new_entry);
}

static inline void lvl_tt_prefetch(const TranspositionTable* tt, uint64_t key) {
    __builtin_prefetch(&tt->entries[(key & (uint64_t)tt->mask) & ~(uint64_t)(TT_BUCKET - 1)]);
}
#define LVL_TT_PF(key) lvl_tt_prefetch(&lvl_tt, (key))
