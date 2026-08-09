/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#ifndef EVAL_H
#define EVAL_H

#include "board.h"

#define PAWN_VALUE   100
#define ROOK_VALUE   525
#define KNIGHT_VALUE 320
#define BISHOP_VALUE 330
#define QUEEN_VALUE  1000

#define BISHOP_PAIR_BONUS 50

void set_eval_debug(bool enable);

int evaluate_position(Position* pos);

int evaluate_material(Position* pos);
int evaluate_position_simple(Position* pos);

int evaluate_piece_square_tables(Position* pos);
int evaluate_pawn_structure(Position* pos);
int evaluate_king_safety(Position* pos);
int evaluate_mobility(Position* pos);
int evaluate_center_control(Position* pos);
int evaluate_tactical_patterns(Position* pos);

bool is_blunder_move(Position* pos, Move* move);
int evaluate_material_threats(Position* pos);

int evaluate_development_urgency(Position* pos);
int evaluate_anti_grabby_patterns(Position* pos);

int evaluate_hung_pieces(Position* pos);

extern Bitboard knight_attacks[64];
extern Bitboard king_attacks[64];

const int* get_king_middlegame_pst(void);
const int* get_king_endgame_pst(void);

#endif 
