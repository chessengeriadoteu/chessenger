/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#include "xboard.h"
#include "board.h"
#include "movegen.h"
#include "utils.h"
#include "san.h"
#include "search.h"
#include "console.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <signal.h>
#include <unistd.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#define ENGINE_VERSION "3.1"

#define XB_START_FEN "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
#define XB_MAX_PLIES 2048
#define XB_QUEUE 4

extern int multipv_setting;

static bool xb_on = false;
static bool xb_v2 = false;
static bool xb_force = false;
static bool xb_analyze = false;
static bool xb_analyze_dirty = false;
static Color xb_engine = BLACK;
static bool xb_post = false;
static bool xb_hard = false;

static char xb_base_fen[256] = XB_START_FEN;
static Move xb_moves[XB_MAX_PLIES];
static int xb_nmoves = 0;
static Position xb_pos;

typedef enum { XTC_NONE, XTC_LEVEL, XTC_ST } XbTc;
static XbTc xb_tc = XTC_NONE;
static int xb_mps = 0;
static long long xb_base_ms = 300000;
static long long xb_inc_ms = 0;
static long long xb_st_ms = 0;
static int xb_sd = 0;
static long long xb_time_cs = -1, xb_otim_cs = -1;

static bool xb_edit = false;
static bool xb_edit_black = false;
static char xb_edit_board[64];

typedef enum { XS_NONE, XS_MOVE, XS_ANALYZE } XbSearch;
static XbSearch xb_search = XS_NONE;
static bool xb_discard = false;
static bool xb_have_best = false;
static Move xb_best;
static char xb_last_pv[1024] = "";
static char xb_hint[16] = "";
static uint64_t xb_hint_hash = 0;

typedef struct {
    char line[MAX_UCI_LINE];
    XbSearch search;
} XbStep;
static XbStep xb_queue[XB_QUEUE];
static int xb_qn = 0;

typedef struct {
    char name[48];
    bool check;
    char feature[96];
} XbOption;
static XbOption xb_opts[8];
static int xb_nopts = 0;

static int xb_cap_saved = -1;
static int xb_cap_read = -1;
static char xb_cap_text[16384];

bool xboard_active(void) { return xb_on; }

bool xboard_takes_search_output(void) { return xb_on && xb_search != XS_NONE; }

static const char* xb_word(const char* s, char* word, size_t n) {
    size_t i = 0;
    while (*s == ' ' || *s == '\t') s++;
    while (*s && *s != ' ' && *s != '\t') {
        if (i + 1 < n) word[i++] = *s;
        s++;
    }
    word[i] = '\0';
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static void xb_flush(void) { fflush(stdout); }

static bool xb_capture_begin(void) {
    if (xb_cap_saved >= 0) return true;
    int fds[2];
    fflush(stdout);
#ifdef _WIN32
    if (_pipe(fds, 65536, _O_BINARY) != 0) return false;
#else
    if (pipe(fds) != 0) return false;
#endif
    const int out = fileno(stdout);
    const int saved = dup(out);
    if (saved < 0 || dup2(fds[1], out) < 0) {
        if (saved >= 0) close(saved);
        close(fds[0]);
        close(fds[1]);
        return false;
    }
    close(fds[1]);
    xb_cap_saved = saved;
    xb_cap_read = fds[0];
    return true;
}

static void xb_capture_end(void) {
    xb_cap_text[0] = '\0';
    if (xb_cap_saved < 0) return;
    fflush(stdout);
    dup2(xb_cap_saved, fileno(stdout));
    close(xb_cap_saved);
    xb_cap_saved = -1;
    size_t used = 0;
    for (;;) {
        char buf[512];
        const int r = (int)read(xb_cap_read, buf, sizeof(buf));
        if (r <= 0) break;
        size_t take = (size_t)r;
        if (used + take >= sizeof(xb_cap_text)) take = sizeof(xb_cap_text) - 1 - used;
        memcpy(xb_cap_text + used, buf, take);
        used += take;
    }
    xb_cap_text[used] = '\0';
    close(xb_cap_read);
    xb_cap_read = -1;
}

static void xb_print_caught(void) {
    const char* s = xb_cap_text;
    while (*s) {
        const char* e = strchr(s, '\n');
        const size_t len = e ? (size_t)(e - s) : strlen(s);
        char ln[1024];
        size_t k = len < sizeof(ln) - 1 ? len : sizeof(ln) - 1;
        memcpy(ln, s, k);
        ln[k] = '\0';
        while (k > 0 && (ln[k - 1] == '\r' || ln[k - 1] == ' ')) ln[--k] = '\0';
        const char* body = strncmp(ln, "info string ", 12) == 0 ? ln + 12 : ln;
        if (body[0]) printf("# %s\n", body);
        s = e ? e + 1 : s + len;
    }
    xb_flush();
}

static void xb_push(const char* line, XbSearch search) {
    if (xb_qn >= XB_QUEUE) return;
    snprintf(xb_queue[xb_qn].line, sizeof(xb_queue[xb_qn].line), "%s", line);
    xb_queue[xb_qn].search = search;
    xb_qn++;
}

static void xb_move_text(const Move* m, char* buf, size_t n) {
    if (m->is_castling && uci_chess960) {
        snprintf(buf, n, "%s", m->to > m->from ? "O-O" : "O-O-O");
        return;
    }
    Move c = *m;
    char s[8];
    move_to_string_buf(&c, s);
    snprintf(buf, n, "%s", s);
}

static bool xb_legal_match(Position* p, const Move* want, Move* out) {
    Move list[MAX_MOVES];
    const int n = generate_legal_moves(p, list);
    for (int i = 0; i < n; i++) {
        if (list[i].from == want->from && list[i].to == want->to && list[i].promotion == want->promotion) {
            *out = list[i];
            return true;
        }
    }
    return false;
}

static void xb_make(Position* p, Move m) {
    if (p->history_count >= HISTORY_GAME_LIMIT) compact_position_history(p);
    UndoInfo u;
    make_move_full(p, &m, &u);
}

static bool xb_replay(int n) {
    Position p;
    if (!parse_fen(&p, xb_base_fen)) return false;
    for (int i = 0; i < n; i++) xb_make(&p, xb_moves[i]);
    xb_pos = p;
    xb_nmoves = n;
    return true;
}

static void xb_set_base(const char* fen) {
    snprintf(xb_base_fen, sizeof(xb_base_fen), "%s", fen);
    xb_nmoves = 0;
    xb_replay(0);
    xb_hint[0] = '\0';
    xb_analyze_dirty = true;
}

static void xb_add_move(Move m) {
    if (xb_nmoves >= XB_MAX_PLIES) {
        snprintf(xb_base_fen, sizeof(xb_base_fen), "%s", position_to_fen(&xb_pos));
        xb_nmoves = 0;
        xb_replay(0);
    }
    xb_moves[xb_nmoves++] = m;
    xb_make(&xb_pos, m);
    xb_analyze_dirty = true;
}

static bool xb_report_end(void) {
    Move list[MAX_MOVES];
    if (generate_legal_moves(&xb_pos, list) > 0) return false;
    if (is_in_check(&xb_pos, xb_pos.to_move))
        printf("%s\n", xb_pos.to_move == WHITE ? "0-1 {Black mates}" : "1-0 {White mates}");
    else
        printf("1/2-1/2 {Stalemate}\n");
    xb_flush();
    return true;
}

static void xb_position_line(char* out, size_t n) {
    const bool start = !uci_chess960 && strcmp(xb_base_fen, XB_START_FEN) == 0;
    int used = snprintf(out, n, start ? "position startpos" : "position fen %s", xb_base_fen);
    if (xb_nmoves > 0 && used > 0 && (size_t)used < n) used += snprintf(out + used, n - (size_t)used, " moves");
    for (int i = 0; i < xb_nmoves && used > 0 && (size_t)used + 8 < n; i++) {
        char s[8];
        Move m = xb_moves[i];
        move_to_string_buf(&m, s);
        used += snprintf(out + used, n - (size_t)used, " %s", s);
    }
    if (used <= 0 || (size_t)used + 8 >= n)
        snprintf(out, n, "position fen %s", position_to_fen(&xb_pos));
}

static void xb_go_line(char* out, size_t n) {
    if (xb_sd > 0 && xb_tc == XTC_NONE && xb_time_cs < 0) {
        snprintf(out, n, "go depth %d", xb_sd);
        return;
    }
    int used;
    if (xb_tc == XTC_ST) {
        used = snprintf(out, n, "go movetime %lld", xb_st_ms > 0 ? xb_st_ms : 1);
        if (xb_sd > 0 && used > 0 && (size_t)used < n) snprintf(out + used, n - (size_t)used, " depth %d", xb_sd);
        return;
    }

    const int mps = xb_tc == XTC_LEVEL ? xb_mps : 40;
    const long long base = xb_tc == XTC_LEVEL ? xb_base_ms : 300000;
    const long long inc = xb_tc == XTC_LEVEL ? xb_inc_ms : 0;
    long long own = xb_time_cs >= 0 ? xb_time_cs * 10 : base;
    long long opp = xb_otim_cs >= 0 ? xb_otim_cs * 10 : base;
    if (own < 10) own = 10;
    if (opp < 10) opp = 10;
    const long long w = xb_pos.to_move == WHITE ? own : opp;
    const long long b = xb_pos.to_move == WHITE ? opp : own;
    used = snprintf(out, n, "go wtime %lld btime %lld winc %lld binc %lld", w, b, inc, inc);
    if (mps > 0 && used > 0 && (size_t)used < n) {
        const int own_moves = xb_nmoves / 2;
        used += snprintf(out + used, n - (size_t)used, " movestogo %d", mps - own_moves % mps);
    }
    if (xb_sd > 0 && used > 0 && (size_t)used < n) snprintf(out + used, n - (size_t)used, " depth %d", xb_sd);
}

static void xb_think(void) {
    if (xb_report_end()) return;
    char line[MAX_UCI_LINE];
    xb_position_line(line, sizeof(line));
    xb_push(line, XS_NONE);
    xb_go_line(line, sizeof(line));
    xb_push(line, XS_MOVE);
}

static void xb_maybe_think(void) {
    if (!xb_force && !xb_analyze && xb_pos.to_move == xb_engine) xb_think();
}

static void xb_add_option(const char* name, bool check, const char* feature) {
    if (xb_nopts >= (int)(sizeof(xb_opts) / sizeof(xb_opts[0]))) return;
    XbOption* o = &xb_opts[xb_nopts++];
    snprintf(o->name, sizeof(o->name), "%s", name);
    o->check = check;
    snprintf(o->feature, sizeof(o->feature), "%s", feature);
}

static void xb_option_from_uci(const char* ln) {
    if (strncmp(ln, "option name ", 12) != 0) return;
    const char* name = ln + 12;
    const char* type = strstr(name, " type ");
    if (!type) return;
    char nm[48];
    size_t k = (size_t)(type - name);
    if (k >= sizeof(nm)) return;
    memcpy(nm, name, k);
    nm[k] = '\0';
    char feature[96];
    int d = 0, a = 0, b = 0;
    char v[16];
    if (sscanf(type, " type spin default %d min %d max %d", &d, &a, &b) == 3) {
        snprintf(feature, sizeof(feature), "%s -spin %d %d %d", nm, d, a, b);
        xb_add_option(nm, false, feature);
    } else if (sscanf(type, " type check default %15s", v) == 1) {
        snprintf(feature, sizeof(feature), "%s -check %d", nm, strcmp(v, "true") == 0 ? 1 : 0);
        xb_add_option(nm, true, feature);
    }
}

static void xb_build_options(void) {
    xb_nopts = 0;
    char ln[128];
    snprintf(ln, sizeof(ln), "option name MultiPV type spin default %d min 1 max %d", multipv_setting, MAX_MULTIPV);
    xb_option_from_uci(ln);
    if (!xb_capture_begin()) return;
    console_strength_uci_options();
    xb_capture_end();
    const char* s = xb_cap_text;
    while (*s) {
        const char* e = strchr(s, '\n');
        const size_t len = e ? (size_t)(e - s) : strlen(s);
        char one[256];
        size_t k = len < sizeof(one) - 1 ? len : sizeof(one) - 1;
        memcpy(one, s, k);
        one[k] = '\0';
        while (k > 0 && (one[k - 1] == '\r' || one[k - 1] == ' ')) one[--k] = '\0';
        xb_option_from_uci(one);
        s = e ? e + 1 : s + len;
    }
}

static void xb_features(void) {
    printf("feature myname=\"Chessenger %s\" ping=1 setboard=1 usermove=1 time=1 draw=1\n", ENGINE_VERSION);
    printf("feature sigint=0 sigterm=0 reuse=1 analyze=1 colors=0 san=0 nps=0 debug=1\n");
    printf("feature memory=1 smp=1 egt=\"syzygy\" variants=\"normal,fischerandom\"\n");
    xb_build_options();
    for (int i = 0; i < xb_nopts; i++) printf("feature option=\"%s\"\n", xb_opts[i].feature);
    printf("feature done=1\n");
    xb_flush();
}

static void xb_new_game_state(void) {
    uci_chess960 = false;
    xb_set_base(XB_START_FEN);
    xb_force = false;
    xb_engine = BLACK;
    xb_sd = 0;
    xb_time_cs = xb_otim_cs = -1;
    xb_edit = false;
}

void xboard_enter(void) {
    xb_on = true;
#ifndef _WIN32

    signal(SIGINT, SIG_IGN);
#endif
    xb_new_game_state();
    xb_analyze_dirty = false;
}

static void xb_play_best(void) {
    Move m;
    bool ok = xb_have_best && xb_legal_match(&xb_pos, &xb_best, &m);
    if (!ok) {
        Move list[MAX_MOVES];
        if (generate_legal_moves(&xb_pos, list) > 0) { m = list[0]; ok = true; }
    }
    if (!ok) { xb_report_end(); return; }
    char txt[16];
    xb_move_text(&m, txt, sizeof(txt));

    char first[16] = "", second[16] = "";
    const char* rest = xb_word(xb_last_pv, first, sizeof(first));
    xb_word(rest, second, sizeof(second));
    char played[8];
    Move c = m;
    move_to_string_buf(&c, played);
    xb_add_move(m);
    printf("move %s\n", txt);
    xb_flush();
    if (strcmp(first, played) == 0 && second[0]) {
        snprintf(xb_hint, sizeof(xb_hint), "%s", second);
        xb_hint_hash = xb_pos.hash;
    } else {
        xb_hint[0] = '\0';
    }
    xb_report_end();
}

bool xboard_before_read(Position* pos, char* line, size_t size) {
    (void)pos;
    if (!xb_on) return false;
    if (xb_cap_saved >= 0) {
        xb_capture_end();
        xb_print_caught();
    }
    if (xb_search != XS_NONE) {
        const XbSearch kind = xb_search;
        xb_search = XS_NONE;
        if (kind == XS_MOVE && !xb_discard) xb_play_best();
        xb_discard = false;
        xb_have_best = false;
    }
    if (xb_qn == 0 && xb_analyze && xb_analyze_dirty) {

        char waiting[16];
        if (!console_queued_line(waiting, sizeof(waiting))) {
            xb_analyze_dirty = false;
            char l[MAX_UCI_LINE];
            xb_position_line(l, sizeof(l));
            xb_push(l, XS_NONE);
            xb_push("go infinite", XS_ANALYZE);
        }
    }
    if (xb_qn > 0) {
        const XbStep step = xb_queue[0];
        memmove(xb_queue, xb_queue + 1, (size_t)(xb_qn - 1) * sizeof(xb_queue[0]));
        xb_qn--;
        snprintf(line, size, "%s", step.line);
        if (step.search != XS_NONE) {
            xb_search = step.search;
            xb_discard = false;
            xb_have_best = false;
            xb_last_pv[0] = '\0';
        } else {
            xb_capture_begin();
        }
        return true;
    }
    return false;
}

static void xb_pv_text(const char* pv, char* out, size_t n) {
    out[0] = '\0';
    if (!uci_chess960) { snprintf(out, n, "%s", pv ? pv : ""); return; }
    Position p = xb_pos;
    size_t used = 0;
    const char* s = pv ? pv : "";
    bool replay = true;
    while (*s) {
        char w[16];
        const char* next = xb_word(s, w, sizeof(w));
        if (!w[0]) break;
        char t[16];
        Move m;
        if (replay && parse_move_human(&p, w, &m) == SAN_OK) {
            xb_move_text(&m, t, sizeof(t));
            xb_make(&p, m);
        } else {
            replay = false;
            snprintf(t, sizeof(t), "%s", w);
        }
        const int r = snprintf(out + used, n - used, "%s%s", used ? " " : "", t);
        if (r < 0 || used + (size_t)r >= n) break;
        used += (size_t)r;
        s = next;
    }
}

void xboard_search_info(int multipv, int depth, int seldepth, int score, bool mate, int mate_moves,
                        long long time_ms, long long nodes, const char* pv) {
    (void)seldepth;
    if (multipv <= 1) snprintf(xb_last_pv, sizeof(xb_last_pv), "%s", pv ? pv : "");
    if (!xb_post && xb_search != XS_ANALYZE) return;
    int s = score;
    if (mate) s = mate_moves > 0 ? 100000 + mate_moves : -100000 + mate_moves;
    char txt[1024];
    xb_pv_text(pv, txt, sizeof(txt));
    printf("%d %d %lld %lld %s\n", depth, s, time_ms / 10, nodes, txt);
    xb_flush();
}

void xboard_search_bestmove(const Move* m) {
    xb_have_best = (m != NULL);
    if (m) xb_best = *m;
}

void xboard_search_refused(const char* reason) {
    printf("# position refused by the engine: %s\n", reason && reason[0] ? reason : "?");
    xb_flush();
    xb_have_best = false;
}

int xboard_line_in_search(const char* typed) {
    if (!xb_on || xb_search == XS_NONE) return 0;
    char w[32];
    const char* rest = xb_word(typed, w, sizeof(w));

    if (strcmp(w, "post") == 0)   { xb_post = true;  return 2; }
    if (strcmp(w, "nopost") == 0) { xb_post = false; return 2; }
    if (strcmp(w, "hard") == 0)   { xb_hard = true;  return 2; }
    if (strcmp(w, "easy") == 0)   { xb_hard = false; return 2; }
    if (strcmp(w, ".") == 0) return 2;
    if (xb_search == XS_MOVE) {
        if (strcmp(w, "?") == 0) return 3;
        if (strcmp(w, "force") == 0 || strcmp(w, "new") == 0 || strcmp(w, "result") == 0 ||
            strcmp(w, "quit") == 0) {
            xb_discard = true;
            return 1;
        }
        return 0;
    }

    if (strcmp(w, "ping") == 0) {
        printf("pong %s\n", rest);
        xb_flush();
        return 2;
    }
    if (strcmp(w, "?") == 0) return 2;
    if (strcmp(w, "time") == 0 || strcmp(w, "otim") == 0 || strcmp(w, "hint") == 0 || strcmp(w, "draw") == 0 ||
        strcmp(w, "computer") == 0 || strcmp(w, "name") == 0 || strcmp(w, "rating") == 0 || strcmp(w, "ics") == 0 ||
        strcmp(w, "random") == 0 || strcmp(w, "accepted") == 0 || strcmp(w, "rejected") == 0 ||
        strcmp(w, "level") == 0 || strcmp(w, "st") == 0 || strcmp(w, "sd") == 0)
        return 0;
    return 1;
}

static double xb_number(const char* s, bool* ok) {
    char* end = NULL;
    const double v = strtod(s, &end);
    *ok = end != s;
    return v;
}

static bool xb_level(const char* args) {
    char a[32], b[32], c[32];
    const char* r = xb_word(args, a, sizeof(a));
    r = xb_word(r, b, sizeof(b));
    xb_word(r, c, sizeof(c));
    bool ok1, ok2, ok3;
    const double mps = xb_number(a, &ok1);
    double base_s;
    const char* colon = strchr(b, ':');
    if (colon) {
        bool okm, oks;
        const double mins = xb_number(b, &okm);
        const double secs = xb_number(colon + 1, &oks);
        ok2 = okm && oks;
        base_s = mins * 60.0 + secs;
    } else {
        base_s = xb_number(b, &ok2) * 60.0;
    }
    const double inc = xb_number(c, &ok3);
    if (!ok1 || !ok2 || !ok3 || mps < 0 || base_s < 0 || inc < 0) return false;
    xb_tc = XTC_LEVEL;
    xb_mps = (int)mps;
    xb_base_ms = (long long)(base_s * 1000.0 + 0.5);
    xb_inc_ms = (long long)(inc * 1000.0 + 0.5);
    return true;
}

static void xb_setboard(const char* fen) {
    Position p;
    if (!parse_fen(&p, fen)) {
        printf("tellusererror Illegal position: %s\n", fen_last_error());
        printf("Error (illegal position): setboard %s\n", fen);
        xb_flush();
        return;
    }
    if (is_in_check(&p, p.to_move == WHITE ? BLACK : WHITE)) {
        printf("tellusererror Illegal position: the side not to move is in check\n");
        printf("Error (illegal position): setboard %s\n", fen);
        xb_flush();
        return;
    }
    xb_set_base(fen);
}

static void xb_edit_begin(void) {
    memset(xb_edit_board, '.', sizeof(xb_edit_board));
    for (int sq = 0; sq < 64; sq++) {
        for (int c = WHITE; c <= BLACK; c++) {
            for (int t = PAWN; t <= KING; t++) {
                if (xb_pos.pieces[c][t] & (1ULL << sq)) {
                    const char letters[] = "PRNBQK";
                    xb_edit_board[sq] = c == WHITE ? letters[t] : (char)tolower((unsigned char)letters[t]);
                }
            }
        }
    }
    xb_edit = true;
    xb_edit_black = false;
}

static void xb_edit_end(void) {
    char fen[128];
    size_t u = 0;
    for (int rank = 7; rank >= 0; rank--) {
        int empty = 0;
        for (int file = 0; file < 8; file++) {
            const char c = xb_edit_board[rank * 8 + file];
            if (c == '.') { empty++; continue; }
            if (empty) { fen[u++] = (char)('0' + empty); empty = 0; }
            fen[u++] = c;
        }
        if (empty) fen[u++] = (char)('0' + empty);
        if (rank) fen[u++] = '/';
    }
    fen[u] = '\0';

    char cr[8];
    size_t k = 0;
    if (xb_edit_board[4] == 'K' && xb_edit_board[7] == 'R') cr[k++] = 'K';
    if (xb_edit_board[4] == 'K' && xb_edit_board[0] == 'R') cr[k++] = 'Q';
    if (xb_edit_board[60] == 'k' && xb_edit_board[63] == 'r') cr[k++] = 'k';
    if (xb_edit_board[60] == 'k' && xb_edit_board[56] == 'r') cr[k++] = 'q';
    if (!k) cr[k++] = '-';
    cr[k] = '\0';
    char full[200];
    snprintf(full, sizeof(full), "%s %c %s - 0 1", fen, xb_pos.to_move == WHITE ? 'w' : 'b', cr);
    xb_edit = false;
    xb_setboard(full);
}

static void xb_edit_line(const char* line) {
    if (strcmp(line, ".") == 0) { xb_edit_end(); return; }
    if (strcmp(line, "#") == 0) { memset(xb_edit_board, '.', sizeof(xb_edit_board)); return; }
    if (strcmp(line, "c") == 0 || strcmp(line, "C") == 0) { xb_edit_black = !xb_edit_black; return; }
    if (strlen(line) == 3 && line[1] >= 'a' && line[1] <= 'h' && line[2] >= '1' && line[2] <= '8') {
        const int sq = (line[2] - '1') * 8 + (line[1] - 'a');
        const char p = (char)toupper((unsigned char)line[0]);
        if (p == 'X') { xb_edit_board[sq] = '.'; return; }
        if (strchr("PNBRQK", p)) {
            xb_edit_board[sq] = xb_edit_black ? (char)tolower((unsigned char)p) : p;
            return;
        }
    }
    printf("Error (unknown edit command): %s\n", line);
    xb_flush();
}

static void xb_side(Color side) {
    char fen[160];
    snprintf(fen, sizeof(fen), "%s", position_to_fen(&xb_pos));
    char* sp = strchr(fen, ' ');
    if (sp && sp[1]) {
        sp[1] = side == WHITE ? 'w' : 'b';

        char* f3 = strchr(sp + 3, ' ');
        if (f3 && f3[1] && f3[1] != '-') {
            char* f4 = strchr(f3 + 1, ' ');
            char tail[64];
            snprintf(tail, sizeof(tail), "%s", f4 ? f4 : "");
            snprintf(f3, sizeof(fen) - (size_t)(f3 - fen), " -%s", tail);
        }
    }
    Position p;
    if (parse_fen(&p, fen) && !is_in_check(&p, side == WHITE ? BLACK : WHITE)) xb_set_base(fen);
    xb_engine = side == WHITE ? BLACK : WHITE;
}

static void xb_setoption(const char* name, const char* value) {
    char l[MAX_UCI_LINE];
    snprintf(l, sizeof(l), "setoption name %s value %s", name, value);
    xb_push(l, XS_NONE);
}

static void xb_option(const char* args) {
    char nm[64], val[256];
    const char* eq = strchr(args, '=');
    size_t k = eq ? (size_t)(eq - args) : strlen(args);
    if (k >= sizeof(nm)) k = sizeof(nm) - 1;
    memcpy(nm, args, k);
    nm[k] = '\0';
    while (k > 0 && nm[k - 1] == ' ') nm[--k] = '\0';
    snprintf(val, sizeof(val), "%s", eq ? eq + 1 : "");
    for (int i = 0; i < xb_nopts; i++) {
        if (strcasecmp(xb_opts[i].name, nm) != 0) continue;
        if (xb_opts[i].check) {
            if (strcmp(val, "1") == 0) snprintf(val, sizeof(val), "true");
            else if (strcmp(val, "0") == 0) snprintf(val, sizeof(val), "false");
        }
        xb_setoption(xb_opts[i].name, val);
        xb_analyze_dirty = true;
        return;
    }
    printf("Error (unknown option): %s\n", nm);
    xb_flush();
}

static void xb_user_move(const char* text, bool must_be_move, const char* whole) {
    Move m;
    const SanResult r = parse_move_human(&xb_pos, text, &m);
    if (r == SAN_UNRECOGNIZED && !must_be_move) {
        printf("Error (unknown command): %s\n", whole);
        xb_flush();
        return;
    }
    if (r != SAN_OK) {
        printf("Illegal move: %s\n", text);
        xb_flush();
        return;
    }
    xb_add_move(m);
    xb_hint[0] = '\0';
    xb_maybe_think();
}

static bool xb_is(const char* w, const char* name) { return strcmp(w, name) == 0; }

XboardAction xboard_handle_line(Position* pos, char* line, size_t size) {
    (void)pos;
    (void)size;
    if (xb_edit) { xb_edit_line(line); return XBOARD_DONE; }
    char w[64];
    const char* args = xb_word(line, w, sizeof(w));
    if (!w[0]) return XBOARD_DONE;

    if (xb_is(w, "quit")) return XBOARD_QUIT;
    if (xb_is(w, "xboard") || xb_is(w, "accepted") || xb_is(w, "rejected") || xb_is(w, "random") ||
        xb_is(w, "computer") || xb_is(w, "name") || xb_is(w, "rating") || xb_is(w, "ics") || xb_is(w, "draw") ||
        xb_is(w, ".") || xb_is(w, "?"))
        return XBOARD_DONE;
    if (xb_is(w, "protover")) { xb_v2 = true; xb_features(); return XBOARD_DONE; }
    if (xb_is(w, "new")) {
        xb_new_game_state();
        xb_push("ucinewgame", XS_NONE);
        return XBOARD_DONE;
    }
    if (xb_is(w, "variant")) {
        if (xb_is(args, "fischerandom")) { uci_chess960 = true; xb_set_base(XB_START_FEN); }
        else if (xb_is(args, "normal")) { uci_chess960 = false; xb_set_base(XB_START_FEN); }
        else { printf("Error (unsupported variant): %s\n", args); xb_flush(); }
        return XBOARD_DONE;
    }
    if (xb_is(w, "force")) { xb_force = true; return XBOARD_DONE; }
    if (xb_is(w, "go")) {
        xb_force = false;
        xb_engine = xb_pos.to_move;
        if (!xb_analyze) xb_think();
        return XBOARD_DONE;
    }
    if (xb_is(w, "white")) { xb_side(WHITE); return XBOARD_DONE; }
    if (xb_is(w, "black")) { xb_side(BLACK); return XBOARD_DONE; }
    if (xb_is(w, "level")) {
        if (!xb_level(args)) { printf("Error (bad level): %s\n", line); xb_flush(); }
        return XBOARD_DONE;
    }
    if (xb_is(w, "st")) {
        bool ok;
        const double s = xb_number(args, &ok);
        if (!ok || s < 0) { printf("Error (bad time): %s\n", line); xb_flush(); return XBOARD_DONE; }
        xb_tc = XTC_ST;
        xb_st_ms = (long long)(s * 1000.0 + 0.5);
        return XBOARD_DONE;
    }
    if (xb_is(w, "sd")) {
        const int d = atoi(args);
        if (d < 1) { printf("Error (bad depth): %s\n", line); xb_flush(); return XBOARD_DONE; }
        xb_sd = d;
        return XBOARD_DONE;
    }
    if (xb_is(w, "time") || xb_is(w, "otim")) {
        bool ok;
        const double v = xb_number(args, &ok);
        if (ok) { if (xb_is(w, "time")) xb_time_cs = (long long)v; else xb_otim_cs = (long long)v; }
        return XBOARD_DONE;
    }
    if (xb_is(w, "usermove")) { xb_user_move(args, true, line); return XBOARD_DONE; }
    if (xb_is(w, "ping")) { printf("pong %s\n", args); xb_flush(); return XBOARD_DONE; }
    if (xb_is(w, "result")) { xb_force = true; return XBOARD_DONE; }
    if (xb_is(w, "setboard")) { xb_setboard(args); return XBOARD_DONE; }
    if (xb_is(w, "edit")) { xb_edit_begin(); return XBOARD_DONE; }
    if (xb_is(w, "undo") || xb_is(w, "remove")) {
        const int n = xb_is(w, "undo") ? 1 : 2;
        if (xb_nmoves < n) {
            printf("Error (command not legal now): %s\n", w);
            xb_flush();
            return XBOARD_DONE;
        }
        xb_replay(xb_nmoves - n);
        xb_hint[0] = '\0';
        xb_analyze_dirty = true;
        return XBOARD_DONE;
    }
    if (xb_is(w, "hard")) { xb_hard = true; return XBOARD_DONE; }
    if (xb_is(w, "easy")) { xb_hard = false; return XBOARD_DONE; }
    if (xb_is(w, "post")) { xb_post = true; return XBOARD_DONE; }
    if (xb_is(w, "nopost")) { xb_post = false; return XBOARD_DONE; }
    if (xb_is(w, "analyze")) { xb_analyze = true; xb_analyze_dirty = true; return XBOARD_DONE; }
    if (xb_is(w, "exit")) { if (xb_analyze) { xb_analyze = false; xb_force = true; } return XBOARD_DONE; }
    if (xb_is(w, "hint")) {

        Move m;
        if (xb_hint[0] && xb_hint_hash == xb_pos.hash && parse_move_human(&xb_pos, xb_hint, &m) == SAN_OK) {
            char t[16];
            xb_move_text(&m, t, sizeof(t));
            printf("Hint: %s\n", t);
            xb_flush();
        }
        return XBOARD_DONE;
    }
    if (xb_is(w, "memory")) { xb_setoption("Hash", args); return XBOARD_DONE; }
    if (xb_is(w, "cores")) { xb_setoption("Threads", args); return XBOARD_DONE; }
    if (xb_is(w, "egtpath")) {
        char type[32];
        const char* path = xb_word(args, type, sizeof(type));
        if (strcasecmp(type, "syzygy") == 0) xb_setoption("SyzygyPath", path);
        else { printf("# egtpath %s: only syzygy tables are supported\n", type); xb_flush(); }
        return XBOARD_DONE;
    }
    if (xb_is(w, "option")) {
        if (xb_nopts == 0) xb_build_options();
        xb_option(args);
        return XBOARD_DONE;
    }

    xb_user_move(w, false, line);
    return XBOARD_DONE;
}
