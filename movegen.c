/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#include "movegen.h"
#include "board.h"
#include "utils.h"
#include "eval.h"
#include "search.h"
#include "magic.h"
#include <stdio.h>
#include <string.h>

Bitboard rook_attacks[64][4096];
Bitboard bishop_attacks[64][512];
Bitboard knight_attacks[64];
Bitboard king_attacks[64];

static const int mvv_lva[7][7] = {

               {105, 405, 205, 305, 505, 605, 0},
               {102, 402, 202, 302, 502, 602, 0},
               {104, 404, 204, 304, 504, 604, 0},
               {103, 403, 203, 303, 503, 603, 0},
               {101, 401, 201, 301, 501, 601, 0},
               {100, 400, 200, 300, 500, 600, 0},
               {0,   0,   0,   0,   0,   0,   0}
};

static inline Bitboard ray_scan_rook(int square, Bitboard occupied) {
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

static inline Bitboard ray_scan_bishop(int square, Bitboard occupied) {
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

static inline Bitboard get_rook_attacks(int square, Bitboard occupied) {
    if (magic_ready) return magic_rook_attacks(square, occupied);
    return ray_scan_rook(square, occupied);
}

static inline Bitboard get_bishop_attacks(int square, Bitboard occupied) {
    if (magic_ready) return magic_bishop_attacks(square, occupied);
    return ray_scan_bishop(square, occupied);
}

Bitboard get_bishop_attacks_public(int square, Bitboard occupied) {
    return get_bishop_attacks(square, occupied);
}

Bitboard get_rook_attacks_public(int square, Bitboard occupied) {
    return get_rook_attacks(square, occupied);
}

Bitboard get_queen_attacks_public(int square, Bitboard occupied) {
    return get_rook_attacks(square, occupied) | get_bishop_attacks(square, occupied);
}

Bitboard ray_scan_rook_public(int square, Bitboard occupied) {
    return ray_scan_rook(square, occupied);
}

Bitboard ray_scan_bishop_public(int square, Bitboard occupied) {
    return ray_scan_bishop(square, occupied);
}

void init_magic_bitboards(void) {

    for (int sq = 0; sq < 64; sq++) {
        int rank = sq / 8;
        int file = sq % 8;

        knight_attacks[sq] = 0ULL;

        static const int knight_moves[8][2] = {
            {-2, -1}, {-2, 1}, {-1, -2}, {-1, 2},
            {1, -2}, {1, 2}, {2, -1}, {2, 1}
        };

        for (int i = 0; i < 8; i++) {
            int new_rank = rank + knight_moves[i][0];
            int new_file = file + knight_moves[i][1];

            if (new_rank >= 0 && new_rank < 8 && new_file >= 0 && new_file < 8) {
                knight_attacks[sq] |= (1ULL << (new_rank * 8 + new_file));
            }
        }
    }

    for (int sq = 0; sq < 64; sq++) {
        int rank = sq / 8;
        int file = sq % 8;

        king_attacks[sq] = 0ULL;

        for (int dr = -1; dr <= 1; dr++) {
            for (int df = -1; df <= 1; df++) {
                if (dr == 0 && df == 0) continue;

                int new_rank = rank + dr;
                int new_file = file + df;

                if (new_rank >= 0 && new_rank < 8 && new_file >= 0 && new_file < 8) {
                    king_attacks[sq] |= (1ULL << (new_rank * 8 + new_file));
                }
            }
        }
    }

    if (magic_init() != 0) {
        fprintf(stderr, "MAGIC: no multiplier found within the attempt limit; using ray scan instead\n");
    }
}

bool is_square_attacked(Position* pos, int square, Color by_color) {
    if (!pos || square < 0 || square > 63) return false;

    Bitboard pawns = pos->pieces[by_color][PAWN];
    Bitboard pawn_attacks = (by_color == WHITE)
        ? (((pawns & ~file_masks[0]) << 7) | ((pawns & ~file_masks[7]) << 9))
        : (((pawns & ~file_masks[0]) >> 9) | ((pawns & ~file_masks[7]) >> 7));
    if (pawn_attacks & (1ULL << square)) return true;

    if (knight_attacks[square] & pos->pieces[by_color][KNIGHT]) return true;

    if (king_attacks[square] & pos->pieces[by_color][KING]) return true;

    Bitboard rook_attackers = pos->pieces[by_color][ROOK] | pos->pieces[by_color][QUEEN];
    if (rook_attackers && (get_rook_attacks(square, pos->all_pieces) & rook_attackers)) {
        return true;
    }

    Bitboard bishop_attackers = pos->pieces[by_color][BISHOP] | pos->pieces[by_color][QUEEN];
    if (bishop_attackers && (get_bishop_attacks(square, pos->all_pieces) & bishop_attackers)) {
        return true;
    }

    return false;
}

bool is_in_check(Position* pos, Color color) {
    if (!pos) return false;

    Bitboard king = pos->pieces[color][KING];
    if (king == 0) return false;

    int king_square = get_lsb(king);
    if (king_square < 0 || king_square > 63) return false;

    return is_square_attacked(pos, king_square, 1 - color);
}

static bool is_king_move_adjacent_safe(Position* pos, int to, Color color) {
    if (!pos || to < 0 || to > 63) return false;

    Color enemy = 1 - color;
    Bitboard enemy_king = pos->pieces[enemy][KING];

    if (enemy_king == 0) return true;

    int enemy_king_square = get_lsb(enemy_king);
    if (enemy_king_square < 0 || enemy_king_square > 63) return true;

    return !(king_attacks[enemy_king_square] & (1ULL << to));
}

int generate_pawn_moves(Position* pos, Move* moves, int move_count) {
    if (!pos || !moves || move_count < 0 || move_count >= MAX_MOVES - 30) return move_count;

    Color us = pos->to_move;
    Color them = 1 - us;
    int direction = (us == WHITE) ? 8 : -8;

    Bitboard pawns = pos->pieces[us][PAWN];
    Bitboard empty = ~pos->all_pieces;
    Bitboard enemies = pos->occupied[them];

    Bitboard single_pushes = (us == WHITE) ?
        (pawns << 8) & empty :
        (pawns >> 8) & empty;

    while (single_pushes && move_count < MAX_MOVES - 10) {
        int to = get_lsb(single_pushes);
        int from = to - direction;

        bool is_promotion = (us == WHITE && to >= 56) || (us == BLACK && to <= 7);

        if (is_promotion) {

            if (move_count <= MAX_MOVES - 4) {
                moves[move_count++] = (Move){from, to, PAWN, EMPTY, QUEEN, false, false, 0};
                moves[move_count++] = (Move){from, to, PAWN, EMPTY, ROOK, false, false, 0};
                moves[move_count++] = (Move){from, to, PAWN, EMPTY, BISHOP, false, false, 0};
                moves[move_count++] = (Move){from, to, PAWN, EMPTY, KNIGHT, false, false, 0};
            }
        } else {

            if (move_count < MAX_MOVES - 1) {
                moves[move_count++] = (Move){from, to, PAWN, EMPTY, EMPTY, false, false, 0};
            }
        }

        pop_lsb(&single_pushes);
    }

    Bitboard starting_pawns = pawns & ((us == WHITE) ? rank_masks[1] : rank_masks[6]);
    Bitboard double_push_targets = (us == WHITE) ?
        ((starting_pawns << 8) & empty) << 8 :
        ((starting_pawns >> 8) & empty) >> 8;
    double_push_targets &= empty;

    while (double_push_targets && move_count < MAX_MOVES - 10) {
        int to = get_lsb(double_push_targets);
        int from = to - 2 * direction;

        if (move_count < MAX_MOVES - 1) {
            moves[move_count++] = (Move){from, to, PAWN, EMPTY, EMPTY, false, false, 0};
        }
        pop_lsb(&double_push_targets);
    }

    Bitboard left_attacks = (us == WHITE) ?
        ((pawns & ~file_masks[0]) << 7) & enemies :
        ((pawns & ~file_masks[0]) >> 9) & enemies;

    while (left_attacks && move_count < MAX_MOVES - 10) {
        int to = get_lsb(left_attacks);
        int from = (us == WHITE) ? to - 7 : to + 9;

        PieceType captured = EMPTY;
        for (PieceType piece = PAWN; piece <= KING; piece++) {
            if (pos->pieces[them][piece] & (1ULL << to)) {
                captured = piece;
                break;
            }
        }

        bool is_promotion = (us == WHITE && to >= 56) || (us == BLACK && to <= 7);

        if (is_promotion) {

            if (move_count <= MAX_MOVES - 4) {
                moves[move_count++] = (Move){from, to, PAWN, captured, QUEEN, false, false, 0};
                moves[move_count++] = (Move){from, to, PAWN, captured, ROOK, false, false, 0};
                moves[move_count++] = (Move){from, to, PAWN, captured, BISHOP, false, false, 0};
                moves[move_count++] = (Move){from, to, PAWN, captured, KNIGHT, false, false, 0};
            }
        } else {

            if (move_count < MAX_MOVES - 1) {
                moves[move_count++] = (Move){from, to, PAWN, captured, EMPTY, false, false, 0};
            }
        }

        pop_lsb(&left_attacks);
    }

    Bitboard right_attacks = (us == WHITE) ?
        ((pawns & ~file_masks[7]) << 9) & enemies :
        ((pawns & ~file_masks[7]) >> 7) & enemies;

    while (right_attacks && move_count < MAX_MOVES - 10) {
        int to = get_lsb(right_attacks);
        int from = (us == WHITE) ? to - 9 : to + 7;

        PieceType captured = EMPTY;
        for (PieceType piece = PAWN; piece <= KING; piece++) {
            if (pos->pieces[them][piece] & (1ULL << to)) {
                captured = piece;
                break;
            }
        }

        bool is_promotion = (us == WHITE && to >= 56) || (us == BLACK && to <= 7);

        if (is_promotion) {

            if (move_count <= MAX_MOVES - 4) {
                moves[move_count++] = (Move){from, to, PAWN, captured, QUEEN, false, false, 0};
                moves[move_count++] = (Move){from, to, PAWN, captured, ROOK, false, false, 0};
                moves[move_count++] = (Move){from, to, PAWN, captured, BISHOP, false, false, 0};
                moves[move_count++] = (Move){from, to, PAWN, captured, KNIGHT, false, false, 0};
            }
        } else {

            if (move_count < MAX_MOVES - 1) {
                moves[move_count++] = (Move){from, to, PAWN, captured, EMPTY, false, false, 0};
            }
        }

        pop_lsb(&right_attacks);
    }

    return move_count;
}

int generate_piece_moves(Position* pos, Move* moves, int move_count) {
    if (!pos || !moves || move_count < 0 || move_count >= MAX_MOVES - 50) return move_count;

    Color us = pos->to_move;
    Color them = 1 - us;

    Bitboard knights = pos->pieces[us][KNIGHT];
    while (knights && move_count < MAX_MOVES - 20) {
        int from = get_lsb(knights);
        Bitboard attacks = knight_attacks[from] & ~pos->occupied[us];

        while (attacks && move_count < MAX_MOVES - 1) {
            int to = get_lsb(attacks);

            PieceType captured = EMPTY;
            if (pos->occupied[them] & (1ULL << to)) {
                for (PieceType piece = PAWN; piece <= KING; piece++) {
                    if (pos->pieces[them][piece] & (1ULL << to)) {
                        captured = piece;
                        break;
                    }
                }
            }

            moves[move_count++] = (Move){from, to, KNIGHT, captured, EMPTY, false, false, 0};
            pop_lsb(&attacks);
        }
        pop_lsb(&knights);
    }

    Bitboard bishops = pos->pieces[us][BISHOP];
    while (bishops && move_count < MAX_MOVES - 20) {
        int from = get_lsb(bishops);
        Bitboard attacks = get_bishop_attacks(from, pos->all_pieces) & ~pos->occupied[us];

        while (attacks && move_count < MAX_MOVES - 1) {
            int to = get_lsb(attacks);

            PieceType captured = EMPTY;
            if (pos->occupied[them] & (1ULL << to)) {
                for (PieceType piece = PAWN; piece <= KING; piece++) {
                    if (pos->pieces[them][piece] & (1ULL << to)) {
                        captured = piece;
                        break;
                    }
                }
            }

            moves[move_count++] = (Move){from, to, BISHOP, captured, EMPTY, false, false, 0};
            pop_lsb(&attacks);
        }
        pop_lsb(&bishops);
    }

    Bitboard rooks = pos->pieces[us][ROOK];
    while (rooks && move_count < MAX_MOVES - 20) {
        int from = get_lsb(rooks);
        Bitboard attacks = get_rook_attacks(from, pos->all_pieces) & ~pos->occupied[us];

        while (attacks && move_count < MAX_MOVES - 1) {
            int to = get_lsb(attacks);

            PieceType captured = EMPTY;
            if (pos->occupied[them] & (1ULL << to)) {
                for (PieceType piece = PAWN; piece <= KING; piece++) {
                    if (pos->pieces[them][piece] & (1ULL << to)) {
                        captured = piece;
                        break;
                    }
                }
            }

            moves[move_count++] = (Move){from, to, ROOK, captured, EMPTY, false, false, 0};
            pop_lsb(&attacks);
        }
        pop_lsb(&rooks);
    }

    Bitboard queens = pos->pieces[us][QUEEN];
    while (queens && move_count < MAX_MOVES - 40) {
        int from = get_lsb(queens);
        Bitboard attacks = get_queen_attacks_public(from, pos->all_pieces) & ~pos->occupied[us];

        while (attacks && move_count < MAX_MOVES - 1) {
            int to = get_lsb(attacks);

            PieceType captured = EMPTY;
            if (pos->occupied[them] & (1ULL << to)) {
                for (PieceType piece = PAWN; piece <= KING; piece++) {
                    if (pos->pieces[them][piece] & (1ULL << to)) {
                        captured = piece;
                        break;
                    }
                }
            }

            moves[move_count++] = (Move){from, to, QUEEN, captured, EMPTY, false, false, 0};
            pop_lsb(&attacks);
        }
        pop_lsb(&queens);
    }

    return move_count;
}

int generate_king_moves(Position* pos, Move* moves, int move_count) {
    if (!pos || !moves || move_count < 0 || move_count >= MAX_MOVES - 10) return move_count;

    Color us = pos->to_move;
    Color them = 1 - us;

    Bitboard king = pos->pieces[us][KING];
    if (king == 0) return move_count;

    int from = get_lsb(king);
    Bitboard attacks = king_attacks[from] & ~pos->occupied[us];

    while (attacks && move_count < MAX_MOVES - 10) {
        int to = get_lsb(attacks);

        if (!is_king_move_adjacent_safe(pos, to, us)) {
            pop_lsb(&attacks);
            continue;
        }

        PieceType captured = EMPTY;

        if (pos->occupied[them] & (1ULL << to)) {
            for (PieceType piece = PAWN; piece <= KING; piece++) {
                if (pos->pieces[them][piece] & (1ULL << to)) {
                    captured = piece;
                    break;
                }
            }
        }

        if (move_count < MAX_MOVES - 1) {
            moves[move_count++] = (Move){from, to, KING, captured, EMPTY, false, false, 0};
        }
        pop_lsb(&attacks);
    }

    return move_count;
}

int generate_castling_moves(Position* pos, Move* moves, int move_count) {
    if (!pos || !moves || move_count < 0 || move_count >= MAX_MOVES - 5) return move_count;

    Color us = pos->to_move;

    if (is_in_check(pos, us)) return move_count;

    Bitboard king = pos->pieces[us][KING];
    if (king == 0) return move_count;

    int king_square = get_lsb(king);

    if (us == WHITE) {

        if ((pos->castling_rights & WHITE_KINGSIDE) &&
            king_square == 4 &&
            !(pos->all_pieces & 0x60ULL) &&
            !is_square_attacked(pos, 5, BLACK) &&
            !is_square_attacked(pos, 6, BLACK)) {

            if (move_count < MAX_MOVES - 1) {
                moves[move_count++] = (Move){4, 6, KING, EMPTY, EMPTY, true, false, 0};
            }
        }

        if ((pos->castling_rights & WHITE_QUEENSIDE) &&
            king_square == 4 &&
            !(pos->all_pieces & 0xEULL) &&
            !is_square_attacked(pos, 3, BLACK) &&
            !is_square_attacked(pos, 2, BLACK)) {

            if (move_count < MAX_MOVES - 1) {
                moves[move_count++] = (Move){4, 2, KING, EMPTY, EMPTY, true, false, 0};
            }
        }
    } else {

        if ((pos->castling_rights & BLACK_KINGSIDE) &&
            king_square == 60 &&
            !(pos->all_pieces & 0x6000000000000000ULL) &&
            !is_square_attacked(pos, 61, WHITE) &&
            !is_square_attacked(pos, 62, WHITE)) {

            if (move_count < MAX_MOVES - 1) {
                moves[move_count++] = (Move){60, 62, KING, EMPTY, EMPTY, true, false, 0};
            }
        }

        if ((pos->castling_rights & BLACK_QUEENSIDE) &&
            king_square == 60 &&
            !(pos->all_pieces & 0xE00000000000000ULL) &&
            !is_square_attacked(pos, 59, WHITE) &&
            !is_square_attacked(pos, 58, WHITE)) {

            if (move_count < MAX_MOVES - 1) {
                moves[move_count++] = (Move){60, 58, KING, EMPTY, EMPTY, true, false, 0};
            }
        }
    }

    return move_count;
}

int generate_en_passant_moves(Position* pos, Move* moves, int move_count) {
    if (!pos || !moves || move_count < 0 || move_count >= MAX_MOVES - 5) return move_count;

    if (pos->en_passant_square == -1) return move_count;

    Color us = pos->to_move;
    int ep_square = pos->en_passant_square;

    Bitboard attacking_pawns = 0;

    if (us == WHITE && ep_square >= 40 && ep_square <= 47) {
        int file = ep_square % 8;

        if (file > 0) {
            int left_square = ep_square - 9;
            if (left_square >= 0 && left_square < 64 &&
                (pos->pieces[WHITE][PAWN] & (1ULL << left_square))) {
                attacking_pawns |= (1ULL << left_square);
            }
        }

        if (file < 7) {
            int right_square = ep_square - 7;
            if (right_square >= 0 && right_square < 64 &&
                (pos->pieces[WHITE][PAWN] & (1ULL << right_square))) {
                attacking_pawns |= (1ULL << right_square);
            }
        }
    } else if (us == BLACK && ep_square >= 16 && ep_square <= 23) {
        int file = ep_square % 8;

        if (file > 0) {
            int left_square = ep_square + 7;
            if (left_square >= 0 && left_square < 64 &&
                (pos->pieces[BLACK][PAWN] & (1ULL << left_square))) {
                attacking_pawns |= (1ULL << left_square);
            }
        }

        if (file < 7) {
            int right_square = ep_square + 9;
            if (right_square >= 0 && right_square < 64 &&
                (pos->pieces[BLACK][PAWN] & (1ULL << right_square))) {
                attacking_pawns |= (1ULL << right_square);
            }
        }
    }

    while (attacking_pawns && move_count < MAX_MOVES - 5) {
        int from = get_lsb(attacking_pawns);
        if (move_count < MAX_MOVES - 1) {
            moves[move_count++] = (Move){from, ep_square, PAWN, PAWN, EMPTY, false, true, 0};
        }
        pop_lsb(&attacking_pawns);
    }

    return move_count;
}

bool is_legal_move(Position* pos, Move* move) {
    if (!pos || !move) return false;

    Color us = pos->to_move;
    UndoInfo undo;
    make_move_full(pos, move, &undo);

    bool legal = !is_in_check(pos, us);

    undo_move_full(pos, move, &undo);

    return legal;
}

bool is_en_passant_legal(Position* pos, Move* move) {
    if (!pos || !move || !move->is_en_passant) return false;

    return is_legal_move(pos, move);
}

int generate_moves(Position* pos, Move* moves) {
    if (!pos || !moves) return 0;

    int move_count = 0;

    move_count = generate_pawn_moves(pos, moves, move_count);
    move_count = generate_piece_moves(pos, moves, move_count);
    move_count = generate_king_moves(pos, moves, move_count);
    move_count = generate_castling_moves(pos, moves, move_count);
    move_count = generate_en_passant_moves(pos, moves, move_count);

    return move_count;
}

int generate_legal_moves(Position* pos, Move* moves) {
    if (!pos || !moves) return 0;

    Move pseudo_moves[MAX_MOVES];
    int pseudo_count = generate_moves(pos, pseudo_moves);

    int legal_count = 0;
    for (int i = 0; i < pseudo_count && legal_count < MAX_MOVES - 1; i++) {
        if (is_legal_move(pos, &pseudo_moves[i])) {
            moves[legal_count++] = pseudo_moves[i];
        }
    }

    return legal_count;
}

static Bitboard checkers_of(Position* pos, int king_sq_idx, Color attacker_color) {
    Bitboard z = 0;

    Bitboard pawns_bb = pos->pieces[attacker_color][PAWN];
    Bitboard pawn_attack_bb = (attacker_color == WHITE)
        ? (((pawns_bb & ~file_masks[0]) << 7) | ((pawns_bb & ~file_masks[7]) << 9))
        : (((pawns_bb & ~file_masks[0]) >> 9) | ((pawns_bb & ~file_masks[7]) >> 7));
    if (pawn_attack_bb & (1ULL << king_sq_idx)) {

        Bitboard pawn_from = (attacker_color == WHITE)
            ? ((((1ULL << king_sq_idx) & ~file_masks[0]) >> 9) | (((1ULL << king_sq_idx) & ~file_masks[7]) >> 7))
            : ((((1ULL << king_sq_idx) & ~file_masks[0]) << 7) | (((1ULL << king_sq_idx) & ~file_masks[7]) << 9));
        z |= pawn_from & pawns_bb;
    }

    z |= knight_attacks[king_sq_idx] & pos->pieces[attacker_color][KNIGHT];

    Bitboard rook_sliders = pos->pieces[attacker_color][ROOK] | pos->pieces[attacker_color][QUEEN];
    if (rook_sliders) z |= get_rook_attacks_public(king_sq_idx, pos->all_pieces) & rook_sliders;

    Bitboard bishop_sliders = pos->pieces[attacker_color][BISHOP] | pos->pieces[attacker_color][QUEEN];
    if (bishop_sliders) z |= get_bishop_attacks_public(king_sq_idx, pos->all_pieces) & bishop_sliders;

    return z;
}

static Bitboard squares_between(int a, int b) {
    if (a == b) return 0;
    const int ra = a / 8, fa = a % 8;
    const int rb = b / 8, fb = b % 8;
    int dr = (rb > ra) - (rb < ra);
    int df = (fb > fa) - (fb < fa);
    if (!(ra == rb || fa == fb || (rb - ra == fb - fa) || (rb - ra == -(fb - fa)))) return 0;
    Bitboard m = 0;
    int r = ra + dr, f = fa + df;
    while (r != rb || f != fb) {
        if (r < 0 || r > 7 || f < 0 || f > 7) return 0;
        m |= 1ULL << (r * 8 + f);
        r += dr; f += df;
    }
    return m;
}

int generate_check_evasions(Position* pos, Move* moves) {
    if (!pos || !moves) return 0;

    const Color own_color = pos->to_move;
    const Color opp_color  = 1 - own_color;
    Bitboard king_bb1 = pos->pieces[own_color][KING];
    if (king_bb1 == 0) return 0;
    const int king_sq_idx = __builtin_ctzll(king_bb1);

    const Bitboard checkers_bb = checkers_of(pos, king_sq_idx, opp_color);
    if (checkers_bb == 0) return 0;
    const bool double_check = (checkers_bb & (checkers_bb - 1)) != 0;

    Bitboard targets = 0;
    int ep_capture_sq = -1;
    if (!double_check) {
        const int checker_sq = __builtin_ctzll(checkers_bb);
        targets |= checkers_bb;
        targets |= squares_between(king_sq_idx, checker_sq);

        if (pos->en_passant_square >= 0 &&
            (pos->pieces[opp_color][PAWN] & (1ULL << checker_sq))) {
            const int ep_pawn_sq = (own_color == WHITE) ? pos->en_passant_square - 8
                                                    : pos->en_passant_square + 8;
            if (ep_pawn_sq == checker_sq) ep_capture_sq = pos->en_passant_square;
        }
    }

    Move buf_area[MAX_MOVES];
    int total_moves = 0;
    total_moves = generate_pawn_moves(pos, buf_area, total_moves);
    total_moves = generate_piece_moves(pos, buf_area, total_moves);
    total_moves = generate_king_moves(pos, buf_area, total_moves);
    total_moves = generate_en_passant_moves(pos, buf_area, total_moves);

    int n = 0;
    for (int i = 0; i < total_moves && n < MAX_MOVES - 1; i++) {
        const Move* m = &buf_area[i];
        bool take_it = (m->piece == KING);
        if (!take_it && !double_check) {
            if (targets & (1ULL << m->to)) take_it = true;
            else if (ep_capture_sq >= 0 && m->is_en_passant &&
                     m->to == ep_capture_sq) take_it = true;
        }
        if (take_it) moves[n++] = buf_area[i];
    }
    return n;
}

int generate_legal_evasions(Position* pos, Move* moves) {
    if (!pos || !moves) return 0;

    Move evasion_cands[MAX_MOVES];
    const int n = generate_check_evasions(pos, evasion_cands);

    int legal_count = 0;
    for (int i = 0; i < n && legal_count < MAX_MOVES - 1; i++) {
        if (is_legal_move(pos, &evasion_cands[i])) {
            moves[legal_count++] = evasion_cands[i];
        }
    }
    return legal_count;
}

int generate_noisy_moves(Position* pos, Move* moves) {
    if (!pos || !moves) return 0;

    Move buf_area[MAX_MOVES];
    int total_moves = 0;
    total_moves = generate_pawn_moves(pos, buf_area, total_moves);
    total_moves = generate_piece_moves(pos, buf_area, total_moves);
    total_moves = generate_king_moves(pos, buf_area, total_moves);
    total_moves = generate_en_passant_moves(pos, buf_area, total_moves);

    int n = 0;
    for (int i = 0; i < total_moves && n < MAX_MOVES - 1; i++) {
        if (buf_area[i].captured != EMPTY || buf_area[i].promotion != EMPTY) {
            moves[n++] = buf_area[i];
        }
    }
    return n;
}

int generate_legal_noisy(Position* pos, Move* moves) {
    if (!pos || !moves) return 0;

    Move pseudo[MAX_MOVES];
    int pseudo_count = generate_noisy_moves(pos, pseudo);

    int legal_count = 0;
    for (int i = 0; i < pseudo_count && legal_count < MAX_MOVES - 1; i++) {
        if (is_legal_move(pos, &pseudo[i])) {
            moves[legal_count++] = pseudo[i];
        }
    }
    return legal_count;
}

void score_moves(Move* moves, int move_count, Move hash_move) {
    if (!moves || move_count <= 0) return;

    for (int i = 0; i < move_count; i++) {
        moves[i].score = 0;

        if (moves[i].from == hash_move.from && moves[i].to == hash_move.to
            && moves[i].promotion == hash_move.promotion) {
            moves[i].score = 10000;
            continue;
        }

        if (moves[i].captured != EMPTY && moves[i].captured < 7 && moves[i].piece < 7) {
            moves[i].score = mvv_lva[moves[i].piece][moves[i].captured];
        }

        if (moves[i].promotion != EMPTY && moves[i].promotion < 7) {

            static const int promo_rank[7] = {
                [PAWN] = 0, [ROOK] = 3, [KNIGHT] = 1, [BISHOP] = 2,
                [QUEEN] = 4, [KING] = 0, [EMPTY] = 0
            };
            moves[i].score = 900 + promo_rank[moves[i].promotion] * 10;
        }

        if (moves[i].is_castling) {
            moves[i].score = 50;
        }
    }
}

void score_moves_with_checks(Position* pos, Move* moves, int move_count, Move hash_move) {
    if (!pos || !moves || move_count <= 0) return;

    score_moves(moves, move_count, hash_move);

    for (int i = 0; i < move_count; i++) {

        UndoInfo temp_undo;
        make_move_full(pos, &moves[i], &temp_undo);
        bool gives_check = is_in_check(pos, pos->to_move);
        undo_move_full(pos, &moves[i], &temp_undo);

        if (gives_check) {
            moves[i].score += 800;

            if (moves[i].piece == QUEEN) {
                moves[i].score += 200;
            }

            if (moves[i].captured == EMPTY) {
                moves[i].score += 100;
            }
        }
    }
}

void score_moves_with_killers(Move* moves, int move_count, Move hash_move, int ply) {
    score_moves(moves, move_count, hash_move);

    for (int i = 0; i < move_count; i++) {
        if (is_killer_move(ply, moves[i])) {
            moves[i].score += 80;
        }
    }
}

void score_moves_with_history(Move* moves, int move_count, Move hash_move, int ply) {
    score_moves_with_killers(moves, move_count, hash_move, ply);

    for (int i = 0; i < move_count; i++) {
        moves[i].score += get_history_score(moves[i]);
    }
}

int compare_moves(const void* a, const void* b) {
    const Move* move_a = (const Move*)a;
    const Move* move_b = (const Move*)b;
    return move_b->score - move_a->score;
}

void debug_en_passant_position(Position* pos) {
    if (!pos) return;

}

void score_moves_with_history_enhanced(Position* pos, Move* moves, int move_count, Move hash_move, int ply) {

    score_moves_with_history(moves, move_count, hash_move, ply);

    for (int i = 0; i < move_count; i++) {
        if (moves[i].captured != EMPTY) {

            int see_value = static_exchange_evaluation(pos, &moves[i]);
            if (see_value < -50) {
                moves[i].score -= 200;
            } else if (see_value > 50) {
                moves[i].score += 150;
            }
        }

        int to_square = moves[i].to;
        if (to_square == 28 || to_square == 35 || to_square == 36 || to_square == 27) {
            moves[i].score += 25;
        }
    }
}
