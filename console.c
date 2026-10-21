/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#include "console.h"
#include "san.h"
#include "movegen.h"
#include "utils.h"
#include "search.h"
#include "tt.h"
#include "xboard.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <math.h>
#include <signal.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <sys/ioctl.h>
#include <fcntl.h>
#include <termios.h>
#include <poll.h>
#endif

#define ENGINE_VERSION "3.1"

#ifdef _WIN32

void win_stdin_console(int on);
void win_stdin_console_prompt(int cooked);
#endif

#define CON_PROMPT "chessenger> "
#define CON_DEFAULT_WIDTH 80

enum {
    G_GAME, G_POSITION, G_ANALYSIS, G_ENGINE, G_DISPLAY, G_TOOLS, G_INFO, G_COUNT
};
static const char* const CON_GROUP_NAME[G_COUNT] = {
    "Game", "Position", "Analysis", "Engine", "Display", "Tools", "Info"
};

typedef enum {
    C_NONE = 0,

    C_MOVE, C_NEW, C_UNDO, C_HISTORY, C_BOARD, C_FEN, C_SETBOARD, C_MOVES, C_FLIP,
    C_HASH, C_THREADS, C_SYZYGY, C_SET, C_OPTIONS,
    C_COLOR, C_THEME, C_PIECES, C_POV, C_WIDTH,
    C_ABOUT, C_VERSION, C_LICENSE, C_HELP, C_UCI, C_QUIT,

    C_PLAY, C_REDO, C_SWITCH, C_HINT, C_RESIGN, C_DRAW, C_CLOCK, C_TIME, C_POST,

    C_SAVE,

    C_LOAD, C_FIRST, C_PREV, C_NEXT, C_LAST, C_GOTO, C_ANALYZE, C_STOP, C_GO, C_EVAL, C_MATE, C_ANNOTATE, C_PONDER,

    C_LINES, C_SKILL, C_ELO, C_STRENGTH,

    C_XBOARD,

    C_LATER
} CommandId;

typedef struct {
    const char* name;
    int group;
    bool available;
    bool in_grid;
    CommandId id;
    const char* usage;
    const char* text;
    const char* example;
    const char* more;
} ConsoleCommand;

static const ConsoleCommand COMMANDS[] = {
    { "play",     G_GAME, true,  true,  C_PLAY,  "play [white|black] [T]", "new game against Chessenger (T: 5+3, 10, 40/90)", "play white 5+3, play black 10, play", "You take White unless you say black. T as in 'help time' (default: 'time')." },
    { "<move>",   G_GAME, true,  false, C_MOVE,  "<move>", "play a move: Nf3, exd5, O-O, e8=Q or g1f3", "e4, then Nf3 or g1f3 (outside a game several moves: e4 e5 Nf3)", "A square alone (d5) or a capture (xd5) plays the only legal move onto it." },
    { "new",      G_GAME, true,  true,  C_NEW,   "new", "restart the game from the initial position", "new", "In a game: a new game with the same sides and time control." },
    { "undo",     G_GAME, true,  true,  C_UNDO,  "undo [N]", "take back N moves (default: yours and the reply)", "undo, undo 2  (alias: back)", "Outside a game N counts half-moves. After 'new' or 'setboard' there is nothing to take back." },
    { "redo",     G_GAME, true,  true,  C_REDO,  "redo [N]", "replay moves taken back", "redo, redo 2", "Counts like 'undo'. A new move, 'new', 'setboard' or 'play' clears what can be replayed." },
    { "switch",   G_GAME, true,  true,  C_SWITCH, "switch", "swap sides: Chessenger moves now", "switch", NULL },
    { "hint",     G_GAME, true,  true,  C_HINT,  "hint", "show the move Chessenger suggests for you", "hint", "Nothing is played. Outside a game: for the side to move." },
    { "resign",   G_GAME, true,  true,  C_RESIGN, "resign | draw", "resign the game, or offer a draw", "resign", NULL },
    { "draw",     G_GAME, true,  true,  C_DRAW,  "resign | draw", "resign the game, or offer a draw", "draw e4  (or 'draw', then your move)", "The offer goes with your move; Chessenger accepts when its score is 0.00 or less." },
    { "clock",    G_GAME, true,  true,  C_CLOCK, "clock", "show both clocks", "clock", NULL },
    { "time",     G_GAME, true,  true,  C_TIME,  "time T", "5+3 | 40/90+30 | move 5 | depth 12 | nodes N", "time 5+3, time 10, time 40/90, time move 5, st 5", "5+3: 5 minutes, 3 seconds added a move. Aliases: level T; st S = move S, sd N = depth N, sn N = nodes N." },
    { "load",     G_GAME, true,  true,  C_LOAD,  "load FILE [N]", "load game N (default 1) from a PGN file", "load games.pgn 2, load My Games/today.pgn", "The board shows the start of the game; 'next' steps through it. A move typed on the way cuts the rest off ('load' again brings it back)." },
    { "save",     G_GAME, true,  true,  C_SAVE,  "save [FILE]", "save the game as PGN (default: chessenger-DATE.pgn)", "save game.pgn, save, save My Games/today.pgn", "The moves 'history' lists. A file that exists keeps its games; this one is added at the end." },
    { "first",    G_GAME, true,  true,  C_FIRST, "first|prev|next|last", "step through the game; prev and next take a count", "first", "Steps through the moves of 'history' and those taken back ('load', 'undo'); in a finished game too, not in one going on." },
    { "prev",     G_GAME, true,  true,  C_PREV, "first|prev|next|last", "step through the game; prev and next take a count", "prev, prev 3", "Steps through the moves of 'history' and those taken back ('load', 'undo'); in a finished game too, not in one going on." },
    { "next",     G_GAME, true,  true,  C_NEXT, "first|prev|next|last", "step through the game; prev and next take a count", "next, next 10", "Steps through the moves of 'history' and those taken back ('load', 'undo'); in a finished game too, not in one going on." },
    { "last",     G_GAME, true,  true,  C_LAST, "first|prev|next|last", "step through the game; prev and next take a count", "last", "Steps through the moves of 'history' and those taken back ('load', 'undo'); in a finished game too, not in one going on." },
    { "goto",     G_GAME, true,  true,  C_GOTO,  "goto M", "jump to move M: goto 12 (White), goto 12... (Black)", "goto 12, goto 12..., goto 0", "The position after that move; 'goto 0' = the start. As 'next': not in a game going on." },
    { "history",  G_GAME, true,  true,  C_HISTORY, "history", "list the moves played so far", "history", NULL },

    { "board",    G_POSITION, true, true, C_BOARD, "board", "show the board, side to move, material and FEN", "board  (alias: d)", NULL },
    { "fen",      G_POSITION, true, true, C_FEN, "fen", "print the FEN of the current position", "fen", NULL },
    { "setboard", G_POSITION, true, true, C_SETBOARD, "setboard FEN", "set up a position (UCI 'position' also works)", "setboard 4k3/8/8/8/8/8/4P3/4K3 w - - 0 1", NULL },
    { "moves",    G_POSITION, true, true, C_MOVES, "moves", "list the legal moves", "moves", NULL },
    { "flip",     G_POSITION, true, true, C_FLIP, "flip", "turn the board around", "flip", NULL },

    { "analyze",  G_ANALYSIS, true,  true, C_ANALYZE, "analyze", "analyze until 'stop'; typed moves are played", "analyze", "Scores from White's view. A line you type stops it, runs, and the analysis starts again on the new position; 'stop', Ctrl+C, 'play', 'uci' and 'quit' end it." },
    { "stop",     G_ANALYSIS, true,  true, C_STOP, "stop", "stop analyzing or thinking", "stop  (or Ctrl+C)", "While Chessenger thinks in a game it plays its best move at once." },
    { "go",       G_ANALYSIS, true,  true, C_GO, "go [LIMITS]", "one search: go depth 20 | movetime 5000 | nodes N", "go depth 20, go movetime 5000, go", "Without limits: as the 'time' setting. Lines as in 'analyze'; nothing is played." },
    { "eval",     G_ANALYSIS, true,  true, C_EVAL, "eval", "static evaluation in pawns, White's view", "eval  (alias: score)", "The number the search starts from in this position (no search)." },
    { "mate",     G_ANALYSIS, true,  true, C_MATE, "mate N", "look for a mate in N moves", "mate 3", "Searches 2N-1 half-moves. 'No mate found' is not a proof: the search prunes." },
    { "lines",    G_ANALYSIS, true,  true, C_LINES, "lines N", "show the N best lines", "lines 3  (alias: multipv)", "'analyze' and 'go' print N lines a depth, numbered. It is the option MultiPV, so a game uses it too (more time a move). Alone: the setting (default 1)." },
    { "annotate", G_ANALYSIS, true,  true, C_ANNOTATE, "annotate FILE [S]", "mark ?! ? ?? in a PGN game, S seconds per move", "annotate game.pgn 1, annotate game.pgn, annotate", "Game 1 of FILE, without FILE the game on the board. Marks by the drop of the winning chances; written to FILE-annotated.pgn (added at the end). S also 'depth N'. Ctrl+C stops it and saves what is done." },

    { "skill",    G_ENGINE, true,  true, C_SKILL, "skill N", "strength level 1-10 (10 = full strength)", "skill 5", "Chessenger plays weaker in your games ('play'): level N is about 1000 + 200*N Elo (1 = 1200, 9 = 2800), by a shallower search and a random pick among its best lines. 'analyze', 'go', 'hint' and 'annotate' keep full strength. Alone: the setting." },
    { "elo",      G_ENGINE, true,  true, C_ELO, "elo N", "play at about N Elo, 1200-2800 (see 'help elo')", "elo 1800", "Chessenger plays at about N Elo in your games ('play'). Scale: engine Elo at blitz speed, set by games between engines; against humans not measured. 'strength full' switches it off." },
    { "strength", G_ENGINE, true,  true, C_STRENGTH, "strength full", "back to full strength", "strength full", "Alone: the strength now ('skill', 'elo')." },
    { "hash",     G_ENGINE, true,  true, C_HASH, "hash MB", "hash table size in MB", "hash 256", "Without a number: the current size." },
    { "threads",  G_ENGINE, true,  true, C_THREADS, "threads N", "number of search threads", "threads 4", "Without a number: the current setting." },
#if TB_EMBEDDED

    { "syzygy",   G_ENGINE, true,  true, C_SYZYGY, "syzygy PATH", "use Syzygy endgame tablebases from PATH", "syzygy /home/me/syzygy", "Several folders: separated by ':' (Windows: ';'). Built in: all 3-4 piece tables, on from the start; PATH replaces them, 'syzygy <empty>' goes back to them, 'set SyzygyProbeLimit 0' switches all off. Levels ('skill', 'elo') play without the built-in ones." },
#elif TB_NEXT_TO_ENGINE

    { "syzygy",   G_ENGINE, true,  true, C_SYZYGY, "syzygy PATH", "use Syzygy endgame tablebases from PATH", "syzygy /home/me/syzygy", "Several folders: separated by ':' (Windows: ';'). From the start: the tables in the folder 'tablebases' next to Chessenger, if there is one; PATH replaces them, 'syzygy <empty>' goes back to them, 'set SyzygyProbeLimit 0' switches all off. Levels ('skill', 'elo') play without those next to Chessenger." },
#else
    { "syzygy",   G_ENGINE, true,  true, C_SYZYGY, "syzygy PATH", "use Syzygy endgame tablebases from PATH", "syzygy /home/me/syzygy", "Several folders: separated by ':' (Windows: ';'). Empty path: off." },
#endif
    { "ponder",   G_ENGINE, true,  true, C_PONDER, "ponder on|off", "think on your time during a game", "ponder on, ponder off, ponder", "Off by default. While on, the line you type in a game has no arrows or history (Chessenger thinks). Alone: the setting and this game's guesses." },
    { "set",      G_ENGINE, true,  true, C_SET, "set NAME VALUE", "set any engine option (same as setoption)", "set Move Overhead 100", "Option names as listed by 'options'; 'setoption name ...' also works." },
    { "options",  G_ENGINE, true,  true, C_OPTIONS, "options", "list engine options and their values", "options", NULL },

    { "color",    G_DISPLAY, true,  true, C_COLOR, "color auto|on|off", "colors (auto: terminal only; NO_COLOR is honored)", "color on", "Also: color 16 | 256 | true. Alone: the setting and a sample of the scale." },
    { "theme",    G_DISPLAY, true,  true, C_THEME, "theme NAME", "dark, light, mono or retro", "theme light", NULL },
    { "pieces",   G_DISPLAY, true,  true, C_PIECES, "pieces unicode|ascii", "piece symbols (default: auto)", "pieces ascii", "auto: Unicode when the terminal uses UTF-8, otherwise ASCII." },
    { "pov",      G_DISPLAY, true,  true, C_POV, "pov white|you", "score from White's view or yours in games", "pov you", NULL },
    { "post",     G_DISPLAY, true,  true, C_POST, "post on|off", "show the engine's thinking lines", "post on, post off", "In a game, one line for each depth Chessenger finishes: depth, score (as 'pov'), time, nodes, speed, main line. Off by default." },
    { "width",    G_DISPLAY, true,  true, C_WIDTH, "width N", "line width (default: terminal width or 80)", "width 72", NULL },

    { "bench",    G_TOOLS, false, true, C_LATER, "bench [DEPTH]", "speed test; prints the node signature", "bench", NULL },
    { "perft",    G_TOOLS, false, true, C_LATER, "perft N | divide N", "move generator test, total or per move", "perft 5", NULL },
    { "divide",   G_TOOLS, false, true, C_LATER, "perft N | divide N", "move generator test, total or per move", "divide 3", NULL },

    { "about",    G_INFO, true,  true,  C_ABOUT, "about | version", "name, version, build, authors", "about", NULL },
    { "version",  G_INFO, true,  true,  C_VERSION, "about | version", "name, version, build, authors", "version", NULL },
    { "license",  G_INFO, true,  true,  C_LICENSE, "license", "license (GNU GPL v3) and where to read it", "license", NULL },
    { "help",     G_INFO, true,  false, C_HELP, "help [CMD|GROUP]", "this help", "help board, help game", NULL },
    { "uci",      G_INFO, true,  true,  C_UCI, "uci", "leave the console, back to UCI mode for GUIs", "uci", NULL },
    { "xboard",   G_INFO, true,  true,  C_XBOARD, "xboard","switch to the WinBoard/XBoard protocol", "xboard", NULL },
    { "quit",     G_INFO, true,  true,  C_QUIT, "quit", "leave Chessenger", "quit  (alias: exit)", NULL },
};
#define COMMAND_COUNT ((int)(sizeof(COMMANDS) / sizeof(COMMANDS[0])))

typedef struct {
    const char* alias;
    const char* target;
} ConsoleAlias;

static const ConsoleAlias ALIASES[] = {
    { "d", "board" }, { "exit", "quit" }, { "back", "undo" }, { "score", "eval" },
    { "multipv", "lines" }, { "level", "time" }, { "st", "time" }, { "sd", "time" }, { "sn", "time" },
};
#define ALIAS_COUNT ((int)(sizeof(ALIASES) / sizeof(ALIASES[0])))

static const char* const UCI_PASS[] = { "setoption", "position", "isready", "ucinewgame" };
#define UCI_PASS_COUNT ((int)(sizeof(UCI_PASS) / sizeof(UCI_PASS[0])))

static bool con_on = false;
static bool con_prompt = false;
static bool con_exit_hook = false;

typedef enum { CM_AUTO, CM_ON, CM_OFF, CM_16, CM_256, CM_TRUE } ColorMode;
typedef enum { TH_DARK, TH_LIGHT, TH_MONO, TH_RETRO, TH_COUNT } Theme;
typedef enum { PC_AUTO, PC_UNICODE, PC_ASCII } PieceMode;
static const char* const THEME_NAME[TH_COUNT] = { "dark", "light", "mono", "retro" };

static ColorMode con_color = CM_AUTO;
static Theme con_theme = TH_DARK;
static PieceMode con_pieces = PC_AUTO;
static bool con_pov_you = true;
static int con_width = 0;
static bool con_flipped = false;

typedef struct {
    Move move;
    char san[16];
} HistoryEntry;
static HistoryEntry* hist = NULL;
static int hist_count = 0;
static int hist_cap = 0;
static int hist_base_fullmove = 1;
static bool hist_base_black = false;
static uint64_t hist_hash = 0;
static char hist_base_fen[256] = "";

static int cap_saved_fd = -1;
static int cap_read_fd = -1;
static char cap_text[16384];

typedef enum { SYNC_NONE, SYNC_RESET, SYNC_POSITION } SyncKind;
static SyncKind sync_kind = SYNC_NONE;
static char sync_line[MAX_UCI_LINE];

#define NOTE_MAX 16
static char note_name[NOTE_MAX][64];
static char note_value[NOTE_MAX][256];
static int note_count = 0;

typedef enum { TC_CLOCK, TC_MOVE, TC_DEPTH, TC_NODES } TcKind;
typedef struct {
    TcKind kind;
    long moves;
    long base_ms;
    long inc_ms;
    long value;
} TimeControl;
static TimeControl con_tc = { TC_CLOCK, 0, 5 * 60000L, 3000L, 0 };

static bool game_on = false;
static bool game_over = false;
static Color game_human = WHITE;
static TimeControl game_tc;
static long long game_clock[2];
static long game_moves_done[2];
static struct timeval turn_start;
static bool draw_offer = false;
static bool game_time_end = false;
static char game_result[8] = "*";

static bool game_viewed = false;
static bool game_viewed_time = false;

typedef enum { SK_NONE, SK_GAME, SK_HINT, SK_ANALYZE, SK_GO, SK_MATE, SK_PONDER, SK_ANNOTATE } SearchKind;
static SearchKind search_kind = SK_NONE;
static struct timeval search_started;
static bool res_info = false, res_mate = false, res_move_ok = false;
static int res_cp = 0, res_mate_moves = 0;
static Move res_move;
static char res_refused[96] = "";
static bool con_post = false;
static bool post_rows = false;
static char search_fen[128] = "";
static int bound_cols = 0;

static int res_depth = 0;
static char res_pv[1024] = "";
static bool post_white = false;
static bool analysis_on = false;
static bool analysis_first = false;
static char own_go[96] = "";
static int mate_target = 0;
static bool queued_shown = false;
static volatile sig_atomic_t con_interrupt = 0;
static bool search_interrupted = false;
static int interrupt_count = 0;
static bool con_ponder = false;
static bool ponder_have = false;
static Move ponder_move;
static char ponder_san[16] = "";
static char ponder_uci[8] = "";
static uint64_t ponder_hash = 0;
static bool ponder_paused = false;
static bool ponder_ran = false;
static Position ponder_saved;
static bool ponder_hit = false;
static struct timeval ponder_hit_time;
static char ponder_typed[64] = "";
static bool ponder_echoed = false;
static int ponder_guesses = 0, ponder_hits = 0;

typedef enum { OWN_NEWGAME, OWN_POSITION, OWN_START, OWN_ENGINE, OWN_HINT, OWN_BOARD,
               OWN_ANALYZE, OWN_GO, OWN_MATE, OWN_SHOW, OWN_ANNOTATE, OWN_RESTORE, OWN_QUIT } OwnStep;
#define OWN_MAX 8
static OwnStep own_queue[OWN_MAX];
static int own_count = 0;
static char own_fen[256];
static void own_push(OwnStep step);

static HistoryEntry* redo_stack = NULL;
static int redo_count = 0;
static int redo_cap = 0;

#ifdef _WIN32
static HANDLE w_out = NULL;
static bool w_console = false;
static DWORD w_old_mode = 0;
static bool w_mode_saved = false;
static bool w_vt = false;
static UINT w_old_cp = 0;
static bool w_cp_saved = false;
static bool w_utf8 = false;
static WORD w_old_attr = 0;
static bool w_handler = false;
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

bool console_active(void) { return con_on; }
void console_set_active(bool on) { con_on = on; }

static int line_width(void) {
    if (con_width > 0) return con_width;
    int w = 0;
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (w_out && GetConsoleScreenBufferInfo(w_out, &info)) w = info.srWindow.Right - info.srWindow.Left + 1;
#elif defined(TIOCGWINSZ)
    struct winsize ws;
    if (isatty(STDOUT_FILENO) && ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) w = ws.ws_col;
#endif
    if (w < 40 || w > CON_DEFAULT_WIDTH) w = CON_DEFAULT_WIDTH;
    return w;
}

static bool env_set(const char* name) {
    const char* v = getenv(name);
    return v && v[0] != '\0';
}

#ifndef _WIN32
static bool contains_nocase(const char* s, const char* sub) {
    size_t n = strlen(sub);
    for (; s && *s; s++)
        if (strncasecmp(s, sub, n) == 0) return true;
    return false;
}
#endif

static const char* split_word(const char* s, char* word, size_t n, bool lower) {
    size_t i = 0;
    while (*s == ' ' || *s == '\t') s++;
    while (*s && *s != ' ' && *s != '\t') {
        if (i + 1 < n) word[i++] = lower ? (char)tolower((unsigned char)*s) : *s;
        s++;
    }
    word[i] = '\0';
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static void print_wrapped(const char* text, int indent, const char* prefer) {
    const int width = line_width();
    int used = 0;
    const char* s = text;
    for (;;) {
        const int room = width - used;
        const int len = (int)strlen(s);
        if (len <= room) {
            printf("%s\n", s);
            return;
        }
        int cut = -1;
        if (prefer) {
            const size_t pl = strlen(prefer);
            for (int i = room; i > 0 && cut < 0; i--)
                if (strncmp(s + i, prefer, pl) == 0) cut = i;
        }
        for (int i = room; i > 0 && cut < 0; i--)
            if (s[i] == ' ') cut = i;
        if (cut < 0) cut = room > 0 ? room : 1;
        printf("%.*s\n", cut, s);
        s += cut;
        while (*s == ' ') s++;
        if (!*s) return;
        printf("%*s", indent, "");
        used = indent;
    }
}

static void print_two_part(const char* first, const char* second) {
    if ((int)(strlen(first) + 1 + strlen(second)) <= line_width()) {
        printf("%s %s\n", first, second);
        return;
    }
    print_wrapped(first, 0, NULL);
    print_wrapped(second, 0, NULL);
}

#define ECHO_MAX 40
static const char* echo_word(const char* w, char* buf, size_t n) {
    if (strlen(w) <= ECHO_MAX) snprintf(buf, n, "%s", w);
    else snprintf(buf, n, "%.*s...", ECHO_MAX - 3, w);
    for (char* p = buf; *p; p++)
        if ((unsigned char)*p < 32 || (unsigned char)*p == 127) *p = '?';
    return buf;
}

typedef struct {
    int col, indent, width, sep;
    bool line_has_word;
    bool space_first;
} Wrap;

static void wrap_start(Wrap* w, const char* first, int indent, int sep, bool space_first) {
    w->width = line_width();
    w->indent = indent;
    w->sep = sep;
    w->line_has_word = false;
    w->space_first = space_first;
    fputs(first, stdout);
    w->col = (int)strlen(first);
}

static void wrap_word(Wrap* w, const char* word) {
    int len = (int)strlen(word);
    if (w->line_has_word && w->col + w->sep + len > w->width) {
        printf("\n%*s", w->indent, "");
        w->col = w->indent;
        w->line_has_word = false;
        w->space_first = false;
    }
    if (w->line_has_word || w->space_first) {
        printf("%*s", w->sep, "");
        w->col += w->sep;
    }
    fputs(word, stdout);
    w->col += len;
    w->line_has_word = true;
}

static void wrap_end(Wrap* w) {
    (void)w;
    fputc('\n', stdout);
}

static bool capture_begin(void) {
    if (cap_saved_fd >= 0) return true;
    int fds[2];
    fflush(stdout);
#ifdef _WIN32
    if (_pipe(fds, 65536, _O_BINARY) != 0) return false;
#else
    if (pipe(fds) != 0) return false;
#endif
    int out = fileno(stdout);
    int saved = dup(out);
    if (saved < 0 || dup2(fds[1], out) < 0) {
        if (saved >= 0) close(saved);
        close(fds[0]);
        close(fds[1]);
        return false;
    }
    close(fds[1]);
    cap_saved_fd = saved;
    cap_read_fd = fds[0];
    return true;
}

static void capture_end(void) {
    cap_text[0] = '\0';
    if (cap_saved_fd < 0) return;
    fflush(stdout);
    dup2(cap_saved_fd, fileno(stdout));
    close(cap_saved_fd);
    cap_saved_fd = -1;
    size_t used = 0;
    for (;;) {
        char buf[512];
        int r = (int)read(cap_read_fd, buf, sizeof(buf));
        if (r <= 0) break;
        size_t take = (size_t)r;
        if (used + take >= sizeof(cap_text)) take = sizeof(cap_text) - 1 - used;
        memcpy(cap_text + used, buf, take);
        used += take;
    }
    cap_text[used] = '\0';
    close(cap_read_fd);
    cap_read_fd = -1;
#ifdef _WIN32
    if (w_console) w_out = GetStdHandle(STD_OUTPUT_HANDLE);
#endif
}

static void print_caught(void) {
    const char* s = cap_text;
    while (*s) {
        const char* e = strchr(s, '\n');
        size_t len = e ? (size_t)(e - s) : strlen(s);
        char ln[1024];
        size_t k = len < sizeof(ln) - 1 ? len : sizeof(ln) - 1;
        memcpy(ln, s, k);
        ln[k] = '\0';
        while (k > 0 && (ln[k - 1] == '\r' || ln[k - 1] == ' ')) ln[--k] = '\0';
        const char* body = strncmp(ln, "info string ", 12) == 0 ? ln + 12 : ln;
        int want = 0;
        double got = 0.0;
        char tail_mpv[8];
        if (sscanf(body, "Hash %d MB REQUESTED -> %lf MiB APPLIED", &want, &got) == 2) {
            if (got == (double)want) printf("Hash: %d MB\n", want);
            else printf("Hash: %.1f MB in use (%d MB requested)\n", got, want);
        } else if (sscanf(body, "MultiPV %d%1s", &want, tail_mpv) == 1) {
            printf("Lines: %d\n", want);
        } else if (body[0]) {
            print_wrapped(body, 2, NULL);
        }
        s = e ? e + 1 : s + len;
    }
}

typedef enum {
    R_WHITE, R_BLACK, R_EMPTY, R_FRAME, R_HILITE,
    R_EVAL0, R_GAIN1, R_GAIN2, R_GAIN3, R_GAIN4, R_LOSS1, R_LOSS2, R_LOSS3, R_LOSS4,
    R_COUNT
} Role;

typedef struct {
    signed char bold, faint, reverse;
    short fg16, bg16, fg256, bg256;
} Style;

#define S_(b, f, r, f16, b16, f256, b256) { b, f, r, f16, b16, f256, b256 }
static const Style STYLE[TH_COUNT][R_COUNT] = {
    [TH_DARK] = {
        [R_WHITE]  = S_(1, 0, 0, 97, -1, 231, -1),
        [R_BLACK]  = S_(0, 0, 0, 33, -1, 214, -1),
        [R_EMPTY]  = S_(0, 0, 0, 90, -1, 242, -1),
        [R_FRAME]  = S_(0, 0, 0, 90, -1, 245, -1),
        [R_HILITE] = S_(0, 0, 0, 30, 43, 16, 220),
        [R_EVAL0]  = S_(0, 0, 0, 94, -1, 75, -1),
        [R_GAIN1]  = S_(0, 0, 0, 32, -1, 151, -1),
        [R_GAIN2]  = S_(0, 0, 0, 32, -1, 114, -1),
        [R_GAIN3]  = S_(1, 0, 0, 92, -1, 40, -1),
        [R_GAIN4]  = S_(1, 0, 0, 92, -1, 46, -1),
        [R_LOSS1]  = S_(0, 0, 0, 31, -1, 217, -1),
        [R_LOSS2]  = S_(0, 0, 0, 31, -1, 210, -1),
        [R_LOSS3]  = S_(1, 0, 0, 91, -1, 203, -1),
        [R_LOSS4]  = S_(1, 0, 0, 91, -1, 196, -1),
    },
    [TH_LIGHT] = {
        [R_WHITE]  = S_(1, 0, 0, 34, -1, 18, -1),
        [R_BLACK]  = S_(0, 0, 0, 31, -1, 130, -1),
        [R_EMPTY]  = S_(0, 0, 0, 90, -1, 247, -1),
        [R_FRAME]  = S_(0, 0, 0, 90, -1, 244, -1),
        [R_HILITE] = S_(0, 0, 0, 30, 103, 16, 229),
        [R_EVAL0]  = S_(0, 0, 0, 34, -1, 25, -1),
        [R_GAIN1]  = S_(0, 0, 0, 32, -1, 71, -1),
        [R_GAIN2]  = S_(0, 0, 0, 32, -1, 34, -1),
        [R_GAIN3]  = S_(1, 0, 0, 32, -1, 28, -1),
        [R_GAIN4]  = S_(1, 0, 0, 32, -1, 22, -1),
        [R_LOSS1]  = S_(0, 0, 0, 31, -1, 167, -1),
        [R_LOSS2]  = S_(0, 0, 0, 31, -1, 160, -1),
        [R_LOSS3]  = S_(1, 0, 0, 31, -1, 124, -1),
        [R_LOSS4]  = S_(1, 0, 0, 31, -1, 88, -1),
    },
    [TH_MONO] = {
        [R_WHITE]  = S_(1, 0, 0, -1, -1, -1, -1),
        [R_BLACK]  = S_(0, 0, 0, -1, -1, -1, -1),
        [R_EMPTY]  = S_(0, 1, 0, -1, -1, -1, -1),
        [R_FRAME]  = S_(0, 0, 0, -1, -1, -1, -1),
        [R_HILITE] = S_(0, 0, 1, -1, -1, -1, -1),
        [R_EVAL0]  = S_(0, 0, 0, -1, -1, -1, -1),
        [R_GAIN1]  = S_(0, 0, 0, -1, -1, -1, -1),
        [R_GAIN2]  = S_(0, 0, 0, -1, -1, -1, -1),
        [R_GAIN3]  = S_(1, 0, 0, -1, -1, -1, -1),
        [R_GAIN4]  = S_(1, 0, 0, -1, -1, -1, -1),
        [R_LOSS1]  = S_(0, 0, 0, -1, -1, -1, -1),
        [R_LOSS2]  = S_(0, 0, 0, -1, -1, -1, -1),
        [R_LOSS3]  = S_(1, 0, 0, -1, -1, -1, -1),
        [R_LOSS4]  = S_(1, 0, 0, -1, -1, -1, -1),
    },
    [TH_RETRO] = {
        [R_WHITE]  = S_(1, 0, 0, 92, -1, 46, -1),
        [R_BLACK]  = S_(0, 0, 0, 32, -1, 34, -1),
        [R_EMPTY]  = S_(0, 1, 0, 32, -1, 22, -1),
        [R_FRAME]  = S_(0, 0, 0, 32, -1, 28, -1),
        [R_HILITE] = S_(0, 0, 0, 30, 42, 16, 34),
        [R_EVAL0]  = S_(0, 1, 0, 32, -1, 28, -1),
        [R_GAIN1]  = S_(0, 0, 0, 32, -1, 34, -1),
        [R_GAIN2]  = S_(0, 0, 0, 32, -1, 40, -1),
        [R_GAIN3]  = S_(1, 0, 0, 92, -1, 46, -1),
        [R_GAIN4]  = S_(1, 0, 0, 92, -1, 46, -1),
        [R_LOSS1]  = S_(0, 0, 0, 32, -1, 34, -1),
        [R_LOSS2]  = S_(0, 0, 0, 32, -1, 40, -1),
        [R_LOSS3]  = S_(1, 0, 0, 92, -1, 46, -1),
        [R_LOSS4]  = S_(1, 0, 0, 92, -1, 46, -1),
    },
};
#undef S_

static int detected_depth(void) {
#ifdef _WIN32
    if (w_console) return w_vt ? 256 : 16;
    return 16;
#else
    const char* ct = getenv("COLORTERM");
    if (ct && (strcmp(ct, "truecolor") == 0 || strcmp(ct, "24bit") == 0)) return 24;
    const char* term = getenv("TERM");
    if (term && strstr(term, "256color")) return 256;
    return 16;
#endif
}

static int color_depth(void) {
    switch (con_color) {
        case CM_OFF:  return 0;
        case CM_16:   return 16;
        case CM_256:  return 256;
        case CM_TRUE: return 24;
        case CM_ON:   return detected_depth();
        case CM_AUTO:
        default: {
            if (env_set("NO_COLOR")) return 0;
            if (!isatty(STDOUT_FILENO)) return 0;
            const char* term = getenv("TERM");
            if (term && strcmp(term, "dumb") == 0) return 0;
            return detected_depth();
        }
    }
}

static const char* color_auto_reason(void) {
    if (env_set("NO_COLOR")) return "NO_COLOR is set";
    if (!isatty(STDOUT_FILENO)) return "output is not a terminal";
    const char* term = getenv("TERM");
    if (term && strcmp(term, "dumb") == 0) return "TERM=dumb";
    return "terminal";
}

static void rgb_of_256(int idx, int* r, int* g, int* b) {
    static const int LEVEL[6] = { 0, 95, 135, 175, 215, 255 };
    if (idx >= 16 && idx <= 231) {
        int i = idx - 16;
        *r = LEVEL[i / 36]; *g = LEVEL[(i / 6) % 6]; *b = LEVEL[i % 6];
    } else if (idx >= 232 && idx <= 255) {
        *r = *g = *b = 8 + 10 * (idx - 232);
    } else {
        *r = *g = *b = 128;
    }
}

#ifdef _WIN32

static bool use_win_attr(int depth) {
    return depth == 16 && w_console && !w_vt;
}

static WORD win_color_bits(int sgr) {

    static const WORD MAP[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };
    if (sgr >= 30 && sgr <= 37) return MAP[sgr - 30];
    if (sgr >= 90 && sgr <= 97) return (WORD)(MAP[sgr - 90] | 8);
    if (sgr >= 40 && sgr <= 47) return MAP[sgr - 40];
    if (sgr >= 100 && sgr <= 107) return (WORD)(MAP[sgr - 100] | 8);
    return 0;
}
#endif

static void style_on(Role role, const Style* extra) {
    int depth = color_depth();
    if (depth == 0) return;
    Style s = STYLE[con_theme][role];
    if (extra) {
        if (extra->fg16 >= 0) s.fg16 = extra->fg16;
        if (extra->bg16 >= 0) s.bg16 = extra->bg16;
        if (extra->fg256 >= 0) s.fg256 = extra->fg256;
        if (extra->bg256 >= 0) s.bg256 = extra->bg256;
        if (extra->reverse > 0) s.reverse = 1;
        s.faint = 0;
    }
#ifdef _WIN32
    if (use_win_attr(depth)) {
        fflush(stdout);
        WORD a = w_old_attr;
        if (s.fg16 >= 0) a = (WORD)((a & 0xF0) | win_color_bits(s.fg16));
        if (s.bg16 >= 0) a = (WORD)((a & 0x0F) | (win_color_bits(s.bg16) << 4));
        if (s.bold > 0) a |= FOREGROUND_INTENSITY;
        if (s.reverse > 0) a = (WORD)(((a & 0x0F) << 4) | ((a & 0xF0) >> 4));
        SetConsoleTextAttribute(w_out, a);
        return;
    }
#endif
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "\033[0");
    if (s.bold > 0)    n += snprintf(buf + n, sizeof(buf) - (size_t)n, ";1");
    if (s.faint > 0)   n += snprintf(buf + n, sizeof(buf) - (size_t)n, ";2");
    if (s.reverse > 0) n += snprintf(buf + n, sizeof(buf) - (size_t)n, ";7");
    if (depth == 16) {
        if (s.fg16 >= 0) n += snprintf(buf + n, sizeof(buf) - (size_t)n, ";%d", s.fg16);
        if (s.bg16 >= 0) n += snprintf(buf + n, sizeof(buf) - (size_t)n, ";%d", s.bg16);
    } else if (depth == 256) {
        if (s.fg256 >= 0) n += snprintf(buf + n, sizeof(buf) - (size_t)n, ";38;5;%d", s.fg256);
        if (s.bg256 >= 0) n += snprintf(buf + n, sizeof(buf) - (size_t)n, ";48;5;%d", s.bg256);
    } else {
        int r, g, b;
        if (s.fg256 >= 0) {
            rgb_of_256(s.fg256, &r, &g, &b);
            n += snprintf(buf + n, sizeof(buf) - (size_t)n, ";38;2;%d;%d;%d", r, g, b);
        }
        if (s.bg256 >= 0) {
            rgb_of_256(s.bg256, &r, &g, &b);
            n += snprintf(buf + n, sizeof(buf) - (size_t)n, ";48;2;%d;%d;%d", r, g, b);
        }
    }
    snprintf(buf + n, sizeof(buf) - (size_t)n, "m");
    fputs(buf, stdout);
}

static void style_off(void) {
    int depth = color_depth();
    if (depth == 0) return;
#ifdef _WIN32
    if (use_win_attr(depth)) {
        fflush(stdout);
        SetConsoleTextAttribute(w_out, w_old_attr);
        return;
    }
#endif
    fputs("\033[0m", stdout);
}

static void put_styled(Role role, const char* text) {
    style_on(role, NULL);
    fputs(text, stdout);
    style_off();
}

static Role eval_role(int cp, int mate) {
    if (mate != 0) return mate > 0 ? R_GAIN4 : R_LOSS4;
    int a = cp < 0 ? -cp : cp;
    int level = a < 25 ? 0 : a < 75 ? 1 : a < 150 ? 2 : a < 300 ? 3 : 4;
    if (level == 0) return R_EVAL0;
    return (Role)((cp > 0 ? R_GAIN1 : R_LOSS1) + level - 1);
}

static void format_eval(int cp, int mate, char* buf, size_t n) {
    if (mate != 0) snprintf(buf, n, "%s#%d", mate > 0 ? "+" : "-", mate > 0 ? mate : -mate);
    else if (cp == 0) snprintf(buf, n, "0.00");
    else snprintf(buf, n, "%c%d.%02d", cp > 0 ? '+' : '-', (cp < 0 ? -cp : cp) / 100, (cp < 0 ? -cp : cp) % 100);
}

static void platform_enter(void) {
#ifdef _WIN32
    w_out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    w_console = (w_out != NULL && w_out != INVALID_HANDLE_VALUE && GetConsoleMode(w_out, &mode));
    if (!w_console) return;
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(w_out, &info)) w_old_attr = info.wAttributes;
    else w_old_attr = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    if (!w_mode_saved) { w_old_mode = mode; w_mode_saved = true; }
    w_vt = SetConsoleMode(w_out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;

    if (env_set("WT_SESSION") && !w_utf8) {
        if (!w_cp_saved) { w_old_cp = GetConsoleOutputCP(); w_cp_saved = true; }
        w_utf8 = SetConsoleOutputCP(65001) != 0;
    }
#endif
}

#ifdef _WIN32

static void platform_restore(void) {
    if (w_console && w_old_attr) SetConsoleTextAttribute(w_out, w_old_attr);
    if (w_mode_saved) SetConsoleMode(w_out, w_old_mode);
    if (w_cp_saved) SetConsoleOutputCP(w_old_cp);
}

static BOOL WINAPI console_ctrl_handler(DWORD type) {
    if (type == CTRL_C_EVENT) {
        con_interrupt = 1;
        return TRUE;
    }
    win_stdin_console(0);
    platform_restore();
    return FALSE;
}
#endif

static void platform_leave(void) {
#ifdef _WIN32
    fflush(stdout);
    platform_restore();
    w_mode_saved = false;
    w_vt = false;
    w_cp_saved = false;
    w_utf8 = false;
#endif
}

static void editor_restore(void);
static void echo_off_begin(void);
static void echo_off_end(void);

static void console_at_exit(void) {
    editor_restore();
    if (con_on) {
        style_off();
        if (con_prompt) fputc('\n', stdout);
        fflush(stdout);
    }
    platform_leave();
}

static bool unicode_pieces(void) {
    if (con_pieces == PC_ASCII) return false;
#ifdef _WIN32
    if (con_pieces == PC_UNICODE && !w_utf8 && w_console) {
        if (!w_cp_saved) { w_old_cp = GetConsoleOutputCP(); w_cp_saved = true; }
        w_utf8 = SetConsoleOutputCP(65001) != 0;
    }
    if (con_pieces == PC_UNICODE) return true;
    return w_utf8;
#else
    if (con_pieces == PC_UNICODE) return true;
    const char* loc = NULL;
    if (env_set("LC_ALL")) loc = getenv("LC_ALL");
    else if (env_set("LC_CTYPE")) loc = getenv("LC_CTYPE");
    else if (env_set("LANG")) loc = getenv("LANG");
    return loc && (contains_nocase(loc, "UTF-8") || contains_nocase(loc, "UTF8"));
#endif
}

static void history_reset(Position* pos) {
    hist_count = 0;
    hist_base_fullmove = pos->fullmove_number > 0 ? pos->fullmove_number : 1;
    hist_base_black = (pos->to_move == BLACK);
    hist_hash = pos->hash;
    snprintf(hist_base_fen, sizeof(hist_base_fen), "%s", position_to_fen(pos));
}

static bool history_push(const Move* m, const char* san) {
    if (hist_count == hist_cap) {
        int cap = hist_cap ? hist_cap * 2 : 256;
        HistoryEntry* n = (HistoryEntry*)realloc(hist, (size_t)cap * sizeof(HistoryEntry));
        if (!n) return false;
        hist = n;
        hist_cap = cap;
    }
    hist[hist_count].move = *m;
    snprintf(hist[hist_count].san, sizeof(hist[hist_count].san), "%s", san);
    hist_count++;
    return true;
}

static void history_label(int i, char* buf, size_t n, bool* black) {
    int ply = i + (hist_base_black ? 1 : 0);
    int number = hist_base_fullmove + ply / 2;
    *black = (ply % 2) == 1;
    snprintf(buf, n, *black ? "%d..." : "%d.", number);
}

static void play_on_position(Position* pos, Move* m) {
    if (pos->history_count >= HISTORY_GAME_LIMIT) compact_position_history(pos);
    UndoInfo undo;
    make_move_full(pos, m, &undo);
}

static bool replay_position_line(const char* line, Position* tmp, bool record) {
    const char* moves = strstr(line, " moves");
    if (strstr(line, "startpos")) {
        setup_starting_position(tmp);
    } else {
        const char* f = strstr(line, "fen");
        char fen[256];
        if (!f) return false;
        f += 3;
        while (*f == ' ') f++;
        size_t len = moves ? (size_t)(moves - f) : strlen(f);
        if (len >= sizeof(fen)) return false;
        memcpy(fen, f, len);
        fen[len] = '\0';
        if (!parse_fen(tmp, fen)) return false;
    }
    if (record) history_reset(tmp);
    if (moves) {
        const char* s = moves + 6;
        char tok[16];
        while (*s) {
            s = split_word(s, tok, sizeof(tok), false);
            if (!tok[0]) break;
            Move m;
            char san[16];
            if (parse_move_human(tmp, tok, &m) != SAN_OK || !move_to_san(tmp, m, san, sizeof(san))) break;
            play_on_position(tmp, &m);
            if (record && !history_push(&m, san)) break;
        }
    }
    return true;
}

static void history_from_position_line(Position* pos, const char* line) {
    Position tmp;
    if (replay_position_line(line, &tmp, false) && tmp.hash == pos->hash) {
        replay_position_line(line, &tmp, true);
        hist_hash = pos->hash;
        return;
    }
    if (pos->hash == hist_hash) return;
    history_reset(pos);
}

static void redo_clear(void) { redo_count = 0; }

static void redo_push(const HistoryEntry* e) {
    if (redo_count == redo_cap) {
        int cap = redo_cap ? redo_cap * 2 : 64;
        HistoryEntry* n = (HistoryEntry*)realloc(redo_stack, (size_t)cap * sizeof(HistoryEntry));
        if (!n) return;
        redo_stack = n;
        redo_cap = cap;
    }
    redo_stack[redo_count++] = *e;
}

static void game_stop(void) {
    if (game_on) printf("Game stopped.\n");
    game_on = false;
    game_over = false;
    game_time_end = false;
    draw_offer = false;
    game_viewed = game_viewed_time = false;
}

static void game_view_keep(void) {
    if (game_on && game_over) {
        game_viewed = true;
        game_viewed_time = game_time_end;
    }
}

#define REC_TAGS 32
static bool rec_on = false;
static int rec_tags = 0;
static char rec_name[REC_TAGS][32];
static char rec_value[REC_TAGS][256];
static char rec_file[MAX_UCI_LINE] = "";
static long rec_game = 0;

static void record_clear(void) {
    rec_on = false;
    rec_tags = 0;
    rec_file[0] = '\0';
    rec_game = 0;
    game_viewed = game_viewed_time = false;
}

static void record_set(const char* name, const char* value) {
    int i;
    for (i = 0; i < rec_tags; i++)
        if (strcmp(rec_name[i], name) == 0) break;
    if (i == rec_tags) {
        if (rec_tags == REC_TAGS) return;
        rec_tags++;
    }
    snprintf(rec_name[i], sizeof(rec_name[i]), "%s", name);
    snprintf(rec_value[i], sizeof(rec_value[i]), "%s", value ? value : "");
}

static const char* record_get(const char* name) {
    if (!rec_on) return NULL;
    for (int i = 0; i < rec_tags; i++)
        if (strcmp(rec_name[i], name) == 0) return rec_value[i];
    return NULL;
}

static void history_check(Position* pos) {
    if (pos->hash != hist_hash) {
        game_stop();
        redo_clear();
        history_reset(pos);
        record_clear();
    }
}

static bool piece_at(const Position* pos, int sq, int* color, int* type) {
    uint64_t bit = 1ULL << sq;
    if (!(pos->all_pieces & bit)) return false;
    for (int c = 0; c < 2; c++)
        for (int t = PAWN; t <= KING; t++)
            if (pos->pieces[c][t] & bit) { *color = c; *type = t; return true; }
    return false;
}

static int material_balance(const Position* pos) {
    static const int VALUE[6] = { 1, 5, 3, 3, 9, 0 };
    int sum = 0;
    for (int t = PAWN; t < KING; t++)
        sum += VALUE[t] * (pop_count(pos->pieces[WHITE][t]) - pop_count(pos->pieces[BLACK][t]));
    return sum;
}

typedef enum { ST_NONE, ST_CHECK, ST_MATE, ST_STALEMATE, ST_DRAW_MATERIAL, ST_DRAW_50, ST_DRAW_REP } Status;

static bool bishops_one_colour(const Position* pos) {
    const Bitboard DARK = 0xAA55AA55AA55AA55ULL;
    if (pos->pieces[WHITE][PAWN]   | pos->pieces[BLACK][PAWN]   | pos->pieces[WHITE][ROOK]  | pos->pieces[BLACK][ROOK]
      | pos->pieces[WHITE][KNIGHT] | pos->pieces[BLACK][KNIGHT] | pos->pieces[WHITE][QUEEN] | pos->pieces[BLACK][QUEEN])
        return false;
    const Bitboard b = pos->pieces[WHITE][BISHOP] | pos->pieces[BLACK][BISHOP];
    return b && (!(b & DARK) || !(b & ~DARK));
}

static Status position_status(Position* pos) {
    Move list[MAX_MOVES];
    int n = generate_legal_moves(pos, list);
    bool check = is_in_check(pos, pos->to_move);
    if (n == 0) return check ? ST_MATE : ST_STALEMATE;
    if (bishops_one_colour(pos)) return ST_DRAW_MATERIAL;
    switch (check_game_result(pos)) {
        case DRAW_INSUFFICIENT_MATERIAL: return ST_DRAW_MATERIAL;
        case DRAW_50_MOVES:              return ST_DRAW_50;
        case DRAW_REPETITION:            return ST_DRAW_REP;
        default: break;
    }
    return check ? ST_CHECK : ST_NONE;
}

static const char* status_text(Status st, Color to_move) {
    switch (st) {
        case ST_CHECK:         return "Check.";
        case ST_MATE:          return to_move == WHITE ? "Checkmate. Black wins." : "Checkmate. White wins.";
        case ST_STALEMATE:     return "Stalemate. Draw.";
        case ST_DRAW_MATERIAL: return "Draw by insufficient material.";
        case ST_DRAW_50:       return "Draw by the 50-move rule.";
        case ST_DRAW_REP:      return "Draw by threefold repetition.";
        default:               return "";
    }
}

static bool parse_count(const char* s, size_t n, long lo, long hi, long* out) {
    if (n == 0 || n > 9) return false;
    long v = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return false;
        v = v * 10 + (s[i] - '0');
    }
    if (v < lo || v > hi) return false;
    *out = v;
    return true;
}

static bool parse_clock_text(const char* w, TimeControl* tc) {
    const char* slash = strchr(w, '/');
    const char* plus = strchr(w, '+');
    const char* minutes = w;
    long moves = 0, base = 0, inc = 0;
    if (slash) {
        if (plus && plus < slash) return false;
        if (!parse_count(w, (size_t)(slash - w), 1, 999, &moves)) return false;
        minutes = slash + 1;
    }
    const size_t mlen = plus ? (size_t)(plus - minutes) : strlen(minutes);
    if (!parse_count(minutes, mlen, 1, 999, &base)) return false;
    if (plus && !parse_count(plus + 1, strlen(plus + 1), 0, 999, &inc)) return false;
    tc->kind = TC_CLOCK;
    tc->moves = moves;
    tc->base_ms = base * 60000L;
    tc->inc_ms = inc * 1000L;
    tc->value = 0;
    return true;
}

static bool parse_time_args(const char* arg, const char* alias, TimeControl* tc) {
    char a[32], b[32];
    const char* rest = split_word(arg, a, sizeof(a), true);
    rest = split_word(rest, b, sizeof(b), true);
    if (*rest) return false;
    const char* kind = a;
    const char* num = b;
    if (alias && (strcasecmp(alias, "st") == 0 || strcasecmp(alias, "sd") == 0 || strcasecmp(alias, "sn") == 0)) {
        if (b[0]) return false;
        kind = strcasecmp(alias, "st") == 0 ? "move" : strcasecmp(alias, "sd") == 0 ? "depth" : "nodes";
        num = a;
    }
    long v = 0;
    if (strcmp(kind, "move") == 0) {
        if (!parse_count(num, strlen(num), 1, 3600, &v)) return false;
        *tc = (TimeControl){ TC_MOVE, 0, 0, 0, v };
    } else if (strcmp(kind, "depth") == 0) {
        if (!parse_count(num, strlen(num), 1, MAX_DEPTH, &v)) return false;
        *tc = (TimeControl){ TC_DEPTH, 0, 0, 0, v };
    } else if (strcmp(kind, "nodes") == 0) {
        if (!parse_count(num, strlen(num), 1, 999999999L, &v)) return false;
        *tc = (TimeControl){ TC_NODES, 0, 0, 0, v };
    } else {
        if (b[0]) return false;
        return parse_clock_text(a, tc);
    }
    return true;
}

static void tc_text(const TimeControl* tc, char* buf, size_t n) {
    switch (tc->kind) {
        case TC_MOVE:  snprintf(buf, n, "%ld s a move", tc->value); return;
        case TC_DEPTH: snprintf(buf, n, "depth %ld a move", tc->value); return;
        case TC_NODES: snprintf(buf, n, "%ld nodes a move", tc->value); return;
        case TC_CLOCK:
        default: {
            char inc[24] = "";
            if (tc->inc_ms > 0) snprintf(inc, sizeof(inc), " + %ld s", tc->inc_ms / 1000);
            if (tc->moves > 0) snprintf(buf, n, "%ld moves in %ld min%s", tc->moves, tc->base_ms / 60000, inc);
            else snprintf(buf, n, "%ld min%s", tc->base_ms / 60000, inc);
            return;
        }
    }
}

static long long elapsed_ms(struct timeval since) {
    long long d = time_diff_ms(since, get_wall_time());
    return d < 0 ? 0 : d;
}

static long long clock_now(const Position* pos, Color c) {
    long long t = game_clock[c];
    if (game_on && !game_over && search_kind == SK_NONE && c == game_human && pos->to_move == game_human)
        t -= elapsed_ms(turn_start);
    return t;
}

static void fmt_clock(long long ms, char* buf, size_t n) {
    if (ms < 0) ms = 0;
    long long s = (ms + 999) / 1000;
    if (s >= 3600) snprintf(buf, n, "%lld:%02lld:%02lld", s / 3600, (s / 60) % 60, s % 60);
    else snprintf(buf, n, "%lld:%02lld", s / 60, s % 60);
}

static const char* const GLYPH_WHITE[6] = {
    "\xe2\x99\x99", "\xe2\x99\x96", "\xe2\x99\x98", "\xe2\x99\x97", "\xe2\x99\x95", "\xe2\x99\x94"
};
static const char* const GLYPH_BLACK[6] = {
    "\xe2\x99\x9f", "\xe2\x99\x9c", "\xe2\x99\x9e", "\xe2\x99\x9d", "\xe2\x99\x9b", "\xe2\x99\x9a"
};
#define U_DOT   "\xc2\xb7"
#define U_H     "\xe2\x94\x80"
#define U_V     "\xe2\x94\x82"
#define U_TL    "\xe2\x94\x8c"
#define U_TR    "\xe2\x94\x90"
#define U_BL    "\xe2\x94\x94"
#define U_BR    "\xe2\x94\x98"

static void board_frame(bool uni, bool top) {
    style_on(R_FRAME, NULL);
    if (uni) {
        fputs("      ", stdout);
        fputs(top ? U_TL : U_BL, stdout);
        for (int i = 0; i < 17; i++) fputs(U_H, stdout);
        fputs(top ? U_TR : U_BR, stdout);
    } else {
        fputs("    +-----------------+", stdout);
    }
    style_off();
    fputc('\n', stdout);
}

static void print_board(Position* pos) {
    history_check(pos);
    bool uni = unicode_pieces();
    bool colors = color_depth() > 0;
    int hl_from = -1, hl_to = -1;
    char panel[8][96];
    memset(panel, 0, sizeof(panel));

    snprintf(panel[2], sizeof(panel[2]), "%s to move", pos->to_move == WHITE ? "White" : "Black");
    if (hist_count > 0) {
        const HistoryEntry* e = &hist[hist_count - 1];
        char label[16];
        bool black;
        history_label(hist_count - 1, label, sizeof(label), &black);
        char from[3], to[3];
        snprintf(from, sizeof(from), "%s", square_to_string(e->move.from));
        snprintf(to, sizeof(to), "%s", square_to_string(e->move.to));
        if (colors) {
            snprintf(panel[3], sizeof(panel[3]), "Last move: %s %s", label, e->san);
            hl_from = e->move.from;
            hl_to = e->move.to;
        } else {
            snprintf(panel[3], sizeof(panel[3]), "Last move: %s %s (%s-%s)", label, e->san, from, to);
        }
    }
    int mat = material_balance(pos);
    if (mat == 0) snprintf(panel[4], sizeof(panel[4]), "Material: equal");
    else snprintf(panel[4], sizeof(panel[4]), "Material: %s +%d", mat > 0 ? "White" : "Black", mat > 0 ? mat : -mat);
    snprintf(panel[5], sizeof(panel[5]), "%s", status_text(position_status(pos), pos->to_move));
    if (game_on) {
        const Color top = con_flipped ? WHITE : BLACK;
        for (int k = 0; k < 2; k++) {
            const Color c = k == 0 ? top : (top == WHITE ? BLACK : WHITE);
            char name[48], clk[16] = "";
            if (c == game_human) snprintf(name, sizeof(name), "You");
            else snprintf(name, sizeof(name), "Chessenger %s", ENGINE_VERSION);
            if (game_tc.kind == TC_CLOCK) fmt_clock(clock_now(pos, c), clk, sizeof(clk));
            if (clk[0]) snprintf(panel[k == 0 ? 0 : 7], sizeof(panel[0]), "%s: %-24s%s", c == WHITE ? "White" : "Black", name, clk);
            else snprintf(panel[k == 0 ? 0 : 7], sizeof(panel[0]), "%s: %s", c == WHITE ? "White" : "Black", name);
        }
    }

    Style hl = STYLE[con_theme][R_HILITE];
    board_frame(uni, true);
    for (int row = 0; row < 8; row++) {
        int rank = con_flipped ? row : 7 - row;
        char rank_label[8];
        snprintf(rank_label, sizeof(rank_label), "%s%d ", uni ? "    " : "  ", rank + 1);
        put_styled(R_FRAME, rank_label);
        put_styled(R_FRAME, uni ? U_V : "|");
        fputc(' ', stdout);
        for (int col = 0; col < 8; col++) {
            int file = con_flipped ? 7 - col : col;
            int sq = rank * 8 + file;
            int c = 0, t = 0;
            const char* glyph;
            Role role;
            if (piece_at(pos, sq, &c, &t)) {
                role = c == WHITE ? R_WHITE : R_BLACK;
                if (uni) glyph = c == WHITE ? GLYPH_WHITE[t] : GLYPH_BLACK[t];
                else {
                    static char one[2];
                    one[0] = "PRNBQK"[t];
                    if (c == BLACK) one[0] = (char)tolower((unsigned char)one[0]);
                    one[1] = '\0';
                    glyph = one;
                }
            } else {
                role = R_EMPTY;
                glyph = uni ? U_DOT : ".";
            }
            style_on(role, (sq == hl_from || sq == hl_to) ? &hl : NULL);
            fputs(glyph, stdout);
            style_off();
            fputc(' ', stdout);
        }
        put_styled(R_FRAME, uni ? U_V : "|");
        if (panel[row][0]) printf("  %s", panel[row]);
        fputc('\n', stdout);
    }
    board_frame(uni, false);
    style_on(R_FRAME, NULL);
    fputs(uni ? "        " : "      ", stdout);
    for (int col = 0; col < 8; col++) {
        int file = con_flipped ? 7 - col : col;
        printf("%c%s", 'a' + file, col < 7 ? " " : "");
    }
    style_off();
    fputc('\n', stdout);
    printf("FEN: %s\n", position_to_fen(pos));
}

static const ConsoleCommand* find_command(const char* word) {
    for (int i = 0; i < COMMAND_COUNT; i++)
        if (COMMANDS[i].id != C_MOVE && strcasecmp(COMMANDS[i].name, word) == 0) return &COMMANDS[i];
    return NULL;
}

static const ConsoleCommand* find_command_or_alias(const char* word, const char** alias) {
    *alias = NULL;
    const ConsoleCommand* c = find_command(word);
    if (c) return c;
    for (int i = 0; i < ALIAS_COUNT; i++)
        if (strcasecmp(ALIASES[i].alias, word) == 0) {
            *alias = ALIASES[i].alias;
            return find_command(ALIASES[i].target);
        }
    return NULL;
}

static void help_line(const ConsoleCommand* c) {
    printf("  %-22s %s\n", c->usage, c->text);
}

static void help_overview(void) {

    printf("Chessenger %s console.\n", ENGINE_VERSION);
    printf("Play a game: play white 5+3 (you take White; 5 minutes, plus 3 seconds a move).\n");
    printf("Details: help <command> or help <group>, for example: help play, help game.\n\n");
    for (int g = 0; g < G_COUNT; g++) {
        bool any = false;
        Wrap w;
        for (int i = 0; i < COMMAND_COUNT; i++) {
            const ConsoleCommand* c = &COMMANDS[i];
            if (c->group != g || !c->available || !c->in_grid) continue;
            if (!any) {
                char head[16];
                snprintf(head, sizeof(head), " %-10s", CON_GROUP_NAME[g]);
                wrap_start(&w, head, 11, 2, false);
                any = true;
            }
            wrap_word(&w, c->name);
        }
        if (any) wrap_end(&w);
    }
    printf("\nMoves: SAN (Nf3, exd5, O-O, e8=Q) or coordinates (g1f3, e7e8q).\n");
}

static void help_more(const char* lead, const char* text) {
    char buf[320];
    snprintf(buf, sizeof(buf), "  %s%s", lead, text);
    print_wrapped(buf, 4, NULL);
}

static const struct { const char* word; const char* usage; const char* text; const char* example; } UCI_WORD_HELP[] = {
    { "setoption",  "setoption name N value V", "set an engine option (UCI form of 'set')", "setoption name Hash value 64" },
    { "position",   "position startpos|fen F", "UCI: set a position; 'moves' in coordinates", "position startpos moves e2e4 e7e5" },
    { "isready",    "isready", "UCI: Chessenger answers 'readyok' when it is ready", "isready" },
    { "ucinewgame", "ucinewgame", "UCI: a new game, the hash table is cleared", "ucinewgame" },
};

static const char* strength_note(void);

static bool help_uci_word(const char* topic) {
    for (size_t i = 0; i < sizeof(UCI_WORD_HELP) / sizeof(UCI_WORD_HELP[0]); i++) {
        if (strcasecmp(UCI_WORD_HELP[i].word, topic) != 0) continue;
        printf("  %-22s %s\n", UCI_WORD_HELP[i].usage, UCI_WORD_HELP[i].text);
        help_more("Example: ", UCI_WORD_HELP[i].example);
        help_more("", "A UCI command: it works in the console as in UCI mode.");
        return true;
    }
    return false;
}

static void help_topic(const char* topic) {
    const char* alias;
    const ConsoleCommand* c = find_command_or_alias(topic, &alias);
    if (!c && strcasecmp(topic, "move") == 0) c = &COMMANDS[1];
    if (c) {

        help_line(c);
        help_more("Example: ", c->example);
        if (!c->available) printf("  Not available in this version yet.\n");
        else if (c->more) help_more("", c->more);
        if (c->available && (c->id == C_SKILL || c->id == C_ELO) && strength_note()[0]) {
            char note[96];
            snprintf(note, sizeof(note), "%s.", strength_note());
            note[0] = (char)toupper((unsigned char)note[0]);
            help_more("", note);
        }
        return;
    }
    for (int g = 0; g < G_COUNT; g++) {
        if (strcasecmp(CON_GROUP_NAME[g], topic) != 0) continue;
        const char* last_usage = NULL;
        int shown = 0;
        for (int i = 0; i < COMMAND_COUNT; i++) {
            const ConsoleCommand* k = &COMMANDS[i];
            if (k->group != g || !k->available) continue;
            if (last_usage && strcmp(last_usage, k->usage) == 0) continue;
            help_line(k);
            last_usage = k->usage;
            shown++;
        }
        if (shown == 0) printf("No commands of the group '%s' are available in this version yet.\n", CON_GROUP_NAME[g]);
        help_uci_word(topic);
        return;
    }
    if (help_uci_word(topic)) return;
    char first[160], e[64];
    snprintf(first, sizeof(first), "No help for '%s'.", echo_word(topic, e, sizeof(e)));
    print_two_part(first, "Type 'help' for a list of commands.");
}

void console_uci_help(void) {

    static const struct { const char* word; const char* text; } UCI_HELP[] = {
        { "console", "interactive mode: board, colors, play, analysis" },
        { "xboard",  "switch to the WinBoard/XBoard protocol" },
        { "uci",     "engine name and options (for GUIs)" },
        { "quit",    "exit" },
    };
    printf("Chessenger %s is a UCI chess engine and is waiting for a chess GUI.\n", ENGINE_VERSION);
    for (size_t i = 0; i < sizeof(UCI_HELP) / sizeof(UCI_HELP[0]); i++) {
        const ConsoleCommand* c = find_command(UCI_HELP[i].word);
        bool available = c ? c->available : strcmp(UCI_HELP[i].word, "console") == 0;
        if (available) printf("  %-10s %s\n", UCI_HELP[i].word, UCI_HELP[i].text);
    }
    printf("From the command line: chessenger console");
    if (find_command("play")->available) printf(" | play");
    if (find_command("analyze")->available) printf(" | analyze");
    printf(" | --help\n");
    fflush(stdout);
}

void console_banner_hint(void) {
    printf("Type 'help' for commands, 'console' for the interactive mode.\n");
    fflush(stdout);
}

static void print_usage(void) {
    printf("Chessenger %s, UCI chess engine by Dariusz Domagala (chessengeria.eu)\n", ENGINE_VERSION);
    printf("Usage: chessenger [command]\n");
    printf("  (none)                  UCI mode for chess GUIs (default)\n");
    printf("  console                 interactive mode for humans\n");
    if (find_command("play")->available)
        printf("  play [white|black] [T]  play a game in the terminal (T: 5+3, 10, 40/90)\n");
    if (find_command("analyze")->available)
        printf("  analyze [FEN]           analyze a position (default: start position)\n");
    if (find_command("annotate")->available)
        printf("  annotate FILE [S]       annotate a PGN game, S seconds per move\n");
    printf("  bench [DEPTH]           speed test, prints the node signature\n");
    printf("  version                 print the version and exit\n");
    printf("  help, --help            this text\n");
    printf("Inside the program type 'help' for all commands.\n");
    fflush(stdout);
}

static void print_license(void) {
    printf("Chessenger is free software under the GNU General Public License v3 (GPL-3)\n");
    printf("and comes with ABSOLUTELY NO WARRANTY. Full text: the LICENSE file shipped\n");
    printf("with the program, or https://www.gnu.org/licenses/gpl-3.0.html\n");
    printf("Code from other authors and its licenses: THIRD-PARTY.md.\n");
}

static int edit_distance(const char* a, const char* b) {
    enum { L = 24 };
    int n = (int)strlen(a), m = (int)strlen(b);
    if (n >= L || m >= L) return 99;
    int d[L][L];
    for (int i = 0; i <= n; i++) d[i][0] = i;
    for (int j = 0; j <= m; j++) d[0][j] = j;
    for (int i = 1; i <= n; i++) {
        for (int j = 1; j <= m; j++) {
            int cost = tolower((unsigned char)a[i - 1]) == tolower((unsigned char)b[j - 1]) ? 0 : 1;
            int v = d[i - 1][j] + 1;
            if (d[i][j - 1] + 1 < v) v = d[i][j - 1] + 1;
            if (d[i - 1][j - 1] + cost < v) v = d[i - 1][j - 1] + cost;
            if (i > 1 && j > 1 &&
                tolower((unsigned char)a[i - 1]) == tolower((unsigned char)b[j - 2]) &&
                tolower((unsigned char)a[i - 2]) == tolower((unsigned char)b[j - 1]) &&
                d[i - 2][j - 2] + 1 < v)
                v = d[i - 2][j - 2] + 1;
            d[i][j] = v;
        }
    }
    return d[n][m];
}

static bool is_ep_remark(const char* w) {
    return strcasecmp(w, "ep") == 0 || strcasecmp(w, "e.p") == 0 || strcasecmp(w, "e.p.") == 0;
}

static bool looks_like_move(const char* w) {
    if (is_ep_remark(w)) return true;
    char b[64];
    size_t n = 0;
    bool digit = false;
    for (const char* p = w; *p && n + 1 < sizeof(b); p++) b[n++] = (char)tolower((unsigned char)*p);
    b[n] = '\0';
    if (n == 0) return false;
    for (size_t i = 0; i < n; i++) {
        if (!strchr("abcdefghkqrnpox0123456789=+#!?-.:/()", b[i])) return false;
        if (b[i] >= '0' && b[i] <= '9') digit = true;
    }
    if (digit || b[0] == 'o') return true;
    if (n > 4) return false;
    for (size_t i = strchr("kqrnb", b[0]) ? 1 : 0; i < n; i++)
        if (!((b[i] >= 'a' && b[i] <= 'h') || b[i] == 'x')) return false;
    return true;
}

static const char* suggest_command(const char* word) {
    const char* best = NULL;
    int best_d = 3;
    for (int i = 0; i < COMMAND_COUNT; i++) {
        const ConsoleCommand* c = &COMMANDS[i];
        if (!c->available || c->id == C_MOVE) continue;
        int d = edit_distance(word, c->name);
        if (d < best_d) { best_d = d; best = c->name; }
    }
    return best;
}

static void unknown_command(const char* word) {
    char first[200], e[64];
    const char* hint = looks_like_move(word) ? NULL : suggest_command(word);
    echo_word(word, e, sizeof(e));
    if (hint) {
        snprintf(first, sizeof(first), "Unknown command: '%s'. Did you mean '%s'?", e, hint);
        print_wrapped(first, 0, NULL);
        printf("Type 'help' for a list of commands.\n");
    } else {
        snprintf(first, sizeof(first), "Unknown command: '%s'.", e);
        print_two_part(first, "Type 'help' for a list of commands.");
    }
}

static void cmd_moves(Position* pos) {
    history_check(pos);
    Move list[MAX_MOVES];
    int n = generate_legal_moves(pos, list);
    if (n == 0) {
        printf("No legal moves: %s\n", is_in_check(pos, pos->to_move) ? "checkmate." : "stalemate.");
        return;
    }
    char head[32];
    snprintf(head, sizeof(head), "Legal moves (%d):", n);
    Wrap w;
    wrap_start(&w, head, 2, 1, true);
    for (int i = 0; i < n; i++) {
        char san[16];
        if (!move_to_san(pos, list[i], san, sizeof(san))) snprintf(san, sizeof(san), "%s", move_to_string(&list[i]));
        wrap_word(&w, san);
    }
    wrap_end(&w);
}

static void cmd_history(Position* pos) {
    history_check(pos);
    if (hist_count == 0) {
        printf("No moves played yet.\n");
        return;
    }

    Wrap w;
    wrap_start(&w, "", 0, 1, false);
    for (int i = 0; i < hist_count; i++) {
        char label[16], token[64];
        bool black;
        history_label(i, label, sizeof(label), &black);
        if (!black && i + 1 < hist_count) {
            snprintf(token, sizeof(token), "%s %s %s", label, hist[i].san, hist[i + 1].san);
            i++;
        } else {
            snprintf(token, sizeof(token), "%s %s", label, hist[i].san);
        }
        wrap_word(&w, token);
    }
    wrap_end(&w);
}

static int candidate_key(const Move* m) {
    int promo = m->promotion == QUEEN ? 0 : m->promotion == ROOK ? 1 : m->promotion == BISHOP ? 2 :
                m->promotion == KNIGHT ? 3 : 0;
    return (m->from * 64 + m->to) * 4 + promo;
}

static void report_candidates(const Position* pos, const char* tok, Move* list, int count) {
    for (int i = 1; i < count; i++) {
        Move m = list[i];
        int j = i - 1;
        while (j >= 0 && candidate_key(&list[j]) > candidate_key(&m)) {
            list[j + 1] = list[j];
            j--;
        }
        list[j + 1] = m;
    }
    char text[1024], first[200], e[64];
    size_t n = (size_t)snprintf(text, sizeof(text), "Did you mean");
    for (int i = 0; i < count && n < sizeof(text); i++) {
        char san[16];
        if (!move_to_san(pos, list[i], san, sizeof(san))) snprintf(san, sizeof(san), "%s", move_to_string(&list[i]));
        size_t k = strlen(san);
        while (k > 0 && (san[k - 1] == '+' || san[k - 1] == '#')) san[--k] = '\0';
        const char* sep = i == 0 ? " " : i == count - 1 ? " or " : ", ";
        n += (size_t)snprintf(text + n, sizeof(text) - n, "%s%s", sep, san);
    }
    if (n < sizeof(text)) snprintf(text + n, sizeof(text) - n, "?");
    snprintf(first, sizeof(first), "Ambiguous move: '%s'.", echo_word(tok, e, sizeof(e)));
    print_two_part(first, text);
}

static bool typed_move_read(const Position* pos, const char* tok, Move* m) {
    Move list[MAX_MOVES];
    int count = 0;
    bool shortcut = false;
    SanResult r = san_read_move(pos, tok, list, &count, &shortcut);
    char first[200], e[64];
    (void)shortcut;
    if (r == SAN_OK) {
        *m = list[0];
        return true;
    }
    if (r == SAN_ILLEGAL) {
        snprintf(first, sizeof(first), "Illegal move: '%s'.", echo_word(tok, e, sizeof(e)));
        print_two_part(first, "Type 'moves' to list the legal moves.");
    } else if (r == SAN_AMBIGUOUS) {
        report_candidates(pos, tok, list, count);
    } else {
        unknown_command(tok);
    }
    return false;
}

static void typed_move_play(Position* pos, Move* m, bool report, bool status) {
    char san[16];
    if (!move_to_san(pos, *m, san, sizeof(san))) snprintf(san, sizeof(san), "%s", move_to_string(m));
    play_on_position(pos, m);
    history_push(m, san);
    hist_hash = pos->hash;
    redo_clear();
    if (report) {
        char label[16];
        bool black;
        history_label(hist_count - 1, label, sizeof(label), &black);
        Status st_now = status ? position_status(pos) : ST_NONE;
        const char* st = st_now == ST_CHECK ? "" : status_text(st_now, pos->to_move);
        printf("%s %s%s%s\n", label, san, st[0] ? "  " : "", st);
    }
}

static bool play_typed_move(Position* pos, const char* tok, bool report) {
    Move m;
    if (!typed_move_read(pos, tok, &m)) return false;
    typed_move_play(pos, &m, report, true);
    return true;
}

static void cmd_typed_moves(Position* pos, const char* line) {
    history_check(pos);
    char tok[64];
    const char* s = line;
    bool played = false;
    while (*s) {
        s = split_word(s, tok, sizeof(tok), false);
        if (!tok[0]) break;
        if (played && is_ep_remark(tok)) continue;
        if (!play_typed_move(pos, tok, true)) break;
        played = true;
    }
    if (played) record_clear();
}

static void cmd_color(const char* arg) {
    char w[16];
    split_word(arg, w, sizeof(w), true);
    if (w[0]) {
        if (strcmp(w, "auto") == 0) con_color = CM_AUTO;
        else if (strcmp(w, "on") == 0) con_color = CM_ON;
        else if (strcmp(w, "off") == 0) con_color = CM_OFF;
        else if (strcmp(w, "16") == 0) con_color = CM_16;
        else if (strcmp(w, "256") == 0) con_color = CM_256;
        else if (strcmp(w, "true") == 0 || strcmp(w, "truecolor") == 0) con_color = CM_TRUE;
        else {
            printf("Unknown color setting: '%s'. Use auto, on, off, 16, 256 or true.\n", w);
            return;
        }
    }
    static const char* const MODE[] = { "auto", "on", "off", "16", "256", "true" };
    int depth = color_depth();
    char depth_text[48];
    if (depth == 0) snprintf(depth_text, sizeof(depth_text), "no colors");
    else if (depth == 24) snprintf(depth_text, sizeof(depth_text), "truecolor");
    else snprintf(depth_text, sizeof(depth_text), "%d colors", depth);
    if (con_color == CM_AUTO)
        printf("Colors: auto (%s: %s), theme %s\n", color_auto_reason(), depth_text, THEME_NAME[con_theme]);
    else
        printf("Colors: %s (%s), theme %s\n", MODE[con_color], depth_text, THEME_NAME[con_theme]);
    if (w[0]) return;

    static const struct { int cp, mate; } SAMPLE[] = {
        { 0, -3 }, { -320, 0 }, { -200, 0 }, { -100, 0 }, { -40, 0 }, { 0, 0 },
        { 40, 0 }, { 100, 0 }, { 200, 0 }, { 320, 0 }, { 0, 3 },
    };
    fputs("Scale:", stdout);
    for (size_t i = 0; i < sizeof(SAMPLE) / sizeof(SAMPLE[0]); i++) {
        char buf[16];
        format_eval(SAMPLE[i].cp, SAMPLE[i].mate, buf, sizeof(buf));
        fputc(' ', stdout);
        put_styled(eval_role(SAMPLE[i].cp, SAMPLE[i].mate), buf);
    }
    fputc('\n', stdout);
}

static void cmd_theme(const char* arg) {
    char w[16];
    split_word(arg, w, sizeof(w), true);
    if (!w[0]) {
        printf("Theme: %s (dark, light, mono, retro)\n", THEME_NAME[con_theme]);
        return;
    }
    for (int t = 0; t < TH_COUNT; t++)
        if (strcmp(w, THEME_NAME[t]) == 0) {
            con_theme = (Theme)t;
            printf("Theme: %s\n", THEME_NAME[t]);
            return;
        }
    printf("Unknown theme: '%s'. Themes: dark, light, mono, retro.\n", w);
}

static void cmd_pieces(const char* arg) {
    char w[16];
    split_word(arg, w, sizeof(w), true);
    if (strcmp(w, "unicode") == 0) con_pieces = PC_UNICODE;
    else if (strcmp(w, "ascii") == 0) con_pieces = PC_ASCII;
    else if (strcmp(w, "auto") == 0) con_pieces = PC_AUTO;
    else if (w[0]) {
        printf("Unknown piece setting: '%s'. Use unicode, ascii or auto.\n", w);
        return;
    }
    static const char* const NAME[] = { "auto", "unicode", "ascii" };
    printf("Pieces: %s (%s now)\n", NAME[con_pieces], unicode_pieces() ? "Unicode" : "ASCII");
}

static void cmd_pov(const char* arg) {
    char w[16];
    split_word(arg, w, sizeof(w), true);
    if (strcmp(w, "white") == 0) con_pov_you = false;
    else if (strcmp(w, "you") == 0) con_pov_you = true;
    else if (w[0]) {
        printf("Unknown point of view: '%s'. Use white or you.\n", w);
        return;
    }
    printf("Scores in games: %s\n", con_pov_you ? "your view (you)" : "White's view (white)");
}

static void cmd_width(const char* arg) {
    char w[16];
    split_word(arg, w, sizeof(w), true);
    if (w[0]) {
        char* end;
        long v = strtol(w, &end, 10);
        if (*end != '\0' || v < 40 || v > 250) {
            printf("Line width: a number from 40 to 250 (now %d).\n", line_width());
            return;
        }
        con_width = (int)v;
    }
    printf("Line width: %d\n", line_width());
}

static void cmd_options(void) {
    printf("Engine options (change with 'set NAME VALUE'):\n");
    for (int i = 0;; i++) {
        const char* name;
        char value[256];
        bool known = true;
        if (!console_option_at(i, &name, value, sizeof(value), &known)) break;
        if (!known)
            for (int k = 0; k < note_count; k++)
                if (strcasecmp(note_name[k], name) == 0) snprintf(value, sizeof(value), "%s", note_value[k]);
        if (strcmp(name, "Clear Hash") == 0) snprintf(value, sizeof(value), "(button)");
        int room = line_width() - 21;
        if ((int)strlen(value) > room) printf("  %-18s %.*s...\n", name, room - 3, value);
        else printf("  %-18s %s\n", name, value);
    }
}

static void show_option(const char* option) {
    for (int i = 0;; i++) {
        const char* name;
        char value[256];
        bool known = true;
        if (!console_option_at(i, &name, value, sizeof(value), &known)) break;
        if (strcasecmp(name, option) != 0) continue;
        if (!known)
            for (int k = 0; k < note_count; k++)
                if (strcasecmp(note_name[k], name) == 0) snprintf(value, sizeof(value), "%s", note_value[k]);
        int room = line_width() - (int)strlen(name) - 2;
        if ((int)strlen(value) > room) printf("%s: %.*s...\n", name, room - 3, value);
        else printf("%s: %s\n", name, value);
        return;
    }
}

static ConsoleAction cmd_set(const char* arg, char* line, size_t size) {
    static char rest[MAX_UCI_LINE];
    snprintf(rest, sizeof(rest), "%s", arg);
    if (!rest[0]) {
        printf("Usage: set NAME VALUE (for example: set Move Overhead 100). Names: 'options'.\n");
        return CONSOLE_DONE;
    }
    size_t best = 0;
    for (int i = 0;; i++) {
        const char* name;
        char value[256];
        bool known;
        if (!console_option_at(i, &name, value, sizeof(value), &known)) break;
        size_t n = strlen(name);
        if (n > best && strncasecmp(rest, name, n) == 0 && (rest[n] == '\0' || rest[n] == ' ')) best = n;
    }
    if (best == 0) {
        char* sp = strrchr(rest, ' ');
        best = sp ? (size_t)(sp - rest) : strlen(rest);
    }
    const char* value = rest + best;
    while (*value == ' ') value++;
    rest[best] = '\0';
    if (*value) snprintf(line, size, "setoption name %s value %s", rest, value);
    else snprintf(line, size, "setoption name %s", rest);
    capture_begin();
    return CONSOLE_PASS;
}

static ConsoleAction cmd_engine_option(const char* option, const char* arg, char* line, size_t size) {
    static char value[MAX_UCI_LINE];
    snprintf(value, sizeof(value), "%s", arg);
    if (!value[0]) {
        show_option(option);
        return CONSOLE_DONE;
    }
    snprintf(line, size, "setoption name %s value %s", option, value);
    capture_begin();
    return CONSOLE_PASS;
}

static ConsoleAction cmd_lines(const char* arg, char* line, size_t size) {
    char w[16];
    split_word(arg, w, sizeof(w), false);
    long n = 0;
    if (!w[0]) {
        for (int i = 0;; i++) {
            const char* name;
            char value[64];
            bool known;
            if (!console_option_at(i, &name, value, sizeof(value), &known)) break;
            if (strcmp(name, "MultiPV") == 0) printf("Lines: %s\n", value);
        }
        return CONSOLE_DONE;
    }
    if (!parse_count(w, strlen(w), 1, MAX_MULTIPV, &n)) {
        printf("Usage: lines N, N = 1-%d (default 1).\n", MAX_MULTIPV);
        return CONSOLE_DONE;
    }
    snprintf(line, size, "setoption name MultiPV value %ld", n);
    capture_begin();
    return CONSOLE_PASS;
}

static const char* ep_drop_reason(const char* ep, const Position* p) {
    if (strlen(ep) != 2 || ep[0] < 'a' || ep[0] > 'h' || ep[1] < '1' || ep[1] > '8') return "not a square";
    const int sq = (ep[1] - '1') * 8 + (ep[0] - 'a');
    const bool white = (p->to_move == WHITE);
    if (ep[1] != (white ? '6' : '3')) return white ? "with White to move it must be on rank 6" :
                                                     "with Black to move it must be on rank 3";
    const int pawn = white ? sq - 8 : sq + 8;
    const int from = white ? sq + 8 : sq - 8;
    if (!(p->pieces[white ? BLACK : WHITE][PAWN] & (1ULL << pawn))) return "no pawn has just moved two squares past it";
    if (p->all_pieces & ((1ULL << sq) | (1ULL << from))) return "the square or the one behind it is occupied";
    return "no double pawn push can have left it";
}

static void note_dropped_fields(const char* fen, const Position* p) {
    if (uci_chess960) return;
    char board[96], side[8], castling[16], ep[8];
    const char* f = split_word(fen, board, sizeof(board), false);
    f = split_word(f, side, sizeof(side), false);
    f = split_word(f, castling, sizeof(castling), false);
    split_word(f, ep, sizeof(ep), false);
    static const struct { char letter; int bit; } RIGHT[4] = {
        { 'K', WHITE_KINGSIDE }, { 'Q', WHITE_QUEENSIDE }, { 'k', BLACK_KINGSIDE }, { 'q', BLACK_QUEENSIDE },
    };
    char lost[8];
    int n = 0;
    for (const char* c = castling; *c && n < 4; c++)
        for (int i = 0; i < 4; i++)
            if (*c == RIGHT[i].letter && !(p->castling_rights & RIGHT[i].bit)) lost[n++] = *c;
    lost[n] = '\0';
    const bool ep_lost = ep[0] && strcmp(ep, "-") != 0 && p->en_passant_square < 0;
    char note[200];
    if (n > 0 && ep_lost)
        snprintf(note, sizeof(note), "Note: castling rights '%s' and en passant square '%s' dropped.", lost, ep);
    else if (n > 0)
        snprintf(note, sizeof(note), "Note: castling rights '%s' dropped (no king or rook on its square).", lost);
    else if (ep_lost)
        snprintf(note, sizeof(note), "Note: en passant square '%s' dropped (%s).", ep, ep_drop_reason(ep, p));
    else
        return;
    print_wrapped(note, 2, NULL);
}

static ConsoleAction cmd_setboard(Position* pos, const char* arg, char* line, size_t size) {
    static char fen[MAX_UCI_LINE];
    snprintf(fen, sizeof(fen), "%s", arg);
    if (!fen[0]) {
        printf("Usage: setboard FEN (for example: setboard 4k3/8/8/8/8/8/4P3/4K3 w - - 0 1)\n");
        return CONSOLE_DONE;
    }

    Position tmp = *pos;
    const char* why = NULL;
    char extra[96];
    if (strlen(fen) >= 256) {
        why = "too long";
    } else if (!parse_fen(&tmp, fen)) {
        why = fen_last_error();
    } else {
        const char* f = fen;
        for (int field = 0; field < 6 && *f; field++) {
            char w[256];
            f = split_word(f, w, sizeof(w), false);
        }
        if (*f) {
            char e[64];
            snprintf(extra, sizeof(extra), "unexpected text after the six fields: '%s'", echo_word(f, e, sizeof(e)));
            why = extra;
        } else if ((tmp.pieces[WHITE][PAWN] | tmp.pieces[BLACK][PAWN]) & 0xFF000000000000FFULL) {
            why = "a pawn on the first or last rank";
        } else if (is_in_check(&tmp, tmp.to_move == WHITE ? BLACK : WHITE)) {
            why = "the side not to move is in check";
        }
    }
    if (why) {
        char first[200];
        snprintf(first, sizeof(first), "Invalid FEN: %s.", why);
        if ((int)strlen(first) <= line_width()) {
            print_two_part(first, "The position is unchanged.");
        } else {
            char reason[200];
            snprintf(reason, sizeof(reason), "%s.", why);
            printf("Invalid FEN:\n");
            print_wrapped(reason, 0, NULL);
            printf("The position is unchanged.\n");
        }
        return CONSOLE_DONE;
    }
    game_stop();
    redo_clear();
    record_clear();
    printf("Position set.\n");
    note_dropped_fields(fen, &tmp);
    snprintf(line, size, "position fen %s", fen);
    sync_kind = SYNC_RESET;
    capture_begin();
    return CONSOLE_PASS;
}

static Color history_mover(int i) {
    return ((i + (hist_base_black ? 1 : 0)) % 2) == 0 ? WHITE : BLACK;
}

static void say_game_over(void) {
    if (game_time_end) printf("The game is over. Type 'new' to play again.\n");
    else printf("The game is over. Type 'new' to play again or 'undo' to take back.\n");
}

static ConsoleAction cmd_undo(Position* pos, const char* arg, char* line, size_t size) {
    history_check(pos);
    char w[16];
    split_word(arg, w, sizeof(w), false);
    long n = 1;
    if (w[0] && !parse_count(w, strlen(w), 1, 999999999L, &n)) {
        if (game_on) printf("Usage: undo [N], N = your moves to take back with the replies (default 1).\n");
        else printf("Usage: undo [N], N = half-moves to take back (default 1).\n");
        return CONSOLE_DONE;
    }
    if (game_on && game_over && game_time_end) {
        if (strncmp(game_result, "1/2", 3) == 0) printf("The game ended on time. Type 'new' to play again.\n");
        else printf("The game was lost on time. Type 'new' to play again.\n");
        return CONSOLE_DONE;
    }
    if (hist_count == 0) {
        printf("No moves to take back.\n");
        return CONSOLE_DONE;
    }
    int keep;
    if (game_on) {
        int idx = hist_count;
        long units = 0;
        while (units < n) {
            int i = idx;
            if (i > 0 && history_mover(i - 1) != game_human) i--;
            if (i > 0 && history_mover(i - 1) == game_human) i--;
            else break;
            idx = i;
            units++;
        }
        if (units == 0) {
            printf("No move of yours to take back.\n");
            return CONSOLE_DONE;
        }
        if (units < n) {
            printf("Cannot take back %ld of your moves: only %ld played.\n", n, units);
            return CONSOLE_DONE;
        }
        keep = idx;
    } else {
        if (n > hist_count) {
            printf("Cannot take back %ld half-moves: only %d played.\n", n, hist_count);
            return CONSOLE_DONE;
        }
        keep = hist_count - (int)n;
    }
    const int taken = hist_count - keep;
    char base[256];
    snprintf(base, sizeof(base), "%s", hist_base_fen);
    size_t len = (size_t)snprintf(line, size, "position fen %s%s", base, keep > 0 ? " moves" : "");
    for (int i = 0; i < keep && len < size; i++)
        len += (size_t)snprintf(line + len, size - len, " %s", move_to_string(&hist[i].move));
    if (len >= size) {
        printf("The game is too long to take moves back.\n");
        line[0] = '\0';
        return CONSOLE_DONE;
    }
    for (int i = hist_count - 1; i >= keep; i--) redo_push(&hist[i]);
    const bool white = ((keep + (hist_base_black ? 1 : 0)) % 2) == 0;
    printf("Took back %d half-move%s. %s to move.\n", taken, taken == 1 ? "" : "s", white ? "White" : "Black");
    if (game_on) {
        if (game_over) {
            printf("Game resumed.\n");
            turn_start = get_wall_time();
        }
        game_over = false;
        draw_offer = false;
        own_count = 0;
        own_push(OWN_BOARD);
    }
    snprintf(sync_line, sizeof(sync_line), "%s", line);
    sync_kind = SYNC_POSITION;
    capture_begin();
    return CONSOLE_PASS;
}

static void own_push(OwnStep step) {
    if (own_count < OWN_MAX) own_queue[own_count++] = step;
}

static bool own_pop(OwnStep* step) {
    if (own_count == 0) return false;
    *step = own_queue[0];
    memmove(own_queue, own_queue + 1, (size_t)(own_count - 1) * sizeof(own_queue[0]));
    own_count--;
    return true;
}

bool console_takes_search_output(void) {
    return search_kind != SK_NONE || xboard_takes_search_output();
}

#define STRENGTH_SKILL_MAX   20
#define STRENGTH_SKILL_TOP   10
#define STRENGTH_ELO_MIN     1200
#define STRENGTH_ELO_MAX     2800
#define STRENGTH_ELO_STEP    200

static int str_skill = STRENGTH_SKILL_TOP;
static bool str_limit = false;
static int str_elo = STRENGTH_ELO_MIN;
static int str_env = -1;
static double str_env_level = 0.0;

static bool strength_env(double* level) {
    if (str_env < 0) {
        const char* env = getenv("CHESSENGER_LEVEL");
        str_env = 0;
        if (env && env[0]) {
            char* end = NULL;
            errno = 0;
            const double v = strtod(env, &end);
            if (end != env && *end == '\0' && errno != ERANGE && v >= 0.0 && v <= STRENGTH_SKILL_MAX) {
                str_env = 1;
                str_env_level = v;
            }
        }
    }
    if (str_env == 1) *level = str_env_level;
    return str_env == 1;
}

static int strength_elo_of_skill(int skill) {
    return STRENGTH_ELO_MIN + STRENGTH_ELO_STEP * (skill - 1);
}

static bool strength_limited(void) {
    double env = 0.0;
    if (strength_env(&env)) return env < STRENGTH_SKILL_MAX;
    return str_limit || str_skill < STRENGTH_SKILL_TOP;
}

static const char* strength_note(void) {
    return "calibrated in 2300 games at 2+0.2, classic evaluation";
}

void console_strength_uci_options(void) {
    printf("option name Skill Level type spin default %d min 1 max %d\n", STRENGTH_SKILL_TOP, STRENGTH_SKILL_TOP);
    printf("option name UCI_LimitStrength type check default false\n");
    printf("option name UCI_Elo type spin default %d min %d max %d\n", STRENGTH_ELO_MIN, STRENGTH_ELO_MIN, STRENGTH_ELO_MAX);
}

bool console_strength_setoption(const char* name, const char* value) {
    if (strcasecmp(name, "UCI_LimitStrength") == 0) {
        if (strcmp(value, "true") == 0 || strcmp(value, "false") == 0) {
            str_limit = value[0] == 't';
            printf("info string UCI_LimitStrength %s\n", str_limit ? "true" : "false");
        } else {
            printf("info string setoption UCI_LimitStrength: value `%s` is neither `true` nor `false`, option ignored\n",
                   value);
        }
        return true;
    }
    const bool skill = strcasecmp(name, "Skill Level") == 0;
    if (!skill && strcasecmp(name, "UCI_Elo") != 0) return false;
    const char* nm = skill ? "Skill Level" : "UCI_Elo";
    const long lo = skill ? 1 : STRENGTH_ELO_MIN, hi = skill ? STRENGTH_SKILL_TOP : STRENGTH_ELO_MAX;
    if (!value[0]) {
        printf("info string setoption %s: missing value, option ignored\n", nm);
        return true;
    }
    char* end = NULL;
    errno = 0;
    const long v = strtol(value, &end, 10);
    if (end == value || *end != '\0' || errno == ERANGE) {
        printf("info string setoption %s: value `%s` is not a number, option ignored\n", nm, value);
        return true;
    }
    const long applied = v < lo ? lo : v > hi ? hi : v;
    if (skill) str_skill = (int)applied;
    else str_elo = (int)applied;
    const char* tail = (!skill && !str_limit) ? " (active when UCI_LimitStrength is true)" :
                       (skill && str_limit) ? " (UCI_Elo sets the level while UCI_LimitStrength is true)" : "";
    if (applied != v) printf("info string %s %ld REQUESTED -> %ld APPLIED (range %ld..%ld)%s\n", nm, v, applied, lo, hi, tail);
    else printf("info string %s %ld%s\n", nm, applied, tail);
    return true;
}

bool console_strength_value(const char* name, char* value, size_t n) {
    if (strcmp(name, "Skill Level") == 0) snprintf(value, n, "%d", str_skill);
    else if (strcmp(name, "UCI_LimitStrength") == 0) snprintf(value, n, "%s", str_limit ? "true" : "false");
    else if (strcmp(name, "UCI_Elo") == 0) snprintf(value, n, "%d", str_elo);
    else return false;
    return true;
}

const LevelStrength* console_strength_go(bool bench) {
    static LevelStrength strength_cfg;
    if (bench || !strength_limited() || (con_on && search_kind != SK_GAME)) return NULL;
    strength_cfg.skill = str_skill;
    strength_cfg.limit = str_limit;
    strength_cfg.elo = str_elo;
    strength_cfg.env = strength_env(&strength_cfg.env_level);
    return &strength_cfg;
}

const LevelStrength* console_strength_game(void) {
    static LevelStrength strength_cfg;
    if (!game_on || !strength_limited()) return NULL;
    strength_cfg.skill = str_skill;
    strength_cfg.limit = str_limit;
    strength_cfg.elo = str_elo;
    strength_cfg.env = strength_env(&strength_cfg.env_level);
    return &strength_cfg;
}

static void strength_text(char* buf, size_t n) {
    double env = 0.0;
    if (!strength_limited()) snprintf(buf, n, "full strength");
    else if (strength_env(&env)) snprintf(buf, n, "level %.2f (CHESSENGER_LEVEL)", env);
    else if (!str_limit) snprintf(buf, n, "skill %d", str_skill);
    else snprintf(buf, n, "about %d Elo", str_elo);
}

static void strength_status(void) {
    char text[200];
    const char* note = strength_note();
    const char* sep = note[0] ? ", " : "";
    double env = 0.0;
    if (strength_env(&env))
        snprintf(text, sizeof(text), "Strength: %slevel %.2f (CHESSENGER_LEVEL%s%s).", strength_limited() ? "" : "full, ",
                 env, strength_limited() ? sep : "", strength_limited() ? note : "");
    else if (!strength_limited())
        snprintf(text, sizeof(text), "Strength: full.");
    else if (!str_limit)
        snprintf(text, sizeof(text), "Strength: skill level %d (about %d Elo%s%s).", str_skill,
                 strength_elo_of_skill(str_skill), sep, note);
    else
        snprintf(text, sizeof(text), "Strength: about %d Elo%s%s%s.", str_elo, note[0] ? " (" : "", note, note[0] ? ")" : "");
    print_wrapped(text, 2, NULL);
}

static void cmd_skill(const char* arg) {
    char w[16];
    split_word(arg, w, sizeof(w), false);
    long v = 0;
    if (w[0]) {
        if (!parse_count(w, strlen(w), 1, STRENGTH_SKILL_TOP, &v)) {
            printf("Usage: skill N, N = 1-%d (%d = full strength).\n", STRENGTH_SKILL_TOP, STRENGTH_SKILL_TOP);
            return;
        }
        str_skill = (int)v;
        str_limit = false;
    }
    strength_status();
}

static void cmd_elo(const char* arg) {
    char w[16];
    split_word(arg, w, sizeof(w), false);
    long v = 0;
    if (w[0]) {
        if (!parse_count(w, strlen(w), 0, 999999999L, &v)) {
            printf("Usage: elo N, N = %d-%d ('help elo').\n", STRENGTH_ELO_MIN, STRENGTH_ELO_MAX);
            return;
        }
        if (v < STRENGTH_ELO_MIN || v > STRENGTH_ELO_MAX) {
            printf("Elo %ld is out of range (%d-%d). Nothing changed.\n", v, STRENGTH_ELO_MIN, STRENGTH_ELO_MAX);
            return;
        }
        str_elo = (int)v;
        str_limit = true;
    }
    strength_status();
}

static void cmd_strength(const char* arg) {
    char w[16];
    split_word(arg, w, sizeof(w), true);
    if (strcmp(w, "full") == 0) {
        str_skill = STRENGTH_SKILL_TOP;
        str_limit = false;
    } else if (w[0]) {
        printf("Usage: strength full (back to full strength); a level: 'skill N' or 'elo N'.\n");
        return;
    }
    strength_status();
}

#define POST_HEAD_YOU   " depth   score    time    nodes    speed   main line (your view)"
#define POST_HEAD_WHITE " depth   score    time    nodes    speed   main line (White's view)"
#define POST_LINE_COL   43

static bool can_erase_line(void) {
#ifdef _WIN32
    return w_vt;
#else
    const char* term = getenv("TERM");
    return !(term && strcmp(term, "dumb") == 0);
#endif
}

static void bound_row_clear(void) {
    if (bound_cols <= 0) return;
    if (can_erase_line()) fputs("\r\033[K", stdout);
    else printf("\r%*s\r", bound_cols, "");
    bound_cols = 0;
}

static void fmt_nodes(long long n, char* buf, size_t size) {
    if (n < 10000) snprintf(buf, size, "%lld", n);
    else if (n < 1000000) snprintf(buf, size, "%lldk", n / 1000);
    else if (n < 1000000000LL) snprintf(buf, size, "%.1fM", (double)n / 1e6);
    else snprintf(buf, size, "%.1fG", (double)n / 1e9);
}

static void fmt_speed(long long nodes, long long ms, char* buf, size_t size) {
    if (ms < 5) { snprintf(buf, size, "-"); return; }
    const long long nps = nodes * 1000LL / ms;
    if (nps < 1000) snprintf(buf, size, "%lldn/s", nps);
    else if (nps < 1000000) snprintf(buf, size, "%lldkn/s", nps / 1000);
    else snprintf(buf, size, "%.1fMn/s", (double)nps / 1e6);
}

static int post_sign(void) {
    const Color eng = (strstr(search_fen, " b ") != NULL) ? BLACK : WHITE;
    if (post_white) return eng == WHITE ? 1 : -1;
    return con_pov_you ? -1 : (eng == WHITE ? 1 : -1);
}

static void post_score(int score, bool mate, int mate_moves, const char* bound, char* buf, size_t size, Role* role) {
    const int sign = post_sign();
    char num[24];
    if (mate) {
        const int mm = sign * mate_moves;
        snprintf(num, sizeof(num), mm >= 0 ? "#%d" : "-#%d", mm >= 0 ? mm : -mm);
        *role = eval_role(0, mm >= 0 ? 1 : -1);
    } else {
        format_eval(sign * score, 0, num, sizeof(num));
        *role = eval_role(sign * score, 0);
    }
    const char* b = "";
    if (bound) b = (sign > 0) == (bound[0] == '>') ? ">=" : "<=";
    snprintf(buf, size, "%s%s", b, num);
}

static void post_main_line(const char* pv, int line_no) {
    int room = line_width() - POST_LINE_COL;
    if (line_no > 0) {
        char num[16];
        snprintf(num, sizeof(num), "%d) ", line_no);
        put_styled(R_FRAME, num);
        room -= (int)strlen(num);
    }
    if (room < 4) return;
    Position tmp;
    if (!parse_fen(&tmp, search_fen)) return;
    const Color eng = tmp.to_move;
    int used = 0;
    bool first = true;
    char tok[16];
    const char* s = pv ? pv : "";
    while (*s) {
        s = split_word(s, tok, sizeof(tok), false);
        if (!tok[0]) break;
        Move m;
        char san[16], text[32];
        if (parse_move_human(&tmp, tok, &m) != SAN_OK || !move_to_san(&tmp, m, san, sizeof(san))) break;
        if (tmp.to_move == WHITE) snprintf(text, sizeof(text), "%d. %s", tmp.fullmove_number, san);
        else if (first) snprintf(text, sizeof(text), "%d... %s", tmp.fullmove_number, san);
        else snprintf(text, sizeof(text), "%s", san);
        const int len = (int)strlen(text) + (first ? 0 : 1);
        const bool more = *s != '\0';
        if (used + len + (more ? 3 : 0) > room) {
            fputs(first ? ".." : " ..", stdout);
            return;
        }
        if (!first) fputc(' ', stdout);
        if (tmp.to_move == eng) fputs(text, stdout);
        else put_styled(R_FRAME, text);
        used += len;
        first = false;
        play_on_position(&tmp, &m);
    }
}

static void post_row(int depth, int seldepth, int score, bool mate, int mate_moves, long long time_ms, long long nodes,
                     const char* pv, const char* bound, int line_no) {
    char dep[16], sc[32], tm[24], nd[24], sp[24];
    Role role;
    bound_row_clear();
    snprintf(dep, sizeof(dep), "%3d/%-2d", depth, seldepth);
    post_score(score, mate, mate_moves, bound, sc, sizeof(sc), &role);
    if (time_ms < 100000) snprintf(tm, sizeof(tm), "%.2fs", (double)time_ms / 1000.0);
    else snprintf(tm, sizeof(tm), "%llds", time_ms / 1000);
    fmt_nodes(nodes, nd, sizeof(nd));
    fmt_speed(nodes, time_ms, sp, sizeof(sp));
    if (line_no > 1) printf("%*s", (int)strlen(dep), "");
    else put_styled(R_FRAME, dep);

    const int sclen = (int)strlen(sc);
    printf("%*s", sclen < 8 ? 8 - sclen : 0, "");
    if (bound) {
        printf("%.2s", sc);
        put_styled(role, sc + 2);
    } else {
        put_styled(role, sc);
    }
    char rest[64];
    if (line_no > 1) snprintf(rest, sizeof(rest), "%8s%9s%9s   ", "", "", "");
    else snprintf(rest, sizeof(rest), "%8s%9s%9s   ", tm, nd, sp);
    put_styled(R_FRAME, rest);
    post_main_line(pv, line_no);
    if (bound) {
        bound_cols = line_width();
    } else {
        fputc('\n', stdout);
    }
    fflush(stdout);
}

static bool row_have = false, row_mate = false, row_skip = false;
static int row_score = 0;
static char row_pv[1024] = "";

void console_search_info(int multipv, int depth, int seldepth, int score, bool mate, int mate_moves,
                         long long time_ms, long long nodes, const char* pv) {
    if (xboard_takes_search_output()) {
        xboard_search_info(multipv, depth, seldepth, score, mate, mate_moves, time_ms, nodes, pv);
        return;
    }
    if (multipv <= 1) {
        res_info = true;
        res_cp = score;
        res_mate = mate;
        res_mate_moves = mate_moves;
        res_depth = depth;
        snprintf(res_pv, sizeof(res_pv), "%s", pv ? pv : "");
    }
    if (!post_rows) return;
    if (multipv <= 1) {
        const char* p = pv ? pv : "";
        row_skip = row_have && score == row_score && mate == row_mate && strcmp(p, row_pv) == 0;
        row_have = true;
        row_score = score;
        row_mate = mate;
        snprintf(row_pv, sizeof(row_pv), "%s", p);
    }
    if (row_skip) {
        bound_row_clear();
        fflush(stdout);
        return;
    }
    post_row(depth, seldepth, score, mate, mate_moves, time_ms, nodes, pv, NULL, multipv);
}

void console_search_bound(int depth, int seldepth, int score, bool mate, int mate_moves, long long nodes,
                          const Move* move, bool lower) {
    if (xboard_takes_search_output()) return;
    if (!post_rows || !isatty(STDOUT_FILENO)) return;
    char pv[16] = "";
    if (move) {
        Move mv = *move;
        snprintf(pv, sizeof(pv), "%s", move_to_string(&mv));
    }
    post_row(depth, seldepth, score, mate, mate_moves, elapsed_ms(search_started), nodes, pv, lower ? ">=" : "<=", 0);
}

void console_search_bestmove(const Move* m) {
    if (xboard_takes_search_output()) { xboard_search_bestmove(m); return; }
    res_move_ok = (m != NULL);
    if (m) res_move = *m;
}

void console_search_refused(const char* reason) {
    if (xboard_takes_search_output()) { xboard_search_refused(reason); return; }
    snprintf(res_refused, sizeof(res_refused), "%s", reason && reason[0] ? reason : "?");
    res_move_ok = false;
}

static bool legal_match(Position* pos, const Move* want, Move* out) {
    Move list[MAX_MOVES];
    int n = generate_legal_moves(pos, list);
    int found = -1;
    for (int i = 0; i < n; i++) {
        if (list[i].from != want->from || list[i].to != want->to) continue;
        if (found < 0 || list[i].promotion == want->promotion) found = i;
    }
    if (found < 0) return false;
    *out = list[found];
    return true;
}

static void shown_score(int sign, char* buf, size_t n, Role* role) {
    if (res_mate) {
        int mm = sign * res_mate_moves;
        snprintf(buf, n, mm >= 0 ? "#%d" : "-#%d", mm >= 0 ? mm : -mm);
        *role = eval_role(0, mm >= 0 ? 1 : -1);
    } else {
        int cp = sign * res_cp;
        format_eval(cp, 0, buf, n);
        *role = eval_role(cp, 0);
    }
}

static const char* result_score(Color winner) {
    return winner == WHITE ? "1-0" : "0-1";
}

static void game_end(const char* result) {
    game_over = true;
    draw_offer = false;
    own_count = 0;

    snprintf(game_result, sizeof(game_result), "%s", strstr(result, "1/2-1/2") ? "1/2-1/2" :
             strstr(result, "1-0") ? "1-0" : strstr(result, "0-1") ? "0-1" : "*");
    if (game_time_end) print_two_part(result, "Type 'save' to keep the game or 'new' to play again.");
    else print_two_part(result, "Type 'save' to keep the game, 'new' to play again or 'undo' to take back.");
}

static bool game_check_end(Position* pos) {
    char text[96];
    const Status st = position_status(pos);
    const Color winner = pos->to_move == WHITE ? BLACK : WHITE;
    switch (st) {
        case ST_MATE:
            if (winner == game_human) snprintf(text, sizeof(text), "Checkmate. You win %s.", result_score(winner));
            else snprintf(text, sizeof(text), "Checkmate. Chessenger wins %s.", result_score(winner));
            break;
        case ST_STALEMATE:     snprintf(text, sizeof(text), "Stalemate. Draw 1/2-1/2."); break;
        case ST_DRAW_MATERIAL: snprintf(text, sizeof(text), "Draw by insufficient material. 1/2-1/2."); break;
        case ST_DRAW_50:       snprintf(text, sizeof(text), "Draw by the 50-move rule. 1/2-1/2."); break;
        case ST_DRAW_REP:      snprintf(text, sizeof(text), "Draw by threefold repetition. 1/2-1/2."); break;
        default: return false;
    }
    print_board(pos);
    game_end(text);
    return true;
}

static bool only_king(const Position* pos, Color c) {
    Bitboard all = 0;
    for (int t = PAWN; t <= KING; t++) all |= pos->pieces[c][t];
    return pop_count(all) == 1;
}

static void game_time_loss(const Position* pos, Color loser) {
    const Color winner = loser == WHITE ? BLACK : WHITE;
    char text[120];
    game_time_end = true;
    if (loser == game_human) {
        if (only_king(pos, winner))
            snprintf(text, sizeof(text), "Your time is up, but Chessenger has only its king. Draw 1/2-1/2.");
        else
            snprintf(text, sizeof(text), "Your time is up. Chessenger wins %s.", result_score(winner));
    } else {
        if (only_king(pos, winner))
            snprintf(text, sizeof(text), "Chessenger's time is up, but you have only your king. Draw 1/2-1/2.");
        else
            snprintf(text, sizeof(text), "Chessenger's time is up. You win %s.", result_score(winner));
    }
    game_end(text);
}

static void clock_after_move(Color c, long long used) {
    game_moves_done[c]++;
    if (game_tc.kind != TC_CLOCK) return;
    game_clock[c] -= used;
    game_clock[c] += game_tc.inc_ms;
    if (game_tc.moves > 0 && game_moves_done[c] % game_tc.moves == 0) game_clock[c] += game_tc.base_ms;
}

static void post_head(void) {
    char head[96];
    snprintf(head, sizeof(head), "%.*s", line_width(), (con_pov_you && !post_white) ? POST_HEAD_YOU : POST_HEAD_WHITE);
    put_styled(R_FRAME, head);
    fputc('\n', stdout);
    fflush(stdout);
}

static bool search_line(Position* pos, SearchKind kind, char* line, size_t size, const char* go) {
    snprintf(line, size, "%s", go);
    search_kind = kind;
    res_info = res_mate = res_move_ok = false;
    res_cp = res_mate_moves = 0;
    res_depth = 0;
    res_pv[0] = '\0';
    search_interrupted = false;
    res_refused[0] = '\0';
    snprintf(search_fen, sizeof(search_fen), "%s", position_to_fen(pos));

    post_white = (kind == SK_ANALYZE || kind == SK_GO || kind == SK_MATE);
    post_rows = (kind == SK_GAME && con_post) || post_white;
    bound_cols = 0;
    row_have = row_skip = false;

    if (kind == SK_GAME || post_white || kind == SK_ANNOTATE) echo_off_begin();
    if (post_rows) post_head();
    search_started = get_wall_time();
    return true;
}

static bool engine_go_line(Position* pos, char* line, size_t size) {
    char go[160];
    const Color eng = pos->to_move;
    switch (game_tc.kind) {
        case TC_MOVE:  snprintf(go, sizeof(go), "go movetime %ld", game_tc.value * 1000L); break;
        case TC_DEPTH: snprintf(go, sizeof(go), "go depth %ld", game_tc.value); break;
        case TC_NODES: snprintf(go, sizeof(go), "go nodes %ld", game_tc.value); break;
        case TC_CLOCK:
        default: {
            long long wt = game_clock[WHITE] > 0 ? game_clock[WHITE] : 1;
            long long bt = game_clock[BLACK] > 0 ? game_clock[BLACK] : 1;
            int len = snprintf(go, sizeof(go), "go wtime %lld btime %lld winc %ld binc %ld", wt, bt,
                               game_tc.inc_ms, game_tc.inc_ms);
            if (game_tc.moves > 0)
                snprintf(go + len, sizeof(go) - (size_t)len, " movestogo %ld",
                         game_tc.moves - game_moves_done[eng] % game_tc.moves);
            break;
        }
    }
    return search_line(pos, SK_GAME, line, size, go);
}

static bool hint_go_line(Position* pos, char* line, size_t size) {
    const TimeControl* tc = (game_on && !game_over) ? &game_tc : &con_tc;
    char go[64];
    if (tc->kind == TC_DEPTH) snprintf(go, sizeof(go), "go depth %ld", tc->value);
    else if (tc->kind == TC_NODES) snprintf(go, sizeof(go), "go nodes %ld", tc->value);
    else snprintf(go, sizeof(go), "go movetime 1000");
    return search_line(pos, SK_HINT, line, size, go);
}

static void print_engine_line(const char* move_text, Color eng) {
    char score[24] = "", who[8] = "", clocks[48] = "";
    Role role = R_EVAL0;
    if (res_info) {
        const int sign = con_pov_you ? -1 : (eng == WHITE ? 1 : -1);
        shown_score(sign, score, sizeof(score), &role);
        snprintf(who, sizeof(who), "%s", con_pov_you ? "you" : "White");
    }
    if (game_tc.kind == TC_CLOCK) {
        char you[16], ce[16];
        fmt_clock(game_clock[game_human], you, sizeof(you));
        fmt_clock(game_clock[eng], ce, sizeof(ce));
        snprintf(clocks, sizeof(clocks), "you %s  CE %s", you, ce);
    }
    const char* lead = "   Chessenger plays ";
    const int move_len = (int)strlen(move_text);
    int field = 17;
    if (field < move_len + 1) field = move_len + 1;
    int tail = 0;
    if (score[0]) tail += 5 + (int)strlen(score) + 2 + (int)strlen(who) + 1;
    if (clocks[0]) tail += (score[0] ? 3 : 0) + (int)strlen(clocks);
    const int over = (int)strlen(lead) + field + tail - line_width();
    if (over > 0) field = field - over > move_len + 1 ? field - over : move_len + 1;
    if (!score[0] && !clocks[0]) {
        printf("%s%s\n", lead, move_text);
        return;
    }
    printf("%s%-*s", lead, field, move_text);
    if (score[0]) {
        fputs("eval ", stdout);
        put_styled(role, score);
        printf(" (%s)", who);
    }
    if (clocks[0]) printf("%s%s", score[0] ? "   " : "", clocks);
    fputc('\n', stdout);
}

static void human_turn(Position* pos) {
    turn_start = get_wall_time();
    print_board(pos);
}

static void ponder_predict(Position* pos);

static void game_engine_done(Position* pos) {
    const long long used = elapsed_ms(search_started);
    const Color eng = pos->to_move;
    Move m;
    if (!game_on || game_over || eng == game_human) return;
    if (res_refused[0]) {
        char text[200];
        snprintf(text, sizeof(text), "Chessenger cannot search: the engine position is invalid (%s); the game stops.",
                 res_refused);
        print_wrapped(text, 0, NULL);
        game_on = false;
        return;
    }
    if (!res_move_ok || !legal_match(pos, &res_move, &m)) {
        printf("Chessenger found no move here; the game stops.\n");
        game_on = false;
        return;
    }
    if (game_tc.kind == TC_CLOCK && game_clock[eng] - used < 0) {
        game_clock[eng] -= used;
        game_time_loss(pos, eng);
        return;
    }
    if (draw_offer) {
        draw_offer = false;
        const bool accept = res_info && (res_mate ? res_mate_moves < 0 : res_cp <= 0);
        if (accept) {
            game_end("Chessenger accepts the draw. 1/2-1/2");
            return;
        }
        printf("Chessenger declines the draw.\n");
    }
    typed_move_play(pos, &m, false, false);
    clock_after_move(eng, used);
    char label[16], text[40];
    bool black;
    history_label(hist_count - 1, label, sizeof(label), &black);
    snprintf(text, sizeof(text), "%s %s", label, hist[hist_count - 1].san);
    print_engine_line(text, eng);
    if (game_check_end(pos)) return;
    ponder_predict(pos);
    human_turn(pos);
}

static void hint_done(Position* pos) {
    Move m;
    if (res_refused[0]) {
        char text[200];
        snprintf(text, sizeof(text), "No hint: the engine position is invalid (%s). Type 'setboard' or 'new'.",
                 res_refused);
        print_wrapped(text, 0, NULL);
        return;
    }
    if (!res_move_ok || !legal_match(pos, &res_move, &m)) {
        printf("No hint: Chessenger found no move here.\n");
        return;
    }
    char san[16];
    if (!move_to_san(pos, m, san, sizeof(san))) snprintf(san, sizeof(san), "%s", move_to_string(&m));
    if (!res_info) {
        printf("Hint: %s\n", san);
        return;
    }

    const bool you = game_on && !game_over && con_pov_you && pos->to_move == game_human;
    const int sign = you ? 1 : (pos->to_move == WHITE ? 1 : -1);
    char score[24];
    Role role;
    shown_score(sign, score, sizeof(score), &role);
    printf("Hint: %s (eval ", san);
    put_styled(role, score);
    printf(" (%s))\n", you ? "you" : "White");
}

static void game_start(Position* pos, Color human, const TimeControl* tc, bool initial) {
    history_check(pos);
    const Status st = position_status(pos);
    const bool finished = st == ST_MATE || st == ST_STALEMATE || st == ST_DRAW_MATERIAL || st == ST_DRAW_50 ||
                          st == ST_DRAW_REP;
    bool from_start = initial || finished;
    if (from_start) {
        Position start;
        setup_starting_position(&start);
        snprintf(own_fen, sizeof(own_fen), "%s", position_to_fen(&start));
    } else {
        snprintf(own_fen, sizeof(own_fen), "%s", position_to_fen(pos));
        Position start;
        setup_starting_position(&start);
        if (strcmp(own_fen, position_to_fen(&start)) == 0) from_start = true;
    }
    game_on = true;
    game_over = false;
    game_time_end = false;
    record_clear();
    snprintf(game_result, sizeof(game_result), "*");
    game_human = human;
    game_tc = *tc;
    game_clock[WHITE] = game_clock[BLACK] = tc->kind == TC_CLOCK ? tc->base_ms : 0;
    game_moves_done[WHITE] = game_moves_done[BLACK] = 0;
    draw_offer = false;
    redo_clear();
    own_count = 0;
    ponder_have = ponder_paused = ponder_ran = false;
    ponder_guesses = ponder_hits = 0;
    char t[96], text[320], strength[48];
    tc_text(tc, t, sizeof(t));
    strength_text(strength, sizeof(strength));
    snprintf(text, sizeof(text), "New game%s: You (%s) vs Chessenger %s (%s), %s%s. Type %s'help game' for game commands.",
             from_start ? "" : " from the current position", human == WHITE ? "White" : "Black", ENGINE_VERSION, strength, t,
             tc->kind == TC_CLOCK ? "" : ", no clocks",
             con_post ? "" : "'post on' to watch Chessenger think, ");
    print_wrapped(text, 0, NULL);
    own_push(OWN_NEWGAME);
    own_push(OWN_POSITION);
    own_push(OWN_START);
}

static bool analysis_line(Position* pos, char* line, size_t size);
static void nav_show(Position* pos);
static bool annotate_step(Position* pos, char* line, size_t size);
static bool restore_line(char* line, size_t size);

static bool own_step_line(Position* pos, OwnStep step, char* line, size_t size) {
    switch (step) {
        case OWN_NEWGAME:
            snprintf(line, size, "ucinewgame");
            return true;
        case OWN_POSITION:
            snprintf(line, size, "position fen %s", own_fen);
            sync_kind = SYNC_RESET;
            return true;
        case OWN_START:
            if (!game_on) return false;
            if (pos->to_move != game_human) return engine_go_line(pos, line, size);
            human_turn(pos);
            return false;
        case OWN_ENGINE:
            if (!game_on || game_over || pos->to_move == game_human) return false;
            return engine_go_line(pos, line, size);
        case OWN_HINT:
            return hint_go_line(pos, line, size);
        case OWN_BOARD:
            if (game_on) print_board(pos);
            return false;
        case OWN_ANALYZE:
            return analysis_line(pos, line, size);
        case OWN_GO:
            return search_line(pos, SK_GO, line, size, own_go);
        case OWN_MATE:
            return search_line(pos, SK_MATE, line, size, own_go);
        case OWN_SHOW:
            nav_show(pos);
            return false;
        case OWN_ANNOTATE:
            return annotate_step(pos, line, size);
        case OWN_RESTORE:
            return restore_line(line, size);
        case OWN_QUIT:
            snprintf(line, size, "quit");
            return true;
    }
    return false;
}

static void game_human_move(Position* pos, const char* line) {
    history_check(pos);
    if (!game_on) {
        cmd_typed_moves(pos, line);
        return;
    }
    char tok[64], next[64];
    const char* rest = split_word(line, tok, sizeof(tok), false);
    if (game_over) {
        Move list[MAX_MOVES];
        int count = 0;
        bool sc = false;
        if (san_read_move(pos, tok, list, &count, &sc) == SAN_UNRECOGNIZED) unknown_command(tok);
        else say_game_over();
        return;
    }
    Move m;
    if (!typed_move_read(pos, tok, &m)) return;
    rest = split_word(rest, next, sizeof(next), false);
    if (is_ep_remark(next)) rest = split_word(rest, next, sizeof(next), false);
    if (next[0]) printf("One move a turn: the rest of the line is not played.\n");
    const long long used = elapsed_ms(turn_start);
    if (game_tc.kind == TC_CLOCK && game_clock[game_human] - used < 0) {
        game_clock[game_human] -= used;
        game_time_loss(pos, game_human);
        return;
    }

    char san[16], typed[64];
    if (!move_to_san(pos, m, san, sizeof(san))) snprintf(san, sizeof(san), "%s", move_to_string(&m));
    snprintf(typed, sizeof(typed), "%s", tok);
    size_t k = strlen(typed);
    while (k > 0 && strchr("+#!?", typed[k - 1])) typed[--k] = '\0';
    k = strlen(san);
    while (k > 0 && strchr("+#", san[k - 1])) san[--k] = '\0';
    typed_move_play(pos, &m, !con_prompt || strcmp(typed, san) != 0, false);
    clock_after_move(game_human, used);
    if (ponder_ran) ponder_guesses++;
    ponder_ran = false;
    ponder_have = false;
    ponder_paused = false;
    if (game_check_end(pos)) return;
    own_push(OWN_ENGINE);
}

static void cmd_play(Position* pos, const char* arg) {
    Color human = WHITE;
    TimeControl tc = con_tc;
    bool have_color = false, have_tc = false, ok = true;
    char w[48];
    const char* s = arg;
    while (ok && *s) {
        s = split_word(s, w, sizeof(w), true);
        if (!w[0]) break;
        if (!have_color && (strcmp(w, "white") == 0 || strcmp(w, "black") == 0)) {
            human = w[0] == 'w' ? WHITE : BLACK;
            have_color = true;
        } else if (!have_tc && parse_clock_text(w, &tc)) {
            have_tc = true;
        } else {
            ok = false;
        }
    }
    if (!ok) {
        printf("Usage: play [white|black] [T], T = 5+3 | 10 | 40/90 | 40/90+30 ('help time').\n");
        return;
    }
    if (have_tc) con_tc = tc;
    game_start(pos, human, &tc, false);
}

static void cmd_redo(Position* pos, const char* arg) {
    history_check(pos);
    char w[16];
    split_word(arg, w, sizeof(w), false);
    long n = 1;
    if (w[0] && !parse_count(w, strlen(w), 1, 999999999L, &n)) {
        printf("Usage: redo [N], N as for 'undo' (default 1).\n");
        return;
    }
    if (redo_count == 0) {
        printf("Nothing to redo.\n");
        return;
    }
    if (game_on && game_over) {
        say_game_over();
        return;
    }
    const long available = game_on ? (redo_count + 1) / 2 : redo_count;
    if (n > available) {
        if (game_on) printf("Cannot redo %ld of your moves: only %ld taken back.\n", n, available);
        else printf("Cannot redo %ld half-moves: only %d taken back.\n", n, redo_count);
        return;
    }
    const long half = game_on ? (2 * n > redo_count ? redo_count : 2 * n) : n;
    HistoryEntry* saved = (HistoryEntry*)malloc((size_t)redo_count * sizeof(HistoryEntry));
    int saved_count = redo_count;
    if (!saved) return;
    memcpy(saved, redo_stack, (size_t)redo_count * sizeof(HistoryEntry));
    for (long i = 0; i < half; i++) {
        const HistoryEntry e = saved[saved_count - 1];
        Move m;
        if (!legal_match(pos, &e.move, &m)) {
            printf("Cannot redo '%s' in this position.\n", e.san);
            saved_count = 0;
            break;
        }
        typed_move_play(pos, &m, true, !game_on);
        saved_count--;
        if (game_on && position_status(pos) >= ST_MATE) break;
    }
    for (int i = 0; i < saved_count; i++) redo_push(&saved[i]);
    free(saved);
    if (!game_on) return;
    if (game_check_end(pos)) return;
    if (pos->to_move != game_human) own_push(OWN_ENGINE);
    else print_board(pos);
}

static void cmd_switch(Position* pos) {
    history_check(pos);
    if (!game_on) {
        printf("No game in progress. Type 'play' to start one.\n");
        return;
    }
    if (game_over) {
        say_game_over();
        return;
    }
    if (game_tc.kind == TC_CLOCK) game_clock[pos->to_move] -= elapsed_ms(turn_start);
    game_human = game_human == WHITE ? BLACK : WHITE;
    draw_offer = false;
    printf("You take %s now; Chessenger moves.\n", game_human == WHITE ? "White" : "Black");
    if (pos->to_move != game_human) own_push(OWN_ENGINE);
}

static void cmd_resign(Position* pos) {
    history_check(pos);
    if (!game_on) {
        printf("No game in progress.\n");
        return;
    }
    if (game_over) {
        say_game_over();
        return;
    }
    const Color winner = game_human == WHITE ? BLACK : WHITE;
    char text[64];
    snprintf(text, sizeof(text), "You resign. Chessenger wins %s.", result_score(winner));
    game_end(text);
}

static void cmd_draw(Position* pos, const char* arg) {
    history_check(pos);
    if (!game_on) {
        printf("No game in progress.\n");
        return;
    }
    if (game_over) {
        say_game_over();
        return;
    }
    draw_offer = true;
    if (!arg[0]) {
        printf("Your draw offer goes with your next move.\n");
        return;
    }
    char moves[MAX_UCI_LINE];
    snprintf(moves, sizeof(moves), "%s", arg);
    game_human_move(pos, moves);
}

static void cmd_hint(Position* pos) {
    history_check(pos);
    if (game_on && game_over) {
        say_game_over();
        return;
    }
    Move list[MAX_MOVES];
    if (generate_legal_moves(pos, list) == 0) {
        printf("No legal moves: %s\n", is_in_check(pos, pos->to_move) ? "checkmate." : "stalemate.");
        return;
    }
    own_push(OWN_HINT);
}

static void cmd_clock(const Position* pos) {
    char t[96];
    if (!game_on) {
        tc_text(&con_tc, t, sizeof(t));
        printf("No game in progress. Time control: %s.\n", t);
        return;
    }
    tc_text(&game_tc, t, sizeof(t));
    if (game_tc.kind != TC_CLOCK) {
        printf("No clocks in this game (%s).\n", t);
        return;
    }
    const Color eng = game_human == WHITE ? BLACK : WHITE;
    char you[16], ce[16], line[160];
    fmt_clock(clock_now(pos, game_human), you, sizeof(you));
    fmt_clock(clock_now(pos, eng), ce, sizeof(ce));
    snprintf(line, sizeof(line), "Clocks: you (%s) %s, Chessenger (%s) %s; %s.", game_human == WHITE ? "White" : "Black",
             you, eng == WHITE ? "White" : "Black", ce, t);
    print_wrapped(line, 2, NULL);
}

static void cmd_time(const char* arg, const char* alias) {
    char t[96];
    const bool short_form = alias && (strcasecmp(alias, "st") == 0 || strcasecmp(alias, "sd") == 0 ||
                                      strcasecmp(alias, "sn") == 0);
    if (!arg[0] && !short_form) {
        tc_text(&con_tc, t, sizeof(t));
        printf("Time control: %s\n", t);
        return;
    }
    TimeControl tc;
    if (!parse_time_args(arg, alias, &tc)) {
        if (short_form && strcasecmp(alias, "st") == 0) printf("Usage: st S, S = seconds a move for Chessenger (1-3600).\n");
        else if (short_form && strcasecmp(alias, "sd") == 0) printf("Usage: sd N, N = search depth (1-%d).\n", MAX_DEPTH);
        else if (short_form) printf("Usage: sn N, N = nodes a move (1-999999999).\n");
        else printf("Usage: time 5+3 | 10 | 40/90 | 40/90+30 | move S | depth N | nodes N.\n");
        return;
    }
    con_tc = tc;
    tc_text(&tc, t, sizeof(t));
    printf("Time control: %s%s\n", t, game_on && !game_over ? " (from the next 'play')" : "");
}

static void cmd_post(const char* arg) {
    char w[16];
    split_word(arg, w, sizeof(w), true);
    if (strcmp(w, "on") == 0) con_post = true;
    else if (strcmp(w, "off") == 0) con_post = false;
    else if (w[0]) {
        printf("Usage: post on|off\n");
        return;
    }
    printf("Thinking lines: %s.\n", con_post ? "on" : "off");
}

static void pgn_time_control(char* buf, size_t n) {
    if ((!game_on && !game_viewed) || game_tc.kind != TC_CLOCK) {
        snprintf(buf, n, "-");
        return;
    }
    char inc[24] = "";
    if (game_tc.inc_ms > 0) snprintf(inc, sizeof(inc), "+%ld", game_tc.inc_ms / 1000);
    if (game_tc.moves > 0) snprintf(buf, n, "%ld/%ld%s", game_tc.moves, game_tc.base_ms / 1000, inc);
    else snprintf(buf, n, "%ld%s", game_tc.base_ms / 1000, inc);
}

static const char* pgn_result(Position* pos) {
    if (game_on) return game_over ? game_result : "*";
    if (game_viewed && redo_count == 0) return game_result;
    switch (position_status(pos)) {
        case ST_MATE:          return pos->to_move == WHITE ? "0-1" : "1-0";
        case ST_STALEMATE:
        case ST_DRAW_MATERIAL:
        case ST_DRAW_50:
        case ST_DRAW_REP:      return "1/2-1/2";
        default:               return "*";
    }
}

typedef struct { FILE* f; int col; } PgnOut;

static void pgn_token(PgnOut* o, const char* t) {
    const int len = (int)strlen(t);
    if (o->col > 0 && o->col + 1 + len > 79) {
        fputc('\n', o->f);
        o->col = 0;
    }
    if (o->col > 0) {
        fputc(' ', o->f);
        o->col++;
    }
    fputs(t, o->f);
    o->col += len;
}

static FILE* open_named(const char* name, const char* mode, bool argv_name) {
#ifdef _WIN32
    UINT cp = CP_ACP;
    DWORD in_mode = 0;
    const HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    if (!argv_name && in != NULL && in != INVALID_HANDLE_VALUE && GetConsoleMode(in, &in_mode)) cp = GetConsoleCP();
    static wchar_t wname[MAX_UCI_LINE];
    wchar_t wmode[8];
    int i = 0;
    for (; mode[i] && i < 7; i++) wmode[i] = (wchar_t)(unsigned char)mode[i];
    wmode[i] = L'\0';
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, wname, MAX_UCI_LINE) &&
        !MultiByteToWideChar(cp, 0, name, -1, wname, MAX_UCI_LINE))
        return fopen(name, mode);
    return _wfopen(wname, wmode);
#else
    (void)argv_name;
    return fopen(name, mode);
#endif
}

static bool pgn_existing(const char* name, int* games, bool* ends_blank, bool* ends_newline, bool argv_name) {
    *games = 0;
    *ends_blank = true;
    *ends_newline = true;
    FILE* f = open_named(name, "rb", argv_name);
    if (!f) return false;
    char line[512];
    bool at_start = true, any = false;
    int last = 0, before = 0;
    while (fgets(line, sizeof(line), f)) {
        if (at_start && strncmp(line, "[Event ", 7) == 0) (*games)++;
        const size_t k = strlen(line);
        at_start = k > 0 && line[k - 1] == '\n';
        for (size_t i = 0; i < k; i++) {
            if (line[i] == '\r') continue;
            before = last;
            last = (unsigned char)line[i];
            any = true;
        }
    }
    fclose(f);
    if (any) {
        *ends_newline = last == '\n';
        *ends_blank = last == '\n' && before == '\n';
    }
    return true;
}

static void cmd_save(Position* pos, const char* arg) {
    history_check(pos);
    time_t now = time(NULL);
    struct tm* lt = localtime(&now);
    char date_tag[16] = "????.??.??", name[MAX_UCI_LINE];
    if (lt) snprintf(date_tag, sizeof(date_tag), "%04d.%02d.%02d", lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday);
    if (arg[0]) snprintf(name, sizeof(name), "%s", arg);
    else if (lt) snprintf(name, sizeof(name), "chessenger-%04d-%02d-%02d.pgn", lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday);
    else snprintf(name, sizeof(name), "chessenger.pgn");

    int games = 0;
    bool ends_blank = true, ends_newline = true;
    const bool exists = pgn_existing(name, &games, &ends_blank, &ends_newline, false);
    errno = 0;
    FILE* f = open_named(name, "a", false);
    if (!f) {
        char text[MAX_UCI_LINE + 128];
        snprintf(text, sizeof(text), "Cannot write '%s': %s.", name, strerror(errno));
        print_wrapped(text, 2, NULL);
        return;
    }
    if (exists && !ends_blank) fputs(ends_newline ? "\n" : "\n\n", f);

    char engine[64], tc[48];
    snprintf(engine, sizeof(engine), "Chessenger %s", ENGINE_VERSION);
    const bool named = game_on || game_viewed;
    const char* white = !named ? "?" : game_human == WHITE ? "You" : engine;
    const char* black = !named ? "?" : game_human == BLACK ? "You" : engine;
    const char* result = pgn_result(pos);
    pgn_time_control(tc, sizeof(tc));
    fprintf(f, "[Event \"Casual game\"]\n[Site \"?\"]\n[Date \"%s\"]\n[Round \"-\"]\n", date_tag);
    fprintf(f, "[White \"%s\"]\n[Black \"%s\"]\n[Result \"%s\"]\n[TimeControl \"%s\"]\n", white, black, result, tc);
    Position start;
    setup_starting_position(&start);
    if (strcmp(hist_base_fen, position_to_fen(&start)) != 0)
        fprintf(f, "[SetUp \"1\"]\n[FEN \"%s\"]\n", hist_base_fen);
    if ((game_on && game_over && game_time_end) || (game_viewed && game_viewed_time && redo_count == 0))
        fprintf(f, "[Termination \"time forfeit\"]\n");
    fputc('\n', f);

    PgnOut o = { f, 0 };
    for (int i = 0; i < hist_count; i++) {
        char label[16], token[48];
        bool black_move;
        history_label(i, label, sizeof(label), &black_move);
        if (!black_move || i == 0) snprintf(token, sizeof(token), "%s %s", label, hist[i].san);
        else snprintf(token, sizeof(token), "%s", hist[i].san);
        pgn_token(&o, token);
    }
    pgn_token(&o, result);
    fputs("\n\n", f);
    const bool write_error = ferror(f) != 0;
    const int saved_errno = errno;
    if (fclose(f) != 0 || write_error) {
        char text[MAX_UCI_LINE + 128];
        snprintf(text, sizeof(text), "Cannot write '%s': %s.", name, strerror(write_error ? saved_errno : errno));
        print_wrapped(text, 2, NULL);
        return;
    }
    char text[MAX_UCI_LINE + 64];
    snprintf(text, sizeof(text), "Saved: %s (game %d).", name, games + 1);
    print_wrapped(text, 2, NULL);
}

static void current_prompt(const Position* pos, char* buf, size_t n);
static char ed_prompt[64];

static void pv_san(const char* fen, const char* pv, char* out, size_t n) {
    out[0] = '\0';
    Position tmp;
    if (!parse_fen(&tmp, fen)) return;
    size_t used = 0;
    bool first = true;
    char tok[16];
    const char* s = pv ? pv : "";
    while (*s) {
        s = split_word(s, tok, sizeof(tok), false);
        if (!tok[0]) break;
        Move m;
        char san[16], text[32];
        if (parse_move_human(&tmp, tok, &m) != SAN_OK || !move_to_san(&tmp, m, san, sizeof(san))) break;
        if (tmp.to_move == WHITE) snprintf(text, sizeof(text), "%d. %s", tmp.fullmove_number, san);
        else if (first) snprintf(text, sizeof(text), "%d... %s", tmp.fullmove_number, san);
        else snprintf(text, sizeof(text), "%s", san);
        const int k = snprintf(out + used, n - used, "%s%s", first ? "" : " ", text);
        if (k < 0 || used + (size_t)k >= n) {
            out[used] = '\0';
            break;
        }
        used += (size_t)k;
        first = false;
        play_on_position(&tmp, &m);
    }
}

static void numbered_san(const Position* pos, const Move* m, char* buf, size_t n) {
    char san[16];
    if (!move_to_san(pos, *m, san, sizeof(san))) snprintf(san, sizeof(san), "%s", move_to_string((Move*)m));
    snprintf(buf, n, pos->to_move == WHITE ? "%d. %s" : "%d... %s", pos->fullmove_number, san);
}

static bool no_legal_moves(Position* pos, const char* lead) {
    Move list[MAX_MOVES];
    if (generate_legal_moves(pos, list) > 0) return false;
    printf("%s%s\n", lead, is_in_check(pos, pos->to_move) ? "checkmate." : "stalemate.");
    return true;
}

static void print_best_move(Position* pos) {
    Move m;
    if (res_refused[0]) {
        char text[200];
        snprintf(text, sizeof(text), "No search: the engine position is invalid (%s). Type 'setboard' or 'new'.", res_refused);
        print_wrapped(text, 0, NULL);
        return;
    }
    if (!res_move_ok || !legal_match(pos, &res_move, &m)) {
        printf("Chessenger found no move here.\n");
        return;
    }
    char mv[32];
    numbered_san(pos, &m, mv, sizeof(mv));
    if (!res_info) {
        printf("Best move: %s.\n", mv);
        return;
    }
    char score[24];
    Role role;
    shown_score(pos->to_move == WHITE ? 1 : -1, score, sizeof(score), &role);
    printf("Best move: %s (", mv);
    put_styled(role, score);
    printf(" for White), depth %d, %.2f s.\n", res_depth, (double)elapsed_ms(search_started) / 1000.0);
}

static void cmd_analyze(Position* pos) {
    history_check(pos);
    if (game_on && !game_over) {
        printf("Not during a game: 'hint' shows the move Chessenger suggests.\n");
        return;
    }
    game_view_keep();
    game_on = game_over = false;
    analysis_on = true;
    analysis_first = true;
}

static bool analysis_line(Position* pos, char* line, size_t size) {
    if (!analysis_on) return false;
    history_check(pos);
    if (game_on && !game_over) {
        analysis_on = false;
        return false;
    }
    if (no_legal_moves(pos, "Analysis stopped: ")) {
        analysis_on = false;
        return false;
    }
    const char* side = pos->to_move == WHITE ? "White" : "Black";
    if (analysis_first) printf("Analyzing, %s to move. Type 'stop' to end; a typed move is played.\n", side);
    else printf("Analyzing, %s to move.\n", side);
    analysis_first = false;
    return search_line(pos, SK_ANALYZE, line, size, "go infinite");
}

static void analyze_done(Position* pos, bool interrupted) {
    char q[8];
    if (!interrupted && analysis_on && console_queued_line(q, sizeof(q))) return;
    analysis_on = false;
    print_best_move(pos);
}

static void cmd_stop(void) {
    if (analysis_on) {
        analysis_on = false;
        printf("Analysis stopped.\n");
        return;
    }
    printf("Nothing to stop: Chessenger is not thinking.\n");
}

static void cmd_go(Position* pos, const char* arg) {
    history_check(pos);
    if (game_on && !game_over) {
        printf("Not during a game: 'hint' shows the move Chessenger suggests.\n");
        return;
    }
    if (no_legal_moves(pos, "No search: ")) return;
    char go[96] = "go";
    size_t len = 2;
    bool have[3] = { false, false, false };
    const char* s = arg;
    char w[32], v[32];
    while (*s) {
        s = split_word(s, w, sizeof(w), true);
        if (!w[0]) break;
        s = split_word(s, v, sizeof(v), false);
        const int k = strcmp(w, "depth") == 0 ? 0 : strcmp(w, "movetime") == 0 ? 1 : strcmp(w, "nodes") == 0 ? 2 : -1;
        long n = 0;
        const long hi = k == 0 ? MAX_DEPTH : k == 1 ? 86400000L : 999999999L;
        if (k < 0 || have[k] || !v[0] || !parse_count(v, strlen(v), 1, hi, &n)) {
            printf("Usage: go [depth N] [movetime MS] [nodes N], for example 'go depth 20'.\n");
            return;
        }
        have[k] = true;
        len += (size_t)snprintf(go + len, sizeof(go) - len, " %s %ld", w, n);
    }
    if (!have[0] && !have[1] && !have[2]) {
        const TimeControl* tc = &con_tc;
        if (tc->kind == TC_MOVE) snprintf(go, sizeof(go), "go movetime %ld", tc->value * 1000L);
        else if (tc->kind == TC_DEPTH) snprintf(go, sizeof(go), "go depth %ld", tc->value);
        else if (tc->kind == TC_NODES) snprintf(go, sizeof(go), "go nodes %ld", tc->value);
        else {
            len = (size_t)snprintf(go, sizeof(go), "go wtime %ld btime %ld winc %ld binc %ld", tc->base_ms, tc->base_ms,
                                   tc->inc_ms, tc->inc_ms);
            if (tc->moves > 0) snprintf(go + len, sizeof(go) - len, " movestogo %ld", tc->moves);
        }
    }
    snprintf(own_go, sizeof(own_go), "%s", go);
    game_view_keep();
    game_on = game_over = false;
    own_push(OWN_GO);
}

static void go_done(Position* pos) {
    print_best_move(pos);
}

static void cmd_mate(Position* pos, const char* arg) {
    history_check(pos);
    char w[16];
    split_word(arg, w, sizeof(w), false);
    long n = 0;
    if (!w[0] || !parse_count(w, strlen(w), 1, (MAX_DEPTH + 1) / 2, &n)) {
        printf("Usage: mate N, N = moves (1-%d), for example 'mate 3'.\n", (MAX_DEPTH + 1) / 2);
        return;
    }
    if (game_on && !game_over) {
        printf("Not during a game: 'hint' shows the move Chessenger suggests.\n");
        return;
    }
    if (no_legal_moves(pos, "No search: ")) return;
    mate_target = (int)n;
    snprintf(own_go, sizeof(own_go), "go mate %ld", n);
    game_view_keep();
    game_on = game_over = false;
    own_push(OWN_MATE);
}

static bool mate_within(Position* p, int plies, Move* first) {
    Move list[MAX_MOVES], reply[MAX_MOVES];
    const int n = generate_legal_moves(p, list);
    for (int i = 0; i < n; i++) {
        UndoInfo u;
        make_move_full(p, &list[i], &u);
        const int rn = generate_legal_moves(p, reply);
        bool mate = rn == 0 && is_in_check(p, p->to_move);
        if (rn > 0 && plies >= 3) {
            mate = true;
            for (int j = 0; j < rn && mate; j++) {
                UndoInfo v;
                Move dummy;
                make_move_full(p, &reply[j], &v);
                mate = mate_within(p, plies - 2, &dummy);
                undo_move_full(p, &reply[j], &v);
            }
        }
        undo_move_full(p, &list[i], &u);
        if (mate) {
            *first = list[i];
            return true;
        }
    }
    return false;
}

static int mate_length(Position* p, int plies, Move* first) {
    for (int q = 1; q <= plies; q += 2)
        if (mate_within(p, q, first)) return q;
    return 0;
}

static bool mate_line_full(const char* fen, const char* pv, int plies, char* out, size_t n) {
    Position tmp;
    if (!parse_fen(&tmp, fen)) return false;
    out[0] = '\0';
    size_t len = 0;
    int k = 0;
    char tok[16];
    const char* s = pv ? pv : "";
    while (*s && k < plies) {
        s = split_word(s, tok, sizeof(tok), false);
        if (!tok[0]) break;
        Move m;
        if (parse_move_human(&tmp, tok, &m) != SAN_OK) return false;
        len += (size_t)snprintf(out + len, n - len, "%s%s", k ? " " : "", move_to_string(&m));
        if (len >= n) return false;
        play_on_position(&tmp, &m);
        k++;
    }
    Move list[MAX_MOVES];
    while (k < plies) {
        const int count = generate_legal_moves(&tmp, list);
        if (count == 0) break;
        const int left = plies - k;
        Move want = { 0 }, m;
        bool ok = false;
        if (left <= 3 && (left % 2) == 1) {
            ok = mate_length(&tmp, left, &m) > 0;
        } else if (left <= 3) {
            int longest = -1;
            for (int i = 0; i < count; i++) {
                UndoInfo u;
                Move reply;
                make_move_full(&tmp, &list[i], &u);
                const int len = mate_length(&tmp, left - 1, &reply);
                undo_move_full(&tmp, &list[i], &u);
                if (len == 0) return false;
                if (len > longest) {
                    longest = len;
                    m = list[i];
                }
            }
            ok = longest > 0;
        } else {
            int score = 0;
            (void)probe_hash(&main_tt, tmp.hash, MAX_DEPTH, -MATE_SCORE - 1, MATE_SCORE + 1, &score, &want, 0);
            ok = want.from != want.to && legal_match(&tmp, &want, &m);
            if (!ok && count == 1) {
                m = list[0];
                ok = true;
            }
        }
        if (!ok) break;
        len += (size_t)snprintf(out + len, n - len, "%s%s", k ? " " : "", move_to_string(&m));
        if (len >= n) return false;
        play_on_position(&tmp, &m);
        k++;
    }
    return generate_legal_moves(&tmp, list) == 0 && is_in_check(&tmp, tmp.to_move);
}

static void mate_done(Position* pos) {
    (void)pos;
    if (res_refused[0]) {
        char text[200];
        snprintf(text, sizeof(text), "No search: the engine position is invalid (%s). Type 'setboard' or 'new'.", res_refused);
        print_wrapped(text, 0, NULL);
        return;
    }
    if (res_info && res_mate && res_mate_moves > 0 && res_mate_moves <= mate_target) {
        char line[1200], moves[600], text[700];
        const bool full = mate_line_full(search_fen, res_pv, 2 * res_mate_moves - 1, line, sizeof(line));
        pv_san(search_fen, full ? line : res_pv, moves, sizeof(moves));
        snprintf(text, sizeof(text), "Mate in %d: %s%s", res_mate_moves, moves, full ? "" : " (the line is not complete)");
        print_wrapped(text, 2, NULL);
        return;
    }
    printf("No mate in %d found (depth %d searched; this is not a proof).\n", mate_target, res_depth);
}

static void cmd_eval(Position* pos) {
    history_check(pos);
    if (no_legal_moves(pos, "No evaluation: ")) return;
    bool network = false;
    const int v = console_static_eval(pos, &network);
    const int white = pos->to_move == WHITE ? v : -v;
    char score[24];
    format_eval(white, 0, score, sizeof(score));
    printf("Static evaluation: ");
    put_styled(eval_role(white, 0), score);
    printf(" (White's view, %s).\n", network ? "network" : "classical evaluation");
}

static void ponder_predict(Position* pos) {
    ponder_have = ponder_paused = ponder_ran = false;
    if (!res_info || !res_pv[0] || hist_count == 0) return;
    char t1[16], t2[16];
    const char* s = split_word(res_pv, t1, sizeof(t1), false);
    split_word(s, t2, sizeof(t2), false);
    if (!t2[0] || strcmp(t1, move_to_string(&hist[hist_count - 1].move)) != 0) return;
    Move m;
    if (parse_move_human(pos, t2, &m) != SAN_OK || !move_to_san(pos, m, ponder_san, sizeof(ponder_san))) return;
    ponder_move = m;
    snprintf(ponder_uci, sizeof(ponder_uci), "%s", move_to_string(&m));
    ponder_hash = pos->hash;
    ponder_have = true;
}

static bool ponder_start(Position* pos, char* line, size_t size) {
    if (!con_ponder || !ponder_have || ponder_paused || !game_on || game_over || pos->to_move != game_human) return false;
    if (strength_limited()) return false;
    if (game_tc.kind != TC_CLOCK && game_tc.kind != TC_MOVE) return false;
    if (pos->hash != ponder_hash || own_count > 0) return false;
    Move m;
    if (!legal_match(pos, &ponder_move, &m)) {
        ponder_have = false;
        return false;
    }
    ponder_saved = *pos;
    play_on_position(pos, &m);
    if (position_status(pos) >= ST_MATE) {
        *pos = ponder_saved;
        ponder_have = false;
        return false;
    }
    const Color eng = pos->to_move;
    char go[200];
    if (game_tc.kind == TC_MOVE) {
        snprintf(go, sizeof(go), "go ponder movetime %ld", game_tc.value * 1000L);
    } else {
        long long you = game_clock[game_human] - elapsed_ms(turn_start);
        if (you < 1) you = 1;
        const long long ce = game_clock[eng] > 0 ? game_clock[eng] : 1;
        const long long wt = eng == WHITE ? ce : you, bt = eng == WHITE ? you : ce;
        int len = snprintf(go, sizeof(go), "go ponder wtime %lld btime %lld winc %ld binc %ld", wt, bt, game_tc.inc_ms,
                           game_tc.inc_ms);
        if (game_tc.moves > 0)
            snprintf(go + len, sizeof(go) - (size_t)len, " movestogo %ld", game_tc.moves - game_moves_done[eng] % game_tc.moves);
    }
    search_line(pos, SK_PONDER, line, size, go);
    ponder_hit = false;
    ponder_echoed = false;
    ponder_ran = true;
    if (con_prompt) {
        char prompt[64];
        current_prompt(&ponder_saved, prompt, sizeof(prompt));
        snprintf(ed_prompt, sizeof(ed_prompt), "%s", prompt);
        fputs(prompt, stdout);
        fflush(stdout);
#ifdef _WIN32
        win_stdin_console_prompt(0);
#endif
    }
    return true;
}

static void ponder_echo(int i) {
    char typed[64], san[16];
    snprintf(typed, sizeof(typed), "%s", ponder_typed);
    size_t k = strlen(typed);
    while (k > 0 && strchr("+#!?", typed[k - 1])) typed[--k] = '\0';
    snprintf(san, sizeof(san), "%s", ponder_san);
    k = strlen(san);
    while (k > 0 && strchr("+#", san[k - 1])) san[--k] = '\0';
    if (!con_prompt || strcmp(typed, san) != 0) {
        char label[16];
        bool black;
        history_label(i, label, sizeof(label), &black);
        printf("%s %s\n", label, ponder_san);
    }
}

int console_line_in_search(const char* typed) {
    if (!con_on) return 0;
    if (search_kind == SK_ANALYZE) return 1;
    if (search_kind != SK_PONDER || ponder_hit) return 0;
    char w[64];
    const char* rest = split_word(typed, w, sizeof(w), false);
    if (w[0] && !rest[0]) {
        char core[64], san[16];
        snprintf(core, sizeof(core), "%s", w);
        size_t k = strlen(core);
        while (k > 0 && strchr("+#!?", core[k - 1])) core[--k] = '\0';
        for (char* p = core; *p; p++)
            if (*p == '0') *p = 'O';
        snprintf(san, sizeof(san), "%s", ponder_san);
        k = strlen(san);
        while (k > 0 && strchr("+#", san[k - 1])) san[--k] = '\0';
        if (strcmp(core, san) == 0 || strcasecmp(w, ponder_uci) == 0) {
            ponder_hit = true;
            ponder_hit_time = get_wall_time();
            snprintf(ponder_typed, sizeof(ponder_typed), "%s", w);
            echo_off_begin();

            if (con_post && !(game_tc.kind == TC_CLOCK &&
                              game_clock[game_human] - time_diff_ms(turn_start, ponder_hit_time) < 0)) {
                ponder_echo(hist_count);
                ponder_echoed = true;
                post_rows = true;
                post_head();
            }
            return 2;
        }
    }
    return 1;
}

void console_stop_in_search(void) {
    if (con_on && search_kind == SK_ANNOTATE) search_interrupted = true;
}

bool console_interrupt_taken(void) {
    if (!con_interrupt || !con_on || search_kind == SK_NONE) return false;
    con_interrupt = 0;
    search_interrupted = true;
    return true;
}

int console_read_interrupted(char* out, size_t size) {
    if (!con_on) return 0;
#ifdef _WIN32
    for (int i = 0; i < 25 && !con_interrupt; i++) Sleep(10);
#endif
    if (!con_interrupt) return 0;
    con_interrupt = 0;
    if (search_kind != SK_NONE) {
        snprintf(out, size, "stop");
        return 2;
    }
    if (interrupt_count > 0) return 0;
    interrupt_count = 1;
    printf("\n(To exit, press Ctrl+C again or type 'quit'.)\n");
    if (con_prompt) fputs(ed_prompt, stdout);
    fflush(stdout);
    return 1;
}

static void ponder_done(Position* pos, bool interrupted) {
    if (ponder_hit) {
        ponder_hit = false;
        ponder_have = false;
        ponder_ran = false;
        ponder_guesses++;
        ponder_hits++;
        const long long used = time_diff_ms(turn_start, ponder_hit_time);
        if (game_tc.kind == TC_CLOCK && game_clock[game_human] - used < 0) {
            *pos = ponder_saved;
            game_clock[game_human] -= used;
            game_time_loss(pos, game_human);
            return;
        }
        history_push(&ponder_move, ponder_san);
        hist_hash = pos->hash;
        redo_clear();

        if (!ponder_echoed) ponder_echo(hist_count - 1);
        ponder_echoed = false;
        clock_after_move(game_human, used);
        if (game_check_end(pos)) return;
        search_started = ponder_hit_time;
        game_engine_done(pos);
        return;
    }
    *pos = ponder_saved;
    char q[8];
    if (console_queued_line(q, sizeof(q))) queued_shown = true;
    else ponder_paused = true;
    if (interrupted) {
        ponder_paused = true;
        interrupt_count = 1;
        printf("\n(To exit, press Ctrl+C again or type 'quit'.)\n");
    }
}

static void cmd_ponder(const char* arg) {
    char w[16];
    split_word(arg, w, sizeof(w), true);
    if (strcmp(w, "on") == 0) con_ponder = true;
    else if (strcmp(w, "off") == 0) con_ponder = false;
    else if (w[0]) {
        printf("Usage: ponder on|off\n");
        return;
    }
    if (!w[0] && game_on)
        printf("Pondering: %s. This game: %d of %d of your moves guessed.\n", con_ponder ? "on" : "off", ponder_hits,
               ponder_guesses);
    else
        printf("Pondering: %s.\n", con_ponder ? "on" : "off");
}

#define PGN_TAGS 32
typedef struct {
    char name[PGN_TAGS][32];
    char value[PGN_TAGS][256];
    int tags;
    char fen[256];
    HistoryEntry* moves;
    int count, cap;
    char stop[200];
} PgnGame;

static void pgn_free(PgnGame* g) {
    free(g->moves);
    g->moves = NULL;
    g->count = g->cap = 0;
}

static const char* pgn_tag(const PgnGame* g, const char* name) {
    for (int i = 0; i < g->tags; i++)
        if (strcmp(g->name[i], name) == 0) return g->value[i];
    return NULL;
}

static void pgn_add_tag(PgnGame* g, const char* name, const char* value) {
    if (g->tags >= PGN_TAGS) return;
    snprintf(g->name[g->tags], sizeof(g->name[0]), "%s", name);
    snprintf(g->value[g->tags], sizeof(g->value[0]), "%s", value);
    g->tags++;
}

static bool pgn_add_move(PgnGame* g, const Move* m, const char* san) {
    if (g->count == g->cap) {
        const int cap = g->cap ? g->cap * 2 : 128;
        HistoryEntry* n = (HistoryEntry*)realloc(g->moves, (size_t)cap * sizeof(HistoryEntry));
        if (!n) return false;
        g->moves = n;
        g->cap = cap;
    }
    g->moves[g->count].move = *m;
    snprintf(g->moves[g->count].san, sizeof(g->moves[0].san), "%s", san);
    g->count++;
    return true;
}

static char* read_text_file(const char* name, char* err, size_t errn, bool argv_name) {
    errno = 0;
    FILE* f = open_named(name, "rb", argv_name);
    if (!f) {
        snprintf(err, errn, "Cannot read '%s': %s.", name, strerror(errno));
        return NULL;
    }
    size_t cap = 65536, used = 0;
    char* buf = (char*)malloc(cap);
    while (buf) {
        if (used + 4096 + 1 > cap) {
            char* n = (char*)realloc(buf, cap * 2);
            if (!n) { free(buf); buf = NULL; break; }
            buf = n;
            cap *= 2;
        }
        const size_t r = fread(buf + used, 1, cap - used - 1, f);
        used += r;
        if (r == 0) break;
    }
    const bool bad = ferror(f) != 0;
    fclose(f);
    if (!buf || bad) {
        free(buf);
        snprintf(err, errn, "Cannot read '%s'.", name);
        return NULL;
    }
    buf[used] = '\0';
    return buf;
}

static bool pgn_result_token(const char* t) {
    return strcmp(t, "1-0") == 0 || strcmp(t, "0-1") == 0 || strcmp(t, "1/2-1/2") == 0 || strcmp(t, "*") == 0;
}

static bool pgn_read(const char* name, int want, PgnGame* g, char* err, size_t errn, bool argv_name) {
    memset(g, 0, sizeof(*g));
    char* text = read_text_file(name, err, errn, argv_name);
    if (!text) return false;
    char (*toks)[32] = NULL;
    int ntok = 0, captok = 0;
    int game = 0;
    bool in_game = false, in_moves = false, line_start = true;
    const char* p = text;
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;
    while (*p) {
        const char c = *p;
        if (c == '\n') { line_start = true; p++; continue; }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') { p++; continue; }
        if (c == '%' && line_start) {
            while (*p && *p != '\n') p++;
            continue;
        }
        line_start = false;
        if (c == '[') {
            if (in_moves) {
                in_game = in_moves = false;
                if (game == want) break;
            }
            if (!in_game) { game++; in_game = true; }
            p++;
            char tname[32], tvalue[256];
            size_t k = 0;
            while (*p == ' ' || *p == '\t') p++;
            while (*p && *p != ' ' && *p != '\t' && *p != '"' && *p != ']' && *p != '\n') {
                if (k + 1 < sizeof(tname)) tname[k++] = *p;
                p++;
            }
            tname[k] = '\0';
            while (*p == ' ' || *p == '\t') p++;
            k = 0;
            if (*p == '"') {
                p++;
                while (*p && *p != '"' && *p != '\n') {
                    if (*p == '\\' && (p[1] == '"' || p[1] == '\\')) p++;
                    if (k + 1 < sizeof(tvalue)) tvalue[k++] = *p;
                    p++;
                }
                if (*p == '"') p++;
            }
            tvalue[k] = '\0';
            while (*p && *p != ']' && *p != '\n') p++;
            if (*p == ']') p++;
            if (game == want && tname[0]) pgn_add_tag(g, tname, tvalue);
            continue;
        }
        if (c == '{') {
            const char* e = strchr(p, '}');
            p = e ? e + 1 : p + strlen(p);
            continue;
        }
        if (c == ';') {
            while (*p && *p != '\n') p++;
            continue;
        }
        if (c == '(') {
            int depth = 0;
            while (*p) {
                if (*p == '{') {
                    const char* e = strchr(p, '}');
                    p = e ? e + 1 : p + strlen(p);
                    continue;
                }
                if (*p == ';') {
                    while (*p && *p != '\n') p++;
                    continue;
                }
                if (*p == '(') depth++;
                else if (*p == ')' && --depth == 0) { p++; break; }
                p++;
            }
            continue;
        }
        if (c == ')') { p++; continue; }
        if (c == '$') {
            p++;
            while (isdigit((unsigned char)*p)) p++;
            continue;
        }
        const char* t = p;
        while (*p && !strchr(" \t\r\n\f\v{}();[$", *p)) p++;
        char tok[32];
        size_t len = (size_t)(p - t);
        if (len >= sizeof(tok)) len = sizeof(tok) - 1;
        memcpy(tok, t, len);
        tok[len] = '\0';
        if (!in_game) { game++; in_game = true; }
        in_moves = true;
        if (pgn_result_token(tok)) {
            in_game = in_moves = false;
            if (game == want) break;
            continue;
        }
        if (game != want) continue;
        size_t k = 0;
        while (isdigit((unsigned char)tok[k])) k++;
        if (k > 0 && tok[k] == '.') {
            while (tok[k] == '.') k++;
        } else if (k > 0 && tok[k] == '\0') {
            continue;
        } else {
            k = 0;
            while (tok[k] == '.') k++;
        }
        const char* mv = tok + k;
        if (!mv[0] || strspn(mv, "!?") == strlen(mv) || is_ep_remark(mv)) continue;
        if (ntok == captok) {
            captok = captok ? captok * 2 : 256;
            char (*n)[32] = realloc(toks, (size_t)captok * sizeof(*toks));
            if (!n) break;
            toks = n;
        }
        snprintf(toks[ntok++], sizeof(toks[0]), "%s", mv);
    }
    free(text);
    if (game < want) {
        free(toks);
        if (game == 0) snprintf(err, errn, "No game in '%s'.", name);
        else snprintf(err, errn, "'%s' has %d game%s.", name, game, game == 1 ? "" : "s");
        return false;
    }
    Position tmp;
    const char* fen = pgn_tag(g, "FEN");
    if (fen && fen[0]) {
        if (!parse_fen(&tmp, fen)) {
            free(toks);
            snprintf(err, errn, "Game %d of '%s': its FEN tag is not a position.", want, name);
            return false;
        }
    } else {
        setup_starting_position(&tmp);
    }
    snprintf(g->fen, sizeof(g->fen), "%s", position_to_fen(&tmp));
    for (int i = 0; i < ntok; i++) {
        Move list[MAX_MOVES];
        int count = 0;
        bool shortcut = false;
        const SanResult r = san_read_move(&tmp, toks[i], list, &count, &shortcut);
        char san[16];
        if (r != SAN_OK || !move_to_san(&tmp, list[0], san, sizeof(san))) {
            char e[64];
            snprintf(g->stop, sizeof(g->stop), "Move %d%s %s is %s here; the game stops before it.", tmp.fullmove_number,
                     tmp.to_move == WHITE ? "." : "...", echo_word(toks[i], e, sizeof(e)),
                     r == SAN_AMBIGUOUS ? "ambiguous" : r == SAN_UNRECOGNIZED ? "not a move" : "illegal");
            break;
        }
        if (!pgn_add_move(g, &list[0], san)) break;
        play_on_position(&tmp, &list[0]);
    }
    free(toks);
    return true;
}

static void split_name_count(const char* arg, char* name, size_t n, long* count) {
    snprintf(name, n, "%s", arg);
    size_t len = strlen(name);
    while (len > 0 && (name[len - 1] == ' ' || name[len - 1] == '\t')) name[--len] = '\0';
    char* sp = strrchr(name, ' ');
    long v = 0;
    if (sp && sp[1] && parse_count(sp + 1, strlen(sp + 1), 1, 999999999L, &v)) {
        *count = v;
        while (sp > name && (sp[-1] == ' ' || sp[-1] == '\t')) sp--;
        *sp = '\0';
    }
}

static int full_moves(int plies, bool black) {
    return (plies + (black ? 1 : 0) + 1) / 2;
}

static ConsoleAction cmd_load(Position* pos, const char* arg, char* line, size_t size) {
    history_check(pos);
    char name[MAX_UCI_LINE];
    long n = 1;
    split_name_count(arg, name, sizeof(name), &n);
    if (!name[0]) {
        printf("Usage: load FILE [N], N = the game in the file (default 1).\n");
        return CONSOLE_DONE;
    }
    static PgnGame g;
    char err[MAX_UCI_LINE + 128];
    if (!pgn_read(name, (int)n, &g, err, sizeof(err), false)) {
        print_wrapped(err, 2, NULL);
        return CONSOLE_DONE;
    }
    game_stop();
    redo_clear();
    for (int i = g.count - 1; i >= 0; i--) redo_push(&g.moves[i]);
    record_clear();
    for (int i = 0; i < g.tags; i++) record_set(g.name[i], g.value[i]);
    snprintf(rec_file, sizeof(rec_file), "%s", name);
    rec_game = n;
    rec_on = true;
    const char* white = pgn_tag(&g, "White");
    const char* black = pgn_tag(&g, "Black");
    const char* result = pgn_tag(&g, "Result");
    char text[MAX_UCI_LINE + 700];
    const int moves = full_moves(g.count, strstr(g.fen, " b ") != NULL);
    snprintf(text, sizeof(text), "Loaded %s, game %ld: %s - %s, %d move%s (%s). Type 'next' to step through it.", name, n,
             white && white[0] ? white : "?", black && black[0] ? black : "?", moves, moves == 1 ? "" : "s",
             result && result[0] ? result : "*");
    print_wrapped(text, 2, NULL);
    if (g.stop[0]) print_wrapped(g.stop, 2, NULL);
    snprintf(line, size, "position fen %s", g.fen);
    pgn_free(&g);
    sync_kind = SYNC_RESET;
    capture_begin();
    own_push(OWN_SHOW);
    return CONSOLE_PASS;
}

static void nav_show(Position* pos) {
    const int total = hist_count + redo_count;
    if (hist_count == 0) {
        printf("At the start (0 of %d half-moves).\n", total);
    } else {
        char label[16];
        bool black;
        history_label(hist_count - 1, label, sizeof(label), &black);
        printf("After %s %s (%d of %d half-moves).\n", label, hist[hist_count - 1].san, hist_count, total);
    }
    print_board(pos);
}

static ConsoleAction cmd_nav(Position* pos, CommandId id, const char* arg, char* line, size_t size) {
    history_check(pos);
    if (game_on && !game_over) {
        printf("Not during a game: 'undo' and 'redo' take moves back and replay them.\n");
        return CONSOLE_DONE;
    }
    const int total = hist_count + redo_count;
    long target = hist_count;
    char w[32];
    split_word(arg, w, sizeof(w), false);
    switch (id) {
        case C_FIRST: target = 0; break;
        case C_LAST:  target = total; break;
        case C_PREV:
        case C_NEXT: {
            long k = 1;
            if (w[0] && !parse_count(w, strlen(w), 1, 999999999L, &k)) {
                printf("Usage: %s [K], K = half-moves (default 1).\n", id == C_PREV ? "prev" : "next");
                return CONSOLE_DONE;
            }
            target = id == C_PREV ? hist_count - k : hist_count + k;
            if (target < 0) target = 0;
            if (target > total) target = total;
            break;
        }
        case C_GOTO: {
            size_t len = strlen(w);
            bool black = false;
            if (len >= 3 && strcmp(w + len - 3, "...") == 0) { black = true; len -= 3; }
            else if (len >= 1 && w[len - 1] == '.') len--;
            long m = 0;
            if (len == 0 || !parse_count(w, len, 0, 99999L, &m)) {
                printf("Usage: goto M (after White's move M), goto M... (after Black's), goto 0 (the start).\n");
                return CONSOLE_DONE;
            }
            if (m == 0) {
                target = 0;
                break;
            }
            const long idx = 2 * (m - hist_base_fullmove) + (black ? 1 : 0) - (hist_base_black ? 1 : 0);
            if (idx < 0 || idx >= total) {
                printf("No move %ld%s in this game.\n", m, black ? "..." : ".");
                return CONSOLE_DONE;
            }
            target = idx + 1;
            break;
        }
        default: break;
    }
    if (game_on && game_over) {
        game_viewed = true;
        game_viewed_time = game_time_end;
    }
    game_on = game_over = game_time_end = draw_offer = false;
    if (target == hist_count) {
        nav_show(pos);
        return CONSOLE_DONE;
    }
    if (target > hist_count) {
        HistoryEntry* saved = (HistoryEntry*)malloc((size_t)redo_count * sizeof(HistoryEntry));
        int saved_count = redo_count;
        if (!saved) return CONSOLE_DONE;
        memcpy(saved, redo_stack, (size_t)redo_count * sizeof(HistoryEntry));
        for (long i = hist_count; i < target; i++) {
            const HistoryEntry e = saved[saved_count - 1];
            Move m;
            if (!legal_match(pos, &e.move, &m)) {
                printf("Cannot replay '%s' in this position.\n", e.san);
                saved_count = 0;
                break;
            }
            typed_move_play(pos, &m, false, false);
            saved_count--;
        }
        for (int i = 0; i < saved_count; i++) redo_push(&saved[i]);
        free(saved);
        nav_show(pos);
        return CONSOLE_DONE;
    }

    size_t len = (size_t)snprintf(line, size, "position fen %s%s", hist_base_fen, target > 0 ? " moves" : "");
    for (long i = 0; i < target && len < size; i++)
        len += (size_t)snprintf(line + len, size - len, " %s", move_to_string(&hist[i].move));
    if (len >= size) {
        printf("The game is too long to go back.\n");
        line[0] = '\0';
        return CONSOLE_DONE;
    }
    for (int i = hist_count - 1; i >= (int)target; i--) redo_push(&hist[i]);
    snprintf(sync_line, sizeof(sync_line), "%s", line);
    sync_kind = SYNC_POSITION;
    capture_begin();
    own_push(OWN_SHOW);
    return CONSOLE_PASS;
}

#define ANNOTATE_SCALE 1.0
#define ANNOTATE_MATE 10000

typedef struct {
    bool on, from_argv;
    PgnGame g;
    char title[MAX_UCI_LINE + 64];
    char out[MAX_UCI_LINE];
    char go[64];
    int phase;
    int at;
    int next;
    int base_full;
    bool base_black;
    int* cp;
    int* mate;
    bool* known;
    char (*best)[24];
    Move* best_move;
    int* mark;
    int marks[2][3];
    long loss[2];
    int judged[2];
} Annotation;
static Annotation ann;
static char restore_buf[MAX_UCI_LINE];

static double win_chance(int cp) {
    return 2.0 / (1.0 + exp(-0.00368208 * (double)cp * ANNOTATE_SCALE)) - 1.0;
}

static void ann_free(void) {
    free(ann.cp); free(ann.mate); free(ann.known); free(ann.best); free(ann.best_move); free(ann.mark);
    ann.cp = ann.mate = ann.mark = NULL;
    ann.known = NULL;
    ann.best = NULL;
    ann.best_move = NULL;
    pgn_free(&ann.g);
    ann.on = false;
}

static void ann_score(int i, char* buf, size_t n) {
    const bool white = ((i + (ann.base_black ? 1 : 0)) % 2) == 0;
    const int s = white ? 1 : -1;
    if (ann.mate[i] != 0) format_eval(0, s * ann.mate[i], buf, n);
    else format_eval(s * ann.cp[i], 0, buf, n);
}

static void ann_label(int j, char* buf, size_t n, bool* black) {
    const int ply = j + (ann.base_black ? 1 : 0);
    *black = (ply % 2) == 1;
    snprintf(buf, n, *black ? "%d..." : "%d.", ann.base_full + ply / 2);
}

static void ann_judge(int j) {
    if (!ann.known[j] || !ann.known[j + 1]) return;
    bool black;
    char label[16];
    ann_label(j, label, sizeof(label), &black);
    const int side = black ? 1 : 0;
    const int before = ann.cp[j], after = -ann.cp[j + 1];
    const Move* played = &ann.g.moves[j].move;
    const bool best = ann.best[j][0] && ann.best_move[j].from == played->from && ann.best_move[j].to == played->to &&
                      ann.best_move[j].promotion == played->promotion;
    const double drop = win_chance(before) - win_chance(after);
    int mark = 0;
    if (!best) mark = drop >= 0.3 ? 3 : drop >= 0.2 ? 2 : drop >= 0.1 ? 1 : 0;
    const int b = before > 1000 ? 1000 : before < -1000 ? -1000 : before;
    const int a = after > 1000 ? 1000 : after < -1000 ? -1000 : after;
    ann.loss[side] += (best || b <= a) ? 0 : b - a;
    ann.judged[side]++;
    ann.mark[j] = mark;
    if (!mark) return;
    ann.marks[side][mark - 1]++;
    static const char* const GLYPH[4] = { "", "?!", "?", "??" };
    char head[48], sb[24], sa[24], better[40];
    snprintf(head, sizeof(head), black ? "%6s %s%s" : "%4s %s%s", label, ann.g.moves[j].san, GLYPH[mark]);
    ann_score(j, sb, sizeof(sb));
    ann_score(j + 1, sa, sizeof(sa));
    snprintf(better, sizeof(better), "%s", ann.best[j]);
    printf("%-15s %6s -> %-6s (White)   better: %-11s %s\n", head, sb, sa, better, sb);
}

static void ann_write(void) {
    int games = 0;
    bool ends_blank = true, ends_newline = true;
    const bool exists = pgn_existing(ann.out, &games, &ends_blank, &ends_newline, ann.from_argv);
    errno = 0;
    FILE* f = open_named(ann.out, "a", ann.from_argv);
    if (!f) {
        char text[MAX_UCI_LINE + 128];
        snprintf(text, sizeof(text), "Cannot write '%s': %s.", ann.out, strerror(errno));
        print_wrapped(text, 2, NULL);
        return;
    }
    if (exists && !ends_blank) fputs(ends_newline ? "\n" : "\n\n", f);
    bool annotator = false;
    for (int i = 0; i < ann.g.tags; i++) {
        fprintf(f, "[%s \"", ann.g.name[i]);
        for (const char* v = ann.g.value[i]; *v; v++) {
            if (*v == '"' || *v == '\\') fputc('\\', f);
            fputc(*v, f);
        }
        fputs("\"]\n", f);
        if (strcmp(ann.g.name[i], "Annotator") == 0) annotator = true;
    }
    if (!annotator) fprintf(f, "[Annotator \"Chessenger %s\"]\n", ENGINE_VERSION);
    fputc('\n', f);
    PgnOut o = { f, 0 };
    bool after_comment = false;
    for (int j = 0; j < ann.g.count; j++) {
        char label[16], token[48];
        bool black;
        ann_label(j, label, sizeof(label), &black);
        if (!black || j == 0 || after_comment) snprintf(token, sizeof(token), "%s %s", label, ann.g.moves[j].san);
        else snprintf(token, sizeof(token), "%s", ann.g.moves[j].san);
        pgn_token(&o, token);
        after_comment = false;
        if (ann.mark[j]) {
            static const char* const NAG[4] = { "", "$6", "$2", "$4" };
            char sb[24], sa[24], comment[96];
            pgn_token(&o, NAG[ann.mark[j]]);
            ann_score(j, sb, sizeof(sb));
            ann_score(j + 1, sa, sizeof(sa));
            snprintf(comment, sizeof(comment), "{%s -> %s; better %s %s}", sb, sa, ann.best[j], sb);
            pgn_token(&o, comment);
            after_comment = true;
        }
    }
    const char* result = pgn_tag(&ann.g, "Result");
    pgn_token(&o, result && pgn_result_token(result) ? result : "*");
    fputs("\n\n", f);
    const bool write_error = ferror(f) != 0;
    if (fclose(f) != 0 || write_error) {
        char text[MAX_UCI_LINE + 128];
        snprintf(text, sizeof(text), "Cannot write '%s'.", ann.out);
        print_wrapped(text, 2, NULL);
        return;
    }
    char text[MAX_UCI_LINE + 64];
    snprintf(text, sizeof(text), "Saved: %s (game %d).", ann.out, games + 1);
    print_wrapped(text, 2, NULL);
}

static void ann_finish(bool stopped) {
    const int done = ann.judged[0] + ann.judged[1];
    if (stopped) printf("Stopped: %d of %d half-moves annotated.\n", done, ann.g.count);
    for (int side = 0; side < 2; side++) {
        const long avg = ann.judged[side] ? (ann.loss[side] + ann.judged[side] / 2) / ann.judged[side] : 0;
        printf("Summary  %s: %d ?!  %d ?  %d ??   average loss %ld cp\n", side == 0 ? "White" : "Black",
               ann.marks[side][0], ann.marks[side][1], ann.marks[side][2], avg);
    }
    ann_write();
    const bool quit = ann.from_argv;
    ann_free();
    own_count = 0;
    own_push(OWN_RESTORE);
    if (quit) own_push(OWN_QUIT);
}

static bool restore_line(char* line, size_t size) {
    if (!restore_buf[0]) return false;
    snprintf(line, size, "%s", restore_buf);
    snprintf(sync_line, sizeof(sync_line), "%s", restore_buf);
    sync_kind = SYNC_POSITION;
    restore_buf[0] = '\0';
    return true;
}

static void cmd_annotate(Position* pos, const char* arg, bool from_argv) {
    history_check(pos);
    if (game_on && !game_over) {
        printf("Not during a game: finish it ('resign'), then 'annotate'.\n");
        if (from_argv) own_push(OWN_QUIT);
        return;
    }

    char buf[MAX_UCI_LINE], per[48] = "1 s per move";
    snprintf(buf, sizeof(buf), "%s", arg);
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == ' ' || buf[len - 1] == '\t')) buf[--len] = '\0';
    snprintf(ann.go, sizeof(ann.go), "go movetime 1000");
    char* last = strrchr(buf, ' ');
    char* word = last ? last + 1 : buf;
    long v = 0;
    bool bad = false;
    if (last) {
        *last = '\0';
        char* prev = strrchr(buf, ' ');
        char* kw = prev ? prev + 1 : buf;
        if ((strcasecmp(kw, "depth") == 0 || strcasecmp(kw, "nodes") == 0)) {
            const bool depth = strcasecmp(kw, "depth") == 0;
            if (!parse_count(word, strlen(word), 1, depth ? MAX_DEPTH : 999999999L, &v)) bad = true;
            else {
                snprintf(ann.go, sizeof(ann.go), "go %s %ld", depth ? "depth" : "nodes", v);
                snprintf(per, sizeof(per), "%s %ld per move", depth ? "depth" : "nodes", v);
                if (prev) *prev = '\0'; else buf[0] = '\0';
            }
            word = NULL;
        } else {
            *last = ' ';
        }
    }
    if (word && !bad) {
        char* end = NULL;
        const double sec = strtod(word, &end);
        if (end && end != word && *end == '\0' && strspn(word, "0123456789.") == strlen(word)) {
            if (sec < 0.1 || sec > 3600) bad = true;
            else {
                snprintf(ann.go, sizeof(ann.go), "go movetime %ld", (long)(sec * 1000.0 + 0.5));
                snprintf(per, sizeof(per), "%g s per move", sec);
                if (word == buf) buf[0] = '\0'; else word[-1] = '\0';
            }
        }
    }
    len = strlen(buf);
    while (len > 0 && (buf[len - 1] == ' ' || buf[len - 1] == '\t')) buf[--len] = '\0';
    if (bad) {
        printf("Usage: annotate [FILE] [S], S = seconds a move (0.1-3600, default 1), or 'depth N'.\n");
        if (from_argv) own_push(OWN_QUIT);
        return;
    }

    size_t rl = (size_t)snprintf(restore_buf, sizeof(restore_buf), "position fen %s%s", hist_base_fen,
                                 hist_count > 0 ? " moves" : "");
    for (int i = 0; i < hist_count && rl < sizeof(restore_buf); i++)
        rl += (size_t)snprintf(restore_buf + rl, sizeof(restore_buf) - rl, " %s", move_to_string(&hist[i].move));
    if (rl >= sizeof(restore_buf)) snprintf(restore_buf, sizeof(restore_buf), "position fen %s", position_to_fen(pos));
    memset(&ann.g, 0, sizeof(ann.g));
    if (buf[0]) {
        char err[MAX_UCI_LINE + 128];
        if (!pgn_read(buf, 1, &ann.g, err, sizeof(err), from_argv)) {
            print_wrapped(err, 2, NULL);
            restore_buf[0] = '\0';
            if (from_argv) own_push(OWN_QUIT);
            return;
        }
        snprintf(ann.title, sizeof(ann.title), "%s, game 1", buf);
        size_t k = strlen(buf);
        if (k > 4 && strcasecmp(buf + k - 4, ".pgn") == 0) k -= 4;
        snprintf(ann.out, sizeof(ann.out), "%.*s-annotated.pgn", (int)k, buf);
    } else {

        time_t now = time(NULL);
        struct tm* lt = localtime(&now);
        char date[16] = "????.??.??", engine[64], tc[48];
        snprintf(engine, sizeof(engine), "Chessenger %s", ENGINE_VERSION);
        if (lt) snprintf(date, sizeof(date), "%04d.%02d.%02d", lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday);
        pgn_time_control(tc, sizeof(tc));
        Position start;
        setup_starting_position(&start);
        const bool set_up = strcmp(hist_base_fen, position_to_fen(&start)) != 0;
        if (rec_on) {
            for (int i = 0; i < rec_tags; i++) pgn_add_tag(&ann.g, rec_name[i], rec_value[i]);
            if (set_up && !record_get("FEN")) {
                pgn_add_tag(&ann.g, "SetUp", "1");
                pgn_add_tag(&ann.g, "FEN", hist_base_fen);
            }
        } else {
            pgn_add_tag(&ann.g, "Event", "Casual game");
            pgn_add_tag(&ann.g, "Site", "?");
            pgn_add_tag(&ann.g, "Date", date);
            pgn_add_tag(&ann.g, "Round", "-");

            const bool named = game_on || game_viewed;
            pgn_add_tag(&ann.g, "White", !named ? "?" : game_human == WHITE ? "You" : engine);
            pgn_add_tag(&ann.g, "Black", !named ? "?" : game_human == BLACK ? "You" : engine);
            pgn_add_tag(&ann.g, "Result", game_viewed ? game_result : redo_count == 0 ? pgn_result(pos) : "*");
            pgn_add_tag(&ann.g, "TimeControl", tc);
            if (set_up) {
                pgn_add_tag(&ann.g, "SetUp", "1");
                pgn_add_tag(&ann.g, "FEN", hist_base_fen);
            }
            if ((game_on && game_over && game_time_end) || (game_viewed && game_viewed_time))
                pgn_add_tag(&ann.g, "Termination", "time forfeit");
        }
        snprintf(ann.g.fen, sizeof(ann.g.fen), "%s", hist_base_fen);
        for (int i = 0; i < hist_count; i++) pgn_add_move(&ann.g, &hist[i].move, hist[i].san);
        for (int i = redo_count - 1; i >= 0; i--) pgn_add_move(&ann.g, &redo_stack[i].move, redo_stack[i].san);
        if (rec_on && rec_file[0]) {
            snprintf(ann.title, sizeof(ann.title), "%s, game %ld", rec_file, rec_game);
            size_t k = strlen(rec_file);
            if (k > 4 && strcasecmp(rec_file + k - 4, ".pgn") == 0) k -= 4;
            snprintf(ann.out, sizeof(ann.out), "%.*s-annotated.pgn", (int)k, rec_file);
        } else {
            snprintf(ann.title, sizeof(ann.title), "the game on the board");
            if (lt) snprintf(ann.out, sizeof(ann.out), "chessenger-%04d-%02d-%02d-annotated.pgn", lt->tm_year + 1900,
                             lt->tm_mon + 1, lt->tm_mday);
            else snprintf(ann.out, sizeof(ann.out), "chessenger-annotated.pgn");
        }
    }
    if (ann.g.count == 0) {
        printf("No moves to annotate.\n");
        pgn_free(&ann.g);
        restore_buf[0] = '\0';
        if (from_argv) own_push(OWN_QUIT);
        return;
    }
    const int n = ann.g.count + 1;
    ann.cp = (int*)calloc((size_t)n, sizeof(int));
    ann.mate = (int*)calloc((size_t)n, sizeof(int));
    ann.mark = (int*)calloc((size_t)n, sizeof(int));
    ann.known = (bool*)calloc((size_t)n, sizeof(bool));
    ann.best = calloc((size_t)n, sizeof(*ann.best));
    ann.best_move = (Move*)calloc((size_t)n, sizeof(Move));
    if (!ann.cp || !ann.mate || !ann.mark || !ann.known || !ann.best || !ann.best_move) {
        ann_free();
        printf("Not enough memory to annotate.\n");
        restore_buf[0] = '\0';
        return;
    }
    Position base;
    parse_fen(&base, ann.g.fen);
    ann.base_full = base.fullmove_number > 0 ? base.fullmove_number : 1;
    ann.base_black = base.to_move == BLACK;
    memset(ann.marks, 0, sizeof(ann.marks));
    ann.loss[0] = ann.loss[1] = 0;
    ann.judged[0] = ann.judged[1] = 0;
    ann.phase = 0;
    ann.at = 0;
    ann.next = 0;
    ann.from_argv = from_argv;
    ann.on = true;
    const char* white = pgn_tag(&ann.g, "White");
    const char* black = pgn_tag(&ann.g, "Black");
    char text[MAX_UCI_LINE + 700];
    const int moves = full_moves(ann.g.count, ann.base_black);
    snprintf(text, sizeof(text), "Annotating %s: %s - %s, %d move%s, %s.", ann.title, white && white[0] ? white : "?",
             black && black[0] ? black : "?", moves, moves == 1 ? "" : "s", per);
    print_wrapped(text, 2, NULL);
    if (ann.g.stop[0]) print_wrapped(ann.g.stop, 2, NULL);
    game_view_keep();
    game_on = game_over = false;
    own_push(OWN_ANNOTATE);
}

static bool annotate_step(Position* pos, char* line, size_t size) {
    if (!ann.on) return false;
    if (ann.phase == 0) {
        ann.phase = 1;
        snprintf(line, size, "position fen %s", ann.g.fen);
        own_push(OWN_ANNOTATE);
        return true;
    }
    for (;;) {
        const int i = ann.next;
        if (i > ann.g.count) {
            ann_finish(false);
            return false;
        }
        while (ann.at < i) {
            Move m = ann.g.moves[ann.at].move;
            play_on_position(pos, &m);
            ann.at++;
        }
        Move list[MAX_MOVES];
        if (generate_legal_moves(pos, list) == 0) {
            const bool mate = is_in_check(pos, pos->to_move);
            ann.known[i] = true;
            ann.cp[i] = mate ? -ANNOTATE_MATE : 0;
            ann.mate[i] = 0;
            ann.best[i][0] = '\0';
            if (i > 0) ann_judge(i - 1);
            ann.next++;
            continue;
        }
        return search_line(pos, SK_ANNOTATE, line, size, ann.go);
    }
}

static void annotate_result(Position* pos, bool interrupted) {
    if (!ann.on) return;
    if (interrupted) {
        ann_finish(true);
        return;
    }
    const int i = ann.next;
    Move m;
    ann.known[i] = res_info;
    ann.mate[i] = res_mate ? res_mate_moves : 0;
    ann.cp[i] = res_mate ? (res_mate_moves > 0 ? ANNOTATE_MATE : -ANNOTATE_MATE) : res_cp;
    ann.best[i][0] = '\0';
    if (res_move_ok && legal_match(pos, &res_move, &m)) {
        ann.best_move[i] = m;
        numbered_san(pos, &m, ann.best[i], sizeof(ann.best[i]));
    }
    if (i > 0) ann_judge(i - 1);
    ann.next++;
    own_push(OWN_ANNOTATE);
}

static void print_banner(void) {
    if (!capture_begin()) {
        console_banner(stdout);
        return;
    }
    console_banner(stdout);
    capture_end();
    const char* s = cap_text;
    while (*s) {
        const char* e = strchr(s, '\n');
        size_t len = e ? (size_t)(e - s) : strlen(s);
        char ln[1024];
        size_t k = len < sizeof(ln) - 1 ? len : sizeof(ln) - 1;
        memcpy(ln, s, k);
        ln[k] = '\0';
        if (strncmp(ln, "Build  : ", 9) == 0) print_wrapped(ln, 9, " - ");
        else print_wrapped(ln, 0, NULL);
        s = e ? e + 1 : s + len;
    }
}

static char ed_prompt[64] = "";

#ifndef _WIN32
#define ED_HISTORY 100
#define ED_FD STDIN_FILENO
static bool ed_ok = false;
static struct termios ed_cooked;
static struct termios ed_rawmode;
static volatile sig_atomic_t ed_reading = 0;
static volatile sig_atomic_t ed_redraw = 0;
static bool ed_handlers = false;

static const int ED_FATAL[3] = { SIGQUIT, SIGTERM, SIGHUP };
static struct sigaction ed_old_fatal[3], ed_old_tstp, ed_old_cont;
static char* ed_hist[ED_HISTORY];
static int ed_hist_count = 0;

static struct termios echo_saved, echo_offmode;
static volatile sig_atomic_t echo_is_off = 0;
static struct sigaction int_old;
static bool int_installed = false;

static void editor_restore(void) {
    if (ed_reading) tcsetattr(STDIN_FILENO, TCSANOW, &ed_cooked);
    if (echo_is_off) tcsetattr(STDIN_FILENO, TCSANOW, &echo_saved);
}

static void ed_on_fatal(int sig) {
    if (ed_reading) tcsetattr(STDIN_FILENO, TCSANOW, &ed_cooked);
    if (echo_is_off) tcsetattr(STDIN_FILENO, TCSANOW, &echo_saved);
    signal(sig, SIG_DFL);
    raise(sig);
}

static void ed_on_tstp(int sig) {
    const int saved_errno = errno;
    (void)sig;
    if (ed_reading) tcsetattr(STDIN_FILENO, TCSANOW, &ed_cooked);
    if (echo_is_off) tcsetattr(STDIN_FILENO, TCSANOW, &echo_saved);
    struct sigaction dfl, mine;
    memset(&dfl, 0, sizeof(dfl));
    dfl.sa_handler = SIG_DFL;
    sigemptyset(&dfl.sa_mask);
    sigaction(SIGTSTP, &dfl, &mine);
    sigset_t m;
    sigemptyset(&m);
    sigaddset(&m, SIGTSTP);
    sigprocmask(SIG_UNBLOCK, &m, NULL);
    raise(SIGTSTP);
    sigaction(SIGTSTP, &mine, NULL);
    errno = saved_errno;
}

static void ed_on_cont(int sig) {
    (void)sig;
    if (ed_reading) {
        tcsetattr(STDIN_FILENO, TCSANOW, &ed_rawmode);
        ed_redraw = 1;
    } else if (echo_is_off) {
        tcsetattr(STDIN_FILENO, TCSANOW, &echo_offmode);
    }
}

static void ed_install(void) {
    if (ed_handlers) return;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = ed_on_fatal;
    for (int i = 0; i < 3; i++) sigaction(ED_FATAL[i], &sa, &ed_old_fatal[i]);
    sa.sa_handler = ed_on_tstp;
    sigaction(SIGTSTP, &sa, &ed_old_tstp);
    sa.sa_handler = ed_on_cont;
    sigaction(SIGCONT, &sa, &ed_old_cont);
    ed_handlers = true;
}

static void ed_uninstall(void) {
    if (!ed_handlers) return;
    for (int i = 0; i < 3; i++) sigaction(ED_FATAL[i], &ed_old_fatal[i], NULL);
    sigaction(SIGTSTP, &ed_old_tstp, NULL);
    sigaction(SIGCONT, &ed_old_cont, NULL);
    ed_handlers = false;
}

static void echo_off_begin(void) {
    if (!console_editor_on() || echo_is_off) return;
    if (tcgetattr(STDIN_FILENO, &echo_saved) != 0) return;
    ed_install();
    echo_offmode = echo_saved;
    echo_offmode.c_lflag &= ~(tcflag_t)ECHO;
    echo_is_off = 1;
    tcsetattr(STDIN_FILENO, TCSANOW, &echo_offmode);
}

static void echo_off_end(void) {
    if (!echo_is_off) return;
    tcsetattr(STDIN_FILENO, TCSANOW, &echo_saved);
    echo_is_off = 0;
}

static void con_on_interrupt(int sig) {
    (void)sig;
    con_interrupt = 1;
}

static void interrupt_install(void) {
    if (int_installed) return;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = con_on_interrupt;
    int_installed = sigaction(SIGINT, &sa, &int_old) == 0;
}

static void interrupt_uninstall(void) {
    if (!int_installed) return;
    sigaction(SIGINT, &int_old, NULL);
    int_installed = false;
}

static void ed_draw(const char* buf, int len, int cur, int* off) {
    int cols = CON_DEFAULT_WIDTH;
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) cols = ws.ws_col;
    const int plen = (int)strlen(ed_prompt);
    int avail = cols - plen - 1;
    if (avail < 8) avail = 8;
    if (cur < *off) *off = cur;
    if (cur > *off + avail) *off = cur - avail;
    if (*off > 0 && len - *off < avail) *off = len - avail > 0 ? len - avail : 0;
    int shown = len - *off;
    if (shown > avail) shown = avail;
    fputc('\r', stdout);
    fputs(ed_prompt, stdout);
    fwrite(buf + *off, 1, (size_t)shown, stdout);
    fputs("\033[K\r", stdout);
    if (plen + cur - *off > 0) printf("\033[%dC", plen + cur - *off);
    fflush(stdout);
}

static int ed_byte(int ms) {
    struct pollfd p = { STDIN_FILENO, POLLIN, 0 };
    if (poll(&p, 1, ms) <= 0) return -1;
    unsigned char c;
    return read(ED_FD, &c, 1) == 1 ? c : -1;
}

typedef enum { EK_NONE, EK_UP, EK_DOWN, EK_LEFT, EK_RIGHT, EK_HOME, EK_END, EK_DELETE } EdKey;

static EdKey ed_escape(void) {
    const int c1 = ed_byte(50);
    if (c1 == 'O') {
        switch (ed_byte(50)) {
            case 'A': return EK_UP;
            case 'B': return EK_DOWN;
            case 'C': return EK_RIGHT;
            case 'D': return EK_LEFT;
            case 'H': return EK_HOME;
            case 'F': return EK_END;
            default:  return EK_NONE;
        }
    }
    if (c1 != '[') return EK_NONE;
    char phalanx_cnt[16];
    int np = 0, fin = -1;
    for (;;) {
        const int b = ed_byte(50);
        if (b < 0) return EK_NONE;
        if (b >= 0x30 && b <= 0x3F) { if (np + 1 < (int)sizeof(phalanx_cnt)) phalanx_cnt[np++] = (char)b; continue; }
        if (b >= 0x20 && b <= 0x2F) { np = (int)sizeof(phalanx_cnt); continue; }
        fin = b;
        break;
    }
    if (np >= (int)sizeof(phalanx_cnt)) return EK_NONE;
    phalanx_cnt[np] = '\0';
    if (np == 0) {
        switch (fin) {
            case 'A': return EK_UP;
            case 'B': return EK_DOWN;
            case 'C': return EK_RIGHT;
            case 'D': return EK_LEFT;
            case 'H': return EK_HOME;
            case 'F': return EK_END;
            default:  return EK_NONE;
        }
    }
    if (fin == '~') {
        if (strcmp(phalanx_cnt, "1") == 0 || strcmp(phalanx_cnt, "7") == 0) return EK_HOME;
        if (strcmp(phalanx_cnt, "4") == 0 || strcmp(phalanx_cnt, "8") == 0) return EK_END;
        if (strcmp(phalanx_cnt, "3") == 0) return EK_DELETE;
    }
    return EK_NONE;
}

static void ed_history_add(const char* line) {
    if (!line[0]) return;
    if (ed_hist_count > 0 && strcmp(ed_hist[ed_hist_count - 1], line) == 0) return;
    char* copy = (char*)malloc(strlen(line) + 1);
    if (!copy) return;
    strcpy(copy, line);
    if (ed_hist_count == ED_HISTORY) {
        free(ed_hist[0]);
        memmove(ed_hist, ed_hist + 1, (ED_HISTORY - 1) * sizeof(ed_hist[0]));
        ed_hist_count--;
    }
    ed_hist[ed_hist_count++] = copy;
}

bool console_editor_on(void) {
    return con_on && ed_ok;
}

bool console_edit_line(char* out, size_t size, const char* start) {
    char buf[MAX_UCI_LINE], fresh[MAX_UCI_LINE];
    int len = 0, cur = 0, off = 0, hpos = ed_hist_count;
    const int cap = (int)(size < sizeof(buf) ? size : sizeof(buf)) - 1;
    for (const char* p = start ? start : ""; *p && len < cap; p++)
        if (*p >= 32 && *p < 127) buf[len++] = *p;
    cur = len;
    fresh[0] = '\0';
    fflush(stdout);
    if (tcgetattr(STDIN_FILENO, &ed_cooked) != 0) {
        fputs(ed_prompt, stdout);
        fflush(stdout);
        snprintf(out, size, "%.*s", len, buf);
        const size_t used = strlen(out);
        if (fgets(out + used, (int)(size - used), stdin) == NULL) return used > 0;
        out[strcspn(out, "\r\n")] = '\0';
        return true;
    }
    ed_rawmode = ed_cooked;
    ed_rawmode.c_lflag &= ~(tcflag_t)(ICANON | ECHO | IEXTEN);
    ed_rawmode.c_cc[VMIN] = 1;
    ed_rawmode.c_cc[VTIME] = 0;
    ed_install();
    ed_reading = 1;
    tcsetattr(STDIN_FILENO, TCSANOW, &ed_rawmode);
    ed_draw(buf, len, cur, &off);
    bool line_done = false, leave = false;
    for (;;) {
        unsigned char c;
        if (con_interrupt) {
            con_interrupt = 0;
            if (search_kind != SK_NONE) {
                snprintf(buf, sizeof(buf), "stop");
                len = 4;
                line_done = true;
                break;
            }
            if (interrupt_count > 0) {
                leave = true;
                break;
            }
            interrupt_count = 1;
            len = cur = off = 0;
            fputs("\n(To exit, press Ctrl+C again or type 'quit'.)\n", stdout);
            ed_draw(buf, len, cur, &off);
            continue;
        }
        const ssize_t r = read(ED_FD, &c, 1);
        if (r < 0 && errno == EINTR) {
            if (ed_redraw) {
                ed_redraw = 0;
                ed_draw(buf, len, cur, &off);
            }
            continue;
        }
        if (r <= 0) break;
        interrupt_count = 0;
        if (c == '\r' || c == '\n') { line_done = true; break; }
        EdKey key = EK_NONE;
        if (c == 27) key = ed_escape();
        else if (c == 1) key = EK_HOME;
        else if (c == 5) key = EK_END;
        else if (c == 4) {
            if (len == 0) break;
            key = EK_DELETE;
        } else if (c == 127 || c == 8) {
            if (cur > 0) {
                memmove(buf + cur - 1, buf + cur, (size_t)(len - cur));
                cur--;
                len--;
            }
        } else if (c == 21) {
            len = cur = 0;
        } else if (c >= 32 && c < 127) {
            if (len < cap) {
                memmove(buf + cur + 1, buf + cur, (size_t)(len - cur));
                buf[cur++] = (char)c;
                len++;
            }
        }
        switch (key) {
            case EK_LEFT:   if (cur > 0) cur--; break;
            case EK_RIGHT:  if (cur < len) cur++; break;
            case EK_HOME:   cur = 0; break;
            case EK_END:    cur = len; break;
            case EK_DELETE:
                if (cur < len) {
                    memmove(buf + cur, buf + cur + 1, (size_t)(len - cur - 1));
                    len--;
                }
                break;
            case EK_UP:
            case EK_DOWN: {
                const int to = key == EK_UP ? hpos - 1 : hpos + 1;
                if (to < 0 || to > ed_hist_count) break;
                if (hpos == ed_hist_count) snprintf(fresh, sizeof(fresh), "%.*s", len, buf);
                hpos = to;
                const char* src = hpos == ed_hist_count ? fresh : ed_hist[hpos];
                len = (int)strlen(src);
                if (len > cap) len = cap;
                memcpy(buf, src, (size_t)len);
                cur = len;
                off = 0;
                break;
            }
            default: break;
        }
        ed_draw(buf, len, cur, &off);
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &ed_cooked);
    ed_reading = 0;
    buf[len] = '\0';
    if (leave) return false;
    if (!line_done && len == 0) return false;
    fputc('\n', stdout);
    fflush(stdout);
    ed_history_add(buf);
    if (search_kind != SK_NONE) queued_shown = true;
    snprintf(out, size, "%s", buf);
    return true;
}
#else
bool console_editor_on(void) { return false; }
bool console_edit_line(char* out, size_t size, const char* start) {
    (void)out; (void)size; (void)start;
    return false;
}

static HANDLE w_in = NULL;
static DWORD w_in_mode = 0;
static bool w_echo_off = false;
static void echo_off_begin(void) {
    if (!con_prompt || w_echo_off) return;
    w_in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD m = 0;
    if (w_in == NULL || w_in == INVALID_HANDLE_VALUE || !GetConsoleMode(w_in, &m) || !(m & ENABLE_ECHO_INPUT)) return;
    w_in_mode = m;
    w_echo_off = SetConsoleMode(w_in, m & ~(DWORD)ENABLE_ECHO_INPUT) != 0;
}
static void echo_off_end(void) {
    if (!w_echo_off) return;
    SetConsoleMode(w_in, w_in_mode);
    w_echo_off = false;
}
static void editor_restore(void) { echo_off_end(); }
static void interrupt_install(void) {}
static void interrupt_uninstall(void) {}
#endif

static void console_enter_mode(Position* pos, bool greet) {
    if (!con_exit_hook) {
        atexit(console_at_exit);
        con_exit_hook = true;
    }
    console_set_active(true);
    con_prompt = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
#ifndef _WIN32
    {
        const char* term = getenv("TERM");
        ed_ok = con_prompt && !(term && strcmp(term, "dumb") == 0);
    }
#endif
    platform_enter();
#ifdef _WIN32
    if (!w_handler) w_handler = SetConsoleCtrlHandler(console_ctrl_handler, TRUE) != 0;
    win_stdin_console(1);
#endif
    interrupt_install();
    con_interrupt = 0;
    interrupt_count = 0;
    history_reset(pos);
    sync_kind = SYNC_NONE;
    game_on = game_over = draw_offer = false;
    game_time_end = false;
    analysis_on = false;
    ponder_have = false;
    own_count = 0;
    redo_clear();
    record_clear();
    if (greet) printf("Chessenger %s console. Type 'help' for commands, 'uci' for UCI mode.\n", ENGINE_VERSION);
    fflush(stdout);
}

void console_enter(Position* pos) {
    console_enter_mode(pos, true);
}

static void console_leave(void) {
    style_off();
    fflush(stdout);
    editor_restore();
    platform_leave();
    interrupt_uninstall();
    analysis_on = false;
#ifndef _WIN32
    ed_uninstall();
#endif
#ifdef _WIN32
    if (w_handler) {
        SetConsoleCtrlHandler(console_ctrl_handler, FALSE);
        w_handler = false;
    }
    win_stdin_console_prompt(1);
    win_stdin_console(0);
#endif
    console_set_active(false);
}

static void current_prompt(const Position* pos, char* buf, size_t n) {
    if (game_on && !game_over && pos->to_move == game_human)
        snprintf(buf, n, "%d%s You > ", pos->fullmove_number, game_human == WHITE ? "." : "...");
    else
        snprintf(buf, n, "%s", CON_PROMPT);
}

static bool ponder_start(Position* pos, char* line, size_t size);

bool console_before_read(Position* pos, char* line, size_t size) {
    if (cap_saved_fd >= 0) {
        capture_end();
        print_caught();
        fflush(stdout);
    }
    if (search_kind != SK_NONE) {
        const SearchKind kind = search_kind;
        const bool interrupted = search_interrupted;
        bound_row_clear();
        echo_off_end();
        search_kind = SK_NONE;
        search_interrupted = false;
        post_rows = false;
        post_white = false;
        switch (kind) {
            case SK_GAME:     game_engine_done(pos); break;
            case SK_HINT:     hint_done(pos); break;
            case SK_ANALYZE:  analyze_done(pos, interrupted); break;
            case SK_GO:       go_done(pos); break;
            case SK_MATE:     mate_done(pos); break;
            case SK_PONDER:   ponder_done(pos, interrupted); break;
            case SK_ANNOTATE: annotate_result(pos, interrupted); break;
            default: break;
        }
        fflush(stdout);
    }
    if (!con_on) return false;
    if (sync_kind == SYNC_RESET) history_reset(pos);
    else if (sync_kind == SYNC_POSITION) history_from_position_line(pos, sync_line);
    sync_kind = SYNC_NONE;
    char queued[MAX_UCI_LINE];
    bool have_queued = false;
    for (;;) {
        OwnStep step;
        while (own_pop(&step)) {
            if (own_step_line(pos, step, line, size)) {
                fflush(stdout);
                return true;
            }
        }
        have_queued = console_queued_line(queued, sizeof(queued));

        if (analysis_on && !have_queued) {
            own_push(OWN_ANALYZE);
            continue;
        }
        if (!have_queued && ponder_start(pos, line, size)) {
            fflush(stdout);
            return true;
        }
        break;
    }
    fflush(stdout);
    if (con_prompt) {
        char prompt[64];
        current_prompt(pos, prompt, sizeof(prompt));
        snprintf(ed_prompt, sizeof(ed_prompt), "%s", prompt);
        if (have_queued && queued_shown) {

        } else if (have_queued) {

            trim_uci_line(queued);
            for (char* q = queued; *q; q++)
                if ((unsigned char)*q < 32 || (unsigned char)*q == 127) *q = '?';
            const int room = line_width() - (int)strlen(prompt);
            if ((int)strlen(queued) > room && room > 3) printf("%s%.*s...\n", prompt, room - 3, queued);
            else printf("%s%s\n", prompt, queued);
        } else if (console_editor_on()) {

        } else {
            fputs(prompt, stdout);
        }
        fflush(stdout);
    }
    if (have_queued) queued_shown = false;
#ifdef _WIN32
    if (!have_queued) win_stdin_console_prompt(1);
#endif
    return false;
}

void console_note_option(const char* name, const char* value) {
    if (!name || !name[0]) return;
    int k;
    for (k = 0; k < note_count; k++)
        if (strcasecmp(note_name[k], name) == 0) break;
    if (k == note_count) {
        if (note_count == NOTE_MAX) return;
        note_count++;
    }
    snprintf(note_name[k], sizeof(note_name[k]), "%s", name);
    snprintf(note_value[k], sizeof(note_value[k]), "%s", value && value[0] ? value : "<empty>");
}

ConsoleAction console_handle_line(Position* pos, char* line, size_t size) {
    char word[64];
    const char* arg = split_word(line, word, sizeof(word), false);
    interrupt_count = 0;
    if (!word[0]) return CONSOLE_DONE;

    for (int i = 0; i < UCI_PASS_COUNT; i++) {
        if (strcasecmp(word, UCI_PASS[i]) != 0) continue;
        static char rest[MAX_UCI_LINE];
        snprintf(rest, sizeof(rest), "%s", arg);
        if (rest[0]) snprintf(line, size, "%s %s", UCI_PASS[i], rest);
        else snprintf(line, size, "%s", UCI_PASS[i]);
        if (i == 1 || i == 3) {
            game_stop();
            redo_clear();
            record_clear();
        }
        if (i == 1) {
            snprintf(sync_line, sizeof(sync_line), "%s", line);
            sync_kind = SYNC_POSITION;
        } else if (i == 3) {
            sync_kind = SYNC_RESET;
        }
        if (i <= 1) capture_begin();
        return CONSOLE_PASS;
    }

    const char* alias;
    const ConsoleCommand* c = find_command_or_alias(word, &alias);
    if (c && !c->available) {
        char first[160];
        snprintf(first, sizeof(first), "'%s' is not available in this version yet.", alias ? alias : c->name);
        print_two_part(first, "Type 'help' for a list of commands.");
        return CONSOLE_DONE;
    }
    if (c) {

        if (c->id == C_PLAY || c->id == C_UCI || c->id == C_XBOARD || c->id == C_QUIT) analysis_on = false;
        switch (c->id) {
            case C_NEW:
                if (game_on) {
                    game_start(pos, game_human, &game_tc, true);
                    break;
                }
                redo_clear();
                record_clear();
                printf("New game.\n");
                snprintf(line, size, "ucinewgame");
                sync_kind = SYNC_RESET;
                return CONSOLE_PASS;
            case C_UNDO:     return cmd_undo(pos, arg, line, size);
            case C_PLAY:     cmd_play(pos, arg); break;
            case C_REDO:     cmd_redo(pos, arg); break;
            case C_SWITCH:   cmd_switch(pos); break;
            case C_HINT:     cmd_hint(pos); break;
            case C_RESIGN:   cmd_resign(pos); break;
            case C_DRAW:     cmd_draw(pos, arg); break;
            case C_CLOCK:    cmd_clock(pos); break;
            case C_TIME:     cmd_time(arg, alias); break;
            case C_POST:     cmd_post(arg); break;
            case C_SAVE:     cmd_save(pos, arg); break;
            case C_LOAD:     return cmd_load(pos, arg, line, size);
            case C_FIRST:
            case C_PREV:
            case C_NEXT:
            case C_LAST:
            case C_GOTO:     return cmd_nav(pos, c->id, arg, line, size);
            case C_ANALYZE:  cmd_analyze(pos); break;
            case C_STOP:     cmd_stop(); break;
            case C_GO:       cmd_go(pos, arg); break;
            case C_EVAL:     cmd_eval(pos); break;
            case C_MATE:     cmd_mate(pos, arg); break;
            case C_ANNOTATE: cmd_annotate(pos, arg, false); break;
            case C_PONDER:   cmd_ponder(arg); break;
            case C_LINES:    return cmd_lines(arg, line, size);
            case C_SKILL:    cmd_skill(arg); break;
            case C_ELO:      cmd_elo(arg); break;
            case C_STRENGTH: cmd_strength(arg); break;
            case C_HISTORY:  cmd_history(pos); break;
            case C_BOARD:    print_board(pos); break;
            case C_FEN:      history_check(pos); printf("%s\n", position_to_fen(pos)); break;
            case C_SETBOARD: return cmd_setboard(pos, arg, line, size);
            case C_MOVES:    cmd_moves(pos); break;
            case C_FLIP:
                con_flipped = !con_flipped;
                print_board(pos);
                break;
            case C_HASH:     return cmd_engine_option("Hash", arg, line, size);
            case C_THREADS:  return cmd_engine_option("Threads", arg, line, size);
            case C_SYZYGY:   return cmd_engine_option("SyzygyPath", arg, line, size);
            case C_SET:      return cmd_set(arg, line, size);
            case C_OPTIONS:  cmd_options(); break;
            case C_COLOR:    cmd_color(arg); break;
            case C_THEME:    cmd_theme(arg); break;
            case C_PIECES:   cmd_pieces(arg); break;
            case C_POV:      cmd_pov(arg); break;
            case C_WIDTH:    cmd_width(arg); break;
            case C_ABOUT:
            case C_VERSION:  print_banner(); break;
            case C_LICENSE:  print_license(); break;
            case C_HELP: {
                char topic[64];
                split_word(arg, topic, sizeof(topic), false);
                if (topic[0]) help_topic(topic);
                else help_overview();
                break;
            }
            case C_UCI:
                console_leave();
                snprintf(line, size, "uci");
                return CONSOLE_PASS;
            case C_XBOARD:

                console_leave();
                printf("WinBoard/XBoard protocol from now on ('quit' leaves Chessenger).\n");
                fflush(stdout);
                xboard_enter();
                return CONSOLE_DONE;
            case C_QUIT:
                console_leave();
                return CONSOLE_QUIT;
            default: break;
        }
        fflush(stdout);
        return CONSOLE_DONE;
    }

    if (game_on) game_human_move(pos, line);
    else cmd_typed_moves(pos, line);
    fflush(stdout);
    return CONSOLE_DONE;
}

int console_command_line(int argc, char** argv, Position* pos) {
    if (argc < 2) return 0;
    const char* a = argv[1];
    if (strcmp(a, "console") == 0) {
        console_enter(pos);
        return 1;
    }
    if (strcmp(a, "play") == 0) {
        char args[256] = "";
        size_t len = 0;
        for (int i = 2; i < argc && len + 1 < sizeof(args); i++)
            len += (size_t)snprintf(args + len, sizeof(args) - len, "%s%s", i > 2 ? " " : "", argv[i]);
        console_enter(pos);
        cmd_play(pos, args);
        fflush(stdout);
        return 1;
    }
    if (strcmp(a, "analyze") == 0 || strcmp(a, "annotate") == 0) {
        char args[MAX_UCI_LINE] = "";
        size_t len = 0;
        for (int i = 2; i < argc && len + 1 < sizeof(args); i++)
            len += (size_t)snprintf(args + len, sizeof(args) - len, "%s%s", i > 2 ? " " : "", argv[i]);
        if (a[1] == 'n' && a[2] == 'a') {
            console_enter(pos);
            if (args[0]) {
                Position tmp;
                if (!parse_fen(&tmp, args)) {
                    char text[MAX_UCI_LINE + 64], e[64];
                    snprintf(text, sizeof(text), "Invalid FEN: '%s'. The start position is analyzed.", echo_word(args, e, sizeof(e)));
                    print_wrapped(text, 2, NULL);
                } else {
                    snprintf(own_fen, sizeof(own_fen), "%s", args);
                    own_push(OWN_POSITION);
                }
            }
            cmd_analyze(pos);
        } else {
            console_enter_mode(pos, false);
            if (!args[0]) {
                printf("Usage: chessenger annotate FILE [S], S = seconds a move (default 1).\n");
                own_push(OWN_QUIT);
            } else {
                cmd_annotate(pos, args, true);
            }
        }
        fflush(stdout);
        return 1;
    }
    if (strcmp(a, "help") == 0 || strcmp(a, "--help") == 0) {
        print_usage();
        return 2;
    }
    if (strcmp(a, "version") == 0) {
        console_banner(stdout);
        return 2;
    }
    return 0;
}

void console_enter_analyze(Position* pos) {
    console_enter(pos);
    cmd_analyze(pos);
    fflush(stdout);
}

void console_enter_play(Position* pos, const char* args) {
    console_enter(pos);
    while (*args == ' ' || *args == '\t') args++;
    char copy[MAX_UCI_LINE];
    snprintf(copy, sizeof(copy), "%s", args);
    cmd_play(pos, copy);
    fflush(stdout);
}
