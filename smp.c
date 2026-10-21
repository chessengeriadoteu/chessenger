/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#include <pthread.h>
#include <string.h>
#include "smp.h"
#include "search.h"
#include "board.h"
#include "eval.h"
#include "utils.h"
#include "tune.h"

#define SMP_HELPERS_MAX 63
#define SMP_STACK           (64u << 20)
#define SMP_FULL_WINDOW     31000
_Static_assert(SMP_FULL_WINDOW > MATE_SCORE, "the helper full window must lie above the mate band");

typedef struct {
    pthread_t thread;
    int       id;
    long long node_count;
    unsigned  gen0, game0;
} Helper;

static Helper        helpers[SMP_HELPERS_MAX];
static int             helper_count      = 0;
static bool            running    = false;
static pthread_mutex_t smp_mutex      = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  cv_start   = PTHREAD_COND_INITIALIZER;
static pthread_cond_t  cv_done  = PTHREAD_COND_INITIALIZER;

static unsigned        gen        = 0;
static unsigned        game_id     = 0;
static int             active_count  = 0;
static int             limit_id   = 0;
static Position        shared_position;

extern void search_new_game(void);

static void smp_search(Position* pos, int id) {
    SearchInfo info;
    int  prev_score = 0;
    bool have_base       = false;
    int  depth     = 1;

    smp_helper_new_go();
    while (depth <= MAX_DEPTH && !smp_stop_read()) {
        int alpha = -SMP_FULL_WINDOW, beta = SMP_FULL_WINDOW, delta = 0;
        memset(&info, 0, sizeof(info));
        info.depth = depth;
        if (have_base && depth > 8) {
            delta = ASP_DELTA0 + (prev_score < 0 ? -prev_score : prev_score) / ASP_DIV;
            alpha = prev_score - delta;
            beta  = prev_score + delta;
        }
        for (;;) {
            if (alpha < -2 * QUEEN_VALUE) alpha = -SMP_FULL_WINDOW;
            if (beta  >  2 * QUEEN_VALUE) beta  =  SMP_FULL_WINDOW;
            (void)search_single_depth_with_nodes(pos, depth, alpha, beta, &info);
            if (smp_stop_read()) break;
            if (info.score <= alpha && alpha > -SMP_FULL_WINDOW) {
                beta  = (alpha + beta) / 2;
                alpha = info.score - delta;
            } else if (info.score >= beta && beta < SMP_FULL_WINDOW) {
                beta  = info.score + delta;
            } else {
                break;
            }
            delta += delta * ASP_GROW / 256;
        }
        if (smp_stop_read()) break;
        prev_score = info.score;
        have_base       = true;
        depth     = (depth == 1) ? 2 + (id & 1) : depth + 1;
    }
    __atomic_store_n(smp_nodes_pub, get_nodes_searched(), __ATOMIC_RELAXED);
}

static void* smp_thread(void* arg) {
    Helper* p = (Helper*)arg;
    unsigned  my_gen, my_game;

    smp_helper  = true;
    smp_nodes_pub = &p->node_count;

    my_gen     = p->gen0;
    my_game = p->game0;
    pthread_mutex_lock(&smp_mutex);
    for (;;) {
        while (gen == my_gen && p->id <= limit_id) pthread_cond_wait(&cv_start, &smp_mutex);
        if (p->id > limit_id) break;
        my_gen = gen;
        Position pos = shared_position;
        const bool new_game = (game_id != my_game);
        my_game = game_id;
        pthread_mutex_unlock(&smp_mutex);

        if (new_game) search_new_game();
        smp_search(&pos, p->id);

        pthread_mutex_lock(&smp_mutex);
        if (--active_count == 0) pthread_cond_broadcast(&cv_done);
    }
    pthread_mutex_unlock(&smp_mutex);
    return NULL;
}

int smp_set_threads(int threads_n) {
    int wanted = threads_n - 1;
    if (wanted < 0) wanted = 0;
    if (wanted > SMP_HELPERS_MAX) wanted = SMP_HELPERS_MAX;
    smp_halt();

    if (wanted < helper_count) {
        pthread_mutex_lock(&smp_mutex);
        limit_id = wanted;
        pthread_cond_broadcast(&cv_start);
        pthread_mutex_unlock(&smp_mutex);
        for (int i = wanted; i < helper_count; i++) pthread_join(helpers[i].thread, NULL);
        helper_count = wanted;
    } else if (wanted > helper_count) {
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, SMP_STACK);
        pthread_mutex_lock(&smp_mutex);
        limit_id = wanted;
        const unsigned gen_now = gen, game_now = game_id;
        pthread_mutex_unlock(&smp_mutex);
        int i;
        for (i = helper_count; i < wanted; i++) {
            helpers[i].id      = i + 1;
            helpers[i].node_count   = 0;
            helpers[i].gen0    = gen_now;
            helpers[i].game0 = game_now;
            if (pthread_create(&helpers[i].thread, &attr, smp_thread, &helpers[i]) != 0) break;
        }
        pthread_attr_destroy(&attr);
        helper_count = i;
        if (helper_count < wanted) {
            pthread_mutex_lock(&smp_mutex);
            limit_id = helper_count;
            pthread_mutex_unlock(&smp_mutex);
        }
    }
    return helper_count + 1;
}

void smp_start(const Position* pos, int multipv) {

    smp_init_shared_search();
    smp_init_shared_eval();
    if (helper_count == 0) return;

    for (int i = 0; i < helper_count; i++) __atomic_store_n(&helpers[i].node_count, 0, __ATOMIC_RELAXED);
    if (multipv > 1) return;

    pthread_mutex_lock(&smp_mutex);
    shared_position = *pos;
    __atomic_store_n(&smp_stop, 0, __ATOMIC_RELAXED);
    active_count = helper_count;
    gen++;
    pthread_cond_broadcast(&cv_start);
    pthread_mutex_unlock(&smp_mutex);
    running = true;
}

void smp_halt(void) {
    if (!running) return;
    __atomic_store_n(&smp_stop, 1, __ATOMIC_RELAXED);
    pthread_mutex_lock(&smp_mutex);
    while (active_count > 0) pthread_cond_wait(&cv_done, &smp_mutex);
    pthread_mutex_unlock(&smp_mutex);
    running = false;
}

long long smp_helper_nodes(void) {
    long long helpers_total = 0;
    for (int i = 0; i < helper_count; i++) helpers_total += __atomic_load_n(&helpers[i].node_count, __ATOMIC_RELAXED);
    return helpers_total;
}

void smp_new_game(void) {
    pthread_mutex_lock(&smp_mutex);
    game_id++;
    pthread_mutex_unlock(&smp_mutex);
}

void smp_shutdown(void) {
    (void)smp_set_threads(1);
}
