/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT
 * Copyright (C) Dariusz Domagała
 * Copyright (C) 2021 kz04px (4ku 5.1, MIT, see LICENSES/4KU-MIT.txt)
 * Copyright (C) 2019-2025 Morgan Houppin (Stash 37)
 * Portions of this file are adapted from Stash 37. Modified by Dariusz Domagała, 2026-10-21.
 * KPK bitbase adapted from Stash 37, whose KPK bitbase follows Stockfish's bitbase.cpp.
 */
static int lvl_get_king_square(Position* pos, Color color);
static int lvl_evaluate_king_shelter(Position* pos, int king_square, Color color);
static void lvl_endgame_init(void);
static int lvl_is_endgame_special(const Position* pos);
static int lvl_scale_material_balance(const Position* pos, int balance);
static int lvl_evaluate_position(Position* pos);

#define KING_SHELTER_WEIGHT 100
#define PAWN_HASH_BITS 14
#define PAWN_TERMS_WEIGHT 100
#define OPEN_FILE_NEAR_KING_PENALTY -40

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

    const int total_material = pos->mat_phase;

    if (total_material > 30) return PHASE_OPENING;
    if (total_material < 12) return PHASE_ENDGAME;
    return PHASE_MIDDLEGAME;
}

#define PHASE_MATERIAL_MAX 62

#define PHASE_ANCHOR_MID ((21 * 256 + PHASE_MATERIAL_MAX / 2) / PHASE_MATERIAL_MAX)

static inline int game_phase_256(const Position* pos) {
    if (!pos) return 256;

    int total_material = pos->mat_phase;
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

#define PAWN_VALUE   100
#define QUEEN_VALUE  1000
#define MATERIAL_VALUE(t) (lvl_material_values_ptr[t])

static int lvl_get_king_square(Position* pos, Color color) {
    if (!pos) return -1;

    Bitboard king = pos->pieces[color][KING];
    return (king != 0) ? get_lsb(king) : -1;
}

static int lvl_evaluate_king_shelter(Position* pos, int king_square, Color color) {
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

#define WEIGHT_TABLE static const

#define BISHOP_PAIR_VALUE_MG     33
#define BISHOP_PAIR_VALUE_EG     65
#define INITIATIVE_MG    8
#define INITIATIVE_EG    0
#define SCALE_BASE_FACTOR 130
#define SCALE_PER_PAWN 49

static bool eval_debug_enabled = false;

typedef struct {
    Bitboard attacks_from[64];
    Bitboard combined[2];
} AttackMaps;

static inline int phase_minus_knights(const Position* pos) {

    return pos->mat_phase
         - __builtin_popcountll(pos->pieces[WHITE][KNIGHT])
         - __builtin_popcountll(pos->pieces[BLACK][KNIGHT]);
}

static inline Bitboard pawn_attacks_all(const Position* pos, Color by) {
    Bitboard p = pos->pieces[by][PAWN];
    return (by == WHITE)
        ? (((p & ~file_masks[0]) << 7) | ((p & ~file_masks[7]) << 9))
        : (((p & ~file_masks[0]) >> 9) | ((p & ~file_masks[7]) >> 7));
}

static Bitboard knight_center_mask = 0;
static inline Bitboard knight_center_squares(void) {
    if (!knight_center_mask) {
        for (int sq = 0; sq < 64; sq++)
            if (__builtin_popcountll(knight_attacks[sq]) >= 4)
                knight_center_mask |= (1ULL << sq);
    }
    return knight_center_mask;
}

static Bitboard attacked_squares_of(Position* pos, Color color) __attribute__((unused));
static Bitboard attacked_squares_of(Position* pos, Color color) {
    Bitboard occ = pos->all_pieces;

    Bitboard pawns = pos->pieces[color][PAWN];
    Bitboard map = (color == WHITE)
        ? (((pawns & ~file_masks[0]) << 7) | ((pawns & ~file_masks[7]) << 9))
        : (((pawns & ~file_masks[0]) >> 9) | ((pawns & ~file_masks[7]) >> 7));

    Bitboard b = pos->pieces[color][KNIGHT];
    while (b) { map |= knight_attacks[get_lsb(b)]; pop_lsb(&b); }

    b = pos->pieces[color][KING];
    while (b) { map |= king_attacks[get_lsb(b)]; pop_lsb(&b); }

    b = pos->pieces[color][ROOK] | pos->pieces[color][QUEEN];
    while (b) { map |= get_rook_attacks_public(get_lsb(b), occ); pop_lsb(&b); }

    b = pos->pieces[color][BISHOP] | pos->pieces[color][QUEEN];
    while (b) { map |= get_bishop_attacks_public(get_lsb(b), occ); pop_lsb(&b); }

    return map;
}

static int lvl_hung_penalty_stm = 0;

WEIGHT_TABLE int lvl_pst_mg_pawn[64] = {
         0,     0,     0,     0,     0,     0,     0,     0,
       102,   134,    77,   103,    94,   126,    34,   -13,
        20,   -11,    56,    21,    69,    78,    45,    12,
        10,     9,    16,    25,    27,    32,     5,   -15,
        -5,     4,    -3,     4,     9,    10,     2,   -11,
       -14,   -22,   -16,   -10,   -13,   -13,    -7,   -28,
       -11,   -21,   -16,   -29,   -23,     0,    10,   -38,
         0,     0,     0,     0,     0,     0,     0,     0,
};

WEIGHT_TABLE int lvl_pst_eg_pawn[64] = {
         0,     0,     0,     0,     0,     0,     0,     0,
       140,   135,   178,   146,   151,   132,   165,   162,
        86,    74,    59,    49,    32,    45,    58,    50,
        52,    32,    17,     3,     6,    14,    37,    25,
        33,    21,     9,    -7,     5,    12,    15,     7,
        20,    11,    10,     9,    12,    15,     3,     4,
        33,    28,     8,    18,    29,    24,    14,    17,
         0,     0,     0,     0,     0,     0,     0,     0,
};

WEIGHT_TABLE int lvl_pst_mg_knight[64] = {
      -167,   -89,   -34,   -49,    61,   -97,   -15,  -107,
       -73,   -41,    78,    36,    23,    62,     7,   -17,
       -47,    45,    33,    71,    98,   117,    73,    44,
        13,    15,    39,    57,    35,    53,    28,    38,
        13,    18,    32,    13,    32,    23,    25,    30,
       -11,     1,     4,    15,    27,    15,    15,     2,
       -29,   -53,     6,     3,    -1,    18,    -2,     7,
      -105,   -15,   -48,   -32,    -3,    -4,   -29,   -23,
};

WEIGHT_TABLE int lvl_pst_eg_knight[64] = {
       -58,   -38,   -13,   -28,   -31,   -27,   -63,   -99,
       -25,    -8,   -12,    -2,    -9,   -25,   -24,   -52,
       -24,   -14,     8,    11,     1,   -21,   -19,   -41,
       -13,    13,    24,    22,    18,    11,    20,    -4,
        -4,     4,    16,    21,    22,    17,     6,    -8,
       -31,    -3,    -5,    11,    10,   -19,   -16,   -20,
       -42,   -20,   -12,   -15,   -20,   -32,   -13,   -28,
       -29,   -27,   -27,    -2,   -24,   -34,   -44,   -64,
};

WEIGHT_TABLE int lvl_pst_mg_bishop[64] = {
       -29,     4,   -82,   -37,   -25,   -42,     7,    -8,
       -22,     6,    -1,   -13,    25,    56,    18,   -47,
         0,    17,    33,    30,    41,    68,    45,    30,
        -8,     5,    21,    44,    29,    39,    33,   -13,
        -2,    11,    19,    30,    22,    12,    34,    30,
        12,    31,    13,    15,    14,     7,    38,    30,
        30,    11,    20,     8,    15,    25,    33,    23,
         2,    19,    12,   -17,     3,     0,   -39,   -21,
};

WEIGHT_TABLE int lvl_pst_eg_bishop[64] = {
       -14,   -21,   -11,    -8,    -7,    -9,   -17,   -24,
         0,   -18,    15,     0,    -9,   -13,    -4,   -14,
         4,   -22,   -10,    -5,     4,    10,     8,     0,
         1,    21,    14,     9,     8,     2,    -5,     0,
         2,     1,    11,    17,     9,     6,   -15,   -15,
        -2,     3,    -6,    18,    17,    -5,   -13,   -15,
        -2,   -26,   -15,     3,    -8,    -7,   -35,   -17,
       -19,    -7,   -11,    -5,    -7,    -4,    -5,   -17,
};

WEIGHT_TABLE int lvl_pst_mg_rook[64] = {
        32,    42,    30,    51,    55,     9,    42,    54,
        31,    14,    52,    56,    76,    68,    30,    46,
         7,    27,    28,    38,    31,    45,    75,    12,
       -18,    27,    19,    20,    26,    37,    24,   -10,
       -36,   -18,   -16,     3,    -3,   -25,     3,   -35,
       -29,   -18,   -20,   -13,    -1,    -8,     9,   -29,
       -48,   -16,   -20,    -9,     0,     1,    12,   -45,
       -13,    -5,    -7,   -17,    -6,     1,    -5,   -24,
};

WEIGHT_TABLE int lvl_pst_eg_rook[64] = {
         9,    22,    28,    15,     6,    22,    32,    22,
        33,    33,    23,    22,    13,    13,    20,    16,
        43,    29,    29,    19,    10,    19,    27,    19,
        34,    33,    29,    17,    14,     9,    20,    20,
        19,    27,    24,     4,     5,    14,     2,    11,
         6,     3,    13,    13,   -11,     2,    -4,   -12,
         2,     4,     4,    -2,   -11,    -3,   -15,    -5,
        -1,    -2,     3,     5,   -13,    -5,     8,    12,
};

WEIGHT_TABLE int lvl_pst_mg_queen[64] = {
       -28,     0,    29,    12,    59,    44,    43,    45,
       -28,   -36,     1,    -1,   -16,    57,    28,    54,
         2,     3,     6,   -12,    16,    40,    47,    38,
        -3,   -29,    -3,   -30,   -13,    17,     5,    13,
         7,    -3,    -9,   -10,    -4,     1,    19,    13,
       -21,     4,     9,    -6,    15,    38,    33,    17,
       -25,   -16,    11,    16,    16,    19,     9,     1,
        13,   -26,   -13,     2,    -1,   -21,   -31,   -50,
};

WEIGHT_TABLE int lvl_pst_eg_queen[64] = {
        -9,    22,    22,    27,    27,    19,    10,    20,
         1,    34,    36,    40,    58,    25,    30,     0,
       -10,    24,    23,    57,    41,    37,    19,     3,
        17,    42,    33,    51,    55,    38,    52,    32,
        -8,    39,    31,    57,    47,    42,    49,    23,
       -13,    -3,    15,    30,    15,    -3,    12,    -7,
       -20,   -15,   -14,    -8,    -8,   -31,   -41,   -32,
       -37,   -20,   -28,   -51,   -17,   -50,   -20,   -41,
};

WEIGHT_TABLE int lvl_pst_mg_king[64] = {
       -65,    23,    16,   -15,   -56,   -34,     2,    13,
        29,    -1,   -20,    -7,    -8,    -4,   -38,   -29,
        -9,    24,     2,   -10,   -16,    10,    24,   -22,
       -17,   -18,   -17,   -25,   -24,   -21,    -8,   -36,
       -49,     8,   -21,   -31,   -38,   -36,   -27,   -63,
       -14,   -10,   -13,   -31,   -24,   -22,   -23,   -39,
        -9,    -5,    10,   -18,   -23,    10,     9,   -16,
        -5,    -2,     0,   -50,   -28,   -20,    16,    -2,
};

WEIGHT_TABLE int lvl_pst_eg_king[64] = {
       -74,   -35,   -18,   -18,   -11,    15,     4,   -17,
       -12,    17,    14,    17,    17,    38,    23,    11,
        10,    17,    41,    49,    40,    67,    58,    13,
        -8,    36,    42,    47,    38,    53,    47,    11,
       -18,    17,    25,    30,    35,    33,    21,   -13,
       -19,    -7,    -1,    17,    15,     8,    -9,   -29,
       -23,   -25,    -4,    -1,    -2,   -12,   -25,   -37,
       -77,   -42,   -25,   -11,   -46,   -34,   -60,   -83,
};

WEIGHT_TABLE int lvl_passed_bonus_mg[8] = {
         0,   -11,   -34,    -6,    31,    90,   148,     0,
};

WEIGHT_TABLE int lvl_passed_bonus_eg[8] = {
         0,    12,    32,    47,    72,    96,    92,     0,
};

WEIGHT_TABLE int lvl_mobility_mg[6] = {
         0,     3,     4,     4,     2,     0,
};

WEIGHT_TABLE int lvl_mobility_eg[6] = {
         0,     4,     4,     5,     4,     0,
};

WEIGHT_TABLE int lvl_king_zone_weight[6] = {
         0,     3,     2,     2,     5,     0,
};

WEIGHT_TABLE int lvl_eval_piece_value[6] = {    95,   679,   386,   427,  1298,     0, };

static const int* const pst_mg_tables[6] = { lvl_pst_mg_pawn, lvl_pst_mg_rook, lvl_pst_mg_knight,
                                         lvl_pst_mg_bishop, lvl_pst_mg_queen, lvl_pst_mg_king };
static const int* const pst_eg_tables[6] = { lvl_pst_eg_pawn, lvl_pst_eg_rook, lvl_pst_eg_knight,
                                         lvl_pst_eg_bishop, lvl_pst_eg_queen, lvl_pst_eg_king };

static const int* const lvl_material_values_ptr = lvl_eval_piece_value;

WEIGHT_TABLE int lvl_ks_piece_weight_mg[6] = { 0, 36, 55, 43, 3, 0 };
WEIGHT_TABLE int lvl_ks_piece_weight_eg[6] = { 0, -47, 35, 65, 3, 0 };
#define KS_ZONE_ATTACK_MG      8
#define KS_ZONE_ATTACK_EG     (-2)
#define KS_WEAK_SQ_MG    15
#define KS_WEAK_SQ_EG   (-51)
#define KS_CHECK_N_MG  67
#define KS_CHECK_N_EG  19
#define KS_CHECK_B_MG  22
#define KS_CHECK_B_EG  76
#define KS_CHECK_R_MG  86
#define KS_CHECK_R_EG  92
#define KS_CHECK_Q_MG  44
#define KS_CHECK_Q_EG 127
#define KS_UNSAFE_MG  16
#define KS_UNSAFE_EG  61
#define KS_NO_QUEEN_MG (-133)
#define KS_NO_QUEEN_EG (-29)
#define KS_OFFSET_MG   (-2)
#define KS_OFFSET_EG   44
#define KS_PAWN_NORM_MG    103
#define KS_PAWN_NORM_EG    211

WEIGHT_TABLE int lvl_pawn_protected_mg[6] = { 26, 5, 2, 7, -11, -23 };
WEIGHT_TABLE int lvl_pawn_protected_eg[6] = { 12, 15, -1, 5, 23, 25 };

WEIGHT_TABLE int lvl_pawn_threat_mg[6] = { 4, 18, 6, 3, 8, -2 };
WEIGHT_TABLE int lvl_pawn_threat_eg[6] = { -23, 12, 2, 9, 11, 19 };

WEIGHT_TABLE int lvl_attacked_by_pawn_mg[2] = { 66, 89 };
WEIGHT_TABLE int lvl_attacked_by_pawn_eg[2] = { 34, 67 };

WEIGHT_TABLE int lvl_protected_passer_mg = 18;
WEIGHT_TABLE int lvl_protected_passer_eg = -8;

WEIGHT_TABLE int lvl_blocked_passer_mg[4] = { 22, 20, 5, 34 };
WEIGHT_TABLE int lvl_blocked_passer_eg[4] = { 26, 35, 59, 58 };

WEIGHT_TABLE int lvl_passer_king_dist_mg[2] = { -1, -1 };
WEIGHT_TABLE int lvl_passer_king_dist_eg[2] = { -4, 8 };

WEIGHT_TABLE int lvl_phalanx_mg = 13;
WEIGHT_TABLE int lvl_phalanx_eg = 11;

WEIGHT_TABLE int lvl_doubled_mg = 2;
WEIGHT_TABLE int lvl_doubled_eg = 22;

static inline int iabs(int x) { return x < 0 ? -x : x; }
static inline int imax(int a, int b) { return a > b ? a : b; }

WEIGHT_TABLE int lvl_rook_open_file_mg[2][6] = { { 0, 5, 2, -7, 3, 0 }, { 0, 17, -4, -11, -17, 0 } };
WEIGHT_TABLE int lvl_rook_open_file_eg[2][6] = { { 0, 25, 2, 14, 12, 0 }, { 0, 9, -10, -1, 24, 0 } };

static Bitboard passed_span_mask[2][64];
static Bitboard file_mask_of_sq[64];
static bool pawn_masks_ready = false;

static void build_pawn_masks(void) {
    for (int sq = 0; sq < 64; sq++) {
        const int file_idx = sq % 8;
        const int rank_idx = sq / 8;
        Bitboard files_bb = 0x0101010101010101ULL << file_idx;
        file_mask_of_sq[sq] = files_bb;
        if (file_idx > 0) files_bb |= 0x0101010101010101ULL << (file_idx - 1);
        if (file_idx < 7) files_bb |= 0x0101010101010101ULL << (file_idx + 1);

        Bitboard ahead_white = (rank_idx == 7) ? 0ULL : (~0ULL << (8 * (rank_idx + 1)));
        Bitboard ahead_black = (rank_idx == 0) ? 0ULL : ((1ULL << (8 * rank_idx)) - 1);
        passed_span_mask[WHITE][sq] = files_bb & ahead_white;
        passed_span_mask[BLACK][sq] = files_bb & ahead_black;
    }
    pawn_masks_ready = true;
}

static inline Bitboard pawn_set_attacks(Bitboard pawns_bb, Color side_c) {
    const Bitboard not_a_file = pawns_bb & ~0x0101010101010101ULL;
    const Bitboard not_h_file = pawns_bb & ~0x8080808080808080ULL;
    return (side_c == WHITE) ? ((not_a_file << 7) | (not_h_file << 9))
                            : ((not_h_file >> 7) | (not_a_file >> 9));
}

WEIGHT_TABLE int lvl_knight_mob_mg[9]  = { -49, -34, -18, -8, -4, 4, 14, 17, 22 };
WEIGHT_TABLE int lvl_knight_mob_eg[9]  = { 17, -32, 45, 95, 117, 127, 141, 148, 116 };
WEIGHT_TABLE int lvl_bishop_mob_mg[14] = { -85, -53, -25, -21, -12, -5, -1, 0, 7, 1, 15, 9, 11, 24 };
WEIGHT_TABLE int lvl_bishop_mob_eg[14] = { -42, -45, -38, 22, 29, 50, 73, 59, 70, 78, 53, 48, 59, 49 };
WEIGHT_TABLE int lvl_rook_mob_mg[15] = { -123, -61, -37, -40, -31, -37, -32, -27, -18, -11, -13, -6, -5, 8, 21 };
WEIGHT_TABLE int lvl_rook_mob_eg[15] = { -45, 41, 69, 100, 106, 142, 167, 155, 180, 183, 197, 201, 198, 191, 179 };
WEIGHT_TABLE int lvl_queen_mob_mg[28] = { -50, 23, 3, -3, -24, -12, -5, -7, 0, -1, 6, 7, 12, 11, 12, 16, 29, 28, 24, 36, 43, 45, 36, 28, 50, -1, 14, 43 };
WEIGHT_TABLE int lvl_queen_mob_eg[28] = { -138, 192, 153, 114, 68, 119, 165, 184, 182, 199, 211, 219, 237, 249, 242, 261, 249, 253, 233, 235, 206, 197, 183, 170, 158, 167, 178, 154 };

static const int mob_offset_mg[6] = { 0,  39,  31,  34,  13, 0 };
static const int mob_offset_eg[6] = { 0, -82, -50,  13, -133, 0 };

WEIGHT_TABLE int lvl_knight_closed_mg[5] = { 45, 52, 44, 56, 55 };
WEIGHT_TABLE int lvl_knight_closed_eg[5] = { 7, 39, 70, 77, 96 };
#define KNIGHT_CLOSED_OFFSET_MG 12
#define KNIGHT_CLOSED_OFFSET_EG 24

WEIGHT_TABLE int lvl_bishop_color_pawns_mg[7] = { 29, 37, 29, 19, 8, 3, -1 };
WEIGHT_TABLE int lvl_bishop_color_pawns_eg[7] = { 80, 38, 31, 17, 13, 1, -23 };
#define BISHOP_COLOR_OFFSET_MG 10
#define BISHOP_COLOR_OFFSET_EG 16

WEIGHT_TABLE int lvl_far_from_king_mg[6]   = { 0, -11, -16, 1, -3, 0 };
WEIGHT_TABLE int lvl_far_from_king_eg[6]   = { 0, 30, 6, -18, 56, 0 };
static const int far_king_offset_mg[6] = { 0,  -5,  -8,  -3,  -3, 0 };
static const int far_king_offset_eg[6] = { 0,   3,  -4,  -4,   5, 0 };
#define KNIGHT_SHIELDED_MG     24
#define KNIGHT_SHIELDED_EG    25
#define KNIGHT_OUTPOST_MG   25
#define KNIGHT_OUTPOST_EG   27
#define BISHOP_SHIELDED_MG      9
#define BISHOP_SHIELDED_EG      1
#define BISHOP_OUTPOST_MG    34
#define BISHOP_OUTPOST_EG    0
#define BISHOP_CENTER_ATTACK_MG   17
#define BISHOP_CENTER_ATTACK_EG   30
#define ROOK_BLOCKED_PAWN_MG    (-16)
#define ROOK_BLOCKED_PAWN_EG   6
#define ROOK_QUEEN_FILE_MG     17
#define ROOK_QUEEN_FILE_EG      16
#define ROOK_TRAPPED_CASTLING_MG   31
#define ROOK_TRAPPED_CASTLING_EG  (-10)
#define ROOK_TRAPPED_NOCASTLE_MG  (-65)
#define ROOK_TRAPPED_NOCASTLE_EG  (-7)

#define BACKWARD_PAWN_MG     (-23)
#define BACKWARD_PAWN_EG     (-16)
#define ISOLATED_PAWN_MG     9
#define ISOLATED_PAWN_EG    (-30)
WEIGHT_TABLE int lvl_phalanx_rank_mg[8] = { 0, 7, 20, 26, 90, 175, 183, 0 };
WEIGHT_TABLE int lvl_phalanx_rank_eg[8] = { 0, -20, 17, 42, 99, 267, 252, 0 };
WEIGHT_TABLE int lvl_supported_pawn_mg[8] = { 0, 24, 9, 13, 69, 179, 0, 0 };
WEIGHT_TABLE int lvl_supported_pawn_eg[8] = { 0, 46, 34, 42, 96, 171, 0, 0 };
#define EXTRA_PAWN_NORM_MG 103
#define EXTRA_PAWN_NORM_EG 211
#define DARK_SQUARES  0xAA55AA55AA55AA55ULL
#define CENTER_SQUARES 0x0000001818000000ULL

static inline int rel_rank_of_sq(int sq, Color k) { return (k == WHITE) ? (sq >> 3) : 7 - (sq >> 3); }
static inline int sq_distance(int a, int b) {
    const int dr = (a >> 3) - (b >> 3), df = (a & 7) - (b & 7);
    const int r = dr < 0 ? -dr : dr, f = df < 0 ? -df : df;
    return r > f ? r : f;
}

static Bitboard pawn_reach(Bitboard pawns_bb, Color k) {
    Bitboard z = 0;
    while (pawns_bb) {
        const int sq = get_lsb(pawns_bb);
        pop_lsb(&pawns_bb);
        z |= passed_span_mask[k][sq] & ~file_mask_of_sq[sq];
    }
    return z;
}

static void extra_pawn_terms(const Bitboard pawns_by_color[2], const Bitboard pawn_att[2],
                     const Bitboard reach[2], int* mg, int* eg) {
    for (Color k = WHITE; k <= BLACK; k++) {
        const Color w = (k == WHITE) ? BLACK : WHITE;
        const int side_sign = (k == WHITE) ? 1 : -1;
        const Bitboard own_pawn_bb = pawns_by_color[k], opp_color = pawns_by_color[w];

        const Bitboard stop = (k == WHITE) ? (own_pawn_bb << 8) : (own_pawn_bb >> 8);
        const Bitboard s = stop & pawn_att[w] & ~reach[k];
        Bitboard behind = (k == WHITE) ? (s >> 8) : (s << 8);
        for (Bitboard b = behind; b; ) {
            const int sq = get_lsb(b);
            pop_lsb(&b);
            if (passed_span_mask[k][sq] & file_mask_of_sq[sq] & opp_color) behind &= ~(1ULL << sq);
        }
        const int n_behind = __builtin_popcountll(behind);
        *mg += side_sign * n_behind * BACKWARD_PAWN_MG;
        *eg += side_sign * n_behind * BACKWARD_PAWN_EG;

        for (int f = 0; f < 8; f++) {
            const Bitboard pl = 0x0101010101010101ULL << f;
            if (!(own_pawn_bb & pl)) continue;
            Bitboard beside = 0;
            if (f > 0) beside |= pl >> 1;
            if (f < 7) beside |= pl << 1;
            if (!(own_pawn_bb & beside)) { *mg += side_sign * ISOLATED_PAWN_MG; *eg += side_sign * ISOLATED_PAWN_EG; }
        }

        for (Bitboard b = own_pawn_bb & ((own_pawn_bb >> 1) & ~0x8080808080808080ULL); b; ) {
            const int r = rel_rank_of_sq(get_lsb(b), k);
            pop_lsb(&b);
            *mg += side_sign * lvl_phalanx_rank_mg[r];
            *eg += side_sign * lvl_phalanx_rank_eg[r];
        }

        const Bitboard support_squares = pawn_set_attacks(own_pawn_bb, w);
        for (Bitboard b = own_pawn_bb & support_squares; b; ) {
            const int r = rel_rank_of_sq(get_lsb(b), k);
            pop_lsb(&b);
            *mg += side_sign * lvl_supported_pawn_mg[r];
            *eg += side_sign * lvl_supported_pawn_eg[r];
        }
    }
}

static Bitboard eval_pinned_pieces(const Position* pos, Color k, Bitboard occ_all) {
    const Bitboard king_bb1 = pos->pieces[k][KING];
    if (!king_bb1) return 0ULL;
    const int ks = get_lsb(king_bb1);
    const Color w = (k == WHITE) ? BLACK : WHITE;
    const Bitboard wq = pos->pieces[w][QUEEN];
    Bitboard result_bb = 0ULL;
    for (int line_kind = 0; line_kind < 2; line_kind++) {
        Bitboard slider_bb = (line_kind == 0)
            ? (get_rook_attacks_public(ks, pos->occupied[w]) & (pos->pieces[w][ROOK] | wq))
            : (get_bishop_attacks_public(ks, pos->occupied[w]) & (pos->pieces[w][BISHOP] | wq));
        while (slider_bb) {
            const int s = get_lsb(slider_bb);
            pop_lsb(&slider_bb);
            const Bitboard between_bb = (line_kind == 0)
                ? (get_rook_attacks_public(ks, occ_all) & get_rook_attacks_public(s, occ_all))
                : (get_bishop_attacks_public(ks, occ_all) & get_bishop_attacks_public(s, occ_all));
            const Bitboard fig = between_bb & occ_all;
            if (fig && !(fig & (fig - 1)) && (fig & pos->occupied[k])) result_bb |= fig;
        }
    }
    return result_bb;
}

static inline Bitboard line_through(int ks, int sq) {
    if ((ks >> 3) == (sq >> 3) || (ks & 7) == (sq & 7))
        return get_rook_attacks_public(ks, 0ULL) & get_rook_attacks_public(sq, 0ULL);
    return get_bishop_attacks_public(ks, 0ULL) & get_bishop_attacks_public(sq, 0ULL);
}

typedef struct {
    Bitboard wp, bp;
    Bitboard cand;
    int32_t mg_pst, eg_pst, mg_passed, eg_passed, mg_pawn_terms, eg_pawn_terms, shelter;
    int8_t kb, kc;
    uint8_t open_near1[2], open_near2[2];
    Bitboard pawn_reach[2];
    int32_t extra_mg, extra_eg;
} PawnHashEntry;

static PawnHashEntry pawn_hash[1u << PAWN_HASH_BITS];
static int pawn_hash_ready = 0;
#define PAWN_HASH_TABLE pawn_hash
#define PAWN_HASH_IS_READY pawn_hash_ready

static inline uint32_t pawn_hash_index(Bitboard wp, Bitboard bp, int kb, int kc) {
    uint64_t h = wp ^ (bp * 0x9E3779B97F4A7C15ULL)
               ^ ((uint64_t)((kb + 1) * 65 + (kc + 1)) * 0xBF58476D1CE4E5B9ULL);
    h ^= h >> 31;
    h *= 0x94D049BB133111EBULL;
    return (uint32_t)(h >> (64 - PAWN_HASH_BITS));
}

static void pawn_hash_compute(Position* pos, const Bitboard pawn_att[2], PawnHashEntry* w) {
    int mg_pst = 0, eg_pst = 0, mg_passed = 0, eg_passed = 0, mg_pawn_terms = 0, eg_pawn_terms = 0;
    Bitboard cand = 0;
    for (Color side_c = WHITE; side_c <= BLACK; side_c++) {
        const int side_sign = (side_c == WHITE) ? 1 : -1;
        const Color enemy_side = (side_c == WHITE) ? BLACK : WHITE;
        const Bitboard own_pawns = pos->pieces[side_c][PAWN];
        {
            const int phalanx_cnt = __builtin_popcountll(
                ((own_pawns >> 1) & ~0x8080808080808080ULL) & own_pawns);
            (void)phalanx_cnt;
            int doubled_cnt = 0;
            for (int file_idx = 0; file_idx < 8; file_idx++) {
                const int n = __builtin_popcountll(
                    own_pawns & (0x0101010101010101ULL << file_idx));
                if (n > 1) doubled_cnt += n - 1;
            }
            mg_pawn_terms -= side_sign * doubled_cnt * lvl_doubled_mg;
            eg_pawn_terms -= side_sign * doubled_cnt * lvl_doubled_eg;
        }
        Bitboard bb = own_pawns;
        while (bb) {
            const int sq = get_lsb(bb);
            pop_lsb(&bb);
            const int sq_pst = (side_c == WHITE) ? (sq ^ 56) : sq;
            mg_pst += side_sign * pst_mg_tables[PAWN][sq_pst];
            eg_pst += side_sign * pst_eg_tables[PAWN][sq_pst];
            {
                if ((passed_span_mask[side_c][sq] & file_mask_of_sq[sq])
                    & pawn_att[enemy_side]) {
                    mg_pawn_terms -= side_sign * lvl_pawn_threat_mg[PAWN];
                    eg_pawn_terms -= side_sign * lvl_pawn_threat_eg[PAWN];
                }
            }
            if (!(passed_span_mask[side_c][sq] & pos->pieces[enemy_side][PAWN])) {
                const int rel_rank = (side_c == WHITE) ? (sq / 8) : 7 - (sq / 8);
                const int r = (rel_rank < 0) ? 0 : (rel_rank > 7 ? 7 : rel_rank);
                mg_passed += side_sign * lvl_passed_bonus_mg[r];
                eg_passed += side_sign * lvl_passed_bonus_eg[r];
                if (rel_rank >= 3 && rel_rank <= 6) {
                    const int advance = rel_rank - 1;
                    const int front_sq = (side_c == WHITE) ? (sq + 8) : (sq - 8);
                    cand |= 1ULL << sq;
                    if ((1ULL << sq) & pawn_att[side_c]) {
                        mg_pawn_terms += side_sign * lvl_protected_passer_mg;
                        eg_pawn_terms += side_sign * lvl_protected_passer_eg;
                    }
                    for (int i_k = 0; i_k < 2; i_k++) {
                        const Color kk = (i_k == 0) ? side_c : enemy_side;
                        const Bitboard kbb = pos->pieces[kk][KING];
                        if (!kbb) continue;
                        const int ks = get_lsb(kbb);
                        const int d = imax(iabs(ks / 8 - front_sq / 8),
                                              iabs(ks % 8 - front_sq % 8));
                        mg_pawn_terms += side_sign * lvl_passer_king_dist_mg[i_k] * advance * d;
                        eg_pawn_terms += side_sign * lvl_passer_king_dist_eg[i_k] * advance * d;
                    }
                }
            }
        }
    }
    w->mg_pst = mg_pst; w->eg_pst = eg_pst;
    w->mg_passed = mg_passed; w->eg_passed = eg_passed;
    w->mg_pawn_terms = mg_pawn_terms; w->eg_pawn_terms = eg_pawn_terms;
    w->cand = cand;
    {
        const Bitboard pk[2] = { pos->pieces[WHITE][PAWN], pos->pieces[BLACK][PAWN] };
        w->pawn_reach[WHITE] = pawn_reach(pk[WHITE], WHITE);
        w->pawn_reach[BLACK] = pawn_reach(pk[BLACK], BLACK);
        int pm = 0, pe = 0;
        extra_pawn_terms(pk, pawn_att, w->pawn_reach, &pm, &pe);
        w->extra_mg = pm; w->extra_eg = pe;
    }

    const int kr[2] = { w->kb, w->kc };
    w->shelter = lvl_evaluate_king_shelter(pos, kr[WHITE], WHITE)
              - lvl_evaluate_king_shelter(pos, kr[BLACK], BLACK);
    const Bitboard pawns_bb = pos->pieces[WHITE][PAWN] | pos->pieces[BLACK][PAWN];
    for (int k = 0; k < 2; k++) {
        w->open_near1[k] = 0; w->open_near2[k] = 0;
        if (kr[k] < 0) continue;
        const int kfile = kr[k] & 7;
        for (int df = -2; df <= 2; df++) {
            const int f = kfile + df;
            if (f < 0 || f >= 8) continue;
            if (pawns_bb & (0x0101010101010101ULL << f)) continue;
            if (df >= -1 && df <= 1) w->open_near1[k] |= (uint8_t)(1u << f);
            else                     w->open_near2[k] |= (uint8_t)(1u << f);
        }
    }
}

static const PawnHashEntry* pawn_hash_probe(Position* pos, const Bitboard pawn_att[2]) {
    if (!PAWN_HASH_IS_READY) {
        for (uint32_t i = 0; i < (1u << PAWN_HASH_BITS); i++) PAWN_HASH_TABLE[i].kb = 127;
        PAWN_HASH_IS_READY = 1;
    }
    const Bitboard wp = pos->pieces[WHITE][PAWN], bp = pos->pieces[BLACK][PAWN];
    const int kb = lvl_get_king_square(pos, WHITE), kc = lvl_get_king_square(pos, BLACK);
    PawnHashEntry* w = &PAWN_HASH_TABLE[pawn_hash_index(wp, bp, kb, kc)];
    if (w->wp == wp && w->bp == bp && w->kb == kb && w->kc == kc) return w;
    w->wp = wp; w->bp = bp; w->kb = (int8_t)kb; w->kc = (int8_t)kc;
    pawn_hash_compute(pos, pawn_att, w);
    return w;
}

static inline unsigned file_occupancy_byte(Bitboard b) {
    b |= b >> 32; b |= b >> 16; b |= b >> 8;
    return (unsigned)(b & 0xFF);
}

static int eval_tapered(Position* pos) {
    if (!pawn_masks_ready) build_pawn_masks();

    const int p256 = game_phase_256(pos);
    const Bitboard occ_all = pos->all_pieces;

    int material = pos->mat_balance;
    int mg_pst = 0, eg_pst = 0;
    int mg_passed = 0, eg_passed = 0;
    int mg_mob = 0, eg_mob = 0;
    int mg_pawn_terms = 0, eg_pawn_terms = 0;
    int mg_file_sum = 0, eg_file_sum = 0;

    Bitboard pawn_att[2];
    pawn_att[WHITE] = pawn_set_attacks(pos->pieces[WHITE][PAWN], WHITE);
    pawn_att[BLACK] = pawn_set_attacks(pos->pieces[BLACK][PAWN], BLACK);
    const PawnHashEntry* const ph = pawn_hash_probe(pos, pawn_att);
    mg_pst += ph->mg_pst;     eg_pst += ph->eg_pst;
    mg_passed += ph->mg_passed; eg_passed += ph->eg_passed;
    mg_pawn_terms += ph->mg_pawn_terms; eg_pawn_terms += ph->eg_pawn_terms;
    for (Bitboard b = ph->cand; b; ) {
        const int sq = get_lsb(b);
        pop_lsb(&b);
        const Color side_c = (pos->pieces[WHITE][PAWN] & (1ULL << sq)) ? WHITE : BLACK;
        const int side_sign = (side_c == WHITE) ? 1 : -1;
        const Color enemy_side = (side_c == WHITE) ? BLACK : WHITE;
        const int rel_rank = (side_c == WHITE) ? (sq / 8) : 7 - (sq / 8);
        const int idx = rel_rank - 3;
        const int front_sq = (side_c == WHITE) ? (sq + 8) : (sq - 8);
        if ((1ULL << front_sq) & pos->occupied[enemy_side]) {
            mg_pawn_terms -= side_sign * lvl_blocked_passer_mg[idx];
            eg_pawn_terms -= side_sign * lvl_blocked_passer_eg[idx];
        }
    }

    int extra_mg = 0, extra_eg = 0;
    Bitboard reach_by_side[2];
    reach_by_side[WHITE] = ph->pawn_reach[WHITE]; reach_by_side[BLACK] = ph->pawn_reach[BLACK];
    extra_mg += ph->extra_mg; extra_eg += ph->extra_eg;

    Bitboard mob_area[2], pinned_by_side[2];
    int king_sq_by_side[2];
    for (Color k = WHITE; k <= BLACK; k++) {
        const Color w = (k == WHITE) ? BLACK : WHITE;
        const Bitboard pk = pos->pieces[k][PAWN];
        const Bitboard low_ranks = (k == WHITE) ? 0x0000000000FFFF00ULL : 0x00FFFF0000000000ULL;
        const Bitboard pod = (k == WHITE) ? (occ_all >> 8) : (occ_all << 8);
        mob_area[k] = ~pawn_att[w] & ~(pk & (pod | low_ranks)) & ~pos->pieces[k][KING];
        pinned_by_side[k] = eval_pinned_pieces(pos, k, occ_all);
        king_sq_by_side[k] = pos->pieces[k][KING] ? get_lsb(pos->pieces[k][KING]) : -1;
    }

    const Bitboard blocked_pawns =
        (pos->pieces[WHITE][PAWN] & ((occ_all | pawn_att[BLACK]) >> 8))
      | (pos->pieces[BLACK][PAWN] & ((occ_all | pawn_att[WHITE]) << 8));
    int closedness = __builtin_popcountll(blocked_pawns) / 2;
    if (closedness > 4) closedness = 4;
    (void)reach_by_side;

    Bitboard ks_att_all[2], ks_att_twice[2], ks_att_by_type[2][6], ks_zone_bb[2];
    int ks_attackers[2] = { 0, 0 }, ks_zone_attacks[2] = { 0, 0 };
    int ks_weight_sum_mg[2] = { 0, 0 }, ks_weight_sum_eg[2] = { 0, 0 };
    for (Color k = WHITE; k <= BLACK; k++) {
        const Bitboard king_bb1 = pos->pieces[k][KING];
        const Bitboard around = king_bb1 ? king_attacks[get_lsb(king_bb1)] : 0ULL;
        const Bitboard pawns_bb = pos->pieces[k][PAWN];
        const Bitboard pp2 = (k == WHITE)
            ? (((pawns_bb & ~0x0101010101010101ULL) << 7) & ((pawns_bb & ~0x8080808080808080ULL) << 9))
            : (((pawns_bb & ~0x8080808080808080ULL) >> 7) & ((pawns_bb & ~0x0101010101010101ULL) >> 9));
        for (int t = 0; t < 6; t++) ks_att_by_type[k][t] = 0ULL;
        ks_att_by_type[k][KING] = around;
        ks_att_by_type[k][PAWN] = pawn_att[k];
        ks_att_twice[k] = (around & pawn_att[k]) | pp2;
        ks_att_all[k] = around | pawn_att[k];
        ks_zone_bb[k] = (around | ((k == WHITE) ? (around << 8) : (around >> 8))) & ~pawn_att[k];
    }

    for (Color side_c = WHITE; side_c <= BLACK; side_c++) {
        const int side_sign = (side_c == WHITE) ? 1 : -1;
        const Color enemy_side = (side_c == WHITE) ? BLACK : WHITE;

        for (PieceType ptype = PAWN; ptype <= KING; ptype++) {
            Bitboard bb = pos->pieces[side_c][ptype];
            if (ptype == PAWN) continue;

            while (bb) {
                const int sq = get_lsb(bb);
                pop_lsb(&bb);
                const int sq_pst = (side_c == WHITE) ? (sq ^ 56) : sq;

                mg_pst += side_sign * pst_mg_tables[ptype][sq_pst];
                eg_pst += side_sign * pst_eg_tables[ptype][sq_pst];
                {
                    const Bitboard bit = 1ULL << sq;
                    if ((bit & pawn_att[side_c]) && (!(7 & 4) || ptype != PAWN)) {
                        mg_pawn_terms += side_sign * lvl_pawn_protected_mg[ptype];
                        eg_pawn_terms += side_sign * lvl_pawn_protected_eg[ptype];
                    }

                    if ((passed_span_mask[side_c][sq] & file_mask_of_sq[sq])
                        & pawn_att[enemy_side]) {
                        mg_pawn_terms -= side_sign * lvl_pawn_threat_mg[ptype];
                        eg_pawn_terms -= side_sign * lvl_pawn_threat_eg[ptype];
                    }
                    if (ptype != PAWN && (bit & pawn_att[enemy_side])) {
                        const int i_stm = (side_c == pos->to_move) ? 0 : 1;
                        mg_pawn_terms -= side_sign * lvl_attacked_by_pawn_mg[i_stm];
                        eg_pawn_terms -= side_sign * lvl_attacked_by_pawn_eg[i_stm];
                    }
                }

                if (ptype == PAWN) {

                    if (!(passed_span_mask[side_c][sq] & pos->pieces[enemy_side][PAWN])) {

                        const int rel_rank = (side_c == WHITE) ? (sq / 8) : 7 - (sq / 8);
                        const int r = (rel_rank < 0) ? 0 : (rel_rank > 7 ? 7 : rel_rank);
                        mg_passed += side_sign * lvl_passed_bonus_mg[r];
                        eg_passed += side_sign * lvl_passed_bonus_eg[r];

                        if (rel_rank >= 3 && rel_rank <= 6) {
                            const int idx = rel_rank - 3;
                            const int advance = rel_rank - 1;
                            const int front_sq = (side_c == WHITE) ? (sq + 8) : (sq - 8);
                            if ((1ULL << sq) & pawn_att[side_c]) {
                                mg_pawn_terms += side_sign * lvl_protected_passer_mg;
                                eg_pawn_terms += side_sign * lvl_protected_passer_eg;
                            }
                            if ((1ULL << front_sq) & pos->occupied[enemy_side]) {
                                mg_pawn_terms -= side_sign * lvl_blocked_passer_mg[idx];
                                eg_pawn_terms -= side_sign * lvl_blocked_passer_eg[idx];
                            }
                            for (int i_k = 0; i_k < 2; i_k++) {
                                const Color kk = (i_k == 0) ? side_c : enemy_side;
                                const Bitboard kbb = pos->pieces[kk][KING];
                                if (!kbb) continue;
                                const int ks = get_lsb(kbb);
                                const int d = imax(iabs(ks / 8 - front_sq / 8),
                                                      iabs(ks % 8 - front_sq % 8));
                                mg_pawn_terms += side_sign * lvl_passer_king_dist_mg[i_k] * advance * d;
                                eg_pawn_terms += side_sign * lvl_passer_king_dist_eg[i_k] * advance * d;
                            }
                        }
                    }
                    continue;
                }
                if (ptype == KING) continue;

                if ((!1 || ptype == ROOK)
                    && !(file_mask_of_sq[sq] & pos->pieces[side_c][PAWN])) {
                    const int open_file = !(file_mask_of_sq[sq] & pos->pieces[enemy_side][PAWN]);
                    mg_file_sum += side_sign * lvl_rook_open_file_mg[open_file][ptype];
                    eg_file_sum += side_sign * lvl_rook_open_file_eg[open_file][ptype];
                }

                Bitboard att_bb;
                switch (ptype) {
                    case KNIGHT: att_bb = knight_attacks[sq];                     break;
                    case BISHOP: att_bb = get_bishop_attacks_public(sq, occ_all);  break;
                    case ROOK:   att_bb = get_rook_attacks_public(sq, occ_all);    break;
                    case QUEEN:  att_bb = get_queen_attacks_public(sq, occ_all);   break;
                    default:     att_bb = 0ULL;                                   break;
                }
                const Bitboard mob_squares = att_bb & ~pos->occupied[side_c] & ~pawn_att[enemy_side];
                const int n = __builtin_popcountll(mob_squares);
                (void)n;
                int mob_count = 0;

                {

                    Bitboard b;
                    switch (ptype) {
                        case BISHOP:
                            b = get_bishop_attacks_public(sq, occ_all ^ pos->pieces[side_c][QUEEN]);
                            break;
                        case ROOK:
                            b = get_rook_attacks_public(sq,
                                occ_all ^ (pos->pieces[side_c][ROOK] | pos->pieces[side_c][QUEEN]));
                            break;
                        case QUEEN:
                            b = get_bishop_attacks_public(sq, occ_all ^ pos->pieces[side_c][BISHOP])
                              | get_rook_attacks_public(sq, occ_all ^ pos->pieces[side_c][ROOK]);
                            break;
                        default:
                            b = att_bb;
                            break;
                    }
                    ks_att_by_type[side_c][ptype] |= b;
                    ks_att_twice[side_c] |= ks_att_all[side_c] & b;
                    ks_att_all[side_c] |= b;
                    {
                        Bitboard bm = b;
                        if (pinned_by_side[side_c] & (1ULL << sq))
                            bm &= (ptype == KNIGHT) ? 0ULL : line_through(king_sq_by_side[side_c], sq);
                        mob_count = __builtin_popcountll(bm & mob_area[side_c]);
                        if (ptype == BISHOP && __builtin_popcountll(bm & CENTER_SQUARES) > 1) {
                            extra_mg += side_sign * BISHOP_CENTER_ATTACK_MG;
                            extra_eg += side_sign * BISHOP_CENTER_ATTACK_EG;
                        }
                    }
                    {
                        int tm, te;
                        switch (ptype) {
                            case KNIGHT: tm = lvl_knight_mob_mg[mob_count]; te = lvl_knight_mob_eg[mob_count]; break;
                            case BISHOP: tm = lvl_bishop_mob_mg[mob_count]; te = lvl_bishop_mob_eg[mob_count]; break;
                            case ROOK:   tm = lvl_rook_mob_mg[mob_count]; te = lvl_rook_mob_eg[mob_count]; break;
                            default:     tm = lvl_queen_mob_mg[mob_count]; te = lvl_queen_mob_eg[mob_count]; break;
                        }
                        extra_mg += side_sign * (tm + mob_offset_mg[ptype]);
                        extra_eg += side_sign * (te + mob_offset_eg[ptype]);
                    }
                    const Bitboard in_king_zone = b & ks_zone_bb[enemy_side];
                    if (in_king_zone) {
                        ks_attackers[side_c] += 1;
                        ks_zone_attacks[side_c] += __builtin_popcountll(in_king_zone);
                        ks_weight_sum_mg[side_c] += lvl_ks_piece_weight_mg[ptype];
                        ks_weight_sum_eg[side_c] += lvl_ks_piece_weight_eg[ptype];
                    }
                }
                {

                    const Bitboard bit = 1ULL << sq;
                    const Bitboard own_pawn_set = pos->pieces[side_c][PAWN];
                    const Bitboard behind_pawns = (side_c == WHITE) ? (own_pawn_set >> 8) : (own_pawn_set << 8);
                    const Bitboard outpost_zone = ((side_c == WHITE) ? 0x0000FFFFFF000000ULL : 0x000000FFFFFF0000ULL)
                                        & pawn_att[side_c] & ~reach_by_side[enemy_side];
                    if (king_sq_by_side[side_c] >= 0 && sq_distance(sq, king_sq_by_side[side_c]) > 3) {
                        extra_mg += side_sign * lvl_far_from_king_mg[ptype];
                        extra_eg += side_sign * lvl_far_from_king_eg[ptype];
                    }
                    extra_mg -= side_sign * far_king_offset_mg[ptype];
                    extra_eg -= side_sign * far_king_offset_eg[ptype];
                    if (ptype == KNIGHT) {
                        extra_mg += side_sign * (lvl_knight_closed_mg[closedness] - KNIGHT_CLOSED_OFFSET_MG);
                        extra_eg += side_sign * (lvl_knight_closed_eg[closedness] - KNIGHT_CLOSED_OFFSET_EG);
                        if (bit & behind_pawns) { extra_mg += side_sign * KNIGHT_SHIELDED_MG;  extra_eg += side_sign * KNIGHT_SHIELDED_EG; }
                        if (bit & outpost_zone)    { extra_mg += side_sign * KNIGHT_OUTPOST_MG; extra_eg += side_sign * KNIGHT_OUTPOST_EG; }
                    } else if (ptype == BISHOP) {
                        const Bitboard square_color_bb = (bit & DARK_SQUARES) ? DARK_SQUARES : ~DARK_SQUARES;
                        int np = __builtin_popcountll(square_color_bb & own_pawn_set);
                        if (np > 6) np = 6;
                        extra_mg += side_sign * (lvl_bishop_color_pawns_mg[np] - BISHOP_COLOR_OFFSET_MG);
                        extra_eg += side_sign * (lvl_bishop_color_pawns_eg[np] - BISHOP_COLOR_OFFSET_EG);
                        if (bit & behind_pawns) { extra_mg += side_sign * BISHOP_SHIELDED_MG;  extra_eg += side_sign * BISHOP_SHIELDED_EG; }
                        if (bit & outpost_zone)    { extra_mg += side_sign * BISHOP_OUTPOST_MG; extra_eg += side_sign * BISHOP_OUTPOST_EG; }
                    } else if (ptype == ROOK) {
                        const Bitboard file_idx = file_mask_of_sq[sq];
                        const Bitboard pl_p = file_idx & own_pawn_set;

                        if (pl_p) {
                            const Bitboard occ_no_rq = occ_all ^ (pos->pieces[side_c][ROOK] | pos->pieces[side_c][QUEEN]);
                            const Bitboard above = (side_c == WHITE) ? (pl_p << 8) : (pl_p >> 8);
                            if (above & occ_no_rq) { extra_mg += side_sign * ROOK_BLOCKED_PAWN_MG; extra_eg += side_sign * ROOK_BLOCKED_PAWN_EG; }
                        }
                        if (file_idx & pos->pieces[enemy_side][QUEEN]) {
                            extra_mg += side_sign * ROOK_QUEEN_FILE_MG;
                            extra_eg += side_sign * ROOK_QUEEN_FILE_EG;
                        }
                        if (mob_count <= 4 && rel_rank_of_sq(sq, side_c) <= 1 && king_sq_by_side[side_c] >= 0) {
                            const int pk = king_sq_by_side[side_c] & 7, pw = sq & 7;
                            if (pk != pw && ((pk < pw) == (pk >= 4))) {
                                const int castle_rights = pos->castling_rights & ((side_c == WHITE) ? 0x03 : 0x0C);
                                extra_mg += side_sign * (castle_rights ? ROOK_TRAPPED_CASTLING_MG : ROOK_TRAPPED_NOCASTLE_MG);
                                extra_eg += side_sign * (castle_rights ? ROOK_TRAPPED_CASTLING_EG : ROOK_TRAPPED_NOCASTLE_EG);
                            }
                        }
                    }
                }
            }
        }
    }

#define THREATS_TERM

    const int extra_eval = ((extra_mg * PAWN_VALUE / EXTRA_PAWN_NORM_MG) * p256
                        + (extra_eg * PAWN_VALUE / EXTRA_PAWN_NORM_EG) * (256 - p256)) / 256;
#define EXTRA_EVAL_TERM + extra_eval

    const int initiative = (INITIATIVE_MG * p256 + INITIATIVE_EG * (256 - p256)) / 256;
#define INITIATIVE_TERM + (pos->to_move == WHITE ? initiative : -initiative)

    int ks_mg[2] = { 0, 0 }, ks_eg[2] = { 0, 0 };
    for (Color us = WHITE; us <= BLACK; us++) {
        const Color them = (us == WHITE) ? BLACK : WHITE;
        const int no_queen = !pos->pieces[us][QUEEN];
        if (ks_attackers[us] < 1 + no_queen || !pos->pieces[them][KING]) continue;
        const int target_king_sq = get_lsb(pos->pieces[them][KING]);

        const Bitboard weak_sq = ks_att_all[us] & ~ks_att_twice[them]
                             & (~ks_att_all[them] | ks_att_by_type[them][KING]);
        const Bitboard safe_sq = ~pos->occupied[us]
                                  & (~ks_att_all[them] | (weak_sq & ks_att_twice[us]));
        const Bitboard rook_rays = get_rook_attacks_public(target_king_sq, occ_all);
        const Bitboard bishop_rays = get_bishop_attacks_public(target_king_sq, occ_all);
        const Bitboard chk_knight = ks_att_by_type[us][KNIGHT] & knight_attacks[target_king_sq];
        const Bitboard chk_bishop = ks_att_by_type[us][BISHOP] & bishop_rays;
        const Bitboard chk_rook = ks_att_by_type[us][ROOK] & rook_rays;
        const Bitboard chk_queen = ks_att_by_type[us][QUEEN] & (bishop_rays | rook_rays);
        const Bitboard chk_all = chk_knight | chk_bishop | chk_rook | chk_queen;
        const int n_weak_sq = __builtin_popcountll(weak_sq & ks_zone_bb[them]);
        const int n_sn = __builtin_popcountll(chk_knight & safe_sq);
        const int n_sb = __builtin_popcountll(chk_bishop & safe_sq);
        const int n_sr = __builtin_popcountll(chk_rook & safe_sq);
        const int n_sq = __builtin_popcountll(chk_queen & safe_sq);
        const int n_nb = __builtin_popcountll(chk_all & ~safe_sq);

        const int b_mg = ks_weight_sum_mg[us] + KS_OFFSET_MG + KS_ZONE_ATTACK_MG * ks_zone_attacks[us]
                       + KS_WEAK_SQ_MG * n_weak_sq + KS_NO_QUEEN_MG * no_queen
                       + KS_CHECK_N_MG * n_sn + KS_CHECK_B_MG * n_sb + KS_CHECK_R_MG * n_sr
                       + KS_CHECK_Q_MG * n_sq + KS_UNSAFE_MG * n_nb;
        const int b_eg = ks_weight_sum_eg[us] + KS_OFFSET_EG + KS_ZONE_ATTACK_EG * ks_zone_attacks[us]
                       + KS_WEAK_SQ_EG * n_weak_sq + KS_NO_QUEEN_EG * no_queen
                       + KS_CHECK_N_EG * n_sn + KS_CHECK_B_EG * n_sb + KS_CHECK_R_EG * n_sr
                       + KS_CHECK_Q_EG * n_sq + KS_UNSAFE_EG * n_nb;

        ks_mg[us] = ((b_mg > 0 ? b_mg : 0) * b_mg / 256) * PAWN_VALUE / KS_PAWN_NORM_MG;
        ks_eg[us] = ((b_eg > 0 ? b_eg : 0) / 16) * PAWN_VALUE / KS_PAWN_NORM_EG;
    }
    const int king_zone_penalty = ((ks_mg[WHITE] - ks_mg[BLACK]) * p256
                          + (ks_eg[WHITE] - ks_eg[BLACK]) * (256 - p256)) / 256;

    const unsigned rq_b = file_occupancy_byte(pos->pieces[BLACK][ROOK] | pos->pieces[BLACK][QUEEN]);
    const unsigned rq_c = file_occupancy_byte(pos->pieces[WHITE][ROOK] | pos->pieces[WHITE][QUEEN]);
    const int open_file_white = OPEN_FILE_NEAR_KING_PENALTY
        * (2 * __builtin_popcount(ph->open_near1[WHITE] & rq_b) + __builtin_popcount(ph->open_near2[WHITE] & rq_b));
    const int open_file_black = OPEN_FILE_NEAR_KING_PENALTY
        * (2 * __builtin_popcount(ph->open_near1[BLACK] & rq_c) + __builtin_popcount(ph->open_near2[BLACK] & rq_c));
    const int king_shelter_raw = ph->shelter + open_file_white - open_file_black;
    const int king_shelter = king_shelter_raw * KING_SHELTER_WEIGHT * p256 / (100 * 256);
#define KING_SHELTER_TERM + king_shelter

    const int bishop_pair_value = ((BISHOP_PAIR_VALUE_MG) * p256 + (BISHOP_PAIR_VALUE_EG) * (256 - p256)) / 256;
    const int bishop_pair =
        ((__builtin_popcountll(pos->pieces[WHITE][BISHOP]) >= 2) ? bishop_pair_value : 0)
      - ((__builtin_popcountll(pos->pieces[BLACK][BISHOP]) >= 2) ? bishop_pair_value : 0);
#define BISHOP_PAIR_TERM + bishop_pair

    const int pawn_terms =
        ((mg_pawn_terms * p256 + eg_pawn_terms * (256 - p256)) / 256) * PAWN_TERMS_WEIGHT / 100;
#define PAWN_STRUCT_TERM + pawn_terms

    const int open_files =
        ((mg_file_sum * p256 + eg_file_sum * (256 - p256)) / 256) * 100 / 100;
#define OPEN_FILES_TERM + open_files

    const int mg = mg_pst + mg_passed + mg_mob;
    const int eg = eg_pst + eg_passed + eg_mob;
    const int positional = (mg * p256 + eg * (256 - p256)) / 256;
    const int total = material + positional + king_zone_penalty KING_SHELTER_TERM BISHOP_PAIR_TERM PAWN_STRUCT_TERM OPEN_FILES_TERM THREATS_TERM EXTRA_EVAL_TERM INITIATIVE_TERM;

    if (eval_debug_enabled) {

        const int pst_t   = (mg_pst   * p256 + eg_pst   * (256 - p256)) / 256;
        const int pawns_t = (mg_passed * p256 + eg_passed * (256 - p256)) / 256;
        const int mob_t   = (mg_mob   * p256 + eg_mob   * (256 - p256)) / 256;
        printf("DEBUG: ================================\n");
        printf("DEBUG: EVALUATION BREAKDOWN (TAPERED)\n");
        printf("DEBUG: Game phase: %d/256\n", p256);
        printf("DEBUG: ================================\n");
        printf("DEBUG:   Material:      %+d\n", material);
        printf("DEBUG:   PST:           %+d\n", pst_t);
        printf("DEBUG:   Pawns:         %+d\n", pawns_t);
        printf("DEBUG:   Mobility:      %+d\n", mob_t);
        printf("DEBUG:   King Safety:   %+d\n", king_zone_penalty);
        printf("DEBUG:   PawnTerms:     %+d\n", pawn_terms);
        printf("DEBUG:   TOTAL:         %+d\n", total);
        printf("DEBUG:   Perspective:   %s\n", pos->to_move == WHITE ? "WHITE" : "BLACK");
        printf("DEBUG:   Final:         %+d\n", (pos->to_move == WHITE) ? total : -total);
        printf("DEBUG: ================================\n\n");
    }

    return (pos->to_move == WHITE) ? total : -total;
}

#define KNOWN_WIN 10000
#define SCALE_NORMAL   256
#define GENERAL_SCALE(c) ((SCALE_BASE_FACTOR + SCALE_PER_PAWN * __builtin_popcountll(pos->pieces[c][PAWN])) < SCALE_NORMAL \
                            ? (SCALE_BASE_FACTOR + SCALE_PER_PAWN * __builtin_popcountll(pos->pieces[c][PAWN])) : SCALE_NORMAL)
#define DARK_SQUARES_MASK  0xAA55AA55AA55AA55ULL
#define FILE_A_MASK  0x0101010101010101ULL
#define FILE_H_MASK  0x8080808080808080ULL
#define KINGSIDE_FILES  0xF0F0F0F0F0F0F0F0ULL
#define QUEENSIDE_FILES  0x0F0F0F0F0F0F0F0FULL

static inline int square_distance(int a, int b) {
    const int df = abs((a & 7) - (b & 7)), dr = abs((a >> 3) - (b >> 3));
    return df > dr ? df : dr;
}
static inline int corner_push(int sq) {
    const int r = sq >> 3, f = sq & 7;
    const int rd = r < (r ^ 7) ? r : (r ^ 7), fd = f < (f ^ 7) ? f : (f ^ 7);
    return 50 - 2 * (fd * fd + rd * rd);
}
static inline int proximity_bonus(int a, int b) { return 70 - 10 * square_distance(a, b); }
static inline int distance_bonus(int a, int b) { return 10 + 10 * square_distance(a, b); }
static inline int relative_sq(int sq, Color c) { return c == BLACK ? sq ^ 56 : sq; }
static inline int rank_relative_to(int sq, Color c) { return (sq >> 3) ^ (c == BLACK ? 7 : 0); }
static inline int normalize_sq(Color strong, int sq, int mirror) { return relative_sq(sq, strong) ^ (mirror ? 7 : 0); }
static inline int single_bit(Bitboard b) { return b && !(b & (b - 1)); }
static inline Bitboard squares_ahead(int sq, Color c) {
    const int r = sq >> 3;
    if (c == WHITE) return r == 7 ? 0ULL : (~0ULL << (8 * (r + 1)));
    return r == 0 ? 0ULL : (~0ULL >> (8 * (8 - r)));
}
static inline Bitboard file_mask_at(int sq) { return FILE_A_MASK << (sq & 7); }
static inline Bitboard pawn_path(int sq, Color c) { return squares_ahead(sq, c) & file_mask_at(sq); }
static inline Bitboard pawn_span(int sq, Color c) {
    const Bitboard p = file_mask_at(sq);
    return squares_ahead(sq, c) & (p | ((p << 1) & ~FILE_A_MASK) | ((p >> 1) & ~FILE_H_MASK));
}
static inline int non_pawn_material(const Position* pos, Color c) {
    return __builtin_popcountll(pos->pieces[c][KNIGHT]) * MATERIAL_VALUE(KNIGHT)
         + __builtin_popcountll(pos->pieces[c][BISHOP]) * MATERIAL_VALUE(BISHOP)
         + __builtin_popcountll(pos->pieces[c][ROOK])   * MATERIAL_VALUE(ROOK)
         + __builtin_popcountll(pos->pieces[c][QUEEN])  * MATERIAL_VALUE(QUEEN);
}

#define KPK_SIZE (2 * 24 * 64 * 64)
enum { KPK_INVALID = 0, KPK_UNKNOWN = 1, KPK_DRAW = 2, KPK_WIN = 4 };
static uint8_t ok_kpk[KPK_SIZE / 8];
static uint8_t kpk_table[KPK_SIZE];
static int kpk_ready = 0;

static inline uint32_t kpk_index(int weak, int strong, int pawn_bit, int stm) {
    return (uint32_t)weak | ((uint32_t)strong << 6) | ((uint32_t)stm << 12)
         | ((uint32_t)(pawn_bit & 7) << 13) | ((uint32_t)(6 - (pawn_bit >> 3)) << 15);
}
static inline Bitboard white_pawn_attacks(int sq) {
    const Bitboard b = 1ULL << sq;
    return ((b << 7) & ~FILE_H_MASK) | ((b << 9) & ~FILE_A_MASK);
}
static uint8_t kpk_init_entry(uint32_t i) {
    const int weak = (int)(i & 63), strong = (int)((i >> 6) & 63), stm = (int)((i >> 12) & 1);
    const int pawn_bit = (int)(6 - (i >> 15)) * 8 + (int)((i >> 13) & 3);
    if (square_distance(strong, weak) <= 1 || weak == pawn_bit || strong == pawn_bit) return KPK_INVALID;
    if (stm == WHITE) {
        if (white_pawn_attacks(pawn_bit) & (1ULL << weak)) return KPK_INVALID;
        if ((pawn_bit >> 3) == 6 && strong != pawn_bit + 8
            && (square_distance(weak, pawn_bit + 8) > 1 || square_distance(strong, pawn_bit + 8) == 1)) return KPK_WIN;
    } else {
        if (!(king_attacks[weak] & ~(king_attacks[strong] | white_pawn_attacks(pawn_bit)))) return KPK_DRAW;
        if (king_attacks[weak] & ~king_attacks[strong] & (1ULL << pawn_bit)) return KPK_DRAW;
    }
    return KPK_UNKNOWN;
}
static uint8_t kpk_classify(uint32_t i) {
    const int weak = (int)(i & 63), strong = (int)((i >> 6) & 63), stm = (int)((i >> 12) & 1);
    const int pawn_bit = (int)(6 - (i >> 15)) * 8 + (int)((i >> 13) & 3);
    const uint8_t good = (stm == WHITE) ? KPK_WIN : KPK_DRAW;
    const uint8_t bad_value = good ^ KPK_DRAW ^ KPK_WIN;
    uint8_t w = KPK_INVALID;
    for (Bitboard b = king_attacks[stm == WHITE ? strong : weak]; b; ) {
        const int sq = get_lsb(b);
        pop_lsb(&b);
        w |= kpk_table[stm == WHITE ? kpk_index(weak, sq, pawn_bit, BLACK) : kpk_index(sq, strong, pawn_bit, WHITE)];
    }
    if (stm == WHITE) {
        if ((pawn_bit >> 3) < 6) w |= kpk_table[kpk_index(weak, strong, pawn_bit + 8, BLACK)];
        if ((pawn_bit >> 3) == 1 && pawn_bit + 8 != strong && pawn_bit + 8 != weak)
            w |= kpk_table[kpk_index(weak, strong, pawn_bit + 16, BLACK)];
    }
    return (w & good) ? good : (w & KPK_UNKNOWN) ? KPK_UNKNOWN : bad_value;
}
static void kpk_build(void) {
    for (uint32_t i = 0; i < KPK_SIZE; i++) kpk_table[i] = kpk_init_entry(i);
    int changed;
    do {
        changed = 0;
        for (uint32_t i = 0; i < KPK_SIZE; i++)
            if (kpk_table[i] == KPK_UNKNOWN) {
                kpk_table[i] = kpk_classify(i);
                changed |= kpk_table[i] != KPK_UNKNOWN;
            }
    } while (changed);
    for (uint32_t i = 0; i < KPK_SIZE / 8; i++) ok_kpk[i] = 0;
    for (uint32_t i = 0; i < KPK_SIZE; i++)
        if (kpk_table[i] == KPK_WIN) ok_kpk[i >> 3] |= (uint8_t)(1u << (i & 7));
    kpk_ready = 1;
}
static inline int kpk_is_win(int weak, int strong, int pawn_bit, int stm) {
    if (!kpk_ready) kpk_build();
    const uint32_t i = kpk_index(weak, strong, pawn_bit, stm);
    return (ok_kpk[i >> 3] >> (i & 7)) & 1;
}
static void lvl_endgame_init(void) {
    if ((31 & (2 | 16)) && !kpk_ready) kpk_build();
}

#define KING_SQ(c) get_lsb(pos->pieces[c][KING])
static int kpk_eval(const Position* pos, Color strong) {
    const Color weak = (strong == WHITE) ? BLACK : WHITE;
    const int us = (strong == pos->to_move) ? WHITE : BLACK;
    const int pawn_sq_raw = get_lsb(pos->pieces[strong][PAWN]);
    const int mirror = (pawn_sq_raw & 7) >= 4;
    const int pawn_bit = normalize_sq(strong, pawn_sq_raw, mirror);
    const int w = kpk_is_win(normalize_sq(strong, KING_SQ(weak), mirror), normalize_sq(strong, KING_SQ(strong), mirror), pawn_bit, us)
                ? KNOWN_WIN + MATERIAL_VALUE(PAWN) + (pawn_bit >> 3) * 3 : 0;
    return us == WHITE ? w : -w;
}
static int eval_kbnk(const Position* pos, Color strong) {
    const Color weak = (strong == WHITE) ? BLACK : WHITE;
    int k_weak = KING_SQ(weak);
    int w = KNOWN_WIN + MATERIAL_VALUE(KNIGHT) + MATERIAL_VALUE(BISHOP) + proximity_bonus(k_weak, KING_SQ(strong));
    if (pos->pieces[strong][BISHOP] & DARK_SQUARES_MASK) k_weak ^= 56;
    w += abs((k_weak & 7) - (k_weak >> 3)) * 100;
    return pos->to_move == strong ? w : -w;
}
static int eval_krkp(const Position* pos, Color strong) {
    const Color weak = (strong == WHITE) ? BLACK : WHITE;
    const int k_strong = KING_SQ(strong), k_weak = KING_SQ(weak);
    const int rook_sq_idx = get_lsb(pos->pieces[strong][ROOK]), pawn_bit = get_lsb(pos->pieces[weak][PAWN]);
    const int push_sq = pawn_bit + (weak == WHITE ? 8 : -8);
    const int promo_sq = (pawn_bit & 7) + (weak == WHITE ? 56 : 0);
    const int strong_to_move = pos->to_move == strong;
    int w;
    if (pawn_path(k_strong, strong) & (1ULL << pawn_bit))
        w = MATERIAL_VALUE(ROOK) - square_distance(k_strong, pawn_bit);
    else if (square_distance(k_weak, pawn_bit) >= 3 + !strong_to_move && square_distance(k_weak, rook_sq_idx) >= 3)
        w = MATERIAL_VALUE(ROOK) - square_distance(k_strong, pawn_bit);
    else if (rank_relative_to(k_weak, strong) <= 2 && square_distance(k_weak, pawn_bit) == 1
             && rank_relative_to(k_strong, strong) >= 3 && square_distance(k_strong, pawn_bit) >= 3 + strong_to_move)
        w = 40 - 4 * square_distance(k_strong, pawn_bit);
    else
        w = 100 - 4 * (square_distance(k_strong, push_sq) - square_distance(k_weak, push_sq) - square_distance(pawn_bit, promo_sq));
    return strong_to_move ? w : -w;
}
static int eval_krkn(const Position* pos, Color strong) {
    const Color weak = (strong == WHITE) ? BLACK : WHITE;
    const int k_weak = KING_SQ(weak);
    const int w = corner_push(k_weak) + distance_bonus(k_weak, get_lsb(pos->pieces[weak][KNIGHT]));
    return pos->to_move == strong ? w : -w;
}
static int eval_krkb(const Position* pos, Color strong) {
    const int w = corner_push(KING_SQ((strong == WHITE) ? BLACK : WHITE));
    return pos->to_move == strong ? w : -w;
}
static int eval_kqkp(const Position* pos, Color strong) {
    const Color weak = (strong == WHITE) ? BLACK : WHITE;
    const int k_weak = KING_SQ(weak), pawn_bit = get_lsb(pos->pieces[weak][PAWN]), segment = pawn_bit & 7;
    int w = proximity_bonus(k_weak, KING_SQ(strong));

    if (rank_relative_to(pawn_bit, weak) != 6 || square_distance(k_weak, pawn_bit) != 1
        || segment == 1 || segment == 3 || segment == 4 || segment == 6)
        w += MATERIAL_VALUE(QUEEN) - MATERIAL_VALUE(PAWN);
    return pos->to_move == strong ? w : -w;
}
static int eval_kqkr(const Position* pos, Color strong) {
    const int k_weak = KING_SQ((strong == WHITE) ? BLACK : WHITE);
    const int w = MATERIAL_VALUE(QUEEN) - MATERIAL_VALUE(ROOK) + corner_push(k_weak) + proximity_bonus(k_weak, KING_SQ(strong));
    return pos->to_move == strong ? w : -w;
}
static int eval_knnkp(const Position* pos, Color strong) {
    const Color weak = (strong == WHITE) ? BLACK : WHITE;
    const int w = MATERIAL_VALUE(PAWN) + corner_push(KING_SQ(weak))
                - 5 * rank_relative_to(get_lsb(pos->pieces[weak][PAWN]), weak);
    return pos->to_move == strong ? w : -w;
}

static int eval_kmpk(Position* pos, Color strong, int defender_is_bishop) {
    const Color weak = (strong == WHITE) ? BLACK : WHITE;
    const int w = eval_tapered(pos);
    const int k_strong = KING_SQ(strong), k_weak = KING_SQ(weak), pawn_bit = get_lsb(pos->pieces[strong][PAWN]);
    const Bitboard pawn_route = pawn_path(pawn_bit, strong), span = pawn_span(pawn_bit, strong);
    const int defender_sq = get_lsb(pos->pieces[weak][defender_is_bishop ? BISHOP : KNIGHT]);

    Bitboard att_bb = defender_is_bishop ? get_bishop_attacks_public(defender_sq, pos->all_pieces) : knight_attacks[defender_sq];
    if (defender_is_bishop) {
        const Bitboard g_strong = pos->pieces[strong][BISHOP], g_weak = pos->pieces[weak][BISHOP];
        if (single_bit(g_strong) && single_bit(g_weak) && !(g_strong & DARK_SQUARES_MASK) != !(g_weak & DARK_SQUARES_MASK))
            att_bb = (g_weak & DARK_SQUARES_MASK) ? DARK_SQUARES_MASK : ~DARK_SQUARES_MASK;
    }
    if (pawn_route & (1ULL << k_weak)) return w / 16;
    if (pawn_route & att_bb) return w / 8;
    if ((span & (1ULL << k_weak)) && !(span & (1ULL << k_strong)) && pos->to_move != strong) return w / 4;
    return w;
}
static int eval_krpkr(Position* pos, Color strong) {
    const Color weak = (strong == WHITE) ? BLACK : WHITE;
    const int w = eval_tapered(pos);
    const int k_strong = relative_sq(KING_SQ(strong), strong), k_weak = relative_sq(KING_SQ(weak), strong);
    const int pawn_bit = relative_sq(get_lsb(pos->pieces[strong][PAWN]), strong);
    const int rook_weak = relative_sq(get_lsb(pos->pieces[weak][ROOK]), strong);
    const int strong_to_move = pos->to_move == strong;
    if (!(pawn_span(pawn_bit, WHITE) & (1ULL << k_weak))) return w;

    const int rank_k_strong = k_strong >> 3, rank_k_weak = k_weak >> 3, rp = pawn_bit >> 3, rank_rook_weak = rook_weak >> 3, segment = pawn_bit & 7;
    if (rank_k_weak == 5 || rank_k_weak == 6) {
        if (rank_rook_weak == 4 && rank_k_strong < 4 && rp < 4) return w / 64;
        if (rank_rook_weak == 0 && rp == 4) return w / 64;
    }
    if (rank_k_weak >= 6) {
        if (rank_rook_weak == 5 && rank_k_strong < 5 && rp < 5) return w / 64;
        if (rank_rook_weak <= 1 && rp == 5) return w / 64;
    }
    if (rank_k_weak == 7 && rank_rook_weak == 7) {
        if (segment <= 1 || segment >= 6) return w / 16;
        if (rank_k_strong + strong_to_move < 5) return w / 16;
    }
    return w / 2;
}
static int eval_kxk(Position* pos, Color strong) {
    const Color weak = (strong == WHITE) ? BLACK : WHITE;
    if (pos->to_move == weak && !is_in_check(pos, weak)) {
        Move move_list[MAX_MOVES];
        if (lvl_generate_legal_moves(pos, move_list) == 0) return 0;
    }
    const Bitboard n = pos->pieces[strong][KNIGHT], g = pos->pieces[strong][BISHOP];
    int w = non_pawn_material(pos, strong) + __builtin_popcountll(pos->pieces[strong][PAWN]) * MATERIAL_VALUE(PAWN)
          + corner_push(KING_SQ(weak)) + proximity_bonus(KING_SQ(strong), KING_SQ(weak));
    if ((pos->pieces[strong][QUEEN] | pos->pieces[strong][ROOK]) || (n && g)
        || ((g & DARK_SQUARES_MASK) && (g & ~DARK_SQUARES_MASK)) || __builtin_popcountll(n) >= 3)
        w += KNOWN_WIN;
    return pos->to_move == strong ? w : -w;
}

enum { ENDGAME_NONE, ENDGAME_DRAW, ENDGAME_KPK, ENDGAME_KBNK, ENDGAME_KRKP, ENDGAME_KRKN, ENDGAME_KRKB, ENDGAME_KQKP, ENDGAME_KQKR, ENDGAME_KNNKP,
       ENDGAME_KMPKN, ENDGAME_KMPKB, ENDGAME_KRPKR };
#define SIG_PAWN 0x1
#define SIG_KNIGHT 0x10
#define SIG_BISHOP 0x100
#define SIG_ROOK 0x1000
#define SIG_QUEEN 0x10000
static inline int material_signature(const Position* pos, Color c) {
    return __builtin_popcountll(pos->pieces[c][PAWN])   * SIG_PAWN
         + __builtin_popcountll(pos->pieces[c][KNIGHT]) * SIG_KNIGHT
         + __builtin_popcountll(pos->pieces[c][BISHOP]) * SIG_BISHOP
         + __builtin_popcountll(pos->pieces[c][ROOK])   * SIG_ROOK
         + __builtin_popcountll(pos->pieces[c][QUEEN])  * SIG_QUEEN;
}
static int classify_endgame(int s, int sl) {
    if (sl == 0 && (s == 0 || s == SIG_KNIGHT || s == SIG_BISHOP || s == 2 * SIG_KNIGHT)) return ENDGAME_DRAW;
    if ((s == SIG_BISHOP || s == SIG_KNIGHT) && sl == SIG_KNIGHT) return ENDGAME_DRAW;
    if ((s == SIG_BISHOP || s == 2 * SIG_BISHOP) && sl == SIG_BISHOP) return ENDGAME_DRAW;
    if (s == SIG_PAWN && sl == 0) return ENDGAME_KPK;
    if (s == SIG_BISHOP + SIG_KNIGHT && sl == 0) return ENDGAME_KBNK;
    if (s == SIG_ROOK && sl == SIG_PAWN) return ENDGAME_KRKP;
    if (s == SIG_ROOK && sl == SIG_KNIGHT) return ENDGAME_KRKN;
    if (s == SIG_ROOK && sl == SIG_BISHOP) return ENDGAME_KRKB;
    if (s == SIG_QUEEN && sl == SIG_PAWN) return ENDGAME_KQKP;
    if (s == SIG_QUEEN && sl == SIG_ROOK) return ENDGAME_KQKR;
    if (s == 2 * SIG_KNIGHT && sl == SIG_PAWN) return ENDGAME_KNNKP;
    if ((s == SIG_KNIGHT + SIG_PAWN || s == SIG_BISHOP + SIG_PAWN) && sl == SIG_KNIGHT) return ENDGAME_KMPKN;
    if ((s == SIG_KNIGHT + SIG_PAWN || s == SIG_BISHOP + SIG_PAWN) && sl == SIG_BISHOP) return ENDGAME_KMPKB;
    if (s == SIG_ROOK + SIG_PAWN && sl == SIG_ROOK) return ENDGAME_KRPKR;
    (void)s; (void)sl;
    return ENDGAME_NONE;
}

static int eval_special_endgame(Position* pos, int* result_bb) {
    if (__builtin_popcountll(pos->all_pieces) <= 5) {
        const int sb = material_signature(pos, WHITE), sc = material_signature(pos, BLACK);
        for (int side_pass = 0; side_pass < 2; side_pass++) {
            const Color strong = side_pass ? BLACK : WHITE;
            switch (side_pass ? classify_endgame(sc, sb) : classify_endgame(sb, sc)) {
            case ENDGAME_DRAW: *result_bb = 0;                           return 1;
            case ENDGAME_KPK:   *result_bb = kpk_eval(pos, strong);    return 1;
            case ENDGAME_KBNK:  *result_bb = eval_kbnk(pos, strong);         return 1;
            case ENDGAME_KRKP:  *result_bb = eval_krkp(pos, strong);         return 1;
            case ENDGAME_KRKN:  *result_bb = eval_krkn(pos, strong);         return 1;
            case ENDGAME_KRKB:  *result_bb = eval_krkb(pos, strong);         return 1;
            case ENDGAME_KQKP:  *result_bb = eval_kqkp(pos, strong);         return 1;
            case ENDGAME_KQKR:  *result_bb = eval_kqkr(pos, strong);         return 1;
            case ENDGAME_KNNKP: *result_bb = eval_knnkp(pos, strong);        return 1;
            case ENDGAME_KMPKN: *result_bb = eval_kmpk(pos, strong, 0);      return 1;
            case ENDGAME_KMPKB: *result_bb = eval_kmpk(pos, strong, 1);      return 1;
            case ENDGAME_KRPKR: *result_bb = eval_krpkr(pos, strong);        return 1;
            default: break;
            }
        }
    }
    if (pos->occupied[BLACK] == pos->pieces[BLACK][KING] && non_pawn_material(pos, WHITE) >= MATERIAL_VALUE(ROOK)) {
        *result_bb = eval_kxk(pos, WHITE);
        return 1;
    }
    if (pos->occupied[WHITE] == pos->pieces[WHITE][KING] && non_pawn_material(pos, BLACK) >= MATERIAL_VALUE(ROOK)) {
        *result_bb = eval_kxk(pos, BLACK);
        return 1;
    }
    (void)pos; (void)result_bb;
    return 0;
}

static int passed_count(const Position* pos, Color c) {
    const Bitboard enemy_pawn_bb = pos->pieces[(c == WHITE) ? BLACK : WHITE][PAWN];
    int n = 0;
    for (Bitboard b = pos->pieces[c][PAWN]; b; ) {
        const int sq = get_lsb(b);
        pop_lsb(&b);
        if (!(pawn_span(sq, c) & enemy_pawn_bb)) n++;
    }
    return n;
}

static int endgame_scale(const Position* pos, Color strong) {
    const Color weak = (strong == WHITE) ? BLACK : WHITE;
    const Bitboard p_strong = pos->pieces[strong][PAWN], p_weak = pos->pieces[weak][PAWN];
    const Bitboard g_strong = pos->pieces[strong][BISHOP], g_weak = pos->pieces[weak][BISHOP];
    const Bitboard fig_strong = pos->occupied[strong] & ~p_strong & ~pos->pieces[strong][KING];
    const Bitboard fig_weak = pos->occupied[weak] & ~p_weak & ~pos->pieces[weak][KING];
    if (!p_strong) {
        const int m_strong = non_pawn_material(pos, strong), m_weak = non_pawn_material(pos, weak);
        if (m_strong - m_weak <= MATERIAL_VALUE(BISHOP)) {
            if (m_strong <= MATERIAL_VALUE(BISHOP)) return 0;
            const int f = (m_strong - m_weak) * 8 / MATERIAL_VALUE(BISHOP);
            return f > 0 ? f : 0;
        }
    }
    if (single_bit(g_strong) && single_bit(g_weak) && !((g_strong & DARK_SQUARES_MASK) == 0) != !((g_weak & DARK_SQUARES_MASK) == 0)) {
        return (non_pawn_material(pos, strong) + non_pawn_material(pos, weak) > 2 * MATERIAL_VALUE(BISHOP))
             ? 71 + 9 * __builtin_popcountll(pos->occupied[strong])
             : 33 + 21 * passed_count(pos, strong);
    }
    if (fig_strong == pos->pieces[strong][ROOK] && single_bit(fig_strong) && fig_weak == pos->pieces[weak][ROOK] && single_bit(fig_weak)
        && __builtin_popcountll(p_strong) < 2 + __builtin_popcountll(p_weak)
        && !(KINGSIDE_FILES & p_strong) != !(QUEENSIDE_FILES & p_strong)
        && (king_attacks[KING_SQ(weak)] & p_weak))
        return 130;
    if (!fig_strong && !fig_weak) {
        if (!p_strong) return 0;
        if ((p_strong & FILE_A_MASK) == p_strong || (p_strong & FILE_H_MASK) == p_strong) {
            const int pr = (strong == WHITE) ? 63 - __builtin_clzll(p_strong) : get_lsb(p_strong);
            const int mirror = (pr & 7) >= 4;
            const int us = (strong == pos->to_move) ? WHITE : BLACK;
            if (!kpk_is_win(normalize_sq(strong, KING_SQ(weak), mirror), normalize_sq(strong, KING_SQ(strong), mirror),
                                normalize_sq(strong, pr, mirror), us)) return 0;
        }
        return SCALE_NORMAL;
    }
    if (!fig_weak && fig_strong == g_strong && single_bit(g_strong)) {
        if (!p_strong) return 0;
        const int dark = (int)((DARK_SQUARES_MASK >> relative_sq(get_lsb(g_strong), strong)) & 1);
        if ((p_strong & (dark ? FILE_A_MASK : FILE_H_MASK)) == p_strong) {
            const int d = square_distance(relative_sq(KING_SQ(weak), strong), dark ? 56 : 63);
            if (d < 2) return 0;
            return SCALE_NORMAL * (d - 1) / d;
        }
        return SCALE_NORMAL;
    }
    return GENERAL_SCALE(strong);
}

static int lvl_is_endgame_special(const Position* pos) {
    if (__builtin_popcountll(pos->all_pieces) <= 5
        || pos->occupied[WHITE] == pos->pieces[WHITE][KING]
        || pos->occupied[BLACK] == pos->pieces[BLACK][KING]) return 1;
    if (endgame_scale(pos, WHITE) != GENERAL_SCALE(WHITE) || endgame_scale(pos, BLACK) != GENERAL_SCALE(BLACK)) return 1;
    (void)pos;
    return 0;
}

static int lvl_scale_material_balance(const Position* pos, int balance) {
    const int f = GENERAL_SCALE(balance >= 0 ? WHITE : BLACK);
    if (f == SCALE_NORMAL) return balance;
    const int p = game_phase_256(pos);
    return (int)((long long)balance * (p * SCALE_NORMAL + f * (256 - p)) / (256 * SCALE_NORMAL));
}

static int lvl_evaluate_position(Position* pos) {
    if (!pos) return 0;
    int w;
    if (eval_special_endgame(pos, &w)) return w;
    w = eval_tapered(pos);
    const int wb = (pos->to_move == WHITE) ? w : -w;
    const int f = endgame_scale(pos, wb > 0 ? WHITE : BLACK);
    if (f != SCALE_NORMAL) {
        const int p = game_phase_256(pos);
        w = (int)((long long)w * (p * SCALE_NORMAL + f * (256 - p)) / (256 * SCALE_NORMAL));
    }
    return w;
}
