/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#include "eval.h"
#include "eval_data.h"
#include "pawn_structure.h"

#define PAWN_TERMS_WEIGHT 100
#define KING_SHELTER_WEIGHT 100
#define BISHOP_PAIR_MG 33
#define BISHOP_PAIR_EG 57

#include "king_safety.h"
#include "board.h"
#include "utils.h"
#include "movegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static bool eval_debug_enabled = false;

void set_eval_debug(bool enable) {
    eval_debug_enabled = enable;
}

static int evaluate_material_count(Position* pos) {
    if (!pos) return 0;

    int score = 0;

    score = pos->mat_balance;

    for (Color color = WHITE; color <= BLACK; color++) {
        int color_multiplier = (color == WHITE) ? 1 : -1;
        if (__builtin_popcountll(pos->pieces[color][BISHOP]) >= 2) {
            score += color_multiplier * BISHOP_PAIR_BONUS;
        }
    }

    return score;
}

static int evaluate_piece_square_tables_advanced(Position* pos) {
    if (!pos) return 0;

    int score = 0;
    GamePhase phase = detect_game_phase(pos);
    const int p256 = game_phase_256(pos);

    for (Color color = WHITE; color <= BLACK; color++) {
        int color_multiplier = (color == WHITE) ? 1 : -1;
        int color_pst = 0;

        for (PieceType piece = PAWN; piece <= KING; piece++) {
            Bitboard pieces = pos->pieces[color][piece];
            const int* pst_table = get_pst_table(piece, phase);

            while (pieces) {
                int square = get_lsb(pieces);
                int pst_square = (color == WHITE) ? (square ^ 56) : square;

                color_pst += (piece == KING) ? king_pst_tapered(pst_square, p256)
                                             : pst_table[pst_square];
                pop_lsb(&pieces);
            }
        }

        score += color_multiplier * color_pst;
    }

    return score;
}

typedef struct {
    Bitboard attacks_from[64];
    Bitboard combined[2];
} AttackMaps;

static void build_attack_maps(Position* pos, AttackMaps* m) {
    const Bitboard occ = pos->all_pieces;

    for (Color color = WHITE; color <= BLACK; color++) {
        Bitboard combined = 0;

        Bitboard b = pos->pieces[color][PAWN];
        while (b) {
            const int sq = get_lsb(b);
            const Bitboard pawn_bit = 1ULL << sq;

            const Bitboard a = (color == WHITE)
                ? (((pawn_bit & ~file_masks[0]) << 7) | ((pawn_bit & ~file_masks[7]) << 9))
                : (((pawn_bit & ~file_masks[0]) >> 9) | ((pawn_bit & ~file_masks[7]) >> 7));
            m->attacks_from[sq] = a; combined |= a;
            pop_lsb(&b);
        }

        b = pos->pieces[color][KNIGHT];
        while (b) { const int sq = get_lsb(b); m->attacks_from[sq] = knight_attacks[sq]; combined |= knight_attacks[sq]; pop_lsb(&b); }

        b = pos->pieces[color][KING];
        while (b) { const int sq = get_lsb(b); m->attacks_from[sq] = king_attacks[sq]; combined |= king_attacks[sq]; pop_lsb(&b); }

        b = pos->pieces[color][BISHOP];
        while (b) { const int sq = get_lsb(b); const Bitboard a = get_bishop_attacks_public(sq, occ); m->attacks_from[sq] = a; combined |= a; pop_lsb(&b); }

        b = pos->pieces[color][ROOK];
        while (b) { const int sq = get_lsb(b); const Bitboard a = get_rook_attacks_public(sq, occ); m->attacks_from[sq] = a; combined |= a; pop_lsb(&b); }

        b = pos->pieces[color][QUEEN];
        while (b) {
            const int sq = get_lsb(b);
            const Bitboard a = get_rook_attacks_public(sq, occ) | get_bishop_attacks_public(sq, occ);
            m->attacks_from[sq] = a; combined |= a;
            pop_lsb(&b);
        }

        m->combined[color] = combined;
    }
}

static int evaluate_center_control_advanced_m(Position* pos, const AttackMaps* maps) {
    if (!pos) return 0;

    int score = 0;

    Bitboard center_squares = 0x0000001818000000ULL;

    for (Color color = WHITE; color <= BLACK; color++) {
        int color_multiplier = (color == WHITE) ? 1 : -1;
        int center_control = 0;

        Bitboard pawns = pos->pieces[color][PAWN];
        while (pawns) {

            Bitboard pawn_attacks = maps->attacks_from[get_lsb(pawns)];
            center_control += __builtin_popcountll(pawn_attacks & center_squares);
            pop_lsb(&pawns);
        }

        static const PieceType center_pieces[4] = { KNIGHT, BISHOP, ROOK, QUEEN };
        for (int pi = 0; pi < 4; pi++) {
            PieceType piece = center_pieces[pi];
            Bitboard pieces = pos->pieces[color][piece];
            while (pieces) {
                int square = get_lsb(pieces);

                Bitboard attacks = maps->attacks_from[square];
                center_control += __builtin_popcountll(attacks & center_squares);
                pop_lsb(&pieces);
            }
        }

        score += color_multiplier * center_control * CENTER_CONTROL_BONUS;
    }

    return score;
}

static int evaluate_pseudo_mobility_m(Position* pos, const AttackMaps* maps) {
    if (!pos) return 0;

    int score = 0;

    for (Color color = WHITE; color <= BLACK; color++) {
        int color_multiplier = (color == WHITE) ? 1 : -1;
        int mobility = 0;

        Bitboard knights = pos->pieces[color][KNIGHT];
        while (knights) {
            int square = get_lsb(knights);
            mobility += __builtin_popcountll(knight_attacks[square] & ~pos->occupied[color]);
            pop_lsb(&knights);
        }

        Bitboard bishops = pos->pieces[color][BISHOP];
        while (bishops) {
            int square = get_lsb(bishops);
            Bitboard attacks = maps->attacks_from[square];
            mobility += __builtin_popcountll(attacks & ~pos->occupied[color]);
            pop_lsb(&bishops);
        }

        Bitboard rooks = pos->pieces[color][ROOK];
        while (rooks) {
            int square = get_lsb(rooks);
            Bitboard attacks = maps->attacks_from[square];
            mobility += __builtin_popcountll(attacks & ~pos->occupied[color]);
            pop_lsb(&rooks);
        }

        score += color_multiplier * mobility * MOBILITY_BONUS_FACTOR;
    }

    return score;
}

static int evaluate_pins_and_forks(Position* pos) {
    if (!pos) return 0;

    int score = 0;

    for (Color color = WHITE; color <= BLACK; color++) {
        int color_multiplier = (color == WHITE) ? 1 : -1;
        Color enemy_color = 1 - color;

        Bitboard knights = pos->pieces[color][KNIGHT];
        while (knights) {
            int square = get_lsb(knights);
            Bitboard attacks = knight_attacks[square];
            int attacked_pieces = __builtin_popcountll(attacks & pos->occupied[enemy_color]);

            if (attacked_pieces >= 2) {
                score += color_multiplier * FORK_BONUS;
            }
            pop_lsb(&knights);
        }

        Bitboard enemy_king_bb = pos->pieces[enemy_color][KING];
        if (!enemy_king_bb) continue;
        int enemy_king_square = get_lsb(enemy_king_bb);

        Bitboard king_bit = 1ULL << enemy_king_square;
        Bitboard occ = pos->all_pieces;
        Bitboard enemy_occ = pos->occupied[enemy_color];

        Bitboard diag_sliders = pos->pieces[color][BISHOP] | pos->pieces[color][QUEEN];
        while (diag_sliders) {
            int square = get_lsb(diag_sliders);
            pop_lsb(&diag_sliders);

            Bitboard direct = get_bishop_attacks_public(square, occ);
            if (direct & king_bit) continue;
            Bitboard candidates = direct & enemy_occ;
            while (candidates) {
                int blocker = get_lsb(candidates);
                pop_lsb(&candidates);
                if (get_bishop_attacks_public(square, occ ^ (1ULL << blocker)) & king_bit) {
                    score += color_multiplier * (PIN_BONUS / 2);
                    break;
                }
            }
        }

        Bitboard line_sliders = pos->pieces[color][ROOK] | pos->pieces[color][QUEEN];
        while (line_sliders) {
            int square = get_lsb(line_sliders);
            pop_lsb(&line_sliders);

            Bitboard direct = get_rook_attacks_public(square, occ);
            if (direct & king_bit) continue;
            Bitboard candidates = direct & enemy_occ;
            while (candidates) {
                int blocker = get_lsb(candidates);
                pop_lsb(&candidates);
                if (get_rook_attacks_public(square, occ ^ (1ULL << blocker)) & king_bit) {
                    score += color_multiplier * (PIN_BONUS / 2);
                    break;
                }
            }
        }
    }

    return score;
}

static int evaluate_check_patterns_enhanced_m(Position* pos, const AttackMaps* maps) {
    if (!pos) return 0;

    int score = 0;

    const GamePhase phase_sum = detect_game_phase(pos);

    for (Color color = WHITE; color <= BLACK; color++) {
        int color_multiplier = (color == WHITE) ? 1 : -1;
        Color enemy_color = (color == WHITE) ? BLACK : WHITE;

        int enemy_king_square = -1;
        Bitboard enemy_king = pos->pieces[enemy_color][KING];
        if (enemy_king) {
            enemy_king_square = get_lsb(enemy_king);
        }

        if (enemy_king_square == -1) continue;

        Bitboard queens = pos->pieces[color][QUEEN];
        while (queens) {
            int queen_square = get_lsb(queens);

            Bitboard queen_attacks = maps->attacks_from[queen_square];
            if (queen_attacks & enemy_king) {

                score += color_multiplier * 150;

                if (phase_sum != PHASE_ENDGAME) {
                    score += color_multiplier * 100;
                }

                bool enemy_has_castled = has_castled(pos, enemy_color);
                if (!enemy_has_castled) {
                    score += color_multiplier * 80;
                }
            }

            pop_lsb(&queens);
        }

        Bitboard rooks = pos->pieces[color][ROOK];
        while (rooks) {
            int rook_square = get_lsb(rooks);

            Bitboard rook_attacks = maps->attacks_from[rook_square];
            if (rook_attacks & enemy_king) {
                score += color_multiplier * 80;

                int king_rank = enemy_king_square / 8;
                int rook_rank = rook_square / 8;
                if (king_rank == rook_rank && (king_rank == 0 || king_rank == 7)) {
                    score += color_multiplier * 60;
                }
            }

            pop_lsb(&rooks);
        }

        Bitboard bishops = pos->pieces[color][BISHOP];
        while (bishops) {
            int bishop_square = get_lsb(bishops);

            Bitboard bishop_attacks = maps->attacks_from[bishop_square];
            if (bishop_attacks & enemy_king) {
                score += color_multiplier * 70;
            }

            pop_lsb(&bishops);
        }

        Bitboard knights = pos->pieces[color][KNIGHT];
        while (knights) {
            int knight_square = get_lsb(knights);

            if (knight_attacks[knight_square] & enemy_king) {
                score += color_multiplier * 85;

                Bitboard fork_targets = knight_attacks[knight_square] &
                                      (pos->pieces[enemy_color][QUEEN] | pos->pieces[enemy_color][ROOK]);
                if (fork_targets) {
                    score += color_multiplier * 120;
                }
            }

            pop_lsb(&knights);
        }
    }

    return score;
}

int evaluate_development_urgency(Position* pos) {
    if (!pos) return 0;

    if (detect_game_phase(pos) != PHASE_OPENING) return 0;

    int score = 0;

    for (Color color = WHITE; color <= BLACK; color++) {
        int color_multiplier = (color == WHITE) ? 1 : -1;

        Bitboard back_rank = (color == WHITE) ? rank_masks[0] : rank_masks[7];
        int developed_knights = __builtin_popcountll(pos->pieces[color][KNIGHT] & ~back_rank);
        int developed_bishops = __builtin_popcountll(pos->pieces[color][BISHOP] & ~back_rank);

        score += color_multiplier * (developed_knights + developed_bishops) * 25;
    }

    return score;
}

int evaluate_anti_grabby_patterns(Position* pos) {
    if (!pos) return 0;

    Bitboard edge_files = 0x8181818181818181ULL;

    int score = 0;
    if (pos->pieces[WHITE][QUEEN] & edge_files) score -= 60;
    if (pos->pieces[BLACK][QUEEN] & edge_files) score += 60;

    return score;
}

int evaluate_material_threats(Position* pos) {
    if (!pos) return 0;

    int score = 0;

    for (Color color = WHITE; color <= BLACK; color++) {
        int color_multiplier = (color == WHITE) ? 1 : -1;
        Color enemy_color = 1 - color;

        Bitboard enemy_pieces = pos->occupied[enemy_color] & ~pos->pieces[enemy_color][KING];

        while (enemy_pieces) {
            int square = get_lsb(enemy_pieces);

            if (is_square_attacked(pos, square, color)) {

                PieceType piece_type = EMPTY;
                for (PieceType pt = PAWN; pt <= QUEEN; pt++) {
                    if (pos->pieces[enemy_color][pt] & (1ULL << square)) {
                        piece_type = pt;
                        break;
                    }
                }

                int piece_value = (piece_type == QUEEN) ? QUEEN_VALUE :
                                 (piece_type == ROOK) ? ROOK_VALUE :
                                 (piece_type == BISHOP) ? BISHOP_VALUE :
                                 (piece_type == KNIGHT) ? KNIGHT_VALUE :
                                 (piece_type == PAWN) ? PAWN_VALUE : 0;

                score += color_multiplier * (piece_value / 4);
            }

            pop_lsb(&enemy_pieces);
        }
    }

    return score;
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

int hung_penalty_stm = 0;

static int evaluate_hung_pieces_m(Position* pos, const AttackMaps* maps) {
    hung_penalty_stm = 0;
    if (!pos) return 0;

    int score = 0;

    const GamePhase phase_sum = detect_game_phase(pos);

    Bitboard attacked_by[2];
    attacked_by[WHITE] = maps->combined[WHITE];
    attacked_by[BLACK] = maps->combined[BLACK];

    for (Color color = WHITE; color <= BLACK; color++) {
        int color_multiplier = (color == WHITE) ? 1 : -1;
        Bitboard attacked_by_enemy = attacked_by[1 - color];
        Bitboard defended_by_own   = attacked_by[color];

        for (PieceType piece_type = PAWN; piece_type <= KING; piece_type++) {
            Bitboard pieces = pos->pieces[color][piece_type];

            while (pieces) {
                int square = get_lsb(pieces);
                Bitboard square_bit = 1ULL << square;

                bool is_attacked = (attacked_by_enemy & square_bit) != 0;

                if (is_attacked) {

                    bool can_defend = (defended_by_own & square_bit) != 0;

                    if (!can_defend) {
                        int piece_value = (piece_type == QUEEN) ? QUEEN_VALUE :
                                         (piece_type == ROOK) ? ROOK_VALUE :
                                         (piece_type == BISHOP) ? BISHOP_VALUE :
                                         (piece_type == KNIGHT) ? KNIGHT_VALUE :
                                         (piece_type == PAWN) ? PAWN_VALUE : 0;

                        int penalty = piece_value / 2;
                        score += color_multiplier * (-penalty);

                        if (color == pos->to_move) hung_penalty_stm += penalty;

                        if (piece_type != PAWN && phase_sum == PHASE_OPENING) {
                            score += color_multiplier * (-100);
                            if (color == pos->to_move) hung_penalty_stm += 100;
                        }
                    }
                }

                pop_lsb(&pieces);
            }
        }
    }

    return score;
}

static int evaluate_center_control_advanced(Position* pos) {
    if (!pos) return 0;
    AttackMaps maps; build_attack_maps(pos, &maps);
    return evaluate_center_control_advanced_m(pos, &maps);
}

static int evaluate_check_patterns_enhanced(Position* pos) {
    if (!pos) return 0;
    AttackMaps maps; build_attack_maps(pos, &maps);
    return evaluate_check_patterns_enhanced_m(pos, &maps);
}

static int evaluate_pseudo_mobility(Position* pos) {
    if (!pos) return 0;
    AttackMaps maps; build_attack_maps(pos, &maps);
    return evaluate_pseudo_mobility_m(pos, &maps);
}

int evaluate_hung_pieces(Position* pos) {
    hung_penalty_stm = 0;
    if (!pos) return 0;
    AttackMaps maps; build_attack_maps(pos, &maps);
    return evaluate_hung_pieces_m(pos, &maps);
}

static const int pst_mg_pawn[64] = {
      0,   0,   0,   0,   0,   0,   0,   0,
     98, 134,  61,  95,  68, 126,  34, -11,
     -6,   7,  26,  31,  65,  56,  25, -20,
    -14,  13,   6,  21,  23,  12,  17, -23,
    -27,  -2,  -5,  12,  17,   6,  10, -25,
    -26,  -4,  -4, -10,   3,   3,  33, -12,
    -35,  -1, -20, -23, -15,  24,  38, -22,
      0,   0,   0,   0,   0,   0,   0,   0,
};
static const int pst_eg_pawn[64] = {
      0,   0,   0,   0,   0,   0,   0,   0,
    178, 173, 158, 134, 147, 132, 165, 187,
     94, 100,  85,  67,  56,  53,  82,  84,
     32,  24,  13,   5,  -2,   4,  17,  17,
     13,   9,  -3,  -7,  -7,  -8,   3,  -1,
      4,   7,  -6,   1,   0,  -5,  -1,  -8,
     13,   8,   8,  10,  13,   0,   2,  -7,
      0,   0,   0,   0,   0,   0,   0,   0,
};
static const int pst_mg_knight[64] = {
   -167, -89, -34, -49,  61, -97, -15, -107,
    -73, -41,  72,  36,  23,  62,   7,  -17,
    -47,  60,  37,  65,  84, 129,  73,   44,
     -9,  17,  19,  53,  37,  69,  18,   22,
    -13,   4,  16,  13,  28,  19,  21,   -8,
    -23,  -9,  12,  10,  19,  17,  25,  -16,
    -29, -53, -12,  -3,  -1,  18, -14,  -19,
   -105, -21, -58, -33, -17, -28, -19,  -23,
};
static const int pst_eg_knight[64] = {
    -58, -38, -13, -28, -31, -27, -63, -99,
    -25,  -8, -25,  -2,  -9, -25, -24, -52,
    -24, -20,  10,   9,  -1,  -9, -19, -41,
    -17,   3,  22,  22,  22,  11,   8, -18,
    -18,  -6,  16,  25,  16,  17,   4, -18,
    -23,  -3,  -1,  15,  10,  -3, -20, -22,
    -42, -20, -10,  -5,  -2, -20, -23, -44,
    -29, -51, -23, -15, -22, -18, -50, -64,
};
static const int pst_mg_bishop[64] = {
    -29,   4, -82, -37, -25, -42,   7,  -8,
    -26,  16, -18, -13,  30,  59,  18, -47,
    -16,  37,  43,  40,  35,  50,  37,  -2,
     -4,   5,  19,  50,  37,  37,   7,  -2,
     -6,  13,  13,  26,  34,  12,  10,   4,
      0,  15,  15,  15,  14,  27,  18,  10,
      4,  15,  16,   0,   7,  21,  33,   1,
    -33,  -3, -14, -21, -13, -12, -39, -21,
};
static const int pst_eg_bishop[64] = {
    -14, -21, -11,  -8,  -7,  -9, -17, -24,
     -8,  -4,   7, -12,  -3, -13,  -4, -14,
      2,  -8,   0,  -1,  -2,   6,   0,   4,
     -3,   9,  12,   9,  14,  10,   3,   2,
     -6,   3,  13,  19,   7,  10,  -3,  -9,
    -12,  -3,   8,  10,  13,   3,  -7, -15,
    -14, -18,  -7,  -1,   4,  -9, -15, -27,
    -23,  -9, -23,  -5,  -9, -16,  -5, -17,
};
static const int pst_mg_rook[64] = {
     32,  42,  32,  51,  63,   9,  31,  43,
     27,  32,  58,  62,  80,  67,  26,  44,
     -5,  19,  26,  36,  17,  45,  61,  16,
    -24, -11,   7,  26,  24,  35,  -8, -20,
    -36, -26, -12,  -1,   9,  -7,   6, -23,
    -45, -25, -16, -17,   3,   0,  -5, -33,
    -44, -16, -20,  -9,  -1,  11,  -6, -71,
    -19, -13,   1,  17,  16,   7, -37, -26,
};
static const int pst_eg_rook[64] = {
     13,  10,  18,  15,  12,  12,   8,   5,
     11,  13,  13,  11,  -3,   3,   8,   3,
      7,   7,   7,   5,   4,  -3,  -5,  -3,
      4,   3,  13,   1,   2,   1,  -1,   2,
      3,   5,   8,   4,  -5,  -6,  -8, -11,
     -4,   0,  -5,  -1,  -7, -12,  -8, -16,
     -6,  -6,   0,   2,  -9,  -9, -11,  -3,
     -9,   2,   3,  -1,  -5, -13,   4, -20,
};
static const int pst_mg_queen[64] = {
    -28,   0,  29,  12,  59,  44,  43,  45,
    -24, -39,  -5,   1, -16,  57,  28,  54,
    -13, -17,   7,   8,  29,  56,  47,  57,
    -27, -27, -16, -16,  -1,  17,  -2,   1,
     -9, -26,  -9, -10,  -2,  -4,   3,  -3,
    -14,   2, -11,  -2,  -5,   2,  14,   5,
    -35,  -8,  11,   2,   8,  15,  -3,   1,
     -1, -18,  -9,  10, -15, -25, -31, -50,
};
static const int pst_eg_queen[64] = {
     -9,  22,  22,  27,  27,  19,  10,  20,
    -17,  20,  32,  41,  58,  25,  30,   0,
    -20,   6,   9,  49,  47,  35,  19,   9,
      3,  22,  24,  45,  57,  40,  57,  36,
    -18,  28,  19,  47,  31,  34,  39,  23,
    -16, -27,  15,   6,   9,  17,  10,   5,
    -22, -23, -30, -16, -16, -23, -36, -32,
    -33, -28, -22, -43,  -5, -32, -20, -41,
};
static const int pst_mg_king[64] = {
    -65,  23,  16, -15, -56, -34,   2,  13,
     29,  -1, -20,  -7,  -8,  -4, -38, -29,
     -9,  24,   2, -16, -20,   6,  22, -22,
    -17, -20, -12, -27, -30, -25, -14, -36,
    -49,  -1, -27, -39, -46, -44, -33, -51,
    -14, -14, -22, -46, -44, -30, -15, -27,
      1,   7,  -8, -64, -43, -16,   9,   8,
    -15,  36,  12, -54,   8, -28,  24,  14,
};
static const int pst_eg_king[64] = {
    -74, -35, -18, -18, -11,  15,   4, -17,
    -12,  17,  14,  17,  17,  38,  23,  11,
     10,  17,  23,  15,  20,  45,  44,  13,
     -8,  22,  24,  27,  26,  33,  26,   3,
    -18,  -4,  21,  24,  27,  23,   9, -11,
    -19,  -3,  11,  21,  23,  16,   7,  -9,
    -27, -11,   4,  13,  14,   4,  -5, -17,
    -53, -34, -21, -11, -28, -14, -24, -43,
};

static const int* const pst_mg_tables[6] = { pst_mg_pawn, pst_mg_rook, pst_mg_knight,
                                         pst_mg_bishop, pst_mg_queen, pst_mg_king };
static const int* const pst_eg_tables[6] = { pst_eg_pawn, pst_eg_rook, pst_eg_knight,
                                         pst_eg_bishop, pst_eg_queen, pst_eg_king };

static const int eval_piece_value[6] = { PAWN_VALUE, ROOK_VALUE, KNIGHT_VALUE,
                                   BISHOP_VALUE, QUEEN_VALUE, KING_VALUE };

static const int passed_bonus_mg[8] = { 0,  5, 10, 20,  35,  60, 100, 0 };
static const int passed_bonus_eg[8] = { 0, 10, 20, 35,  60, 100, 160, 0 };

static const int mobility_mg[6] = { 0, 3, 4, 4, 2, 0 };
static const int mobility_eg[6] = { 0, 4, 4, 5, 4, 0 };

static const int king_zone_weight[6]  = { 0, 3, 2, 2, 5, 0 };

static const int pawn_protected_mg[6] = {  26,   9,   2,   7,  -9, -33 };
static const int pawn_protected_eg[6] = {  12,   7,  12,  13,  15,  17 };

static const int pawn_threat_mg[6] = {  -4,  11,  24,  13,  10,   4 };
static const int pawn_threat_eg[6] = {   0,  14,   0,   5,  12,   5 };

static const int attacked_by_pawn_mg[2] = {  71, 175 };
static const int attacked_by_pawn_eg[2] = {  10,  95 };

static const int protected_passer_mg = 15;
static const int protected_passer_eg = 16;

static const int blocked_passer_mg[4] = {   6,  -7,  -9,  61 };
static const int blocked_passer_eg[4] = {  13,  31,  59,  60 };

static const int passer_king_dist_mg[2] = {  -1,  -3 };
static const int passer_king_dist_eg[2] = {  -4,   8 };

static const int phalanx_mg = 13;
static const int phalanx_eg = 11;

static const int doubled_mg = 12;
static const int doubled_eg = 26;

static inline int iabs(int x) { return x < 0 ? -x : x; }
static inline int imax(int a, int b) { return a > b ? a : b; }

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

static int eval_tapered(Position* pos) {
    if (!pawn_masks_ready) build_pawn_masks();

    const int p256 = game_phase_256(pos);
    const Bitboard occ_all = pos->all_pieces;

    int material = 0;
    int mg_pst = 0, eg_pst = 0;
    int mg_passed = 0, eg_passed = 0;
    int mg_mob = 0, eg_mob = 0;
    int zone_bb[2] = { 0, 0 };
    int mg_pawn_terms = 0, eg_pawn_terms = 0;

    Bitboard pawn_att[2];
    pawn_att[WHITE] = pawn_set_attacks(pos->pieces[WHITE][PAWN], WHITE);
    pawn_att[BLACK] = pawn_set_attacks(pos->pieces[BLACK][PAWN], BLACK);

    Bitboard king_zone[2];
    for (Color k = WHITE; k <= BLACK; k++) {
        const Bitboard king_bb1 = pos->pieces[k][KING];
        const int king_sq_v = king_bb1 ? get_lsb(king_bb1) : -1;
        king_zone[k] = (king_sq_v >= 0) ? (king_attacks[king_sq_v] | king_bb1) : 0ULL;
    }

    for (Color side_c = WHITE; side_c <= BLACK; side_c++) {
        const int sign = (side_c == WHITE) ? 1 : -1;
        const Color enemy_side = (side_c == WHITE) ? BLACK : WHITE;
        {
            const Bitboard own_pawns = pos->pieces[side_c][PAWN];

            const int phalanx_cnt = __builtin_popcountll(
                ((own_pawns >> 1) & ~0x8080808080808080ULL) & own_pawns);
            mg_pawn_terms += sign * phalanx_cnt * phalanx_mg;
            eg_pawn_terms += sign * phalanx_cnt * phalanx_eg;
            int doubled_cnt = 0;
            for (int file_idx = 0; file_idx < 8; file_idx++) {
                const int n = __builtin_popcountll(
                    own_pawns & (0x0101010101010101ULL << file_idx));
                if (n > 1) doubled_cnt += n - 1;
            }
            mg_pawn_terms -= sign * doubled_cnt * doubled_mg;
            eg_pawn_terms -= sign * doubled_cnt * doubled_eg;
        }

        for (PieceType ptype = PAWN; ptype <= KING; ptype++) {
            Bitboard bb = pos->pieces[side_c][ptype];
            material += sign * __builtin_popcountll(bb) * eval_piece_value[ptype];

            while (bb) {
                const int sq = get_lsb(bb);
                pop_lsb(&bb);
                const int sq_pst = (side_c == WHITE) ? (sq ^ 56) : sq;

                mg_pst += sign * pst_mg_tables[ptype][sq_pst];
                eg_pst += sign * pst_eg_tables[ptype][sq_pst];
                {
                    const Bitboard bit = 1ULL << sq;
                    if (bit & pawn_att[side_c]) {
                        mg_pawn_terms += sign * pawn_protected_mg[ptype];
                        eg_pawn_terms += sign * pawn_protected_eg[ptype];
                    }

                    if ((passed_span_mask[side_c][sq] & file_mask_of_sq[sq])
                        & pawn_att[enemy_side]) {
                        mg_pawn_terms -= sign * pawn_threat_mg[ptype];
                        eg_pawn_terms -= sign * pawn_threat_eg[ptype];
                    }
                    if (ptype != PAWN && (bit & pawn_att[enemy_side])) {
                        const int i_stm = (side_c == pos->to_move) ? 0 : 1;
                        mg_pawn_terms -= sign * attacked_by_pawn_mg[i_stm];
                        eg_pawn_terms -= sign * attacked_by_pawn_eg[i_stm];
                    }
                }

                if (ptype == PAWN) {

                    if (!(passed_span_mask[side_c][sq] & pos->pieces[enemy_side][PAWN])) {

                        const int rel_rank = (side_c == WHITE) ? (sq / 8) : 7 - (sq / 8);
                        const int r = (rel_rank < 0) ? 0 : (rel_rank > 7 ? 7 : rel_rank);
                        mg_passed += sign * passed_bonus_mg[r];
                        eg_passed += sign * passed_bonus_eg[r];

                        if (rel_rank >= 3 && rel_rank <= 6) {
                            const int idx = rel_rank - 3;
                            const int advance = rel_rank - 1;
                            const int front_sq = (side_c == WHITE) ? (sq + 8) : (sq - 8);
                            if ((1ULL << sq) & pawn_att[side_c]) {
                                mg_pawn_terms += sign * protected_passer_mg;
                                eg_pawn_terms += sign * protected_passer_eg;
                            }
                            if ((1ULL << front_sq) & pos->occupied[enemy_side]) {
                                mg_pawn_terms -= sign * blocked_passer_mg[idx];
                                eg_pawn_terms -= sign * blocked_passer_eg[idx];
                            }
                            for (int i_k = 0; i_k < 2; i_k++) {
                                const Color kk = (i_k == 0) ? side_c : enemy_side;
                                const Bitboard kbb = pos->pieces[kk][KING];
                                if (!kbb) continue;
                                const int ks = get_lsb(kbb);
                                const int d = imax(iabs(ks / 8 - front_sq / 8),
                                                      iabs(ks % 8 - front_sq % 8));
                                mg_pawn_terms += sign * passer_king_dist_mg[i_k] * advance * d;
                                eg_pawn_terms += sign * passer_king_dist_eg[i_k] * advance * d;
                            }
                        }
                    }
                    continue;
                }
                if (ptype == KING) continue;

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
                mg_mob += sign * n * mobility_mg[ptype];
                eg_mob += sign * n * mobility_eg[ptype];

                const int zone_hits = __builtin_popcountll(att_bb & king_zone[enemy_side]);
                if (zone_hits) zone_bb[enemy_side] += zone_hits * king_zone_weight[ptype];
            }
        }
    }

    const int king_zone_penalty = (zone_bb[BLACK] - zone_bb[WHITE]) * p256 / 256;

    const int white_king_sq = get_king_square((Position*)pos, WHITE);
    const int black_king_sq = get_king_square((Position*)pos, BLACK);
    const int king_shelter_raw =
        (evaluate_king_shelter((Position*)pos, white_king_sq, WHITE)
         + evaluate_open_files_near_king((Position*)pos, white_king_sq, WHITE))
      - (evaluate_king_shelter((Position*)pos, black_king_sq, BLACK)
         + evaluate_open_files_near_king((Position*)pos, black_king_sq, BLACK));
    const int king_shelter = king_shelter_raw * KING_SHELTER_WEIGHT * p256 / (100 * 256);

    const int bishop_pair_value = ((BISHOP_PAIR_MG) * p256 + (BISHOP_PAIR_EG) * (256 - p256)) / 256;
    const int bishop_pair =
        ((__builtin_popcountll(pos->pieces[WHITE][BISHOP]) >= 2) ? bishop_pair_value : 0)
      - ((__builtin_popcountll(pos->pieces[BLACK][BISHOP]) >= 2) ? bishop_pair_value : 0);

    const int pawn_terms =
        ((mg_pawn_terms * p256 + eg_pawn_terms * (256 - p256)) / 256) * PAWN_TERMS_WEIGHT / 100;

    const int mg = mg_pst + mg_passed + mg_mob;
    const int eg = eg_pst + eg_passed + eg_mob;
    const int positional = (mg * p256 + eg * (256 - p256)) / 256;
    const int total = material + positional + king_zone_penalty + king_shelter + bishop_pair + pawn_terms;

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

int evaluate_position(Position* pos) {
    if (!pos) return 0;
    return eval_tapered(pos);
}

int evaluate_material(Position* pos) {
    return evaluate_material_count(pos);
}

int evaluate_position_simple(Position* pos) {
    if (!pos) return 0;
    int score = evaluate_material_count(pos);
    score += evaluate_piece_square_tables_advanced(pos) / 2;
    return (pos->to_move == WHITE) ? score : -score;
}

int evaluate_piece_square_tables(Position* pos) {
    return evaluate_piece_square_tables_advanced(pos);
}

int evaluate_center_control(Position* pos) {
    return evaluate_center_control_advanced(pos);
}

int evaluate_mobility(Position* pos) {
    if (!pos) return 0;
    return evaluate_pseudo_mobility(pos);
}

int evaluate_tactical_patterns(Position* pos) {
    if (!pos) return 0;
    return evaluate_pins_and_forks(pos) + evaluate_check_patterns_enhanced(pos);
}

bool is_blunder_move(Position* pos, Move* move) {
    if (!pos || !move) return false;
    return false;
}

static void debug_material_loss(void) {
    printf("=== DEBUGGING MATERIAL LOSS ISSUE ===\n");
    printf("Testing positions before/after knight loss:\n\n");

    Position pos_before, pos_after;

    printf("1. POSITION BEFORE Na4 (after 1.Nf3 d5 2.Nc3 d4):\n");
    if (!parse_fen(&pos_before, "rnbqkbnr/ppp1pppp/8/8/3p4/2N2N2/PPPPPPPP/R1BQKB1R w KQkq - 0 3")) {
        printf("   ERROR: %s - aborting debug\n", fen_last_error());
        return;
    }

    printf("   Material count test:\n");
    int material_before = evaluate_material_count(&pos_before);
    printf("   evaluate_material_count(): %+d\n", material_before);

    printf("   Full evaluation breakdown:\n");
    set_eval_debug(true);
    int eval_before = evaluate_position(&pos_before);
    set_eval_debug(false);
    printf("   Final evaluation: %+d\n\n", eval_before);

    printf("2. POSITION AFTER Qxa4 (knight captured):\n");
    if (!parse_fen(&pos_after, "rnb1kbnr/ppp1pppp/8/8/q2p4/5N2/PPPPPPPP/R1BQKB1R w KQkq - 0 6")) {
        printf("   ERROR: %s - aborting debug\n", fen_last_error());
        return;
    }

    printf("   Material count test:\n");
    int material_after = evaluate_material_count(&pos_after);
    printf("   evaluate_material_count(): %+d\n", material_after);

    printf("   Full evaluation breakdown:\n");
    set_eval_debug(true);
    int eval_after = evaluate_position(&pos_after);
    set_eval_debug(false);
    printf("   Final evaluation: %+d\n\n", eval_after);

    printf("3. ANALYSIS:\n");
    printf("   Material difference: %+d - (%+d) = %+d\n",
           material_after, material_before, material_after - material_before);
    printf("   Expected material loss: %+d (knight value)\n", -KNIGHT_VALUE);
    printf("   Evaluation difference: %+d - (%+d) = %+d\n",
           eval_after, eval_before, eval_after - eval_before);

    if (abs(material_after - material_before + KNIGHT_VALUE) < 10) {
        printf("   ✅ MATERIAL COUNTING: Working correctly\n");
    } else {
        printf("   ❌ MATERIAL COUNTING: BUG DETECTED!\n");
    }

    if (abs(eval_after - eval_before + KNIGHT_VALUE) < 50) {
        printf("   ✅ OVERALL EVALUATION: Shows material loss\n");
    } else {
        printf("   ❌ OVERALL EVALUATION: Compensation bug detected!\n");
        printf("   Other components are compensating for material loss\n");
    }

    printf("\n=== MATERIAL LOSS DEBUG COMPLETED ===\n\n");
}

void test_material_debug(void) {
    debug_material_loss();
}

const int* get_king_middlegame_pst(void) {
    return king_middlegame_pst;
}

const int* get_king_endgame_pst(void) {
    return king_endgame_pst;
}

