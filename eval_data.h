/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#ifndef EVAL_DATA_H
#define EVAL_DATA_H

#include "board.h"
#include <stddef.h>

#define CASTLING_BONUS 40
#define CASTLING_RIGHTS_BONUS 10

#define PASSED_PAWN_BONUS 35
#define ISOLATED_PAWN_PENALTY -25
#define DOUBLED_PAWN_PENALTY -20
#define BACKWARD_PAWN_PENALTY -15
#define CONNECTED_PAWN_BONUS 12
#define PAWN_CHAIN_BONUS 8
#define ADVANCED_PASSED_BONUS 50

#define UNCASTLED_KING_PENALTY -60
#define OPEN_FILE_NEAR_KING_PENALTY -40

#define MOBILITY_BONUS_FACTOR 5
#define CENTER_CONTROL_BONUS 20
#define KNIGHT_OUTPOST_BONUS 25
#define ROOK_OPEN_FILE_BONUS 30
#define QUEEN_EARLY_DEVELOPMENT_PENALTY -40

#define PIN_BONUS 20
#define FORK_BONUS 30

#define CENTRAL_PAWN_DEVELOPMENT_BONUS 15

static const int pawn_pst[64] = {
     0,   0,   0,   0,   0,   0,   0,   0,
    50,  50,  50,  50,  50,  50,  50,  50,
    10,  10,  20,  30,  30,  20,  10,  10,
     5,   5,  10,  25,  25,  10,   5,   5,
     0,   0,   0,  20,  20,   0,   0,   0,
     5,  -5, -10,   0,   0, -10,  -5,   5,
     5,  10,  10, -20, -20,  10,  10,   5,
     0,   0,   0,   0,   0,   0,   0,   0
};

static const int rook_pst[64] = {
     0,   0,   0,   0,   0,   0,   0,   0,
     5,  10,  10,  10,  10,  10,  10,   5,
    -5,   0,   0,   0,   0,   0,   0,  -5,
    -5,   0,   0,   0,   0,   0,   0,  -5,
    -5,   0,   0,   0,   0,   0,   0,  -5,
    -5,   0,   0,   0,   0,   0,   0,  -5,
    -5,   0,   0,   0,   0,   0,   0,  -5,
     0,   0,   0,   5,   5,   0,   0,   0
};

static const int knight_pst[64] = {
    -50, -40, -30, -30, -30, -30, -40, -50,
    -40, -20,   0,   0,   0,   0, -20, -40,
    -30,   0,  10,  15,  15,  10,   0, -30,
    -30,   5,  15,  20,  20,  15,   5, -30,
    -30,   0,  15,  20,  20,  15,   0, -30,
    -30,   5,  10,  15,  15,  10,   5, -30,
    -40, -20,   0,   5,   5,   0, -20, -40,
    -50, -40, -30, -30, -30, -30, -40, -50
};

static const int bishop_pst[64] = {
    -20, -10, -10, -10, -10, -10, -10, -20,
    -10,   0,   0,   0,   0,   0,   0, -10,
    -10,   0,   5,  10,  10,   5,   0, -10,
    -10,   5,   5,  10,  10,   5,   5, -10,
    -10,   0,  10,  10,  10,  10,   0, -10,
    -10,  10,  10,  10,  10,  10,  10, -10,
    -10,   5,   0,   0,   0,   0,   5, -10,
    -20, -10, -10, -10, -10, -10, -10, -20
};

static const int queen_pst[64] = {
    -20, -10, -10,  -5,  -5, -10, -10, -20,
    -10,   0,   0,   0,   0,   0,   0, -10,
    -10,   0,   5,   5,   5,   5,   0, -10,
     -5,   0,   5,   5,   5,   5,   0,  -5,
      0,   0,   5,   5,   5,   5,   0,  -5,
    -10,   5,   5,   5,   5,   5,   0, -10,
    -10,   0,   5,   0,   0,   0,   0, -10,
    -20, -10, -10,  -5,  -5, -10, -10, -20
};

static const int king_middlegame_pst[64] = {
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -20, -30, -30, -40, -40, -30, -30, -20,
    -10, -20, -20, -20, -20, -20, -20, -10,
    -20, -200, -300, -300, -300, -300, -200, -20,
     20,  30,  10,   0,   0,  10,  30,  20
};

static const int king_endgame_pst[64] = {
    -50, -40, -30, -20, -20, -30, -40, -50,
    -30, -20, -10,   0,   0, -10, -20, -30,
    -30, -10,  20,  30,  30,  20, -10, -30,
    -30, -10,  30,  40,  40,  30, -10, -30,
    -30, -10,  30,  40,  40,  30, -10, -30,
    -30, -10,  20,  30,  30,  20, -10, -30,
    -30, -30,   0,   0,   0,   0, -30, -30,
    -50, -50, -30, -30, -30, -30, -50, -50
};

static const int* const pst[7] = {
    pawn_pst,
    rook_pst,
    knight_pst,
    bishop_pst,
    queen_pst,
    king_middlegame_pst,
    NULL
};

typedef enum {
    PHASE_OPENING,
    PHASE_MIDDLEGAME,
    PHASE_ENDGAME
} GamePhase;

typedef struct {
    float material;
    float pst;
    float pawns;
    float king_safety;
    float center_control;
    float development;
    float mobility;
    float piece_activity;
    float tactical;
} EvaluationWeights;

static const EvaluationWeights opening_weights = {
    .material = 1.0f,
    .pst = 1.0f,
    .pawns = 0.7f,
    .king_safety = 0.8f,
    .center_control = 1.5f,

    .development = 0.5f,
    .mobility = 0.8f,
    .piece_activity = 0.9f,
    .tactical = 1.0f
};

static const EvaluationWeights middlegame_weights = {
    .material = 1.0f,
    .pst = 1.2f,
    .pawns = 1.1f,
    .king_safety = 1.5f,
    .center_control = 1.0f,
    .development = 0.3f,
    .mobility = 1.3f,
    .piece_activity = 1.5f,
    .tactical = 1.6f
};

static const EvaluationWeights endgame_weights = {
    .material = 1.0f,
    .pst = 1.0f,
    .pawns = 1.6f,
    .king_safety = 1.8f,
    .center_control = 0.6f,
    .development = 0.0f,
    .mobility = 1.0f,
    .piece_activity = 1.3f,
    .tactical = 0.8f
};

static inline const int* get_pst_table(PieceType piece, GamePhase phase) {
    if (piece == KING) {
        return (phase == PHASE_ENDGAME) ? king_endgame_pst : king_middlegame_pst;
    }
    return (piece >= 0 && piece < 6) ? pst[piece] : NULL;
}

static inline EvaluationWeights get_evaluation_weights(GamePhase phase) {
    switch (phase) {
        case PHASE_OPENING:
            return opening_weights;
        case PHASE_ENDGAME:
            return endgame_weights;
        default:
            return middlegame_weights;
    }
}

static inline GamePhase detect_game_phase(Position* pos) {
    if (!pos) return PHASE_MIDDLEGAME;

    int total_material = 0;
    for (Color color = WHITE; color <= BLACK; color++) {
        total_material += __builtin_popcountll(pos->pieces[color][QUEEN]) * 9;
        total_material += __builtin_popcountll(pos->pieces[color][ROOK]) * 5;
        total_material += __builtin_popcountll(pos->pieces[color][BISHOP]) * 3;
        total_material += __builtin_popcountll(pos->pieces[color][KNIGHT]) * 3;
    }

    if (total_material > 30) return PHASE_OPENING;
    if (total_material < 12) return PHASE_ENDGAME;
    return PHASE_MIDDLEGAME;
}

#define PHASE_MATERIAL_MAX 62

#define PHASE_ANCHOR_MID ((21 * 256 + PHASE_MATERIAL_MAX / 2) / PHASE_MATERIAL_MAX)

static inline int game_phase_256(const Position* pos) {
    if (!pos) return 256;

    int total_material = 0;
    for (Color color = WHITE; color <= BLACK; color++) {
        total_material += __builtin_popcountll(pos->pieces[color][QUEEN]) * 9;
        total_material += __builtin_popcountll(pos->pieces[color][ROOK]) * 5;
        total_material += __builtin_popcountll(pos->pieces[color][BISHOP]) * 3;
        total_material += __builtin_popcountll(pos->pieces[color][KNIGHT]) * 3;
    }
    if (total_material > PHASE_MATERIAL_MAX) total_material = PHASE_MATERIAL_MAX;

    return (total_material * 256 + PHASE_MATERIAL_MAX / 2) / PHASE_MATERIAL_MAX;
}

static inline EvaluationWeights get_evaluation_weights_tapered(int p256) {
    const EvaluationWeights* a;
    const EvaluationWeights* b;
    int numerator;
    int denominator;

    if (p256 <= PHASE_ANCHOR_MID) {
        a = &endgame_weights;    b = &middlegame_weights;
        numerator = p256;                     denominator = PHASE_ANCHOR_MID;
    } else {
        a = &middlegame_weights; b = &opening_weights;
        numerator = p256 - PHASE_ANCHOR_MID;  denominator = 256 - PHASE_ANCHOR_MID;
    }

    const float t = (float)numerator / (float)denominator;
    EvaluationWeights w;
    w.material       = a->material       + (b->material       - a->material)       * t;
    w.pst            = a->pst            + (b->pst            - a->pst)            * t;
    w.pawns          = a->pawns          + (b->pawns          - a->pawns)          * t;
    w.king_safety    = a->king_safety    + (b->king_safety    - a->king_safety)    * t;
    w.center_control = a->center_control + (b->center_control - a->center_control) * t;
    w.development    = a->development    + (b->development    - a->development)    * t;
    w.mobility       = a->mobility       + (b->mobility       - a->mobility)       * t;
    w.piece_activity = a->piece_activity + (b->piece_activity - a->piece_activity) * t;
    w.tactical       = a->tactical       + (b->tactical       - a->tactical)       * t;
    return w;
}

static inline int king_pst_tapered(int pst_square, int p256) {
    const int mg = king_middlegame_pst[pst_square];
    const int eg = king_endgame_pst[pst_square];
    return (mg * p256 + eg * (256 - p256)) / 256;
}

#endif 
