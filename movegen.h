/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#ifndef MOVEGEN_H
#define MOVEGEN_H

#include "board.h"

extern Bitboard rook_attacks[64][4096];
extern Bitboard bishop_attacks[64][512];
extern Bitboard knight_attacks[64];
extern Bitboard king_attacks[64];

void init_magic_bitboards(void);

Bitboard get_bishop_attacks_public(int square, Bitboard occupied);
Bitboard get_rook_attacks_public(int square, Bitboard occupied);
Bitboard get_queen_attacks_public(int square, Bitboard occupied);

Bitboard ray_scan_rook_public(int square, Bitboard occupied);
Bitboard ray_scan_bishop_public(int square, Bitboard occupied);

bool is_square_attacked(Position* pos, int square, Color by_color);
bool is_in_check(Position* pos, Color color);
bool is_legal_move(Position* pos, Move* move);

bool is_en_passant_legal(Position* pos, Move* move);

int generate_pawn_moves(Position* pos, Move* moves, int move_count);
int generate_piece_moves(Position* pos, Move* moves, int move_count);
int generate_king_moves(Position* pos, Move* moves, int move_count);
int generate_castling_moves(Position* pos, Move* moves, int move_count);
int generate_en_passant_moves(Position* pos, Move* moves, int move_count);

int generate_moves(Position* pos, Move* moves);
int generate_legal_moves(Position* pos, Move* moves);

void score_moves(Move* moves, int move_count, Move hash_move);
void score_moves_with_killers(Move* moves, int move_count, Move hash_move, int ply);
int compare_moves(const void* a, const void* b);

bool is_killer_move(int ply, Move move);

void debug_en_passant_position(Position* pos);

void score_moves_with_history(Move* moves, int move_count, Move hash_move, int ply);
void score_moves_with_history_enhanced(Position* pos, Move* moves, int move_count, Move hash_move, int ply);
void score_moves_with_checks(Position* pos, Move* moves, int move_count, Move hash_move);

#endif
