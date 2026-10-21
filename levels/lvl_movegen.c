/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
static int lvl_generate_castling_moves(Position* pos, Move* moves, int move_count);
static int lvl_generate_en_passant_moves(Position* pos, Move* moves, int move_count);
static bool lvl_is_legal_move(Position* pos, Move* move);
static void lvl_legality_context_init_pub(Position* pos, LegalityBuffer* buf_area);
static bool lvl_is_legal_fast_pub(Position* pos, Move* this_move, const LegalityBuffer* buf_area);
static int lvl_generate_moves(Position* pos, Move* moves);
static int lvl_generate_legal_moves(Position* pos, Move* moves);
static int lvl_generate_check_evasions(Position* pos, Move* moves);
static int lvl_generate_legal_evasions(Position* pos, Move* moves);
static int lvl_generate_noisy_moves(Position* pos, Move* moves);
static int lvl_generate_legal_noisy(Position* pos, Move* moves);

static Bitboard lvl_squares_between(int a, int b);
static Bitboard lvl_between_table[64][64];
#  define BETWEEN_SQUARES(a, b) (lvl_between_table[(a)][(b)])

static bool lvl_is_king_move_adjacent_safe(Position* pos, int to, Color color) {
    if (!pos || to < 0 || to > 63) return false;

    Color enemy = 1 - color;
    Bitboard enemy_king = pos->pieces[enemy][KING];

    if (enemy_king == 0) return true;

    int enemy_king_square = get_lsb(enemy_king);
    if (enemy_king_square < 0 || enemy_king_square > 63) return true;

    return !(king_attacks[enemy_king_square] & (1ULL << to));
}
static Bitboard lvl_g_targets = ~0ULL;
#define GEN_TARGETS (lvl_g_targets)
#define TARGETS_PARAM
#define TARGETS_FN(n) int n

static TARGETS_FN(lvl_generate_pawn_moves)(Position* pos, Move* moves, int move_count TARGETS_PARAM) {
    if (!pos || !moves || move_count < 0 || move_count >= MAX_MOVES - 1) return move_count;

    Color us = pos->to_move;
    Color them = 1 - us;
    int direction = (us == WHITE) ? 8 : -8;

    Bitboard pawns = pos->pieces[us][PAWN];
    Bitboard empty = ~pos->all_pieces;
    Bitboard enemies = pos->occupied[them];

    Bitboard single_pushes = (us == WHITE) ?
        (pawns << 8) & empty :
        (pawns >> 8) & empty;

    single_pushes &= GEN_TARGETS;

    while (single_pushes && move_count < MAX_MOVES - 1) {
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
    double_push_targets &= GEN_TARGETS;

    while (double_push_targets && move_count < MAX_MOVES - 1) {
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

    left_attacks &= GEN_TARGETS;

    while (left_attacks && move_count < MAX_MOVES - 1) {
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

    right_attacks &= GEN_TARGETS;

    while (right_attacks && move_count < MAX_MOVES - 1) {
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

static TARGETS_FN(lvl_generate_piece_moves)(Position* pos, Move* moves, int move_count TARGETS_PARAM) {
    if (!pos || !moves || move_count < 0 || move_count >= MAX_MOVES - 1) return move_count;

    Color us = pos->to_move;
    Color them = 1 - us;

    Bitboard knights = pos->pieces[us][KNIGHT];
    while (knights && move_count < MAX_MOVES - 1) {
        int from = get_lsb(knights);
        Bitboard attacks = knight_attacks[from] & ~pos->occupied[us] & GEN_TARGETS;

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
    while (bishops && move_count < MAX_MOVES - 1) {
        int from = get_lsb(bishops);
        Bitboard attacks = get_bishop_attacks_public(from, pos->all_pieces) & ~pos->occupied[us] & GEN_TARGETS;

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
    while (rooks && move_count < MAX_MOVES - 1) {
        int from = get_lsb(rooks);
        Bitboard attacks = get_rook_attacks_public(from, pos->all_pieces) & ~pos->occupied[us] & GEN_TARGETS;

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
    while (queens && move_count < MAX_MOVES - 1) {
        int from = get_lsb(queens);
        Bitboard attacks = get_queen_attacks_public(from, pos->all_pieces) & ~pos->occupied[us] & GEN_TARGETS;

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

static TARGETS_FN(lvl_generate_king_moves)(Position* pos, Move* moves, int move_count TARGETS_PARAM) {
    if (!pos || !moves || move_count < 0 || move_count >= MAX_MOVES - 1) return move_count;

    Color us = pos->to_move;
    Color them = 1 - us;

    Bitboard king = pos->pieces[us][KING];
    if (king == 0) return move_count;

    int from = get_lsb(king);
    Bitboard attacks = king_attacks[from] & ~pos->occupied[us] & GEN_TARGETS;

    while (attacks && move_count < MAX_MOVES - 1) {
        int to = get_lsb(attacks);

        if (!lvl_is_king_move_adjacent_safe(pos, to, us)) {
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

static inline Bitboard lvl_squares_span(int a, int b) {
    const int lo = (a < b) ? a : b, hi = (a < b) ? b : a;
    return ((hi == 63) ? ~0ULL : ((1ULL << (hi + 1)) - 1)) & ~((1ULL << lo) - 1);
}

static int lvl_generate_castling_moves_960(Position* pos, Move* moves, int move_count) {
    const Color us = pos->to_move, them = 1 - us;
    if (pos->pieces[us][KING] == 0 || is_in_check(pos, us)) return move_count;
    const int ksq = get_lsb(pos->pieces[us][KING]);
    const int idx0 = (us == WHITE) ? 0 : 2;
    for (int idx = idx0; idx < idx0 + 2; idx++) {
        if (!(pos->castling_rights & (1 << idx))) continue;
        const int rsq = castle_rook_sq[idx];
        if (!(pos->pieces[us][ROOK] & (1ULL << rsq)) || (rsq & 56) != (ksq & 56)) continue;
        int king_to, rook_to;
        castle_960_squares(ksq, rsq, &king_to, &rook_to);
        const Bitboard empty_squares = (lvl_squares_span(ksq, king_to) | lvl_squares_span(rsq, rook_to))
                             & ~((1ULL << ksq) | (1ULL << rsq));
        if (pos->all_pieces & empty_squares) continue;
        bool path_clear = true;
        const int stride = (king_to > ksq) ? 1 : -1;
        for (int s = ksq; s != king_to && path_clear; ) {
            s += stride;
            if (is_square_attacked(pos, s, them)) path_clear = false;
        }
        if (path_clear && move_count < MAX_MOVES - 1)
            moves[move_count++] = (Move){ksq, rsq, KING, EMPTY, EMPTY, true, false, 0};
    }
    return move_count;
}

static int lvl_generate_castling_moves(Position* pos, Move* moves, int move_count) {
    if (!pos || !moves || move_count < 0 || move_count >= MAX_MOVES - 1) return move_count;
    if (uci_chess960) return lvl_generate_castling_moves_960(pos, moves, move_count);

    Color us = pos->to_move;

    if (!(pos->castling_rights & (us == WHITE ? (WHITE_KINGSIDE | WHITE_QUEENSIDE) : (BLACK_KINGSIDE | BLACK_QUEENSIDE)))) return move_count;

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

static int lvl_generate_en_passant_moves(Position* pos, Move* moves, int move_count) {
    if (!pos || !moves || move_count < 0 || move_count >= MAX_MOVES - 1) return move_count;

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

    while (attacking_pawns && move_count < MAX_MOVES - 1) {
        int from = get_lsb(attacking_pawns);
        if (move_count < MAX_MOVES - 1) {
            moves[move_count++] = (Move){from, ep_square, PAWN, PAWN, EMPTY, false, true, 0};
        }
        pop_lsb(&attacking_pawns);
    }

    return move_count;
}

static bool lvl_is_legal_move(Position* pos, Move* move) {
    if (!pos || !move) return false;

    Color us = pos->to_move;
    UndoInfo undo;
    make_move_full(pos, move, &undo);

    bool legal = !is_in_check(pos, us);

    undo_move_full(pos, move, &undo);

    return legal;
}

static Bitboard lvl_checkers_of(Position* pos, int king_sq_idx, Color attacker_color);
static Bitboard lvl_squares_between(int a, int b);

typedef struct {
    bool     usable;
    Color    own_color, opp_color;
    int      king_sq_idx;
    int      num_checkers;
    Bitboard target_bb;
    Bitboard pinned_bb;
    int      num_pinned;
    int      pinned_sq[8];
    Bitboard pin_ray[8];
} LegalityContext;

static bool lvl_square_attacked_occ(Position* pos, int at_sq, Color attacker_color, Bitboard occ) {
    const Bitboard target_bb = 1ULL << at_sq;

    Bitboard pawns_bb = pos->pieces[attacker_color][PAWN];
    Bitboard pawn_attack_bb = (attacker_color == WHITE)
        ? (((pawns_bb & ~file_masks[0]) << 7) | ((pawns_bb & ~file_masks[7]) << 9))
        : (((pawns_bb & ~file_masks[0]) >> 9) | ((pawns_bb & ~file_masks[7]) >> 7));
    if (pawn_attack_bb & target_bb) return true;

    if (knight_attacks[at_sq] & pos->pieces[attacker_color][KNIGHT]) return true;
    if (king_attacks[at_sq]   & pos->pieces[attacker_color][KING])   return true;

    Bitboard rook_sliders = pos->pieces[attacker_color][ROOK] | pos->pieces[attacker_color][QUEEN];
    if (rook_sliders && (get_rook_attacks_public(at_sq, occ) & rook_sliders)) return true;

    Bitboard bishop_sliders = pos->pieces[attacker_color][BISHOP] | pos->pieces[attacker_color][QUEEN];
    if (bishop_sliders && (get_bishop_attacks_public(at_sq, occ) & bishop_sliders)) return true;

    return false;
}

static void lvl_legality_context_init(Position* pos, LegalityContext* k) {
    k->num_pinned = 0;
    k->pinned_bb = 0;
    k->num_checkers = 0;
    k->target_bb = ~0ULL;
    k->own_color = pos->to_move;
    k->opp_color  = 1 - k->own_color;

    Bitboard king_bb1 = pos->pieces[k->own_color][KING];
    if (king_bb1 == 0) { k->usable = false; k->king_sq_idx = -1; return; }
    k->usable = true;
    k->king_sq_idx = get_lsb(king_bb1);

    const Bitboard checkers_bb = lvl_checkers_of(pos, k->king_sq_idx, k->opp_color);
    k->num_checkers = __builtin_popcountll(checkers_bb);
    if (k->num_checkers == 1) {

        k->target_bb = checkers_bb | BETWEEN_SQUARES(k->king_sq_idx, get_lsb(checkers_bb));
    } else if (k->num_checkers > 1) {
        k->target_bb = 0;
    }

    const Bitboard rook_sliders = pos->pieces[k->opp_color][ROOK]   | pos->pieces[k->opp_color][QUEEN];
    const Bitboard bishop_sliders = pos->pieces[k->opp_color][BISHOP] | pos->pieces[k->opp_color][QUEEN];
    Bitboard snipers = (get_rook_attacks_public(k->king_sq_idx, 0ULL)   & rook_sliders)
                       | (get_bishop_attacks_public(k->king_sq_idx, 0ULL) & bishop_sliders);

    while (snipers) {
        const int s = get_lsb(snipers);
        snipers &= snipers - 1;
        const Bitboard segment  = BETWEEN_SQUARES(k->king_sq_idx, s);
        const Bitboard between_bb = segment & pos->all_pieces;
        if (between_bb == 0) continue;
        if (between_bb & (between_bb - 1)) continue;
        if (!(between_bb & pos->occupied[k->own_color])) continue;
        if (k->num_pinned >= 8) { k->usable = false; return; }
        k->pinned_bb |= between_bb;
        k->pinned_sq[k->num_pinned]    = get_lsb(between_bb);
        k->pin_ray[k->num_pinned] = segment | (1ULL << s);
        k->num_pinned++;
    }
}

static bool lvl_is_legal_fast(Position* pos, Move* this_move, const LegalityContext* k) {
    if (!k->usable)                        return lvl_is_legal_move(pos, this_move);
    if (this_move->is_en_passant || this_move->is_castling) return lvl_is_legal_move(pos, this_move);

    if (this_move->from == k->king_sq_idx) {
        const Bitboard occ_no_king = pos->all_pieces ^ (1ULL << k->king_sq_idx);
        return !lvl_square_attacked_occ(pos, this_move->to, k->opp_color, occ_no_king);
    }

    if (k->num_checkers > 1)                     return false;
    if (!((1ULL << this_move->to) & k->target_bb))            return false;

    if ((1ULL << this_move->from) & k->pinned_bb) {
        for (int i = 0; i < k->num_pinned; i++) {
            if (k->pinned_sq[i] == this_move->from)
                return (k->pin_ray[i] & (1ULL << this_move->to)) != 0;
        }
        return false;
    }
    return true;
}

#define LEGAL_CTX_CHECK(p, m, k) lvl_is_legal_fast((p), (m), (k))

#define LEGAL_CTX_DECL(pos)     LegalityContext legal_ctx; lvl_legality_context_init((pos), &legal_ctx)
#define LEGAL_CTX_OK(pos, m)   LEGAL_CTX_CHECK((pos), (m), &legal_ctx)

typedef char legality_buf_size_check[(sizeof(LegalityContext) <= LEGALITY_BUF_BYTES) ? 1 : -1];
static void lvl_legality_context_init_pub(Position* pos, LegalityBuffer* buf_area) {
    (void)sizeof(legality_buf_size_check);
    lvl_legality_context_init(pos, (LegalityContext*)buf_area);
}
static bool lvl_is_legal_fast_pub(Position* pos, Move* this_move, const LegalityBuffer* buf_area) {
    return LEGAL_CTX_CHECK(pos, this_move, (const LegalityContext*)buf_area);
}

static int lvl_generate_moves(Position* pos, Move* moves) {
    if (!pos || !moves) return 0;

    int move_count = 0;

    move_count = lvl_generate_pawn_moves(pos, moves, move_count);
    move_count = lvl_generate_piece_moves(pos, moves, move_count);
    move_count = lvl_generate_king_moves(pos, moves, move_count);
    move_count = lvl_generate_castling_moves(pos, moves, move_count);
    move_count = lvl_generate_en_passant_moves(pos, moves, move_count);

    return move_count;
}

static int lvl_generate_legal_moves(Position* pos, Move* moves) {
    if (!pos || !moves) return 0;

    Move pseudo_moves[MAX_MOVES];
    int pseudo_count = lvl_generate_moves(pos, pseudo_moves);
    LEGAL_CTX_DECL(pos);

    int legal_count = 0;
    for (int i = 0; i < pseudo_count && legal_count < MAX_MOVES - 1; i++) {
        if (LEGAL_CTX_OK(pos, &pseudo_moves[i])) {
            moves[legal_count++] = pseudo_moves[i];
        }
    }

    return legal_count;
}

static Bitboard lvl_checkers_of(Position* pos, int king_sq_idx, Color attacker_color) {
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

static Bitboard lvl_squares_between(int a, int b) {
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

static int lvl_generate_check_evasions(Position* pos, Move* moves) {
    if (!pos || !moves) return 0;

    const Color own_color = pos->to_move;
    const Color opp_color  = 1 - own_color;
    Bitboard king_bb1 = pos->pieces[own_color][KING];
    if (king_bb1 == 0) return 0;
    const int king_sq_idx = __builtin_ctzll(king_bb1);

    const Bitboard checkers_bb = lvl_checkers_of(pos, king_sq_idx, opp_color);
    if (checkers_bb == 0) return 0;
    const bool double_check = (checkers_bb & (checkers_bb - 1)) != 0;

    Bitboard targets = 0;
    int ep_capture_sq = -1;
    if (!double_check) {
        const int checker_sq = __builtin_ctzll(checkers_bb);
        targets |= checkers_bb;
        targets |= BETWEEN_SQUARES(king_sq_idx, checker_sq);

        if (pos->en_passant_square >= 0 &&
            (pos->pieces[opp_color][PAWN] & (1ULL << checker_sq))) {
            const int ep_pawn_sq = (own_color == WHITE) ? pos->en_passant_square - 8
                                                    : pos->en_passant_square + 8;
            if (ep_pawn_sq == checker_sq) ep_capture_sq = pos->en_passant_square;
        }
    }

    Move buf_area[MAX_MOVES];
    int total_moves = 0;

    if (!double_check) {
        lvl_g_targets = targets;
        total_moves = lvl_generate_pawn_moves(pos, buf_area, total_moves);
        total_moves = lvl_generate_piece_moves(pos, buf_area, total_moves);
        lvl_g_targets = ~0ULL;
    }
    total_moves = lvl_generate_king_moves(pos, buf_area, total_moves);
    total_moves = lvl_generate_en_passant_moves(pos, buf_area, total_moves);

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

static int lvl_generate_legal_evasions(Position* pos, Move* moves) {
    if (!pos || !moves) return 0;

    Move evasion_cands[MAX_MOVES];
    const int n = lvl_generate_check_evasions(pos, evasion_cands);
    LEGAL_CTX_DECL(pos);

    int legal_count = 0;
    for (int i = 0; i < n && legal_count < MAX_MOVES - 1; i++) {
        if (LEGAL_CTX_OK(pos, &evasion_cands[i])) {
            moves[legal_count++] = evasion_cands[i];
        }
    }
    return legal_count;
}

static int lvl_generate_noisy_internal(Position* pos, Move* moves, bool use_target_mask) {
    Move buf_area[MAX_MOVES];
    int total_moves = 0;
    const Color    us   = pos->to_move;
    const Bitboard opp_color  = pos->occupied[1 - us];

    const Bitboard prom = (us == WHITE) ? rank_masks[7] : rank_masks[0];
    if (use_target_mask) lvl_g_targets = opp_color | prom;
    total_moves = lvl_generate_pawn_moves(pos, buf_area, total_moves);
    if (use_target_mask) lvl_g_targets = opp_color;
    total_moves = lvl_generate_piece_moves(pos, buf_area, total_moves);
    total_moves = lvl_generate_king_moves(pos, buf_area, total_moves);
    lvl_g_targets = ~0ULL;

    total_moves = lvl_generate_en_passant_moves(pos, buf_area, total_moves);

    int n = 0;
    for (int i = 0; i < total_moves && n < MAX_MOVES - 1; i++) {
        if (buf_area[i].captured != EMPTY || buf_area[i].promotion != EMPTY) {
            moves[n++] = buf_area[i];
        }
    }
    return n;
}

static int lvl_generate_noisy_moves(Position* pos, Move* moves) {
    if (!pos || !moves) return 0;
    return lvl_generate_noisy_internal(pos, moves, 1 != 0);
}

static int lvl_generate_legal_noisy(Position* pos, Move* moves) {
    if (!pos || !moves) return 0;

    Move pseudo[MAX_MOVES];
    int pseudo_count = lvl_generate_noisy_moves(pos, pseudo);
    LEGAL_CTX_DECL(pos);

    int legal_count = 0;
    for (int i = 0; i < pseudo_count && legal_count < MAX_MOVES - 1; i++) {
        if (LEGAL_CTX_OK(pos, &pseudo[i])) {
            moves[legal_count++] = pseudo[i];
        }
    }
    return legal_count;
}

static void lvl_generator_init(void) {
    for (int a = 0; a < 64; a++)
        for (int b = 0; b < 64; b++) lvl_between_table[a][b] = lvl_squares_between(a, b);
}
