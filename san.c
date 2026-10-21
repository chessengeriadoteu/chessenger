/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#include "san.h"
#include "movegen.h"
#include "utils.h"

#include <stdio.h>
#include <string.h>

static const char SAN_LETTER[6] = { 'P', 'R', 'N', 'B', 'Q', 'K' };

const char* san_result_name(SanResult r) {
    switch (r) {
        case SAN_OK:           return "ok";
        case SAN_ILLEGAL:      return "illegal";
        case SAN_AMBIGUOUS:    return "ambiguous";
        case SAN_UNRECOGNIZED: return "unrecognized";
    }
    return "unrecognized";
}

static bool moves_equal(const Move* a, const Move* b) {
    return a->from == b->from && a->to == b->to && a->promotion == b->promotion;
}

bool move_to_san(const Position* pos, Move move, char* buf, size_t n) {
    if (!buf || n == 0) return false;
    buf[0] = '\0';
    if (!pos) return false;

    Position p = *pos;
    Move legal[MAX_MOVES];
    const int n_moves = generate_legal_moves(&p, legal);
    int idx = -1;
    for (int i = 0; i < n_moves; i++) {
        if (moves_equal(&legal[i], &move)) { idx = i; break; }
    }
    if (idx < 0) return false;
    Move m = legal[idx];

    char t[16];
    size_t k = 0;
    if (m.is_castling) {

        const char* r = (m.to > m.from) ? "O-O" : "O-O-O";
        k = strlen(r);
        memcpy(t, r, k);
    } else if (m.piece == PAWN) {
        if (m.captured != EMPTY) {
            t[k++] = (char)('a' + (m.from & 7));
            t[k++] = 'x';
        }
        t[k++] = (char)('a' + (m.to & 7));
        t[k++] = (char)('1' + (m.to >> 3));
        if (m.promotion != EMPTY) {
            t[k++] = '=';
            t[k++] = SAN_LETTER[m.promotion];
        }
    } else {
        t[k++] = SAN_LETTER[m.piece];

        bool others = false, same_file = false, same_rank = false;
        for (int i = 0; i < n_moves; i++) {
            const Move* o = &legal[i];
            if (i == idx || o->is_castling || o->piece != m.piece || o->to != m.to) continue;
            others = true;
            if ((o->from & 7) == (m.from & 7)) same_file = true;
            if ((o->from >> 3) == (m.from >> 3)) same_rank = true;
        }
        if (others) {
            if (!same_file) {
                t[k++] = (char)('a' + (m.from & 7));
            } else if (!same_rank) {
                t[k++] = (char)('1' + (m.from >> 3));
            } else {
                t[k++] = (char)('a' + (m.from & 7));
                t[k++] = (char)('1' + (m.from >> 3));
            }
        }
        if (m.captured != EMPTY) t[k++] = 'x';
        t[k++] = (char)('a' + (m.to & 7));
        t[k++] = (char)('1' + (m.to >> 3));
    }

    UndoInfo u;
    make_move_full(&p, &m, &u);
    if (is_in_check(&p, p.to_move)) {
        Move replies[MAX_MOVES];
        t[k++] = (generate_legal_moves(&p, replies) == 0) ? '#' : '+';
    }
    undo_move_full(&p, &m, &u);

    if (k + 1 > n) return false;
    memcpy(buf, t, k);
    buf[k] = '\0';
    return true;
}

static bool is_file_char(char c) { return c >= 'a' && c <= 'h'; }
static bool is_rank_char(char c)    { return c >= '1' && c <= '8'; }
static bool is_space_char(char c)  { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

static PieceType promo_from_char(char c) {
    switch (c) {
        case 'Q': case 'q': return QUEEN;
        case 'R': case 'r': return ROOK;
        case 'B': case 'b': return BISHOP;
        case 'N': case 'n': return KNIGHT;
        default:            return EMPTY;
    }
}

static PieceType san_piece(char c) {
    switch (c) {
        case 'K': case 'k': return KING;
        case 'Q': case 'q': return QUEEN;
        case 'R': case 'r': return ROOK;
        case 'B':           return BISHOP;
        case 'N': case 'n': return KNIGHT;
        default:            return EMPTY;
    }
}

static void trim_suffix(char* b) {
    for (;;) {
        size_t n = strlen(b);
        if (n > 0 && (is_space_char(b[n - 1]) || b[n - 1] == '+' || b[n - 1] == '#' || b[n - 1] == '!' || b[n - 1] == '?')) {
            b[n - 1] = '\0';
            continue;
        }
        if (n >= 6) {
            const char* s = b + n - 4;
            if ((s[0] == 'e' || s[0] == 'E') && s[1] == '.' && (s[2] == 'p' || s[2] == 'P') && s[3] == '.') {
                b[n - 4] = '\0';
                continue;
            }
        }
        if (n >= 4) {
            const char* s = b + n - 2;
            if ((s[0] == 'e' || s[0] == 'E') && (s[1] == 'p' || s[1] == 'P') && (is_rank_char(s[-1]) || is_space_char(s[-1]))) {
                b[n - 2] = '\0';
                continue;
            }
        }
        return;
    }
}

static void castle_squares(const Position* p, const Move* cm, int* king_dest, int* rook_src) {
    if (uci_chess960) {
        int rook_dest;
        castle_960_squares(cm->from, cm->to, king_dest, &rook_dest);
        *rook_src = cm->to;
    } else {
        const int back_rank_base = (p->to_move == WHITE) ? 0 : 2;
        *king_dest = cm->to;
        *rook_src = castle_rook_sq[back_rank_base + ((cm->to > cm->from) ? 0 : 1)];
    }
}

static SanResult match_result(int hits, int* count_out) {
    *count_out = hits;
    if (hits == 0) return SAN_ILLEGAL;
    if (hits > 1) return SAN_AMBIGUOUS;
    return SAN_OK;
}

static SanResult match_all(const Position* pos, const char* s, Move* mlist, int* count_out) {
    *count_out = 0;
    if (!pos || !s) return SAN_UNRECOGNIZED;

    while (is_space_char(*s)) s++;
    char b[32];
    const size_t dl = strlen(s);
    if (dl == 0 || dl >= sizeof(b)) return SAN_UNRECOGNIZED;
    memcpy(b, s, dl + 1);
    trim_suffix(b);
    if (b[0] == '\0') return SAN_UNRECOGNIZED;
    for (const char* c = b; *c; c++) {
        if (is_space_char(*c)) return SAN_UNRECOGNIZED;
    }

    Position p = *pos;
    Move legal[MAX_MOVES];
    const int n_moves = generate_legal_moves(&p, legal);
    int hits = 0;

    {
        char r[8];
        size_t n = strlen(b);
        if (n == 3 || n == 5) {
            bool ok = true;
            for (size_t i = 0; i < n; i++) {
                const char c = b[i];
                if (i % 2 == 1) { if (c != '-') ok = false; r[i] = '-'; }
                else if (c == 'O' || c == 'o' || c == '0') r[i] = 'O';
                else ok = false;
            }
            if (ok) {
                const bool is_short = (n == 3);
                for (int i = 0; i < n_moves; i++) {
                    if (!legal[i].is_castling || ((legal[i].to > legal[i].from) != is_short)) continue;
                    mlist[hits++] = legal[i];
                }
                return match_result(hits, count_out);
            }
        }
    }

    {
        const size_t n = strlen(b);
        if ((n == 4 || n == 5 || n == 6) && is_file_char(b[0]) && is_rank_char(b[1]) && is_file_char(b[2]) && is_rank_char(b[3])) {
            int promo = EMPTY;
            bool valid = true;
            if (n == 5) {
                promo = promo_from_char(b[4]);
                if (promo == EMPTY) valid = false;
            } else if (n == 6) {
                promo = (b[4] == '=') ? promo_from_char(b[5]) : EMPTY;
                if (promo == EMPTY) valid = false;
            }
            if (!valid) return SAN_UNRECOGNIZED;
            const int from = (b[1] - '1') * 8 + (b[0] - 'a');
            const int to = (b[3] - '1') * 8 + (b[2] - 'a');
            for (int i = 0; i < n_moves; i++) {
                const Move* m = &legal[i];
                if (m->from != from || m->to != to) continue;
                if (promo != EMPTY && m->promotion != promo) continue;
                mlist[hits++] = *m;
            }
            if (hits == 0 && promo == EMPTY) {

                for (int i = 0; i < n_moves; i++) {
                    const Move* m = &legal[i];
                    if (!m->is_castling || m->from != from) continue;
                    int king_dest, rook_src;
                    castle_squares(&p, m, &king_dest, &rook_src);
                    if (to != king_dest && to != rook_src) continue;
                    mlist[hits++] = *m;
                }
            }
            return match_result(hits, count_out);
        }
    }

    const char* q = b;
    int piece_kind = san_piece(*q);
    if (piece_kind != EMPTY) {
        q++;
    } else {
        piece_kind = PAWN;
    }

    char r[16];
    size_t rn = 0;
    for (; *q; q++) {
        if (*q == 'x' || *q == ':' || *q == '-') continue;
        if (rn + 1 >= sizeof(r)) return SAN_UNRECOGNIZED;
        r[rn++] = *q;
    }
    r[rn] = '\0';

    int promo = EMPTY;
    if (rn >= 3 && promo_from_char(r[rn - 1]) != EMPTY && (is_rank_char(r[rn - 2]) || r[rn - 2] == '=')) {
        promo = promo_from_char(r[rn - 1]);
        r[--rn] = '\0';
        if (rn > 0 && r[rn - 1] == '=') r[--rn] = '\0';
    }

    if (rn < 2 || rn > 4 || !is_file_char(r[rn - 2]) || !is_rank_char(r[rn - 1])) return SAN_UNRECOGNIZED;
    const int to = (r[rn - 1] - '1') * 8 + (r[rn - 2] - 'a');
    int src_file = -1, src_rank = -1;
    if (rn == 4) {
        if (!is_file_char(r[0]) || !is_rank_char(r[1])) return SAN_UNRECOGNIZED;
        src_file = r[0] - 'a';
        src_rank = r[1] - '1';
    } else if (rn == 3) {
        if (is_file_char(r[0])) src_file = r[0] - 'a';
        else if (is_rank_char(r[0])) src_rank = r[0] - '1';
        else return SAN_UNRECOGNIZED;
    }
    if (piece_kind != PAWN && promo != EMPTY) return SAN_ILLEGAL;

    if (piece_kind == PAWN && src_file < 0) src_file = to & 7;

    for (int i = 0; i < n_moves; i++) {
        const Move* m = &legal[i];
        if (m->is_castling || m->piece != piece_kind || m->to != to) continue;
        if (src_file >= 0 && (m->from & 7) != src_file) continue;
        if (src_rank >= 0 && (m->from >> 3) != src_rank) continue;
        if (promo != EMPTY && m->promotion != promo) continue;
        mlist[hits++] = *m;
    }
    return match_result(hits, count_out);
}

SanResult parse_move_human(const Position* pos, const char* s, Move* out) {
    Move mlist[MAX_MOVES];
    int n_moves = 0;
    const SanResult r = match_all(pos, s, mlist, &n_moves);
    if (r == SAN_OK && out) *out = mlist[0];
    return r;
}

static bool is_capture(const Move* m) { return m->captured != EMPTY || m->is_en_passant; }

static int destination(const Position* p, const Move* m) {
    if (!m->is_castling) return m->to;
    int king_to, rook_from;
    castle_squares(p, m, &king_to, &rook_from);
    return king_to;
}

SanResult san_read_move(const Position* pos, const char* s, Move* list, int* count, bool* shortcut) {
    *count = 0;
    *shortcut = false;
    if (!pos || !s) return SAN_UNRECOGNIZED;
    while (is_space_char(*s)) s++;
    const size_t len = strlen(s);
    char b[32];
    if (len == 0 || len >= sizeof(b)) return SAN_UNRECOGNIZED;
    memcpy(b, s, len + 1);

    size_t core = len;
    while (core > 0 && strchr("+#!?", b[core - 1])) core--;
    char suffix[32];
    memcpy(suffix, b + core, len - core + 1);
    b[core] = '\0';

    if (core >= 4 && b[core - 1] == ')' && b[core - 3] == '(') {
        b[core - 3] = '=';
        b[--core] = '\0';
    }
    if (core >= 3 && b[core - 2] == '/') b[core - 2] = '=';
    if (core >= 3 && b[core - 1] == '=') b[--core] = '\0';

    for (size_t i = 0; i < core; i++) {
        const char c = b[i];
        if (i == 0 && strchr("KQRBN", c)) continue;
        if ((c >= 'A' && c <= 'H') || c == 'X') b[i] = (char)(c - 'A' + 'a');
    }
    bool bishop_first = (b[0] == 'B');

    if (bishop_first && core >= 3 && strchr("QRBNqrbn", b[core - 1]) && (is_rank_char(b[core - 2]) || b[core - 2] == '=')) {
        b[0] = 'b';
        bishop_first = false;
    }
    const bool capture_square = (core == 3 && b[0] == 'x' && is_file_char(b[1]) && is_rank_char(b[2]));
    bool bare_square = (core == 2 && is_file_char(b[0]) && is_rank_char(b[1]));
    if (strlen(b) + strlen(suffix) >= sizeof(b)) return SAN_UNRECOGNIZED;
    char text[32];
    snprintf(text, sizeof(text), "%s%s", b, suffix);

    SanResult r = SAN_UNRECOGNIZED;
    if (!capture_square) {
        r = match_all(pos, text, list, count);
        if (r == SAN_UNRECOGNIZED && bishop_first) {
            text[0] = 'b';
            r = match_all(pos, text, list, count);
            if (r == SAN_UNRECOGNIZED) {
                text[0] = 'B';
            } else if (core == 2 && is_rank_char(b[1])) {
                b[0] = 'b';
                bare_square = true;
            }
        }
    }

    if (r == SAN_UNRECOGNIZED && core >= 4 && b[core - 2] == '=' && strchr("KkPp", b[core - 1])) {
        char head[32];
        memcpy(head, b, core - 2);
        head[core - 2] = '\0';
        int n = 0;
        if (match_all(pos, head, list, &n) != SAN_UNRECOGNIZED) {
            *count = 0;
            return SAN_ILLEGAL;
        }
        *count = 0;
        return SAN_UNRECOGNIZED;
    }

    if (!bare_square && !capture_square) return r;
    if (bare_square && r == SAN_OK) return r;
    if (bare_square && r == SAN_AMBIGUOUS) {
        bool promotion_only = true;
        for (int i = 1; i < *count; i++)
            if (list[i].from != list[0].from || list[i].to != list[0].to) promotion_only = false;
        if (promotion_only) return r;
    }
    const int sq = (b[core - 1] - '1') * 8 + (b[core - 2] - 'a');
    Position p = *pos;
    Move legal[MAX_MOVES];
    const int n = generate_legal_moves(&p, legal);
    int found = 0;
    for (int i = 0; i < n; i++) {
        if (destination(&p, &legal[i]) != sq) continue;
        if (capture_square && !is_capture(&legal[i])) continue;
        list[found++] = legal[i];
    }
    *count = found;
    *shortcut = found > 0;
    if (found == 0) return SAN_ILLEGAL;
    return found == 1 ? SAN_OK : SAN_AMBIGUOUS;
}
