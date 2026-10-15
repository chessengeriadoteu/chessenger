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

    static const char white_chars[] = "PRNBQK";
    static const char black_chars[] = "prnbqk";

    printf("\n");
    for (int rank = 7; rank >= 0; rank--) {
        printf("%d ", rank + 1);
        for (int file = 0; file < 8; file++) {
            int square = rank * 8 + file;
            PieceType found_piece = EMPTY;
            Color found_color = WHITE;

            for (Color color = WHITE; color <= BLACK; color++) {
                for (PieceType piece = PAWN; piece <= KING; piece++) {
                    if (pos->pieces[color][piece] & (1ULL << square)) {
                        found_piece = piece;
                        found_color = color;
                        break;
                    }
                }
                if (found_piece != EMPTY) break;
            }

            printf("%c ", found_piece == EMPTY
                              ? '.'
                              : (found_color == WHITE ? white_chars : black_chars)[found_piece]);
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
                        piece_char = pieces[piece];
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
    if (uci_chess960) {

        for (int i = 0; i < 4; i++) {
            if (!(pos->castling_rights & (1 << i))) continue;
            *p++ = (char)(((i < 2) ? 'A' : 'a') + (castle_rook_sq[i] & 7));
            has_castling = true;
        }
    } else {
    if (pos->castling_rights & WHITE_KINGSIDE) { *p++ = 'K'; has_castling = true; }
    if (pos->castling_rights & WHITE_QUEENSIDE) { *p++ = 'Q'; has_castling = true; }
    if (pos->castling_rights & BLACK_KINGSIDE) { *p++ = 'k'; has_castling = true; }
    if (pos->castling_rights & BLACK_QUEENSIDE) { *p++ = 'q'; has_castling = true; }
    }
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

__attribute__((unused))
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

static Bitboard see_attackers_both_sides(Position* pos, int square, Bitboard occupied) {
    if (!pos || square < 0 || square > 63) return 0;

    Bitboard attackers = 0;
    const int file = square % 8;

    if (square >= 16) {
        if (file > 0 && (pos->pieces[WHITE][PAWN] & (1ULL << (square - 9)))) {
            attackers |= (1ULL << (square - 9));
        }
        if (file < 7 && (pos->pieces[WHITE][PAWN] & (1ULL << (square - 7)))) {
            attackers |= (1ULL << (square - 7));
        }
    }
    if (square < 48) {
        if (file > 0 && (pos->pieces[BLACK][PAWN] & (1ULL << (square + 7)))) {
            attackers |= (1ULL << (square + 7));
        }
        if (file < 7 && (pos->pieces[BLACK][PAWN] & (1ULL << (square + 9)))) {
            attackers |= (1ULL << (square + 9));
        }
    }

    extern Bitboard knight_attacks[64];
    extern Bitboard king_attacks[64];
    attackers |= knight_attacks[square] & (pos->pieces[WHITE][KNIGHT] | pos->pieces[BLACK][KNIGHT]);
    attackers |= king_attacks[square]   & (pos->pieces[WHITE][KING]   | pos->pieces[BLACK][KING]);

    extern Bitboard get_rook_attacks_public(int square, Bitboard occupied);
    extern Bitboard get_bishop_attacks_public(int square, Bitboard occupied);

    const Bitboard rook_att = get_rook_attacks_public(square, occupied);
    attackers |= rook_att & (pos->pieces[WHITE][ROOK] | pos->pieces[WHITE][QUEEN] |
                             pos->pieces[BLACK][ROOK] | pos->pieces[BLACK][QUEEN]);

    const Bitboard bishop_att = get_bishop_attacks_public(square, occupied);
    attackers |= bishop_att & (pos->pieces[WHITE][BISHOP] | pos->pieces[WHITE][QUEEN] |
                               pos->pieces[BLACK][BISHOP] | pos->pieces[BLACK][QUEEN]);

    return attackers;
}

int static_exchange_evaluation(Position* pos, Move* move) {
    if (!pos || !move) return 0;
    if (move->from < 0 || move->from > 63 || move->to < 0 || move->to > 63) return 0;

    if (uci_chess960 && move->is_castling) return 0;

    static const int see_values[7] = { 100, 500, 325, 325, 975, 10000, 0 };

    const int target_square = move->to;

    int gain[34];
    int depth = 0;

    Bitboard occupied = 0;
    for (int c = 0; c < 2; c++) {
        for (int p = 0; p < 6; p++) {
            occupied |= pos->pieces[c][p];
        }
    }

    occupied &= ~(1ULL << move->from);

    gain[0] = see_values[move->captured];
    PieceType piece_on_target = move->piece;

    if (move->promotion != EMPTY && move->promotion <= KING) {
        gain[0] += see_values[move->promotion] - see_values[PAWN];
        piece_on_target = move->promotion;
    }

    if (move->is_en_passant) {
        int captured_square = (pos->to_move == WHITE) ? target_square - 8 : target_square + 8;
        if (captured_square >= 0 && captured_square < 64) {
            occupied &= ~(1ULL << captured_square);
        }
        gain[0] = see_values[PAWN];
    }

    Color side = 1 - pos->to_move;

    while (depth < 32) {
        depth++;

        gain[depth] = see_values[piece_on_target] - gain[depth - 1];

        Bitboard attackers = see_attackers_both_sides(pos, target_square, occupied) & occupied;

        PieceType attacker_type = EMPTY;
        int attacker_square = get_least_valuable_attacker(pos, attackers, side, &attacker_type);
        if (attacker_square < 0) {
            break;
        }

        if (attacker_type == KING) {
            Bitboard enemy_attackers = 0;
            for (int p = 0; p < 6; p++) {
                enemy_attackers |= (attackers & pos->pieces[1 - side][p]);
            }
            if (enemy_attackers != 0) {
                break;
            }
        }

        occupied &= ~(1ULL << attacker_square);
        piece_on_target = attacker_type;
        side = 1 - side;
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

void trim_uci_line(char* line) {
    if (!line) return;

    size_t cut = strcspn(line, "\r\n");
    line[cut] = '\0';

    size_t len = cut;
    while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t')) {
        line[--len] = '\0';
    }

    size_t start = 0;
    while (line[start] == ' ' || line[start] == '\t') {
        start++;
    }
    if (start > 0) {
        memmove(line, line + start, len - start + 1);
    }
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
