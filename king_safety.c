/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#include "king_safety.h"
#include "utils.h"
#include "movegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static const Bitboard king_zone_masks[64] = {
    0x0000000000000302ULL, 0x0000000000000705ULL, 0x0000000000000e0aULL, 0x0000000000001c14ULL,
    0x0000000000003828ULL, 0x0000000000007050ULL, 0x000000000000e0a0ULL, 0x000000000000c040ULL,
    0x0000000000030203ULL, 0x0000000000070507ULL, 0x00000000000e0a0eULL, 0x00000000001c141cULL,
    0x0000000000382838ULL, 0x0000000000705070ULL, 0x0000000000e0a0e0ULL, 0x0000000000c040c0ULL,
    0x0000000003020300ULL, 0x0000000007050700ULL, 0x000000000e0a0e00ULL, 0x000000001c141c00ULL,
    0x0000000038283800ULL, 0x0000000070507000ULL, 0x00000000e0a0e000ULL, 0x00000000c040c000ULL,
    0x0000000302030000ULL, 0x0000000705070000ULL, 0x0000000e0a0e0000ULL, 0x0000001c141c0000ULL,
    0x0000003828380000ULL, 0x0000007050700000ULL, 0x000000e0a0e00000ULL, 0x000000c040c00000ULL,
    0x0000030203000000ULL, 0x0000070507000000ULL, 0x00000e0a0e000000ULL, 0x00001c141c000000ULL,
    0x0000382838000000ULL, 0x0000705070000000ULL, 0x0000e0a0e0000000ULL, 0x0000c040c0000000ULL,
    0x0003020300000000ULL, 0x0007050700000000ULL, 0x000e0a0e00000000ULL, 0x001c141c00000000ULL,
    0x0038283800000000ULL, 0x0070507000000000ULL, 0x00e0a0e000000000ULL, 0x00c040c000000000ULL,
    0x0302030000000000ULL, 0x0705070000000000ULL, 0x0e0a0e0000000000ULL, 0x1c141c0000000000ULL,
    0x3828380000000000ULL, 0x7050700000000000ULL, 0xe0a0e00000000000ULL, 0xc040c00000000000ULL,
    0x0203000000000000ULL, 0x0507000000000000ULL, 0x0a0e000000000000ULL, 0x141c000000000000ULL,
    0x2838000000000000ULL, 0x5070000000000000ULL, 0xa0e0000000000000ULL, 0x40c0000000000000ULL
};

int get_king_square(Position* pos, Color color) {
    if (!pos) return -1;

    Bitboard king = pos->pieces[color][KING];
    return (king != 0) ? get_lsb(king) : -1;
}

bool has_castled(Position* pos, Color color) {
    if (!pos) return false;

    int king_square = get_king_square(pos, color);
    if (king_square == -1) return false;

    if (color == WHITE) {

        return (king_square == 6 || king_square == 2);
    } else {

        return (king_square == 62 || king_square == 58);
    }
}

bool can_castle_kingside(Position* pos, Color color) {
    if (!pos) return false;

    if (color == WHITE) {
        return (pos->castling_rights & WHITE_KINGSIDE) != 0;
    } else {
        return (pos->castling_rights & BLACK_KINGSIDE) != 0;
    }
}

bool can_castle_queenside(Position* pos, Color color) {
    if (!pos) return false;

    if (color == WHITE) {
        return (pos->castling_rights & WHITE_QUEENSIDE) != 0;
    } else {
        return (pos->castling_rights & BLACK_QUEENSIDE) != 0;
    }
}

Bitboard get_king_danger_zone(int king_square) {
    if (king_square < 0 || king_square >= 64) return 0;
    return king_zone_masks[king_square];
}

Bitboard get_king_safety_zone(int king_square) {

    if (king_square < 0 || king_square >= 64) return 0;

    Bitboard zone = 0;
    int file = king_square & 7;
    int rank = king_square >> 3;

    for (int df = -1; df <= 1; df++) {
        for (int dr = -1; dr <= 1; dr++) {
            if (df == 0 && dr == 0) continue;

            int new_file = file + df;
            int new_rank = rank + dr;

            if (new_file >= 0 && new_file < 8 && new_rank >= 0 && new_rank < 8) {
                zone |= (1ULL << (new_rank * 8 + new_file));
            }
        }
    }

    return zone;
}

int evaluate_king_shelter(Position* pos, int king_square, Color color) {
    if (!pos || king_square < 0) return 0;

    int score = 0;
    int file = king_square & 7;
    int rank = king_square >> 3;

    for (int df = -1; df <= 1; df++) {
        int shelter_file = file + df;
        if (shelter_file < 0 || shelter_file >= 8) continue;

        Bitboard file_mask = 0x0101010101010101ULL << shelter_file;
        Bitboard friendly_pawns = pos->pieces[color][PAWN] & file_mask;

        if (friendly_pawns == 0) {

            score -= 20;
        } else {

            int closest_pawn = -1;
            while (friendly_pawns) {
                int pawn_square = get_lsb(friendly_pawns);
                int pawn_rank = pawn_square >> 3;

                if (color == WHITE) {
                    if (pawn_rank > rank && (closest_pawn == -1 || pawn_rank < (closest_pawn >> 3))) {
                        closest_pawn = pawn_square;
                    }
                } else {
                    if (pawn_rank < rank && (closest_pawn == -1 || pawn_rank > (closest_pawn >> 3))) {
                        closest_pawn = pawn_square;
                    }
                }

                pop_lsb(&friendly_pawns);
            }

            if (closest_pawn != -1) {
                int distance = abs((closest_pawn >> 3) - rank);
                if (distance == 1) score += 15;
                else if (distance == 2) score += 5;
            }
        }
    }

    return score;
}

int evaluate_king_storm(Position* pos, int king_square, Color color) {
    if (!pos || king_square < 0) return 0;

    int score = 0;
    int file = king_square & 7;
    Color enemy = (color == WHITE) ? BLACK : WHITE;

    for (int df = -2; df <= 2; df++) {
        int storm_file = file + df;
        if (storm_file < 0 || storm_file >= 8) continue;

        Bitboard file_mask = 0x0101010101010101ULL << storm_file;
        Bitboard enemy_pawns = pos->pieces[enemy][PAWN] & file_mask;

        while (enemy_pawns) {
            int pawn_square = get_lsb(enemy_pawns);
            int pawn_rank = pawn_square >> 3;
            int king_rank = king_square >> 3;

            int distance = (color == WHITE) ? (pawn_rank - king_rank) : (king_rank - pawn_rank);

            if (distance > 0 && distance <= 3) {
                int storm_penalty = (4 - distance) * 5;
                if (abs(df) <= 1) storm_penalty *= 2;
                score -= storm_penalty;
            }

            pop_lsb(&enemy_pawns);
        }
    }

    return score;
}

int evaluate_open_files_near_king(Position* pos, int king_square, Color color) {
    if (!pos || king_square < 0) return 0;

    int score = 0;
    int file = king_square & 7;
    Color enemy = (color == WHITE) ? BLACK : WHITE;

    for (int df = -2; df <= 2; df++) {
        int check_file = file + df;
        if (check_file < 0 || check_file >= 8) continue;

        Bitboard file_mask = 0x0101010101010101ULL << check_file;
        Bitboard pawns_on_file = (pos->pieces[WHITE][PAWN] | pos->pieces[BLACK][PAWN]) & file_mask;

        if (pawns_on_file == 0) {
            Bitboard enemy_rooks = pos->pieces[enemy][ROOK] & file_mask;
            Bitboard enemy_queens = pos->pieces[enemy][QUEEN] & file_mask;

            if (enemy_rooks || enemy_queens) {
                int penalty = OPEN_FILE_NEAR_KING_PENALTY;
                if (abs(df) <= 1) penalty *= 2;
                score += penalty;
            }
        }
    }

    return score;
}

int count_attackers_near_king(Position* pos, int king_square, Color attacking_color) {
    if (!pos || king_square < 0) return 0;

    const Bitboard danger_zone = get_king_danger_zone(king_square);
    if (!danger_zone) return 0;

    int attackers = 0;

    const Bitboard pawn_squares = (attacking_color == WHITE)
        ? (((danger_zone & ~file_masks[7]) >> 7) | ((danger_zone & ~file_masks[0]) >> 9))
        : (((danger_zone & ~file_masks[7]) << 9) | ((danger_zone & ~file_masks[0]) << 7));
    attackers += __builtin_popcountll(pos->pieces[attacking_color][PAWN] & pawn_squares);

    Bitboard knight_squares = 0;
    Bitboard zone_bb = danger_zone;
    while (zone_bb) {
        knight_squares |= knight_attacks[get_lsb(zone_bb)];
        pop_lsb(&zone_bb);
    }
    attackers += __builtin_popcountll(pos->pieces[attacking_color][KNIGHT] & knight_squares);

    const int kfile = king_square & 7;
    const int krank = king_square >> 3;
    Bitboard near_king = 0;
    for (int f = (kfile > 2 ? kfile - 2 : 0); f <= (kfile < 5 ? kfile + 2 : 7); f++)
        near_king |= file_masks[f];
    for (int r = (krank > 2 ? krank - 2 : 0); r <= (krank < 5 ? krank + 2 : 7); r++)
        near_king |= rank_masks[r];
    const Bitboard slider_pieces = pos->pieces[attacking_color][ROOK]
                           | pos->pieces[attacking_color][BISHOP]
                           | pos->pieces[attacking_color][QUEEN];
    attackers += __builtin_popcountll(slider_pieces & near_king);

    return attackers;
}

bool is_king_in_danger(Position* pos, Color color) {
    if (!pos) return false;

    int king_square = get_king_square(pos, color);
    if (king_square == -1) return false;

    Color enemy = (color == WHITE) ? BLACK : WHITE;
    int attackers = count_attackers_near_king(pos, king_square, enemy);

    return attackers >= 3;
}

int evaluate_king_centralization(Position* pos, Color color) {
    if (!pos) return 0;

    int king_square = get_king_square(pos, color);
    if (king_square == -1) return 0;

    const int* king_pst = get_pst_table(KING, PHASE_ENDGAME);
    int pst_square = (color == WHITE) ? (king_square ^ 56) : king_square;

    return king_pst[pst_square];
}

int evaluate_king_pawn_proximity(Position* pos, Color color) {
    if (!pos) return 0;

    int score = 0;
    int king_square = get_king_square(pos, color);
    if (king_square == -1) return 0;

    int king_file = king_square & 7;
    int king_rank = king_square >> 3;

    Bitboard pawns = pos->pieces[color][PAWN];
    while (pawns) {
        int pawn_square = get_lsb(pawns);
        int pawn_file = pawn_square & 7;
        int pawn_rank = pawn_square >> 3;

        int distance = abs(king_file - pawn_file) + abs(king_rank - pawn_rank);
        if (distance <= 2) {
            score += 5;
        }

        pop_lsb(&pawns);
    }

    return score;
}

int evaluate_king_opposition(Position* pos) {
    if (!pos) return 0;

    int white_king = get_king_square(pos, WHITE);
    int black_king = get_king_square(pos, BLACK);

    if (white_king == -1 || black_king == -1) return 0;

    int file_diff = abs((white_king & 7) - (black_king & 7));
    int rank_diff = abs((white_king >> 3) - (black_king >> 3));

    if ((file_diff == 2 && rank_diff == 0) || (file_diff == 0 && rank_diff == 2)) {

        return (pos->to_move == WHITE) ? -10 : 10;
    }

    return 0;
}

int evaluate_king_activity_endgame(Position* pos) {
    if (!pos) return 0;

    int score = 0;

    for (Color color = WHITE; color <= BLACK; color++) {
        int color_multiplier = (color == WHITE) ? 1 : -1;

        score += color_multiplier * evaluate_king_centralization(pos, color);
        score += color_multiplier * evaluate_king_pawn_proximity(pos, color);
    }

    score += evaluate_king_opposition(pos);

    return score;
}

int evaluate_castling_safety(Position* pos) {
    if (!pos) return 0;

    int score = 0;

    for (Color color = WHITE; color <= BLACK; color++) {
        int color_multiplier = (color == WHITE) ? 1 : -1;

        if (has_castled(pos, color)) {
            score += color_multiplier * CASTLING_BONUS;
        } else {

            if (!can_castle_kingside(pos, color) && !can_castle_queenside(pos, color)) {
                score += color_multiplier * UNCASTLED_KING_PENALTY;
            } else {

                if (can_castle_kingside(pos, color)) {
                    score += color_multiplier * CASTLING_RIGHTS_BONUS;
                }
                if (can_castle_queenside(pos, color)) {
                    score += color_multiplier * (CASTLING_RIGHTS_BONUS / 2);
                }
            }
        }
    }

    return score;
}

int evaluate_king_safety_simplified(Position* pos) {
    if (!pos) return 0;

    const int p256 = game_phase_256(pos);

    int eg = 0;
    int mg = 0;

    if (p256 < 256) {
        eg += evaluate_king_activity_endgame(pos);
    }

    if (p256 > 0) {

        mg += evaluate_castling_safety(pos);

        for (Color color = WHITE; color <= BLACK; color++) {
            int color_multiplier = (color == WHITE) ? 1 : -1;
            int king_square = get_king_square(pos, color);

            if (king_square != -1) {
                mg += color_multiplier * evaluate_king_shelter(pos, king_square, color);
                mg += color_multiplier * evaluate_king_storm(pos, king_square, color);
                mg += color_multiplier * evaluate_open_files_near_king(pos, king_square, color);

                if (is_king_in_danger(pos, color)) {
                    mg += color_multiplier * (-30);
                }
            }
        }
    }

    return (mg * p256 + eg * (256 - p256)) / 256;
}

int evaluate_king_safety(Position* pos) {
    if (!pos) return 0;

    return evaluate_king_safety_simplified(pos);
}