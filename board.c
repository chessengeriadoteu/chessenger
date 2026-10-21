/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#include "board.h"
#include "utils.h"
#include "tt.h"
#include "eval.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

bool uci_chess960 = false;
int castle_rook_sq[4] = { 7, 0, 63, 56 };

bool is_position_legal(const Position *pos) {
    if (!pos) return false;

    int white_kings = __builtin_popcountll(pos->pieces[WHITE][KING]);
    int black_kings = __builtin_popcountll(pos->pieces[BLACK][KING]);

    if (white_kings != 1 || black_kings != 1) {
        return false;
    }

    int white_king_square = __builtin_ctzll(pos->pieces[WHITE][KING]);
    int black_king_square = __builtin_ctzll(pos->pieces[BLACK][KING]);

    int white_king_file = white_king_square % 8;
    int white_king_rank = white_king_square / 8;
    int black_king_file = black_king_square % 8;
    int black_king_rank = black_king_square / 8;

    int file_diff = abs(white_king_file - black_king_file);
    int rank_diff = abs(white_king_rank - black_king_rank);

    if (file_diff <= 1 && rank_diff <= 1) {
        return false;
    }

    return true;
}

void recompute_material(Position* pos) {
    if (!pos) return;
    int balance = 0;
    int phase_sum   = 0;
    for (Color color = WHITE; color <= BLACK; color++) {
        const int side_sign = (color == WHITE) ? 1 : -1;
        balance += side_sign * __builtin_popcountll(pos->pieces[color][PAWN])   * MATERIAL_VALUE(PAWN);
        balance += side_sign * __builtin_popcountll(pos->pieces[color][ROOK])   * MATERIAL_VALUE(ROOK);
        balance += side_sign * __builtin_popcountll(pos->pieces[color][KNIGHT]) * MATERIAL_VALUE(KNIGHT);
        balance += side_sign * __builtin_popcountll(pos->pieces[color][BISHOP]) * MATERIAL_VALUE(BISHOP);
        balance += side_sign * __builtin_popcountll(pos->pieces[color][QUEEN])  * MATERIAL_VALUE(QUEEN);
        phase_sum   += __builtin_popcountll(pos->pieces[color][QUEEN])  * 9;
        phase_sum   += __builtin_popcountll(pos->pieces[color][ROOK])   * 5;
        phase_sum   += __builtin_popcountll(pos->pieces[color][BISHOP]) * 3;
        phase_sum   += __builtin_popcountll(pos->pieces[color][KNIGHT]) * 3;
    }
    pos->mat_balance = balance;
    pos->mat_phase   = phase_sum;
}

static int piece_material_value(PieceType t) {
    switch (t) {
        case PAWN:   return MATERIAL_VALUE(PAWN);
        case ROOK:   return MATERIAL_VALUE(ROOK);
        case KNIGHT: return MATERIAL_VALUE(KNIGHT);
        case BISHOP: return MATERIAL_VALUE(BISHOP);
        case QUEEN:  return MATERIAL_VALUE(QUEEN);
        default:     return 0;
    }
}

static int phase_weight(PieceType t) {
    switch (t) {
        case QUEEN:  return 9;
        case ROOK:   return 5;
        case BISHOP: return 3;
        case KNIGHT: return 3;
        default:     return 0;
    }
}

void setup_starting_position(Position* pos) {
    memset(pos, 0, sizeof(Position));

    pos->pieces[WHITE][PAWN] = 0x000000000000FF00ULL;
    pos->pieces[WHITE][ROOK] = 0x0000000000000081ULL;
    pos->pieces[WHITE][KNIGHT] = 0x0000000000000042ULL;
    pos->pieces[WHITE][BISHOP] = 0x0000000000000024ULL;
    pos->pieces[WHITE][QUEEN] = 0x0000000000000008ULL;
    pos->pieces[WHITE][KING] = 0x0000000000000010ULL;

    pos->pieces[BLACK][PAWN] = 0x00FF000000000000ULL;
    pos->pieces[BLACK][ROOK] = 0x8100000000000000ULL;
    pos->pieces[BLACK][KNIGHT] = 0x4200000000000000ULL;
    pos->pieces[BLACK][BISHOP] = 0x2400000000000000ULL;
    pos->pieces[BLACK][QUEEN] = 0x800000000000000ULL;
    pos->pieces[BLACK][KING] = 0x1000000000000000ULL;

    pos->occupied[WHITE] = 0;
    pos->occupied[BLACK] = 0;

    for (int piece = 0; piece < 7; piece++) {
        pos->occupied[WHITE] |= pos->pieces[WHITE][piece];
        pos->occupied[BLACK] |= pos->pieces[BLACK][piece];
    }

    pos->all_pieces = pos->occupied[WHITE] | pos->occupied[BLACK];

    pos->to_move = WHITE;
    pos->castling_rights = WHITE_KINGSIDE | WHITE_QUEENSIDE | BLACK_KINGSIDE | BLACK_QUEENSIDE;
    castle_rook_sq[0] = 7; castle_rook_sq[1] = 0; castle_rook_sq[2] = 63; castle_rook_sq[3] = 56;
    pos->en_passant_square = -1;
    pos->halfmove_clock = 0;
    pos->fullmove_number = 1;

    pos->history_count = 0;
    pos->history_base = 0;
    memset(pos->position_history, 0, sizeof(pos->position_history));

    pos->hash = compute_hash(pos);
    recompute_material(pos);
}

static char fen_error_msg[160] = "";

const char* fen_last_error(void) {
    return fen_error_msg;
}

static bool fen_fail(const char* reason) {
    snprintf(fen_error_msg, sizeof(fen_error_msg), "%s", reason);
    return false;
}

static bool parse_fen_uint(const char* s, long* out) {
    if (!s || !*s) return false;
    long value = 0;
    for (int i = 0; s[i]; i++) {
        if (s[i] < '0' || s[i] > '9') return false;
        if (value > 1000000) return false;
        value = value * 10 + (s[i] - '0');
    }
    *out = value;
    return true;
}

bool parse_fen(Position* pos, const char* fen) {
    if (!pos) return fen_fail("internal: null position");
    if (!fen) return fen_fail("empty FEN");

    char fen_copy[256];
    if (strlen(fen) >= sizeof(fen_copy)) {

        return fen_fail("FEN too long");
    }
    strcpy(fen_copy, fen);

    Position tmp;
    memset(&tmp, 0, sizeof(Position));

    char* tokens[6];
    int token_count = 0;

    char* token = strtok(fen_copy, " \t");
    while (token && token_count < 6) {
        tokens[token_count++] = token;
        token = strtok(NULL, " \t");
    }

    if (token_count < 4) {

        return fen_fail("FEN needs at least 4 fields (board, side, castling, en passant)");
    }

    int rank = 7;
    int file = 0;
    for (int i = 0; tokens[0][i]; i++) {
        char c = tokens[0][i];

        if (c == '/') {
            if (file != 8) return fen_fail("FEN board: rank does not add up to 8 squares");
            if (rank == 0) return fen_fail("FEN board: more than 8 ranks");
            rank--;
            file = 0;
        } else if (c >= '1' && c <= '8') {
            file += c - '0';
            if (file > 8) return fen_fail("FEN board: rank overflows 8 squares");
        } else {
            Color color = (c >= 'A' && c <= 'Z') ? WHITE : BLACK;

            PieceType piece_type;
            switch (tolower((unsigned char)c)) {
                case 'p': piece_type = PAWN; break;
                case 'r': piece_type = ROOK; break;
                case 'n': piece_type = KNIGHT; break;
                case 'b': piece_type = BISHOP; break;
                case 'q': piece_type = QUEEN; break;
                case 'k': piece_type = KING; break;
                default:

                    return fen_fail("FEN board: unknown piece letter");
            }

            if (file > 7) return fen_fail("FEN board: rank overflows 8 squares");

            int square = rank * 8 + file;
            tmp.pieces[color][piece_type] |= 1ULL << square;
            tmp.occupied[color] |= 1ULL << square;
            file++;
        }
    }
    if (rank != 0 || file != 8) {
        return fen_fail("FEN board: must be exactly 8 ranks of 8 squares");
    }

    tmp.all_pieces = tmp.occupied[WHITE] | tmp.occupied[BLACK];

    if (tokens[1][1] != '\0' || (tokens[1][0] != 'w' && tokens[1][0] != 'b')) {
        return fen_fail("FEN side to move must be 'w' or 'b'");
    }
    tmp.to_move = (tokens[1][0] == 'w') ? WHITE : BLACK;

    tmp.castling_rights = 0;
    int rook_sq[4] = { 7, 0, 63, 56 };
    if (!(tokens[2][0] == '-' && tokens[2][1] == '\0')) {
        for (int i = 0; tokens[2][i]; i++) {
            int bit;
            const char c = tokens[2][i];
            switch (c) {
                case 'K': bit = WHITE_KINGSIDE; break;
                case 'Q': bit = WHITE_QUEENSIDE; break;
                case 'k': bit = BLACK_KINGSIDE; break;
                case 'q': bit = BLACK_QUEENSIDE; break;
                default:
                    if (!uci_chess960 || !((c >= 'A' && c <= 'H') || (c >= 'a' && c <= 'h')))
                        return fen_fail("FEN castling field must be '-' or a subset of KQkq");
                    bit = 0;
                    break;
            }
            if (uci_chess960) {

                const Color rights_color = (c == 'K' || c == 'Q' || (c >= 'A' && c <= 'H')) ? WHITE : BLACK;
                const int back_rank_base = (rights_color == WHITE) ? 0 : 56;
                const Bitboard kr = tmp.pieces[rights_color][KING] & (0xFFULL << back_rank_base);
                const Bitboard rook_sliders = tmp.pieces[rights_color][ROOK] & (0xFFULL << back_rank_base);
                if (!kr) continue;
                const int ksq = get_lsb(kr);
                int at_sq = -1;
                if (c == 'K' || c == 'k') {
                    for (int s = back_rank_base + 7; s > ksq && at_sq < 0; s--) if (rook_sliders & (1ULL << s)) at_sq = s;
                } else if (c == 'Q' || c == 'q') {
                    for (int s = back_rank_base; s < ksq && at_sq < 0; s++) if (rook_sliders & (1ULL << s)) at_sq = s;
                } else {
                    const int s = back_rank_base + ((c | 0x20) - 'a');
                    if (s != ksq && (rook_sliders & (1ULL << s))) at_sq = s;
                }
                if (at_sq < 0) continue;
                bit = (rights_color == WHITE) ? ((at_sq > ksq) ? WHITE_KINGSIDE : WHITE_QUEENSIDE)
                                     : ((at_sq > ksq) ? BLACK_KINGSIDE : BLACK_QUEENSIDE);
                rook_sq[__builtin_ctz(bit)] = at_sq;
            } else {

                const Color rights_color = (bit & (WHITE_KINGSIDE | WHITE_QUEENSIDE)) ? WHITE : BLACK;
                const int king_home_sq = (rights_color == WHITE) ? 4 : 60;
                if (!(tmp.pieces[rights_color][KING] & (1ULL << king_home_sq)) ||
                    !(tmp.pieces[rights_color][ROOK] & (1ULL << rook_sq[__builtin_ctz(bit)]))) continue;
            }
            if (tmp.castling_rights & bit) {
                return fen_fail("FEN castling field repeats a right");
            }
            tmp.castling_rights |= bit;
        }
    }

    if (tokens[3][0] == '-' && tokens[3][1] == '\0') {
        tmp.en_passant_square = -1;
    } else {
        char ep_file = tokens[3][0];
        char ep_rank = tokens[3][1];
        if (ep_file < 'a' || ep_file > 'h' || ep_rank < '1' || ep_rank > '8' ||
            tokens[3][2] != '\0') {
            return fen_fail("FEN en passant field must be '-' or a square like 'd6'");
        }

        if (ep_rank != '3' && ep_rank != '6') {
            return fen_fail("FEN en passant square must be on rank 3 or 6");
        }
        tmp.en_passant_square = (ep_rank - '1') * 8 + (ep_file - 'a');
        if (!uci_chess960) {

            const int ep = tmp.en_passant_square;
            const bool rank_ok = (tmp.to_move == WHITE) ? (ep_rank == '6') : (ep_rank == '3');
            const int pawn_bit = (tmp.to_move == WHITE) ? ep - 8 : ep + 8;
            const int pawn_from_sq = (tmp.to_move == WHITE) ? ep + 8 : ep - 8;
            const Color opponent = (tmp.to_move == WHITE) ? BLACK : WHITE;
            if (!rank_ok || !(tmp.pieces[opponent][PAWN] & (1ULL << pawn_bit)) ||
                (tmp.all_pieces & ((1ULL << ep) | (1ULL << pawn_from_sq)))) {
                tmp.en_passant_square = -1;
            }
        }
    }

    tmp.halfmove_clock = 0;
    tmp.fullmove_number = 1;
    if (token_count >= 5) {
        long value;
        if (!parse_fen_uint(tokens[4], &value)) {
            return fen_fail("FEN halfmove clock must be a non-negative integer");
        }
        tmp.halfmove_clock = (value > 100) ? 100 : (int)value;
    }
    if (token_count >= 6) {
        long value;
        if (!parse_fen_uint(tokens[5], &value)) {
            return fen_fail("FEN fullmove number must be a non-negative integer");
        }
        tmp.fullmove_number = (value < 1) ? 1 : (int)value;
    }

    tmp.history_count = 0;
    tmp.history_base = 0;
    memset(tmp.position_history, 0, sizeof(tmp.position_history));

    if (!is_position_legal(&tmp)) {
        return fen_fail("illegal position: needs exactly one king per side, kings not adjacent");
    }

    tmp.hash = compute_hash(&tmp);
    recompute_material(&tmp);

    *pos = tmp;
    for (int i = 0; i < 4; i++) castle_rook_sq[i] = rook_sq[i];
    fen_error_msg[0] = '\0';
    return true;
}

void make_move_full(Position* pos, Move* move, UndoInfo* undo) {

    undo->captured = move->captured;
    undo->castling_rights = pos->castling_rights;
    undo->en_passant_square = pos->en_passant_square;
    undo->halfmove_clock = pos->halfmove_clock;
    undo->was_en_passant = move->is_en_passant;
    undo->was_castling = move->is_castling;
    undo->captured_square = move->to;

    uint64_t current_hash = pos->hash;
    undo->old_hash = current_hash;
    undo->old_mat_balance = pos->mat_balance;
    undo->old_mat_phase   = pos->mat_phase;
    undo->old_history_count = pos->history_count;
    undo->old_history_base = pos->history_base;
    add_position_to_history(pos, current_hash);

    Color us = pos->to_move;
    Color them = 1 - us;

    pos->pieces[us][move->piece] &= ~(1ULL << move->from);
    pos->occupied[us] &= ~(1ULL << move->from);

    if (move->is_castling && uci_chess960) {

        int king_to, rook_to;
        castle_960_squares(move->from, move->to, &king_to, &rook_to);
        pos->pieces[us][ROOK] &= ~(1ULL << move->to);
        pos->occupied[us] &= ~(1ULL << move->to);
        pos->pieces[us][KING] |= (1ULL << king_to);
        pos->pieces[us][ROOK] |= (1ULL << rook_to);
        pos->occupied[us] |= (1ULL << king_to) | (1ULL << rook_to);

    } else if (move->is_castling) {

        pos->pieces[us][KING] |= (1ULL << move->to);
        pos->occupied[us] |= (1ULL << move->to);

        if (us == WHITE) {
            if (move->to == 6) {
                pos->pieces[WHITE][ROOK] &= ~(1ULL << 7);
                pos->pieces[WHITE][ROOK] |= (1ULL << 5);
                pos->occupied[WHITE] &= ~(1ULL << 7);
                pos->occupied[WHITE] |= (1ULL << 5);
            } else if (move->to == 2) {
                pos->pieces[WHITE][ROOK] &= ~(1ULL << 0);
                pos->pieces[WHITE][ROOK] |= (1ULL << 3);
                pos->occupied[WHITE] &= ~(1ULL << 0);
                pos->occupied[WHITE] |= (1ULL << 3);
            }
        } else {
            if (move->to == 62) {
                pos->pieces[BLACK][ROOK] &= ~(1ULL << 63);
                pos->pieces[BLACK][ROOK] |= (1ULL << 61);
                pos->occupied[BLACK] &= ~(1ULL << 63);
                pos->occupied[BLACK] |= (1ULL << 61);
            } else if (move->to == 58) {
                pos->pieces[BLACK][ROOK] &= ~(1ULL << 56);
                pos->pieces[BLACK][ROOK] |= (1ULL << 59);
                pos->occupied[BLACK] &= ~(1ULL << 56);
                pos->occupied[BLACK] |= (1ULL << 59);
            }
        }

    } else if (move->is_en_passant) {

        int captured_square = (us == WHITE) ? move->to - 8 : move->to + 8;
        undo->captured_square = captured_square;

        pos->pieces[them][PAWN] &= ~(1ULL << captured_square);
        pos->occupied[them] &= ~(1ULL << captured_square);

        pos->pieces[us][PAWN] |= (1ULL << move->to);
        pos->occupied[us] |= (1ULL << move->to);

    } else {

        if (move->captured != EMPTY) {
            pos->pieces[them][move->captured] &= ~(1ULL << move->to);
            pos->occupied[them] &= ~(1ULL << move->to);
        }

        PieceType piece_to_place = (move->promotion != EMPTY) ? move->promotion : move->piece;
        pos->pieces[us][piece_to_place] |= (1ULL << move->to);
        pos->occupied[us] |= (1ULL << move->to);
    }

    {
        const int own_sign = (us == WHITE) ? 1 : -1;
        if (move->is_en_passant) {

            pos->mat_balance += own_sign * MATERIAL_VALUE(PAWN);
        } else {
            if (move->captured != EMPTY) {

                pos->mat_balance += own_sign * piece_material_value(move->captured);
                pos->mat_phase   -= phase_weight(move->captured);
            }
            if (move->promotion != EMPTY) {

                pos->mat_balance += own_sign * (piece_material_value(move->promotion) - MATERIAL_VALUE(PAWN));
                pos->mat_phase   += phase_weight(move->promotion);
            }
        }
    }

    if (move->piece == KING) {
        if (us == WHITE) {
            pos->castling_rights &= ~(WHITE_KINGSIDE | WHITE_QUEENSIDE);
        } else {
            pos->castling_rights &= ~(BLACK_KINGSIDE | BLACK_QUEENSIDE);
        }
    } else if (move->piece == ROOK) {

        if (us == WHITE) {
            if (move->from == castle_rook_sq[1]) pos->castling_rights &= ~WHITE_QUEENSIDE;
            if (move->from == castle_rook_sq[0]) pos->castling_rights &= ~WHITE_KINGSIDE;
        } else {
            if (move->from == castle_rook_sq[3]) pos->castling_rights &= ~BLACK_QUEENSIDE;
            if (move->from == castle_rook_sq[2]) pos->castling_rights &= ~BLACK_KINGSIDE;
        }
    }

    if (move->captured == ROOK) {
        if (move->to == castle_rook_sq[1]) pos->castling_rights &= ~WHITE_QUEENSIDE;
        if (move->to == castle_rook_sq[0]) pos->castling_rights &= ~WHITE_KINGSIDE;
        if (move->to == castle_rook_sq[3]) pos->castling_rights &= ~BLACK_QUEENSIDE;
        if (move->to == castle_rook_sq[2]) pos->castling_rights &= ~BLACK_KINGSIDE;
    }

    pos->en_passant_square = -1;
    if (move->piece == PAWN && abs(move->to - move->from) == 16) {
        pos->en_passant_square = (move->from + move->to) / 2;
    }

    pos->all_pieces = pos->occupied[WHITE] | pos->occupied[BLACK];

    if (move->captured != EMPTY || move->piece == PAWN) {

        pos->halfmove_clock = 0;

        pos->history_base = pos->history_count;
    } else {
        pos->halfmove_clock++;
    }

    if (us == BLACK) {
        pos->fullmove_number++;
    }

    pos->to_move = them;

    {
        uint64_t h = current_hash;

        h ^= zobrist_pieces[us][move->piece][move->from];

        if (move->is_castling && uci_chess960) {

            int king_to, rook_to;
            castle_960_squares(move->from, move->to, &king_to, &rook_to);
            h ^= zobrist_pieces[us][KING][king_to];
            h ^= zobrist_pieces[us][ROOK][move->to];
            h ^= zobrist_pieces[us][ROOK][rook_to];
        } else if (move->is_castling) {

            h ^= zobrist_pieces[us][KING][move->to];
            int rook_from = -1, rook_to = -1;
            if (us == WHITE) {
                if (move->to == 6)       { rook_from = 7;  rook_to = 5;  }
                else if (move->to == 2)  { rook_from = 0;  rook_to = 3;  }
            } else {
                if (move->to == 62)      { rook_from = 63; rook_to = 61; }
                else if (move->to == 58) { rook_from = 56; rook_to = 59; }
            }
            if (rook_from >= 0) {
                h ^= zobrist_pieces[us][ROOK][rook_from];
                h ^= zobrist_pieces[us][ROOK][rook_to];
            }
        } else if (move->is_en_passant) {

            h ^= zobrist_pieces[them][PAWN][undo->captured_square];
            h ^= zobrist_pieces[us][PAWN][move->to];
        } else {
            if (move->captured != EMPTY) {
                h ^= zobrist_pieces[them][move->captured][move->to];
            }
            PieceType placed = (move->promotion != EMPTY) ? move->promotion : move->piece;
            h ^= zobrist_pieces[us][placed][move->to];
        }

        h ^= zobrist_castling[undo->castling_rights];
        h ^= zobrist_castling[pos->castling_rights];

        if (undo->en_passant_square != -1) h ^= zobrist_en_passant[undo->en_passant_square];
        if (pos->en_passant_square != -1)  h ^= zobrist_en_passant[pos->en_passant_square];

        h ^= zobrist_side_to_move;

        pos->hash = h;
    }

}

void undo_move_full(Position* pos, Move* move, UndoInfo* undo) {

    pos->to_move = 1 - pos->to_move;
    Color us = pos->to_move;
    Color them = 1 - us;

    pos->castling_rights = undo->castling_rights;
    pos->en_passant_square = undo->en_passant_square;
    pos->halfmove_clock = undo->halfmove_clock;

    if (us == BLACK) {
        pos->fullmove_number--;
    }

    pos->history_count = undo->old_history_count;
    pos->history_base = undo->old_history_base;

    if (undo->was_castling && uci_chess960) {

        int king_to, rook_to;
        castle_960_squares(move->from, move->to, &king_to, &rook_to);
        pos->pieces[us][KING] &= ~(1ULL << king_to);
        pos->pieces[us][ROOK] &= ~(1ULL << rook_to);
        pos->occupied[us] &= ~((1ULL << king_to) | (1ULL << rook_to));
        pos->pieces[us][KING] |= (1ULL << move->from);
        pos->pieces[us][ROOK] |= (1ULL << move->to);
        pos->occupied[us] |= (1ULL << move->from) | (1ULL << move->to);

    } else if (undo->was_castling) {
        pos->pieces[us][KING] &= ~(1ULL << move->to);
        pos->occupied[us] &= ~(1ULL << move->to);
        pos->pieces[us][KING] |= (1ULL << move->from);
        pos->occupied[us] |= (1ULL << move->from);

        if (us == WHITE) {
            if (move->to == 6) {
                pos->pieces[WHITE][ROOK] &= ~(1ULL << 5);
                pos->pieces[WHITE][ROOK] |= (1ULL << 7);
                pos->occupied[WHITE] &= ~(1ULL << 5);
                pos->occupied[WHITE] |= (1ULL << 7);
            } else if (move->to == 2) {
                pos->pieces[WHITE][ROOK] &= ~(1ULL << 3);
                pos->pieces[WHITE][ROOK] |= (1ULL << 0);
                pos->occupied[WHITE] &= ~(1ULL << 3);
                pos->occupied[WHITE] |= (1ULL << 0);
            }
        } else {
            if (move->to == 62) {
                pos->pieces[BLACK][ROOK] &= ~(1ULL << 61);
                pos->pieces[BLACK][ROOK] |= (1ULL << 63);
                pos->occupied[BLACK] &= ~(1ULL << 61);
                pos->occupied[BLACK] |= (1ULL << 63);
            } else if (move->to == 58) {
                pos->pieces[BLACK][ROOK] &= ~(1ULL << 59);
                pos->pieces[BLACK][ROOK] |= (1ULL << 56);
                pos->occupied[BLACK] &= ~(1ULL << 59);
                pos->occupied[BLACK] |= (1ULL << 56);
            }
        }

    } else if (undo->was_en_passant) {
        pos->pieces[us][PAWN] &= ~(1ULL << move->to);
        pos->occupied[us] &= ~(1ULL << move->to);

        pos->pieces[us][PAWN] |= (1ULL << move->from);
        pos->occupied[us] |= (1ULL << move->from);

        if (undo->captured == PAWN && undo->captured_square >= 0 && undo->captured_square < 64) {
            pos->pieces[them][PAWN] |= (1ULL << undo->captured_square);
            pos->occupied[them] |= (1ULL << undo->captured_square);
        }

    } else {
        PieceType piece_to_remove = (move->promotion != EMPTY) ? move->promotion : move->piece;

        pos->pieces[us][piece_to_remove] &= ~(1ULL << move->to);
        pos->occupied[us] &= ~(1ULL << move->to);

        pos->pieces[us][move->piece] |= (1ULL << move->from);
        pos->occupied[us] |= (1ULL << move->from);

        if (undo->captured != EMPTY) {
            pos->pieces[them][undo->captured] |= (1ULL << move->to);
            pos->occupied[them] |= (1ULL << move->to);
        }
    }

    pos->all_pieces = pos->occupied[WHITE] | pos->occupied[BLACK];

    pos->hash = undo->old_hash;
    pos->mat_balance = undo->old_mat_balance;
    pos->mat_phase   = undo->old_mat_phase;

}

bool has_insufficient_material(Position* pos) {
    if (!pos) return false;

    {
        const Bitboard pawns_rooks_queens_bb = pos->pieces[WHITE][PAWN]  | pos->pieces[BLACK][PAWN]
                                  | pos->pieces[WHITE][ROOK]  | pos->pieces[BLACK][ROOK]
                                  | pos->pieces[WHITE][QUEEN] | pos->pieces[BLACK][QUEEN];
        const Bitboard minor_pieces_bb = pos->pieces[WHITE][KNIGHT] | pos->pieces[BLACK][KNIGHT]
                                  | pos->pieces[WHITE][BISHOP] | pos->pieces[BLACK][BISHOP];
        return !pawns_rooks_queens_bb && !(minor_pieces_bb & (minor_pieces_bb - 1));
    }
}

GameResult check_game_result(Position* pos) {
    if (!pos) return GAME_ONGOING;

    if (has_insufficient_material(pos)) {
        return DRAW_INSUFFICIENT_MATERIAL;
    }

    if (pos->halfmove_clock >= 100) {
        return DRAW_50_MOVES;
    }

    if (is_threefold_repetition(pos)) {
        return DRAW_REPETITION;
    }

    return GAME_ONGOING;
}

bool is_threefold_repetition(Position* pos) {
    if (!pos) return false;

    int occurrences = count_position_occurrences(pos, pos->hash);

    return occurrences >= 3;
}

bool is_approaching_repetition(Position* pos) {
    if (!pos) return false;

    int occurrences = count_position_occurrences(pos, pos->hash);

    return occurrences >= 2;
}

int count_position_occurrences(Position* pos, uint64_t hash) {
    if (!pos) return 0;

    int count = 1;

    for (int i = pos->history_base; i < pos->history_count; i++) {
        if (pos->position_history[i] == hash) {
            count++;
        }
    }

    return count;
}

int count_position_occurrences_max2(Position* pos, uint64_t hash) {
    if (!pos) return 0;
    for (int i = pos->history_base; i < pos->history_count; i++)
        if (pos->position_history[i] == hash) return 2;
    return 1;
}

void add_position_to_history(Position* pos, uint64_t hash) {
    if (!pos) return;

    if (pos->history_count >= MAX_POSITION_HISTORY) {

        return;
    }

    pos->position_history[pos->history_count] = hash;
    pos->history_count++;
}

void compact_position_history(Position* pos) {
    if (!pos) return;

    int live = pos->history_count - pos->history_base;
    if (live < 0) live = 0;

    if (live > HISTORY_GAME_LIMIT) {

        pos->history_base += live - HISTORY_GAME_LIMIT;
        live = HISTORY_GAME_LIMIT;
    }

    if (live > 0 && pos->history_base > 0) {
        memmove(pos->position_history,
                pos->position_history + pos->history_base,
                (size_t)live * sizeof(uint64_t));
    }

    pos->history_base = 0;
    pos->history_count = live;
}

void remove_last_position_from_history(Position* pos, UndoInfo* undo) {
    if (!pos || !undo) return;

    if (pos->history_count > 0) {
        pos->history_count--;
        undo->removed_hash = pos->position_history[pos->history_count];
    }
}

Move parse_move(Position* pos, const char* move_str) {
    Move move = {-1, -1, EMPTY, EMPTY, EMPTY, false, false, 0};

    if (!pos || !move_str || strlen(move_str) < 4) {
        return move;
    }

    int from_file = move_str[0] - 'a';
    int from_rank = move_str[1] - '1';
    int to_file = move_str[2] - 'a';
    int to_rank = move_str[3] - '1';

    if (from_file < 0 || from_file > 7 || from_rank < 0 || from_rank > 7 ||
        to_file < 0 || to_file > 7 || to_rank < 0 || to_rank > 7) {
        return move;
    }

    move.from = from_rank * 8 + from_file;
    move.to = to_rank * 8 + to_file;

    for (PieceType piece = PAWN; piece <= KING; piece++) {
        if (pos->pieces[pos->to_move][piece] & (1ULL << move.from)) {
            move.piece = piece;
            break;
        }
    }

    for (PieceType piece = PAWN; piece <= KING; piece++) {
        if (pos->pieces[1 - pos->to_move][piece] & (1ULL << move.to)) {
            move.captured = piece;
            break;
        }
    }

    if (strlen(move_str) >= 5) {
        char promo = tolower(move_str[4]);
        switch (promo) {
            case 'q': move.promotion = QUEEN; break;
            case 'r': move.promotion = ROOK; break;
            case 'b': move.promotion = BISHOP; break;
            case 'n': move.promotion = KNIGHT; break;
        }
    }

    if (move.piece == KING && abs(move.to - move.from) == 2) {
        move.is_castling = true;
    }

    if (move.piece == PAWN && move.to == pos->en_passant_square && move.captured == EMPTY) {
        move.is_en_passant = true;
        move.captured = PAWN;
    }

    return move;
}