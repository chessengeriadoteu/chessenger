/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>
#include <stdbool.h>

#define DECISIVE_STOP_MIN_DEPTH 10

typedef uint64_t Bitboard;
typedef enum { WHITE, BLACK } Color;
typedef enum { PAWN, ROOK, KNIGHT, BISHOP, QUEEN, KING, EMPTY } PieceType;

#define WHITE_KINGSIDE  1
#define WHITE_QUEENSIDE 2
#define BLACK_KINGSIDE  4
#define BLACK_QUEENSIDE 8

extern bool uci_chess960;
extern int castle_rook_sq[4];
static inline void castle_960_squares(int from, int to, int* king_to, int* rook_to) {
    const int back_rank_base = from & 56;
    *king_to = back_rank_base + ((to > from) ? 6 : 2);
    *rook_to = back_rank_base + ((to > from) ? 5 : 3);
}

#define MAX_POSITION_HISTORY 246

#define HISTORY_SEARCH_RESERVE 144

#define HISTORY_GAME_LIMIT (MAX_POSITION_HISTORY - HISTORY_SEARCH_RESERVE)

typedef struct {
    int8_t from;
    int8_t to;
    int8_t piece;
    int8_t captured;
    int8_t promotion;
    bool is_castling;
    bool is_en_passant;
    int score;
} Move;

typedef struct {
    Bitboard pieces[2][7];
    Bitboard occupied[2];
    Bitboard all_pieces;
    Color to_move;
    int castling_rights;
    int en_passant_square;
    int halfmove_clock;
    int fullmove_number;

    uint64_t hash;

    int mat_balance;
    int mat_phase;

    uint64_t position_history[MAX_POSITION_HISTORY];
    int history_count;
    int history_base;

} Position;

typedef struct {
    PieceType captured;
    int castling_rights;
    int en_passant_square;
    int halfmove_clock;
    bool was_en_passant;
    bool was_castling;
    int captured_square;

    int old_history_count;
    int old_history_base;
    uint64_t removed_hash;

    uint64_t old_hash;

    int old_mat_balance;
    int old_mat_phase;
} UndoInfo;

void recompute_material(Position* pos);

typedef enum {
    GAME_ONGOING,
    CHECKMATE_WHITE,
    CHECKMATE_BLACK,
    STALEMATE,
    DRAW_50_MOVES,
    DRAW_INSUFFICIENT_MATERIAL,
    DRAW_REPETITION
} GameResult;

void setup_starting_position(Position* pos);

bool parse_fen(Position* pos, const char* fen);
const char* fen_last_error(void);
void make_move_full(Position* pos, Move* move, UndoInfo* undo);
void undo_move_full(Position* pos, Move* move, UndoInfo* undo);
GameResult check_game_result(Position* pos);
bool has_insufficient_material(Position* pos);

bool is_threefold_repetition(Position* pos);
bool is_approaching_repetition(Position* pos);
int count_position_occurrences(Position* pos, uint64_t hash);
void add_position_to_history(Position* pos, uint64_t hash);
void remove_last_position_from_history(Position* pos, UndoInfo* undo);

void compact_position_history(Position* pos);

Move parse_move(Position* pos, const char* move_str);

#endif 
