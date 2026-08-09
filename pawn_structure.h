/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#ifndef PAWN_STRUCTURE_H
#define PAWN_STRUCTURE_H

#include "board.h"
#include "eval_data.h"

int evaluate_pawn_structure(Position* pos);
int evaluate_passed_pawns_simplified(Position* pos);
int evaluate_pawn_weaknesses(Position* pos);
int evaluate_connected_pawns(Position* pos);
int evaluate_pawn_chains(Position* pos);

int evaluate_pawn_islands(Position* pos);
int evaluate_advanced_passed_pawns(Position* pos);
bool is_passed_pawn(Position* pos, int square, Color color);
bool is_isolated_pawn(Position* pos, int square, Color color);
bool is_doubled_pawn(Position* pos, int square, Color color);
bool is_backward_pawn(Position* pos, int square, Color color);

int count_pawn_islands(Bitboard pawns);
Bitboard get_passed_pawns(Position* pos, Color color);
Bitboard get_isolated_pawns(Position* pos, Color color);
Bitboard get_doubled_pawns(Position* pos, Color color);
Bitboard get_connected_pawns(Position* pos, Color color);

#endif 
