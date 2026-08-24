/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#ifndef KING_SAFETY_H
#define KING_SAFETY_H

#include "board.h"
#include "eval_data.h"

int evaluate_king_safety(Position* pos);
int evaluate_king_safety_simplified(Position* pos);
int evaluate_king_activity_endgame(Position* pos);

int evaluate_castling_safety(Position* pos);
int evaluate_king_shelter(Position* pos, int king_square, Color color);
int evaluate_king_storm(Position* pos, int king_square, Color color);
int evaluate_open_files_near_king(Position* pos, int king_square, Color color);

int evaluate_king_centralization(Position* pos, Color color);
int evaluate_king_pawn_proximity(Position* pos, Color color);
int evaluate_king_opposition(Position* pos);

int count_attackers_near_king(Position* pos, int king_square, Color attacking_color);
bool is_king_in_danger(Position* pos, Color color);
Bitboard get_king_danger_zone(int king_square);
Bitboard get_king_safety_zone(int king_square);

bool has_castled(Position* pos, Color color);
bool can_castle_kingside(Position* pos, Color color);
bool can_castle_queenside(Position* pos, Color color);
int get_king_square(Position* pos, Color color);

#endif 
