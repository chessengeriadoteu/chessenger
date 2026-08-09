/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#include "eval.h"
#include "eval_data.h"
#include "pawn_structure.h"
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

    for (Color color = WHITE; color <= BLACK; color++) {
        int color_multiplier = (color == WHITE) ? 1 : -1;

        score += color_multiplier * __builtin_popcountll(pos->pieces[color][PAWN]) * PAWN_VALUE;
        score += color_multiplier * __builtin_popcountll(pos->pieces[color][ROOK]) * ROOK_VALUE;
        score += color_multiplier * __builtin_popcountll(pos->pieces[color][KNIGHT]) * KNIGHT_VALUE;
        score += color_multiplier * __builtin_popcountll(pos->pieces[color][BISHOP]) * BISHOP_VALUE;
        score += color_multiplier * __builtin_popcountll(pos->pieces[color][QUEEN]) * QUEEN_VALUE;
    }

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

    for (Color color = WHITE; color <= BLACK; color++) {
        int color_multiplier = (color == WHITE) ? 1 : -1;
        int color_pst = 0;

        for (PieceType piece = PAWN; piece <= KING; piece++) {
            Bitboard pieces = pos->pieces[color][piece];
            const int* pst_table = get_pst_table(piece, phase);

            while (pieces) {
                int square = get_lsb(pieces);
                int pst_square = (color == WHITE) ? square : (square ^ 56);
                color_pst += pst_table[pst_square];
                pop_lsb(&pieces);
            }
        }

        score += color_multiplier * color_pst;
    }

    return score;
}

static int evaluate_center_control_advanced(Position* pos) {
    if (!pos) return 0;

    int score = 0;

    Bitboard center_squares = 0x0000001818000000ULL;

    for (Color color = WHITE; color <= BLACK; color++) {
        int color_multiplier = (color == WHITE) ? 1 : -1;
        int center_control = 0;

        Bitboard pawns = pos->pieces[color][PAWN];
        while (pawns) {
            int square = get_lsb(pawns);

            Bitboard pawn_attacks = 0;
            int file = square % 8;
            if (color == WHITE) {
                if (file > 0) pawn_attacks |= (1ULL << (square + 7));
                if (file < 7) pawn_attacks |= (1ULL << (square + 9));
            } else {
                if (file > 0) pawn_attacks |= (1ULL << (square - 9));
                if (file < 7) pawn_attacks |= (1ULL << (square - 7));
            }
            center_control += __builtin_popcountll(pawn_attacks & center_squares);
            pop_lsb(&pawns);
        }

        for (PieceType piece = KNIGHT; piece <= QUEEN; piece++) {
            Bitboard pieces = pos->pieces[color][piece];
            while (pieces) {
                int square = get_lsb(pieces);

                Bitboard attacks = 0;
                if (piece == KNIGHT) {
                    attacks = knight_attacks[square];
                } else if (piece == BISHOP) {
                    attacks = get_bishop_attacks_public(square, pos->all_pieces);
                } else if (piece == ROOK) {
                    attacks = get_rook_attacks_public(square, pos->all_pieces);
                } else if (piece == QUEEN) {
                    attacks = get_rook_attacks_public(square, pos->all_pieces) |
                             get_bishop_attacks_public(square, pos->all_pieces);
                } else if (piece == KING) {
                    attacks = king_attacks[square];
                }
                center_control += __builtin_popcountll(attacks & center_squares);
                pop_lsb(&pieces);
            }
        }

        score += color_multiplier * center_control * CENTER_CONTROL_BONUS;
    }

    return score;
}

static int evaluate_piece_development(Position* pos) {
    if (!pos) return 0;

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

static int evaluate_pseudo_mobility(Position* pos) {
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
            Bitboard attacks = get_bishop_attacks_public(square, pos->all_pieces);
            mobility += __builtin_popcountll(attacks & ~pos->occupied[color]);
            pop_lsb(&bishops);
        }

        Bitboard rooks = pos->pieces[color][ROOK];
        while (rooks) {
            int square = get_lsb(rooks);
            Bitboard attacks = get_rook_attacks_public(square, pos->all_pieces);
            mobility += __builtin_popcountll(attacks & ~pos->occupied[color]);
            pop_lsb(&rooks);
        }

        score += color_multiplier * mobility * MOBILITY_BONUS_FACTOR;
    }

    return score;
}

static int evaluate_knight_outposts(Position* pos) {
    if (!pos) return 0;

    int score = 0;

    Bitboard white_outposts = 0x00003C3C3C000000ULL;
    Bitboard black_outposts = 0x0000003C3C3C0000ULL;

    for (Color color = WHITE; color <= BLACK; color++) {
        int color_multiplier = (color == WHITE) ? 1 : -1;
        Bitboard outpost_squares = (color == WHITE) ? white_outposts : black_outposts;

        Bitboard knights = pos->pieces[color][KNIGHT] & outpost_squares;
        score += color_multiplier * __builtin_popcountll(knights) * KNIGHT_OUTPOST_BONUS;
    }

    return score;
}

static int evaluate_piece_activity_enhanced(Position* pos) {
    if (!pos) return 0;

    int score = 0;

    for (Color color = WHITE; color <= BLACK; color++) {
        int color_multiplier = (color == WHITE) ? 1 : -1;

        Bitboard rooks = pos->pieces[color][ROOK];
        while (rooks) {
            int square = get_lsb(rooks);
            int file = square % 8;

            if (!(file_masks[file] & (pos->pieces[WHITE][PAWN] | pos->pieces[BLACK][PAWN]))) {
                score += color_multiplier * ROOK_OPEN_FILE_BONUS;
            }
            pop_lsb(&rooks);
        }

        Bitboard bishops = pos->pieces[color][BISHOP];
        while (bishops) {
            int square = get_lsb(bishops);
            Bitboard attacks = get_bishop_attacks_public(square, pos->all_pieces);
            int mobility = __builtin_popcountll(attacks & ~pos->occupied[color]);
            score += color_multiplier * mobility * BISHOP_MOBILITY_BONUS;
            pop_lsb(&bishops);
        }
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

        Bitboard sliding_pieces = pos->pieces[color][BISHOP] | pos->pieces[color][ROOK] | pos->pieces[color][QUEEN];
        int enemy_king_square = get_lsb(pos->pieces[enemy_color][KING]);

        while (sliding_pieces) {
            int square = get_lsb(sliding_pieces);

            if (get_bishop_attacks_public(square, pos->all_pieces) & (1ULL << enemy_king_square)) {

                score += color_multiplier * (PIN_BONUS / 2);
            }

            if (get_rook_attacks_public(square, pos->all_pieces) & (1ULL << enemy_king_square)) {

                score += color_multiplier * (PIN_BONUS / 2);
            }

            pop_lsb(&sliding_pieces);
        }
    }

    return score;
}

static int evaluate_check_patterns_enhanced(Position* pos) {
    if (!pos) return 0;

    int score = 0;

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

            Bitboard queen_attacks = get_queen_attacks_public(queen_square, pos->all_pieces);
            if (queen_attacks & enemy_king) {

                score += color_multiplier * 150;

                GamePhase phase = detect_game_phase(pos);
                if (phase != PHASE_ENDGAME) {
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

            Bitboard rook_attacks = get_rook_attacks_public(rook_square, pos->all_pieces);
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

            Bitboard bishop_attacks = get_bishop_attacks_public(bishop_square, pos->all_pieces);
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

static int evaluate_opening_principles(Position* pos) {
    if (!pos) return 0;

    if (detect_game_phase(pos) != PHASE_OPENING) return 0;

    int score = 0;

    for (Color color = WHITE; color <= BLACK; color++) {
        int color_multiplier = (color == WHITE) ? 1 : -1;

        if (pos->castling_rights & ((color == WHITE) ? 0x03 : 0x0C)) {
            score += color_multiplier * CASTLING_RIGHTS_BONUS;
        }

        Bitboard queens = pos->pieces[color][QUEEN];
        if (queens) {
            int queen_square = get_lsb(queens);
            int queen_rank = queen_square / 8;
            int expected_rank = (color == WHITE) ? 0 : 7;

            if (queen_rank != expected_rank) {
                score += color_multiplier * QUEEN_EARLY_DEVELOPMENT_PENALTY;
            }
        }

        Bitboard center_pawns = pos->pieces[color][PAWN] & 0x0000001818000000ULL;
        score += color_multiplier * __builtin_popcountll(center_pawns) * CENTRAL_PAWN_DEVELOPMENT_BONUS;
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

    Bitboard edge_files = 0x8989818181818181ULL;

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

int evaluate_hung_pieces(Position* pos) {
    if (!pos) return 0;

    int score = 0;

    for (Color color = WHITE; color <= BLACK; color++) {
        int color_multiplier = (color == WHITE) ? 1 : -1;

        for (PieceType piece_type = PAWN; piece_type <= KING; piece_type++) {
            Bitboard pieces = pos->pieces[color][piece_type];

            while (pieces) {
                int square = get_lsb(pieces);

                bool is_attacked = is_square_attacked(pos, square, 1 - color);

                if (is_attacked) {

                    Bitboard original_piece = pos->pieces[color][piece_type];
                    pos->pieces[color][piece_type] &= ~(1ULL << square);
                    pos->all_pieces &= ~(1ULL << square);
                    pos->occupied[color] &= ~(1ULL << square);

                    bool can_defend = is_square_attacked(pos, square, color);

                    pos->pieces[color][piece_type] = original_piece;
                    pos->all_pieces |= (1ULL << square);
                    pos->occupied[color] |= (1ULL << square);

                    if (!can_defend) {
                        int piece_value = (piece_type == QUEEN) ? QUEEN_VALUE :
                                         (piece_type == ROOK) ? ROOK_VALUE :
                                         (piece_type == BISHOP) ? BISHOP_VALUE :
                                         (piece_type == KNIGHT) ? KNIGHT_VALUE :
                                         (piece_type == PAWN) ? PAWN_VALUE : 0;

                        int penalty = piece_value / 2;
                        score += color_multiplier * (-penalty);

                        if (piece_type >= KNIGHT && detect_game_phase(pos) == PHASE_OPENING) {
                            score += color_multiplier * (-100);
                        }
                    }
                }

                pop_lsb(&pieces);
            }
        }
    }

    return score;
}

int evaluate_position(Position* pos) {
    if (!pos) return 0;

    GamePhase phase = detect_game_phase(pos);
    EvaluationWeights weights = get_evaluation_weights(phase);

    int material = evaluate_material_count(pos);
    int pst = evaluate_piece_square_tables_advanced(pos);
    int pawns = evaluate_pawn_structure(pos);
    int king_safety = evaluate_king_safety(pos);
    int center_control = evaluate_center_control_advanced(pos);
    int development = evaluate_piece_development(pos);
    int mobility = evaluate_pseudo_mobility(pos);
    int knight_outposts = evaluate_knight_outposts(pos);
    int piece_activity = evaluate_piece_activity_enhanced(pos);
    int pins_and_forks = evaluate_pins_and_forks(pos);
    int check_patterns = evaluate_check_patterns_enhanced(pos);
    int development_urgency = evaluate_development_urgency(pos);
    int anti_grabby = evaluate_anti_grabby_patterns(pos);
    int hung_pieces = evaluate_hung_pieces(pos);
    int opening_principles = evaluate_opening_principles(pos);

    int weighted_material = (int)(material * weights.material);
    int weighted_pst = (int)(pst * weights.pst);
    int weighted_pawns = (int)(pawns * weights.pawns);
    int weighted_king_safety = (int)(king_safety * weights.king_safety);
    int weighted_center = (int)(center_control * weights.center_control);
    int weighted_development = (int)((development + development_urgency) * weights.development);
    int weighted_mobility = (int)(mobility * weights.mobility);
    int weighted_activity = (int)((piece_activity + knight_outposts) * weights.piece_activity);
    int weighted_tactical = (int)((pins_and_forks + check_patterns) * weights.tactical);

    int total_score = weighted_material + weighted_pst + weighted_pawns +
                     weighted_king_safety + weighted_center + weighted_development +
                     weighted_mobility + weighted_activity + weighted_tactical +
                     anti_grabby + opening_principles + hung_pieces;

    if (eval_debug_enabled) {
        const char* phase_name = (phase == PHASE_OPENING) ?
                                "OPENING" :
                                (phase == PHASE_MIDDLEGAME) ? "MIDDLEGAME" : "ENDGAME";
        printf("DEBUG: ================================\n");
        printf("DEBUG: EVALUATION BREAKDOWN\n");
        printf("DEBUG: Game phase: %s\n", phase_name);
        printf("DEBUG: ================================\n");
        printf("DEBUG:   Material:      %+d (%.1fx) = %+d\n", material, weights.material, weighted_material);
        printf("DEBUG:   PST:           %+d (%.1fx) = %+d\n", pst, weights.pst, weighted_pst);
        printf("DEBUG:   Pawns:         %+d (%.1fx) = %+d\n", pawns, weights.pawns, weighted_pawns);
        printf("DEBUG:   King Safety:   %+d (%.1fx) = %+d\n", king_safety, weights.king_safety, weighted_king_safety);
        printf("DEBUG:   Center:        %+d (%.1fx) = %+d\n", center_control, weights.center_control, weighted_center);
        printf("DEBUG:   Development:   %+d (%.1fx) = %+d\n", development + development_urgency, weights.development, weighted_development);
        printf("DEBUG:   Mobility:      %+d (%.1fx) = %+d\n", mobility, weights.mobility, weighted_mobility);
        printf("DEBUG:   Activity:      %+d (%.1fx) = %+d\n", piece_activity + knight_outposts, weights.piece_activity, weighted_activity);
        printf("DEBUG:   Tactical:      %+d (%.1fx) = %+d\n", pins_and_forks + check_patterns, weights.tactical, weighted_tactical);
        printf("DEBUG:   Anti-grabby:   %+d\n", anti_grabby);
        printf("DEBUG:   Opening Princ: %+d\n", opening_principles);
        printf("DEBUG:   Hung Pieces:   %+d\n", hung_pieces);
        printf("DEBUG:   TOTAL:         %+d\n", total_score);
        printf("DEBUG:   Perspective:   %s\n", pos->to_move == WHITE ? "WHITE" : "BLACK");
        printf("DEBUG:   Final:         %+d\n", (pos->to_move == WHITE) ? total_score : -total_score);
        printf("DEBUG: ================================\n\n");
    }

    return (pos->to_move == WHITE) ? total_score : -total_score;
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
    parse_fen(&pos_before, "rnbqkbnr/ppp1pppp/8/8/3p4/2N2N2/PPPPPPPP/R1BQKB1R w KQkq - 0 3");

    printf("   Material count test:\n");
    int material_before = evaluate_material_count(&pos_before);
    printf("   evaluate_material_count(): %+d\n", material_before);

    printf("   Full evaluation breakdown:\n");
    set_eval_debug(true);
    int eval_before = evaluate_position(&pos_before);
    set_eval_debug(false);
    printf("   Final evaluation: %+d\n\n", eval_before);

    printf("2. POSITION AFTER Qxa4 (knight captured):\n");
    parse_fen(&pos_after, "rnb1kbnr/ppp1pppp/8/8/q2p4/5N2/PPPPPPPP/R1BQKB1R w KQkq - 0 6");

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