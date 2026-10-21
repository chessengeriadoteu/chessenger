/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include "board.h"
#include "levels/levels.h"

typedef enum {
    CONSOLE_DONE = 0,
    CONSOLE_PASS,
    CONSOLE_QUIT
} ConsoleAction;

bool console_active(void);
void console_set_active(bool on);

void console_enter(Position* pos);

ConsoleAction console_handle_line(Position* pos, char* line, size_t size);

bool console_before_read(Position* pos, char* line, size_t size);

void console_enter_play(Position* pos, const char* args);

bool console_takes_search_output(void);

void console_search_info(int multipv, int depth, int seldepth, int score, bool mate, int mate_moves,
                         long long time_ms, long long nodes, const char* pv);

void console_search_bound(int depth, int seldepth, int score, bool mate, int mate_moves, long long nodes,
                          const Move* move, bool lower);
void console_search_bestmove(const Move* m);

void console_search_refused(const char* reason);

bool console_editor_on(void);
bool console_edit_line(char* out, size_t size, const char* start);

void console_enter_analyze(Position* pos);

int console_line_in_search(const char* typed);

bool console_interrupt_taken(void);
int console_read_interrupted(char* out, size_t size);

void console_strength_uci_options(void);
bool console_strength_setoption(const char* name, const char* value);
bool console_strength_value(const char* name, char* value, size_t n);
const LevelStrength* console_strength_go(bool bench);
const LevelStrength* console_strength_game(void);

void console_uci_help(void);

void console_banner_hint(void);

int console_command_line(int argc, char** argv, Position* pos);

void console_note_option(const char* name, const char* value);

void console_banner(FILE* f);

bool console_queued_line(char* out, size_t n);

bool console_option_at(int i, const char** name, char* value, size_t n, bool* known);

int console_static_eval(Position* p, bool* network);

#endif
