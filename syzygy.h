/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#ifndef SYZYGY_H
#define SYZYGY_H

#include <stdbool.h>
#include "board.h"

#define SYZYGY_WIN_SCORE 20000

extern int syzygy_limit;
extern long long syzygy_hits;

#define SYZYGY_HITS() __atomic_load_n(&syzygy_hits, __ATOMIC_RELAXED)

void syzygy_set_path(const char* tb_path);
void syzygy_set_limit(int limit);
void syzygy_set_depth(int depth_arg);
void syzygy_set_rule50(bool enabled);
int  syzygy_largest(void);

bool syzygy_probe_wdl(const Position* pos, int depth, int* wdl);

int  syzygy_root(Position* pos, Move* mlist, int max, int* wdl);

#endif
