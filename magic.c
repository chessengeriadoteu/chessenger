/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#include "magic.h"
#include <string.h>

#define ROOK_INDEX_BITS   12
#define BISHOP_INDEX_BITS   9
#define ROOK_SLOTS  (1u << ROOK_INDEX_BITS)
#define BISHOP_SLOTS (1u << BISHOP_INDEX_BITS)

#define MAGIC_MAX_TRIES 40000000UL

static Bitboard rook_table[64][ROOK_SLOTS];
static Bitboard bishop_table[64][BISHOP_SLOTS];
static Bitboard rook_mask[64];
static Bitboard bishop_mask[64];
static Bitboard rook_multiplier[64];
static Bitboard bishop_multiplier[64];
static int rook_shift[64];
static int bishop_shift[64];

int magic_ready = 0;

static unsigned long rook_tries_total;
static unsigned long bishop_tries_total;

static Bitboard scan_rook(int square, Bitboard occupied) {
    Bitboard attacks = 0ULL;
    int rank = square / 8;
    int file = square % 8;

    for (int r = rank + 1; r < 8; r++) {
        int sq = r * 8 + file;
        attacks |= (1ULL << sq);
        if (occupied & (1ULL << sq)) break;
    }
    for (int r = rank - 1; r >= 0; r--) {
        int sq = r * 8 + file;
        attacks |= (1ULL << sq);
        if (occupied & (1ULL << sq)) break;
    }
    for (int f = file + 1; f < 8; f++) {
        int sq = rank * 8 + f;
        attacks |= (1ULL << sq);
        if (occupied & (1ULL << sq)) break;
    }
    for (int f = file - 1; f >= 0; f--) {
        int sq = rank * 8 + f;
        attacks |= (1ULL << sq);
        if (occupied & (1ULL << sq)) break;
    }
    return attacks;
}

static Bitboard scan_bishop(int square, Bitboard occupied) {
    Bitboard attacks = 0ULL;
    int rank = square / 8;
    int file = square % 8;

    for (int r = rank + 1, f = file + 1; r < 8 && f < 8; r++, f++) {
        int sq = r * 8 + f;
        attacks |= (1ULL << sq);
        if (occupied & (1ULL << sq)) break;
    }
    for (int r = rank + 1, f = file - 1; r < 8 && f >= 0; r++, f--) {
        int sq = r * 8 + f;
        attacks |= (1ULL << sq);
        if (occupied & (1ULL << sq)) break;
    }
    for (int r = rank - 1, f = file + 1; r >= 0 && f < 8; r--, f++) {
        int sq = r * 8 + f;
        attacks |= (1ULL << sq);
        if (occupied & (1ULL << sq)) break;
    }
    for (int r = rank - 1, f = file - 1; r >= 0 && f >= 0; r--, f--) {
        int sq = r * 8 + f;
        attacks |= (1ULL << sq);
        if (occupied & (1ULL << sq)) break;
    }
    return attacks;
}

static Bitboard build_rook_mask(int square) {
    Bitboard m = 0ULL;
    int rank = square / 8;
    int file = square % 8;

    for (int r = rank + 1; r <= 6; r++) m |= (1ULL << (r * 8 + file));
    for (int r = rank - 1; r >= 1; r--) m |= (1ULL << (r * 8 + file));
    for (int f = file + 1; f <= 6; f++) m |= (1ULL << (rank * 8 + f));
    for (int f = file - 1; f >= 1; f--) m |= (1ULL << (rank * 8 + f));
    return m;
}

static Bitboard build_bishop_mask(int square) {
    Bitboard m = 0ULL;
    int rank = square / 8;
    int file = square % 8;

    for (int r = rank + 1, f = file + 1; r <= 6 && f <= 6; r++, f++) m |= (1ULL << (r * 8 + f));
    for (int r = rank + 1, f = file - 1; r <= 6 && f >= 1; r++, f--) m |= (1ULL << (r * 8 + f));
    for (int r = rank - 1, f = file + 1; r >= 1 && f <= 6; r--, f++) m |= (1ULL << (r * 8 + f));
    for (int r = rank - 1, f = file - 1; r >= 1 && f >= 1; r--, f--) m |= (1ULL << (r * 8 + f));
    return m;
}

static int count_bits(Bitboard b) {
    int n = 0;
    while (b) { b &= b - 1ULL; n++; }
    return n;
}

static Bitboard nth_subset(unsigned slot_index, Bitboard mask_bb) {
    Bitboard result_bb = 0ULL;
    int i = 0;
    while (mask_bb) {
        Bitboard bit = mask_bb & (~mask_bb + 1ULL);
        mask_bb ^= bit;
        if (slot_index & (1u << i)) result_bb |= bit;
        i++;
    }
    return result_bb;
}

static Bitboard rng_state = 0x9E3779B97F4A7C15ULL;

static Bitboard random_u64(void) {
    rng_state ^= rng_state >> 12;
    rng_state ^= rng_state << 25;
    rng_state ^= rng_state >> 27;
    return rng_state * 0x2545F4914F6CDD1DULL;
}

static Bitboard random_sparse(void) {
    return random_u64() & random_u64() & random_u64();
}

static int find_magic(int square, int rook_sq,
                           Bitboard mask_bb, Bitboard *out_multiplier,
                           int *out_shift,
                           Bitboard *table_out, unsigned slots,
                           unsigned long *tries_counter) {
    int nbits = count_bits(mask_bb);
    unsigned num_subsets = 1u << nbits;
    int shift_bits = 64 - nbits;

    static Bitboard occupancies[ROOK_SLOTS];
    static Bitboard reference_attacks[ROOK_SLOTS];
    static unsigned slot_epoch[ROOK_SLOTS];
    static unsigned epoch = 0;

    if (num_subsets > slots) return 1;

    for (unsigned i = 0; i < num_subsets; i++) {
        occupancies[i] = nth_subset(i, mask_bb);
        reference_attacks[i] = rook_sq ? scan_rook(square, occupancies[i])
                          : scan_bishop(square, occupancies[i]);
    }

    for (unsigned long attempt = 0; attempt < MAGIC_MAX_TRIES; attempt++) {
        Bitboard candidate = random_sparse();
        (*tries_counter)++;

        if (count_bits((mask_bb * candidate) & 0xFF00000000000000ULL) < 6) continue;

        epoch++;
        int good = 1;
        for (unsigned i = 0; i < num_subsets; i++) {
            unsigned slot_index = (unsigned)((occupancies[i] * candidate) >> shift_bits);
            if (slot_index >= slots) { good = 0; break; }
            if (slot_epoch[slot_index] != epoch) {
                slot_epoch[slot_index] = epoch;
                table_out[slot_index] = reference_attacks[i];
            } else if (table_out[slot_index] != reference_attacks[i]) {
                good = 0;
                break;
            }
        }
        if (good) {

            memset(table_out, 0, (size_t)slots * sizeof(Bitboard));
            for (unsigned i = 0; i < num_subsets; i++) {
                unsigned slot_index = (unsigned)((occupancies[i] * candidate) >> shift_bits);
                table_out[slot_index] = reference_attacks[i];
            }
            *out_multiplier = candidate;
            *out_shift = shift_bits;
            return 0;
        }
    }
    return 1;
}

int magic_init(void) {
    magic_ready = 0;
    rng_state = 0x9E3779B97F4A7C15ULL;
    rook_tries_total = 0;
    bishop_tries_total = 0;

    for (int sq = 0; sq < 64; sq++) {
        rook_mask[sq] = build_rook_mask(sq);
        bishop_mask[sq] = build_bishop_mask(sq);
    }
    for (int sq = 0; sq < 64; sq++) {
        if (find_magic(sq, 1, rook_mask[sq], &rook_multiplier[sq],
                            &rook_shift[sq], rook_table[sq], ROOK_SLOTS,
                            &rook_tries_total) != 0) return 1;
        if (find_magic(sq, 0, bishop_mask[sq], &bishop_multiplier[sq],
                            &bishop_shift[sq], bishop_table[sq], BISHOP_SLOTS,
                            &bishop_tries_total) != 0) return 1;
    }
    magic_ready = 1;
    return 0;
}

Bitboard magic_rook_attacks(int square, Bitboard occupied) {
    Bitboard relevant = occupied & rook_mask[square];
    unsigned slot_index = (unsigned)((relevant * rook_multiplier[square]) >> rook_shift[square]);
    return rook_table[square][slot_index];
}

Bitboard magic_bishop_attacks(int square, Bitboard occupied) {
    Bitboard relevant = occupied & bishop_mask[square];
    unsigned slot_index = (unsigned)((relevant * bishop_multiplier[square]) >> bishop_shift[square]);
    return bishop_table[square][slot_index];
}

Bitboard magic_rook_mask(int square)   { return rook_mask[square]; }
Bitboard magic_bishop_mask(int square) { return bishop_mask[square]; }

unsigned long magic_tries_rook(void)  { return rook_tries_total; }
unsigned long magic_tries_bishop(void) { return bishop_tries_total; }

int magic_corrupt_rook_mask(int square) {
    Bitboard m = rook_mask[square];
    if (m == 0ULL) return 1;
    m &= m - 1ULL;
    rook_mask[square] = m;
    unsigned long ignored_tries = 0;
    return find_magic(square, 1, rook_mask[square], &rook_multiplier[square],
                           &rook_shift[square], rook_table[square], ROOK_SLOTS,
                           &ignored_tries);
}
