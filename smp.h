/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#ifndef SMP_H
#define SMP_H

#include <stdbool.h>
#include "board.h"
#define SMP_TLS __thread

extern int smp_stop;

extern SMP_TLS bool smp_helper;

extern SMP_TLS long long* smp_nodes_pub;

static inline bool smp_stop_read(void) { return __atomic_load_n(&smp_stop, __ATOMIC_RELAXED) != 0; }

void smp_helper_new_go(void);

void smp_init_shared_search(void);
void smp_init_shared_eval(void);

int       smp_set_threads(int threads_n);
void      smp_start(const Position* pos, int multipv);
void      smp_halt(void);
long long smp_helper_nodes(void);
void      smp_new_game(void);
void      smp_shutdown(void);

#define SMP_NODES(x) ((x) + smp_helper_nodes())

#endif
