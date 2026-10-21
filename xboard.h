/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#ifndef XBOARD_H
#define XBOARD_H

#include <stdbool.h>
#include <stddef.h>
#include "board.h"

typedef enum {
    XBOARD_DONE = 0,
    XBOARD_QUIT
} XboardAction;

bool xboard_active(void);

void xboard_enter(void);

XboardAction xboard_handle_line(Position* pos, char* line, size_t size);

bool xboard_before_read(Position* pos, char* line, size_t size);

bool xboard_takes_search_output(void);
void xboard_search_info(int multipv, int depth, int seldepth, int score, bool mate, int mate_moves,
                        long long time_ms, long long nodes, const char* pv);
void xboard_search_bestmove(const Move* m);
void xboard_search_refused(const char* reason);

int xboard_line_in_search(const char* typed);

#endif
