/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#include "../board.h"
#include "../magic.h"
#include "../movegen.h"
#include "../utils.h"
#include "../search.h"
#include "../tt.h"
#include "../syzygy.h"
#include "../console.h"
#include "levels.h"
#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>

#include "lvl_tt.c"
#include "lvl_movegen.c"
#include "lvl_eval.c"

static long long lvl_node_count = 0;
static bool      lvl_aborted = false;
static long long lvl_node_limit = 0;
static bool      lvl_limit_hit = false;

static bool lvl_stop(void) {
    if (lvl_node_limit > 0 && lvl_node_count >= lvl_node_limit) {
        lvl_limit_hit = true;
        stop_requested = true;
        return true;
    }
    return fast_check_stop();
}

#include "lvl_search.c"
#include "lvl_driver.c"
