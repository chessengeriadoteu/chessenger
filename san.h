/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#ifndef SAN_H
#define SAN_H

#include <stddef.h>
#include <stdbool.h>
#include "board.h"

typedef enum {
    SAN_OK = 0,
    SAN_ILLEGAL,
    SAN_AMBIGUOUS,
    SAN_UNRECOGNIZED
} SanResult;

const char* san_result_name(SanResult r);

bool move_to_san(const Position* pos, Move move, char* buf, size_t n);

SanResult parse_move_human(const Position* pos, const char* s, Move* out);

SanResult san_read_move(const Position* pos, const char* s, Move* list, int* count, bool* shortcut);

#endif
