/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#include "utils.h"
#include "board.h"
#include "movegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <sys/time.h>

Bitboard rank_masks[8];
Bitboard file_masks[8];

struct timeval get_wall_time(void) {
    struct timeval t;
    gettimeofday(&t, NULL);
    return t;
}

long long time_diff_ms(struct timeval start, struct timeval end) {
    return (end.tv_sec - start.tv_sec) * 1000LL +
           (end.tv_usec - start.tv_usec) / 1000LL;
}

int get_lsb(Bitboard bb) {
    if (bb == 0) return -1;

    return __builtin_ctzll(bb);
}

void pop_lsb(Bitboard* bb) {
    if (*bb != 0) {
        *bb &= (*bb - 1);
    }
}

int pop_count(Bitboard bb) {
    return __builtin_popcountll(bb);
}

void init_masks(void) {

    for (int rank = 0; rank < 8; rank++) {
        rank_masks[rank] = 0x00000000000000FFULL << (rank * 8);
    }

    for (int file = 0; file < 8; file++) {
        file_masks[file] = 0x0101010101010101ULL << file;
    }
}

void print_bitboard(Bitboard bb) {
    printf("\n");
    for (int rank = 7; rank >= 0; rank--) {
        printf("%d ", rank + 1);
        for (int file = 0; file < 8; file++) {
            int square = rank * 8 + file;
            if (bb & (1ULL << square)) {
                printf("1 ");
            } else {
                printf(". ");
            }
        }
        printf("\n");
    }
    printf("  a b c d e f g h\n\n");
}

void print_position(Position* pos) {
    if (!pos) {
        printf("Position is NULL\n");
        return;
    }

    char pieces[] = ".PRNBQK.prnbqk.";

    printf("\n");
    for (int rank = 7; rank >= 0; rank--) {
        printf("%d ", rank + 1);
        for (int file = 0; file < 8; file++) {
            int square = rank * 8 + file;
            int piece_type = EMPTY;

            for (Color color = WHITE; color <= BLACK; color++) {
                for (PieceType piece = PAWN; piece <= KING; piece++) {
                    if (pos->pieces[color][piece] & (1ULL << square)) {
                        piece_type = piece + (color == BLACK ? 8 : 0);
                        break;
                    }
                }
                if (piece_type != EMPTY) break;
            }

            printf("%c ", pieces[piece_type]);
        }
        printf("\n");
    }
    printf("  a b c d e f g h\n");
    printf("To move: %s\n", pos->to_move == WHITE ? "White" : "Black");
    printf("En passant: %d\n", pos->en_passant_square);
    printf("Castling: %d\n", pos->castling_rights);
    printf("Halfmove: %d\n", pos->halfmove_clock);
    printf("Fullmove: %d\n", pos->fullmove_number);
    printf("\n");
}

char* move_to_string(Move* move) {
    static char move_str[6];
    move_to_string_buf(move, move_str);
    return move_str;
}

void move_to_string_buf(Move* move, char* buffer) {
    if (!move || !buffer) {
        if (buffer) strcpy(buffer, "0000");
        return;
    }

    char from_file = 'a' + (move->from % 8);
    char from_rank = '1' + (move->from / 8);
    char to_file = 'a' + (move->to % 8);
    char to_rank = '1' + (move->to / 8);

    buffer[0] = from_file;
    buffer[1] = from_rank;
    buffer[2] = to_file;
    buffer[3] = to_rank;

    if (move->promotion != EMPTY) {
        char promo_char = 'q';
        switch (move->promotion) {
            case QUEEN: promo_char = 'q'; break;
            case ROOK: promo_char = 'r'; break;
            case BISHOP: promo_char = 'b'; break;
            case KNIGHT: promo_char = 'n'; break;
            default: promo_char = 'q'; break;
        }
        buffer[4] = promo_char;
        buffer[5] = '\0';
    } else {
        buffer[4] = '\0';
    }
}

char* position_to_fen(Position* pos) {
    static char fen[128];
    char* p = fen;

    if (!pos) {
        strcpy(fen, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        return fen;
    }

    for (int rank = 7; rank >= 0; rank--) {
        int empty_count = 0;

        for (int file = 0; file < 8; file++) {
            int square = rank * 8 + file;
            char piece_char = '\0';

            for (Color color = WHITE; color <= BLACK; color++) {
                for (PieceType piece = PAWN; piece <= KING; piece++) {
                    if (pos->pieces[color][piece] & (1ULL << square)) {
                        char pieces[] = "PRNBQK";
                        piece_char = pieces[piece - 1];
                        if (color == BLACK) piece_char += 32;
                        break;
                    }
                }
                if (piece_char) break;
            }

            if (piece_char) {
                if (empty_count > 0) {
                    *p++ = '0' + empty_count;
                    empty_count = 0;
                }
                *p++ = piece_char;
            } else {
                empty_count++;
            }
        }

        if (empty_count > 0) {
            *p++ = '0' + empty_count;
        }

        if (rank > 0) *p++ = '/';
    }

    *p++ = ' ';
    *p++ = pos->to_move == WHITE ? 'w' : 'b';

    *p++ = ' ';
    bool has_castling = false;
    if (pos->castling_rights & WHITE_KINGSIDE) { *p++ = 'K'; has_castling = true; }
    if (pos->castling_rights & WHITE_QUEENSIDE) { *p++ = 'Q'; has_castling = true; }
    if (pos->castling_rights & BLACK_KINGSIDE) { *p++ = 'k'; has_castling = true; }
    if (pos->castling_rights & BLACK_QUEENSIDE) { *p++ = 'q'; has_castling = true; }
    if (!has_castling) *p++ = '-';

    *p++ = ' ';
    if (pos->en_passant_square == -1) {
        *p++ = '-';
    } else {
        *p++ = 'a' + (pos->en_passant_square % 8);
        *p++ = '1' + (pos->en_passant_square / 8);
    }

    sprintf(p, " %d %d", pos->halfmove_clock, pos->fullmove_number);

    return fen;
}

uint64_t perft(Position* pos, int depth) {
    if (depth == 0) return 1;

    Move moves[256];
    int move_count = generate_legal_moves(pos, moves);

    if (depth == 1) return move_count;

    uint64_t nodes = 0;
    for (int i = 0; i < move_count; i++) {
        UndoInfo undo;
        make_move_full(pos, &moves[i], &undo);
        nodes += perft(pos, depth - 1);
        undo_move_full(pos, &moves[i], &undo);
    }

    return nodes;
}

uint64_t perft_debug(Position* pos, int depth, int max_depth) {
    if (depth == 0) return 1;

    Move moves[256];
    int move_count = generate_legal_moves(pos, moves);

    if (depth == 1) return move_count;

    uint64_t nodes = 0;
    for (int i = 0; i < move_count; i++) {
        UndoInfo undo;
        make_move_full(pos, &moves[i], &undo);
        uint64_t child_nodes = perft(pos, depth - 1);
        undo_move_full(pos, &moves[i], &undo);

        nodes += child_nodes;

        if (depth == max_depth) {
            char move_str[8];
            move_to_string_buf(&moves[i], move_str);
            printf("%s: %llu\n", move_str, (unsigned long long)child_nodes);
        }
    }

    return nodes;
}

uint64_t perft_verbose(Position* pos, int depth) {
    printf("Perft %d from position:\n", depth);
    print_position(pos);

    uint64_t nodes = perft_debug(pos, depth, depth);
    printf("\nTotal nodes: %llu\n", (unsigned long long)nodes);
    return nodes;
}

bool validate_move_before_send(Position* pos, Move* move) {
    if (!pos || !move) return false;

    Move legal_moves[MAX_MOVES];
    int count = generate_legal_moves(pos, legal_moves);

    for (int i = 0; i < count; i++) {
        if (legal_moves[i].from == move->from &&
            legal_moves[i].to == move->to &&
            legal_moves[i].promotion == move->promotion) {
            return true;
        }
    }

    return false;
}

bool is_material_losing_move(Position* pos, Move* move) {
    if (!pos || !move) return false;

    int simple_exchange = calculate_material_exchange(pos, move);
    if (simple_exchange < -100) {
        return true;
    }

    Position temp_pos = *pos;
    UndoInfo undo;

    make_move_full(&temp_pos, move, &undo);

    Move opponent_moves[MAX_MOVES];
    int move_count = generate_legal_moves(&temp_pos, opponent_moves);

    bool threat_detected = false;
    for (int i = 0; i < move_count; i++) {
        int opponent_gain = calculate_material_exchange(&temp_pos, &opponent_moves[i]);
        if (opponent_gain >= 100) {

            threat_detected = true;
            break;
        }
    }

    undo_move_full(&temp_pos, move, &undo);

    return threat_detected;
}

int calculate_material_exchange(Position* pos, Move* move) {
    if (!pos || !move) return 0;

    static const int piece_values[] = {0, 100, 500, 320, 330, 900, 10000, 0};

    int gain = 0;
    int loss = 0;

    if (move->captured != EMPTY && move->captured <= KING) {
        gain = piece_values[move->captured];
    }

    if (move->piece <= KING) {

        Color enemy = 1 - pos->to_move;
        if (is_square_attacked(pos, move->to, enemy)) {
            loss = piece_values[move->piece];
        }
    }

    return gain - loss;
}

static Bitboard get_attackers_of_square(Position* pos, int square, Color color, Bitboard occupied) {
    if (!pos || square < 0 || square > 63) return 0;

    Bitboard attackers = 0;

    if (color == WHITE) {
        if (square >= 16) {
            int file = square % 8;
            if (file > 0 && (pos->pieces[WHITE][PAWN] & (1ULL << (square - 9)))) {
                attackers |= (1ULL << (square - 9));
            }
            if (file < 7 && (pos->pieces[WHITE][PAWN] & (1ULL << (square - 7)))) {
                attackers |= (1ULL << (square - 7));
            }
        }
    } else {
        if (square < 48) {
            int file = square % 8;
            if (file > 0 && (pos->pieces[BLACK][PAWN] & (1ULL << (square + 7)))) {
                attackers |= (1ULL << (square + 7));
            }
            if (file < 7 && (pos->pieces[BLACK][PAWN] & (1ULL << (square + 9)))) {
                attackers |= (1ULL << (square + 9));
            }
        }
    }

    extern Bitboard knight_attacks[64];
    attackers |= (knight_attacks[square] & pos->pieces[color][KNIGHT]);

    extern Bitboard king_attacks[64];
    attackers |= (king_attacks[square] & pos->pieces[color][KING]);

    extern Bitboard get_rook_attacks_public(int square, Bitboard occupied);
    extern Bitboard get_bishop_attacks_public(int square, Bitboard occupied);

    Bitboard rook_attacks = get_rook_attacks_public(square, occupied);
    attackers |= (rook_attacks & (pos->pieces[color][ROOK] | pos->pieces[color][QUEEN]));

    Bitboard bishop_attacks = get_bishop_attacks_public(square, occupied);
    attackers |= (bishop_attacks & (pos->pieces[color][BISHOP] | pos->pieces[color][QUEEN]));

    return attackers;
}

static int get_least_valuable_attacker(Position* pos, Bitboard attackers, Color color, PieceType* piece_type) {
    if (!pos || attackers == 0 || !piece_type) return -1;

    static const PieceType piece_order[] = {PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING};

    for (int i = 0; i < 6; i++) {
        PieceType pt = piece_order[i];
        Bitboard pieces = pos->pieces[color][pt] & attackers;
        if (pieces) {
            *piece_type = pt;
            return get_lsb(pieces);
        }
    }

    return -1;
}

int static_exchange_evaluation(Position* pos, Move* move) {
    if (!pos || !move) return 0;

    static const int see_values[] = {0, 100, 500, 325, 325, 975, 10000};

    int target_square = move->to;
    Color side_to_move = pos->to_move;

    int gain[32];
    int depth = 0;

    gain[depth] = (move->captured != EMPTY) ? see_values[move->captured] : 0;

    Bitboard occupied = 0;
    for (int c = 0; c < 2; c++) {
        for (int p = 0; p < 6; p++) {
            occupied |= pos->pieces[c][p];
        }
    }

    occupied &= ~(1ULL << move->from);

    Bitboard white_attackers = get_attackers_of_square(pos, target_square, WHITE, occupied);
    Bitboard black_attackers = get_attackers_of_square(pos, target_square, BLACK, occupied);

    PieceType attacking_piece = move->piece;
    Color side = 1 - side_to_move;

    while ((side == WHITE ? white_attackers : black_attackers) != 0) {
        depth++;
        if (depth >= 31) break;

        PieceType piece_type;
        Bitboard current_attackers = (side == WHITE) ? white_attackers : black_attackers;
        int attacker_square = get_least_valuable_attacker(pos, current_attackers, side, &piece_type);

        if (attacker_square == -1) break;

        occupied &= ~(1ULL << attacker_square);
        if (side == WHITE) {
            white_attackers &= ~(1ULL << attacker_square);
        } else {
            black_attackers &= ~(1ULL << attacker_square);
        }

        gain[depth] = see_values[attacking_piece] - gain[depth - 1];

        if (piece_type == ROOK || piece_type == QUEEN) {
            Bitboard new_rook_attackers = get_rook_attacks_public(target_square, occupied);
            white_attackers |= (new_rook_attackers & (pos->pieces[WHITE][ROOK] | pos->pieces[WHITE][QUEEN]));
            black_attackers |= (new_rook_attackers & (pos->pieces[BLACK][ROOK] | pos->pieces[BLACK][QUEEN]));
        }
        if (piece_type == BISHOP || piece_type == QUEEN) {
            Bitboard new_bishop_attackers = get_bishop_attacks_public(target_square, occupied);
            white_attackers |= (new_bishop_attackers & (pos->pieces[WHITE][BISHOP] | pos->pieces[WHITE][QUEEN]));
            black_attackers |= (new_bishop_attackers & (pos->pieces[BLACK][BISHOP] | pos->pieces[BLACK][QUEEN]));
        }

        attacking_piece = piece_type;

        side = 1 - side;

        if (-gain[depth - 1] < gain[depth]) {
            gain[depth] = -gain[depth - 1];
        }
    }

    while (--depth > 0) {
        if (gain[depth] > -gain[depth - 1]) {
            gain[depth - 1] = -gain[depth];
        }
    }

    return gain[0];
}

bool see_is_good_capture(Position* pos, Move* move) {
    if (!pos || !move) return false;

    if (move->captured == EMPTY) return false;

    return static_exchange_evaluation(pos, move) >= 0;
    printf("En passant: %d\n", pos->en_passant_square);
    printf("Castling: %d\n", pos->castling_rights);
    printf("Halfmove: %d\n", pos->halfmove_clock);
    printf("Fullmove: %d\n", pos->fullmove_number);
    printf("\n");
}

char* square_to_string(int square) {
    static char square_str[3];

    if (square < 0 || square > 63) {
        strcpy(square_str, "--");
        return square_str;
    }

    int file = square % 8;
    int rank = square / 8;

    square_str[0] = 'a' + file;
    square_str[1] = '1' + rank;
    square_str[2] = '\0';

    return square_str;
}
