/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#include "tt.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

void init_tt(TranspositionTable* tt, int size_mb) {
    int entries = (size_mb * 1024 * 1024) / sizeof(HashEntry);

    int size = 1;
    while (size <= entries / 2) {
        size *= 2;
    }

    tt->entries = calloc(size, sizeof(HashEntry));
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
        if (tt->entries[i].key != 0) occ_all++;
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
        free(tt->entries);
        tt->entries = NULL;
    }
    tt->size = 0;
    tt->mask = 0;
    tt->age = 0;
}

#define MATE_DISTANCE_BOUND 128

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

_Static_assert(sizeof(HashEntry) == 16, "HashEntry must be 16 B; a different field layout costs table entries");

void store_hash(TranspositionTable* tt, uint64_t key, int depth, int score, HashFlag flag, Move best_move, int ply) {
    if (!tt->entries || tt->size == 0) return;

    if (score > MATE_SCORE || score < -MATE_SCORE) return;

    score = score_to_tt(score, ply);

    int index = key & tt->mask;
    HashEntry* entry = &tt->entries[index];

    if (entry->key == 0 ||
        entry->key == key ||
        depth >= entry->depth ||
        entry->age != (uint8_t)tt->age) {

        entry->key = key;
        entry->depth = (int16_t)depth;
        entry->score = (int16_t)score;
        entry->flag = (uint8_t)flag;
        entry->best_move = pack_move(best_move);
        entry->age = (uint8_t)tt->age;
    }
}

bool probe_hash(TranspositionTable* tt, uint64_t key, int depth, int alpha, int beta, int* score, Move* best_move, int ply) {
    if (!tt->entries || tt->size == 0) return false;

    int index = key & tt->mask;
    HashEntry* entry = &tt->entries[index];

    if (entry->key != key) return false;

    if (best_move && entry->best_move != 0) {
        *best_move = unpack_move(entry->best_move);
    }

    if (entry->depth >= depth) {

        int s = score_from_tt(entry->score, ply);

        if (entry->flag == HASH_EXACT) {
            *score = s;
            return true;
        } else if (entry->flag == HASH_ALPHA && s <= alpha) {
            *score = alpha;
            return true;
        } else if (entry->flag == HASH_BETA && s >= beta) {
            *score = beta;
            return true;
        }
    }

    return false;
}

bool probe_hash_entry(TranspositionTable* tt, uint64_t key, int* depth, int* flag, int* score, int ply) {
    if (!tt->entries || tt->size == 0) return false;
    HashEntry* entry = &tt->entries[key & tt->mask];
    if (entry->key != key) return false;
    *depth = entry->depth;
    *flag  = entry->flag;
    *score = score_from_tt(entry->score, ply);
    return true;
}

bool tt_node_value(TranspositionTable* tt, uint64_t key, int* score, int ply) {
    if (!tt->entries || tt->size == 0) return false;
    HashEntry* entry = &tt->entries[key & tt->mask];
    if (entry->key != key) return false;
    if (entry->flag != HASH_EXACT) return false;
    int s = score_from_tt(entry->score, ply);
    if (s >=  MATE_SCORE - MATE_DISTANCE_BOUND) return false;
    if (s <= -MATE_SCORE + MATE_DISTANCE_BOUND) return false;
    *score = s;
    return true;
}
