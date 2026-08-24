/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#include "pawn_structure.h"
#include "utils.h"
#include "movegen.h"
#include <stdio.h>

static const Bitboard pawn_attacks_white[64] = {
    0x0000000000000200ULL, 0x0000000000000500ULL, 0x0000000000000a00ULL, 0x0000000000001400ULL,
    0x0000000000002800ULL, 0x0000000000005000ULL, 0x000000000000a000ULL, 0x0000000000004000ULL,
    0x0000000000020000ULL, 0x0000000000050000ULL, 0x00000000000a0000ULL, 0x0000000000140000ULL,
    0x0000000000280000ULL, 0x0000000000500000ULL, 0x0000000000a00000ULL, 0x0000000000400000ULL,
    0x0000000002000000ULL, 0x0000000005000000ULL, 0x000000000a000000ULL, 0x0000000014000000ULL,
    0x0000000028000000ULL, 0x0000000050000000ULL, 0x00000000a0000000ULL, 0x0000000040000000ULL,
    0x0000000200000000ULL, 0x0000000500000000ULL, 0x0000000a00000000ULL, 0x0000001400000000ULL,
    0x0000002800000000ULL, 0x0000005000000000ULL, 0x000000a000000000ULL, 0x0000004000000000ULL,
    0x0000020000000000ULL, 0x0000050000000000ULL, 0x00000a0000000000ULL, 0x0000140000000000ULL,
    0x0000280000000000ULL, 0x0000500000000000ULL, 0x0000a00000000000ULL, 0x0000400000000000ULL,
    0x0002000000000000ULL, 0x0005000000000000ULL, 0x000a000000000000ULL, 0x0014000000000000ULL,
    0x0028000000000000ULL, 0x0050000000000000ULL, 0x00a0000000000000ULL, 0x0040000000000000ULL,
    0x0200000000000000ULL, 0x0500000000000000ULL, 0x0a00000000000000ULL, 0x1400000000000000ULL,
    0x2800000000000000ULL, 0x5000000000000000ULL, 0xa000000000000000ULL, 0x4000000000000000ULL,
    0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL,
    0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL
};

static const Bitboard pawn_attacks_black[64] = {
    0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL,
    0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL,
    0x0000000000000002ULL, 0x0000000000000005ULL, 0x000000000000000aULL, 0x0000000000000014ULL,
    0x0000000000000028ULL, 0x0000000000000050ULL, 0x00000000000000a0ULL, 0x0000000000000040ULL,
    0x0000000000000200ULL, 0x0000000000000500ULL, 0x0000000000000a00ULL, 0x0000000000001400ULL,
    0x0000000000002800ULL, 0x0000000000005000ULL, 0x000000000000a000ULL, 0x0000000000004000ULL,
    0x0000000000020000ULL, 0x0000000000050000ULL, 0x00000000000a0000ULL, 0x0000000000140000ULL,
    0x0000000000280000ULL, 0x0000000000500000ULL, 0x0000000000a00000ULL, 0x0000000000400000ULL,
    0x0000000002000000ULL, 0x0000000005000000ULL, 0x000000000a000000ULL, 0x0000000014000000ULL,
    0x0000000028000000ULL, 0x0000000050000000ULL, 0x00000000a0000000ULL, 0x0000000040000000ULL,
    0x0000000200000000ULL, 0x0000000500000000ULL, 0x0000000a00000000ULL, 0x0000001400000000ULL,
    0x0000002800000000ULL, 0x0000005000000000ULL, 0x000000a000000000ULL, 0x0000004000000000ULL,
    0x0000020000000000ULL, 0x0000050000000000ULL, 0x00000a0000000000ULL, 0x0000140000000000ULL,
    0x0000280000000000ULL, 0x0000500000000000ULL, 0x0000a00000000000ULL, 0x0000400000000000ULL,
    0x0002000000000000ULL, 0x0005000000000000ULL, 0x000a000000000000ULL, 0x0014000000000000ULL,
    0x0028000000000000ULL, 0x0050000000000000ULL, 0x00a0000000000000ULL, 0x0040000000000000ULL
};

static inline int get_file(int square) {
    return square & 7;
}

static inline int get_rank(int square) {
    return square >> 3;
}

static inline Bitboard get_adjacent_files(int file) {
    Bitboard mask = 0;
    if (file > 0) mask |= file_masks[file - 1];
    if (file < 7) mask |= file_masks[file + 1];
    return mask;
}

static inline Bitboard get_forward_mask(int square, Color color) {
    int rank = square >> 3;
    if (color == WHITE) {
        if (rank >= 7) return 0;
        return 0xFFFFFFFFFFFFFFFFULL << ((rank + 1) * 8);
    } else {
        if (rank <= 0) return 0;
        return (1ULL << (rank * 8)) - 1ULL;
    }
}

bool is_passed_pawn(Position* pos, int square, Color color) {
    if (!pos) return false;

    int file = get_file(square);
    Color enemy = (color == WHITE) ? BLACK : WHITE;

    Bitboard blocking_files = file_masks[file];
    if (file > 0) blocking_files |= file_masks[file - 1];
    if (file < 7) blocking_files |= file_masks[file + 1];

    Bitboard enemy_pawns = pos->pieces[enemy][PAWN] & blocking_files;

    Bitboard forward_squares = get_forward_mask(square, color);

    return (enemy_pawns & forward_squares) == 0;
}

bool is_isolated_pawn(Position* pos, int square, Color color) {
    if (!pos) return false;

    int file = get_file(square);
    Bitboard adjacent_files = get_adjacent_files(file);

    return (pos->pieces[color][PAWN] & adjacent_files) == 0;
}

bool is_doubled_pawn(Position* pos, int square, Color color) {
    if (!pos) return false;

    int file = get_file(square);
    Bitboard file_pawns = pos->pieces[color][PAWN] & file_masks[file];

    return __builtin_popcountll(file_pawns) > 1;
}

bool is_backward_pawn(Position* pos, int square, Color color) {
    if (!pos) return false;

    int file = get_file(square);
    int rank = get_rank(square);

    Bitboard adjacent_files = get_adjacent_files(file);
    Bitboard friendly_pawns = pos->pieces[color][PAWN] & adjacent_files;

    if (friendly_pawns == 0) return false;

    while (friendly_pawns) {
        int pawn_square = get_lsb(friendly_pawns);
        int pawn_rank = get_rank(pawn_square);

        if (color == WHITE) {
            if (pawn_rank <= rank) return false;
        } else {
            if (pawn_rank >= rank) return false;
        }

        pop_lsb(&friendly_pawns);
    }

    return true;
}

Bitboard get_passed_pawns(Position* pos, Color color) {
    if (!pos) return 0;

    Bitboard passed = 0;
    Bitboard pawns = pos->pieces[color][PAWN];

    while (pawns) {
        int square = get_lsb(pawns);
        if (is_passed_pawn(pos, square, color)) {
            passed |= (1ULL << square);
        }
        pop_lsb(&pawns);
    }

    return passed;
}

Bitboard get_isolated_pawns(Position* pos, Color color) {
    if (!pos) return 0;

    Bitboard isolated = 0;
    Bitboard pawns = pos->pieces[color][PAWN];

    while (pawns) {
        int square = get_lsb(pawns);
        if (is_isolated_pawn(pos, square, color)) {
            isolated |= (1ULL << square);
        }
        pop_lsb(&pawns);
    }

    return isolated;
}

Bitboard get_doubled_pawns(Position* pos, Color color) {
    if (!pos) return 0;

    Bitboard doubled = 0;

    for (int file = 0; file < 8; file++) {
        Bitboard file_pawns = pos->pieces[color][PAWN] & file_masks[file];
        if (__builtin_popcountll(file_pawns) > 1) {
            doubled |= file_pawns;
        }
    }

    return doubled;
}

Bitboard get_connected_pawns(Position* pos, Color color) {
    if (!pos) return 0;

    Bitboard connected = 0;
    Bitboard pawns = pos->pieces[color][PAWN];

    while (pawns) {
        int square = get_lsb(pawns);

        Bitboard attacks = (color == WHITE) ? pawn_attacks_black[square] : pawn_attacks_white[square];
        if (attacks & pos->pieces[color][PAWN]) {
            connected |= (1ULL << square);
        }

        pop_lsb(&pawns);
    }

    return connected;
}

int count_pawn_islands(Bitboard pawns) {
    if (pawns == 0) return 0;

    int islands = 0;
    bool in_island = false;

    for (int file = 0; file < 8; file++) {
        bool has_pawn = (pawns & file_masks[file]) != 0;

        if (has_pawn && !in_island) {
            islands++;
            in_island = true;
        } else if (!has_pawn && in_island) {
            in_island = false;
        }
    }

    return islands;
}

int evaluate_passed_pawns_simplified(Position* pos) {
    if (!pos) return 0;

    int score = 0;

    for (Color color = WHITE; color <= BLACK; color++) {
        int color_multiplier = (color == WHITE) ? 1 : -1;
        Bitboard passed_pawns = get_passed_pawns(pos, color);

        while (passed_pawns) {
            int square = get_lsb(passed_pawns);
            int rank = get_rank(square);

            int bonus = PASSED_PAWN_BONUS;

            if (color == WHITE) {
                if (rank >= 5) bonus += ADVANCED_PASSED_BONUS * (rank - 4);
            } else {
                if (rank <= 2) bonus += ADVANCED_PASSED_BONUS * (3 - rank);
            }

            score += color_multiplier * bonus;
            pop_lsb(&passed_pawns);
        }
    }

    return score;
}

int evaluate_pawn_weaknesses(Position* pos) {
    if (!pos) return 0;

    int score = 0;

    for (Color color = WHITE; color <= BLACK; color++) {
        int color_multiplier = (color == WHITE) ? 1 : -1;

        Bitboard isolated = get_isolated_pawns(pos, color);
        score += color_multiplier * ISOLATED_PAWN_PENALTY * __builtin_popcountll(isolated);

        Bitboard doubled = get_doubled_pawns(pos, color);
        score += color_multiplier * DOUBLED_PAWN_PENALTY * (__builtin_popcountll(doubled) - 8 + count_pawn_islands(doubled));

        Bitboard pawns = pos->pieces[color][PAWN];
        while (pawns) {
            int square = get_lsb(pawns);
            if (is_backward_pawn(pos, square, color)) {
                score += color_multiplier * BACKWARD_PAWN_PENALTY;
            }
            pop_lsb(&pawns);
        }
    }

    return score;
}

int evaluate_connected_pawns(Position* pos) {
    if (!pos) return 0;

    int score = 0;

    for (Color color = WHITE; color <= BLACK; color++) {
        int color_multiplier = (color == WHITE) ? 1 : -1;
        Bitboard connected = get_connected_pawns(pos, color);

        score += color_multiplier * CONNECTED_PAWN_BONUS * __builtin_popcountll(connected);
    }

    return score;
}

int evaluate_pawn_chains(Position* pos) {
    if (!pos) return 0;

    int score = 0;

    for (Color color = WHITE; color <= BLACK; color++) {
        int color_multiplier = (color == WHITE) ? 1 : -1;
        Bitboard pawns = pos->pieces[color][PAWN];

        for (int file = 0; file < 6; file++) {
            Bitboard chain = 0;

            for (int rank = 1; rank < 7; rank++) {
                int square1 = rank * 8 + file;
                int square2 = rank * 8 + file + 1;
                int square3 = rank * 8 + file + 2;

                if ((pawns & (1ULL << square1)) &&
                    (pawns & (1ULL << square2)) &&
                    (pawns & (1ULL << square3))) {
                    chain |= (1ULL << square1) | (1ULL << square2) | (1ULL << square3);
                }
            }

            if (chain) {
                score += color_multiplier * PAWN_CHAIN_BONUS * __builtin_popcountll(chain);
            }
        }
    }

    return score;
}

int evaluate_pawn_islands(Position* pos) {
    if (!pos) return 0;

    int score = 0;

    for (Color color = WHITE; color <= BLACK; color++) {
        int color_multiplier = (color == WHITE) ? 1 : -1;
        Bitboard pawns = pos->pieces[color][PAWN];

        int islands = count_pawn_islands(pawns);

        if (islands > 3) {
            score += color_multiplier * (-10 * (islands - 3));
        }
    }

    return score;
}

int evaluate_pawn_structure(Position* pos) {
    if (!pos) return 0;

    int score = 0;

    score += evaluate_passed_pawns_simplified(pos);
    score += evaluate_pawn_weaknesses(pos);
    score += evaluate_connected_pawns(pos);
    score += evaluate_pawn_chains(pos);
    score += evaluate_pawn_islands(pos);

    return score;
}