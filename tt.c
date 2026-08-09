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
    random_seed = (uint64_t)time(NULL);

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
    while (size < entries) {
        size *= 2;
    }
    size /= 2;

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

void free_tt(TranspositionTable* tt) {
    if (tt->entries) {
        free(tt->entries);
        tt->entries = NULL;
    }
    tt->size = 0;
    tt->mask = 0;
    tt->age = 0;
}

void store_hash(TranspositionTable* tt, uint64_t key, int depth, int score, HashFlag flag, Move best_move) {
    if (!tt->entries || tt->size == 0) return;

    int index = key & tt->mask;
    HashEntry* entry = &tt->entries[index];

    if (entry->key == 0 ||
        entry->key == key ||
        depth >= entry->depth ||
        entry->age != tt->age) {

        entry->key = key;
        entry->depth = depth;
        entry->score = score;
        entry->flag = flag;
        entry->best_move = best_move;
        entry->age = tt->age;
    }
}

bool probe_hash(TranspositionTable* tt, uint64_t key, int depth, int alpha, int beta, int* score, Move* best_move) {
    if (!tt->entries || tt->size == 0) return false;

    int index = key & tt->mask;
    HashEntry* entry = &tt->entries[index];

    if (entry->key != key) return false;

    if (best_move && entry->best_move.from != entry->best_move.to) {
        *best_move = entry->best_move;
    }

    if (entry->depth >= depth) {
        if (entry->flag == HASH_EXACT) {
            *score = entry->score;
            return true;
        } else if (entry->flag == HASH_ALPHA && entry->score <= alpha) {
            *score = alpha;
            return true;
        } else if (entry->flag == HASH_BETA && entry->score >= beta) {
            *score = beta;
            return true;
        }
    }

    return false;
}