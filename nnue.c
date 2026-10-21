/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 * Portions of this file are adapted from Starzix 6.0 (zzzzz, WTFPL v2, see LICENSES/STARZIX-WTFPL.txt). Modified by Dariusz Domagała, 2026-10-21.
 */
#include "nnue.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "syzygy.h"
#include <pthread.h>
#if defined(__ARM_NEON)
#include <arm_neon.h>
#ifdef _WIN32
#include <malloc.h>
#endif
#endif

#define NNUE_FEATURES 768
#define NNUE_OUTPUT_SCALE 400
#define NNUE_QA 255
#define NNUE_QB 64

typedef struct {
    int16_t feature_weights[2][NNUE_BUCKETS][NNUE_FEATURES][NNUE_HIDDEN];
    int16_t hidden_bias[2][NNUE_HIDDEN];
    int16_t output_weights[2][NNUE_HIDDEN];
    int16_t output_bias;
} NnueNetwork;

#define NNUE_WEIGHT_COUNT (2 * NNUE_BUCKETS * NNUE_FEATURES * NNUE_HIDDEN + 2 * NNUE_HIDDEN + 2 * NNUE_HIDDEN + 1)
#define NNUE_FILE_SIZE ((long)NNUE_WEIGHT_COUNT * 2)

typedef char nnue_network_size_ok[(sizeof(NnueNetwork) == (size_t)NNUE_FILE_SIZE) ? 1 : -1];

static NnueNetwork net __attribute__((aligned(64)));
static bool net_loaded = false;
#if NNUE_LIGHT
static unsigned net_generation = 0;
#endif

static const int nnue_net_piece_type[6] = { 0, 3, 1, 2, 4, 5 };

static const int nnue_queen_buckets[64] = {
    1, 1, 1, 1, 2, 2, 2, 2,
    1, 1, 1, 1, 2, 2, 2, 2,
    1, 1, 1, 1, 2, 2, 2, 2,
    1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4,
    3, 3, 3, 3, 4, 4, 4, 4,
    3, 3, 3, 3, 4, 4, 4, 4,
    3, 3, 3, 3, 4, 4, 4, 4,
};

bool nnue_load(const char* net_path) {
    net_loaded = false;

    const uint16_t endian_probe = 1;
    if (*(const unsigned char*)&endian_probe != 1) {
        fprintf(stderr, "nnue: big-endian host, network not loaded\n");
        return false;
    }

    FILE* f = fopen(net_path, "rb");
    if (!f) {
        fprintf(stderr, "nnue: cannot open network file: %s\n", net_path);
        return false;
    }

    const size_t n_bytes_read = fread(&net, 1, (size_t)NNUE_FILE_SIZE, f);
    const bool excess = (fgetc(f) != EOF);
    fclose(f);
    if (n_bytes_read != (size_t)NNUE_FILE_SIZE || excess) {
        fprintf(stderr, "nnue: wrong network file size (expected %ld B): %s\n", NNUE_FILE_SIZE, net_path);
        memset(&net, 0, sizeof(net));
        return false;
    }

    net_loaded = true;
    return true;
}

#if !NNUE_LIGHT
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
#endif
static const unsigned char embedded_net[] = {
#embed "siec_g1_w01.bin"
};
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
typedef char nnue_embedded_size_ok[(sizeof(embedded_net) == (size_t)NNUE_FILE_SIZE) ? 1 : -1];

bool nnue_load_embedded(void) {
    net_loaded = false;
    const uint16_t endian_probe = 1;
    if (*(const unsigned char*)&endian_probe != 1) {
        fprintf(stderr, "nnue: big-endian host, network not loaded\n");
        return false;
    }
    memcpy(&net, embedded_net, sizeof(net));
    net_loaded = true;
    return true;
}
#endif

#if NNUE_LIGHT

long nnue_net_file_size(void) {
    return NNUE_FILE_SIZE;
}

bool nnue_set_from_buffer(const unsigned char* data_buf, size_t n, const char** reason_msg) {
    const uint16_t endian_probe = 1;
    if (*(const unsigned char*)&endian_probe != 1) {
        *reason_msg = "big-endian host";
        return false;
    }
    if (data_buf == NULL || n != (size_t)NNUE_FILE_SIZE) {
        *reason_msg = "wrong network file size";
        return false;
    }
    memcpy(&net, data_buf, sizeof(net));
    net_loaded = true;
    net_generation++;
    *reason_msg = NULL;
    return true;
}

static void nnue_refresh_reset(NnueThread* w) {
    for (int c = 0; c < 2; c++)
        for (int l = 0; l < 2; l++)
            for (int k = 0; k < NNUE_BUCKETS; k++) {
                memcpy(w->refresh[c][l][k].acc, net.hidden_bias[c], sizeof(w->refresh[c][l][k].acc));
                memset(w->refresh[c][l][k].bb, 0, sizeof(w->refresh[c][l][k].bb));
            }
    w->net_gen = net_generation;
}
#endif

bool nnue_ready(void) {
    return net_loaded;
}

static inline void acc_add(int16_t* restrict a, const int16_t* restrict w) {
    for (int i = 0; i < NNUE_HIDDEN; i++)
        a[i] = (int16_t)(a[i] + w[i]);
}

static inline void acc_sub(int16_t* restrict a, const int16_t* restrict w) {
    for (int i = 0; i < NNUE_HIDDEN; i++)
        a[i] = (int16_t)(a[i] - w[i]);
}

static inline void acc_add1_del1(int16_t* restrict dest, const int16_t* restrict src_acc,
                             const int16_t* restrict add0, const int16_t* restrict del0) {
    for (int i = 0; i < NNUE_HIDDEN; i++)
        dest[i] = (int16_t)(src_acc[i] + add0[i] - del0[i]);
}

static inline void acc_add1_del2(int16_t* restrict dest, const int16_t* restrict src_acc, const int16_t* restrict add0,
                             const int16_t* restrict del0, const int16_t* restrict del1) {
    for (int i = 0; i < NNUE_HIDDEN; i++)
        dest[i] = (int16_t)(src_acc[i] + add0[i] - del0[i] - del1[i]);
}

static inline void acc_add2_del2(int16_t* restrict dest, const int16_t* restrict src_acc, const int16_t* restrict add0,
                             const int16_t* restrict add1, const int16_t* restrict del0, const int16_t* restrict del1) {
    for (int i = 0; i < NNUE_HIDDEN; i++)
        dest[i] = (int16_t)(src_acc[i] + add0[i] + add1[i] - del0[i] - del1[i]);
}

static inline void nnue_mirror_bucket(Bitboard king_set, Bitboard opp_queens, bool* mirror, int* bucket) {
    const int king_sq_idx = king_set ? __builtin_ctzll(king_set) : 0;
    *mirror = (king_sq_idx & 7) >= 4;
    if (__builtin_popcountll(opp_queens) != 1) {
        *bucket = 0;
    } else {
        const int queen_sq = __builtin_ctzll(opp_queens) ^ (*mirror ? 7 : 0);
        *bucket = nnue_queen_buckets[queen_sq];
    }
}

void nnue_recompute(const Position* pos, NnueAccumulators* a) {
    for (int c = 0; c < 2; c++) {
        nnue_mirror_bucket(pos->pieces[c][KING], pos->pieces[c ^ 1][QUEEN], &a->mirror[c], &a->bucket[c]);
        memcpy(a->acc[c], net.hidden_bias[c], sizeof(a->acc[c]));
    }

    for (int piece_color = 0; piece_color < 2; piece_color++) {
        for (int ptype = PAWN; ptype <= KING; ptype++) {
            Bitboard bb = pos->pieces[piece_color][ptype];
            while (bb) {
                const int piece_sq = __builtin_ctzll(bb);
                bb &= bb - 1;
                for (int c = 0; c < 2; c++) {

                    const int feat_idx = piece_color * 384 + nnue_net_piece_type[ptype] * 64 + (piece_sq ^ (a->mirror[c] ? 7 : 0));
                    acc_add(a->acc[c], net.feature_weights[c][a->bucket[c]][feat_idx]);
                }
            }
        }
    }
}

int nnue_eval(const NnueAccumulators* a, Color perspective) {
    uint32_t acc_sum = 0;

    for (int k = 0; k < 2; k++) {
        const int16_t* acc = a->acc[(int)perspective ^ k];
        const int16_t* w = net.output_weights[k];
        for (int i = 0; i < NNUE_HIDDEN; i++) {
            int32_t x = acc[i];
            if (x < 0) x = 0;
            if (x > NNUE_QA) x = NNUE_QA;
            const int16_t y = (int16_t)(x * w[i]);
            acc_sum += (uint32_t)((int32_t)y * x);
        }
    }

    const int32_t s = (int32_t)acc_sum;

    const int32_t pre_scale = (int32_t)((uint32_t)(s / NNUE_QA + net.output_bias) * (uint32_t)NNUE_OUTPUT_SCALE);
    return pre_scale / (NNUE_QA * NNUE_QB);
}

int nnue_eval_full(const Position* pos) {
    NnueAccumulators a;
    nnue_recompute(pos, &a);
    return nnue_eval(&a, pos->to_move);
}

int nnue_scale_by_material(const Position* pos, int eval_value) {
    const int mat = 3 * __builtin_popcountll(pos->pieces[WHITE][KNIGHT] | pos->pieces[BLACK][KNIGHT])
                  + 3 * __builtin_popcountll(pos->pieces[WHITE][BISHOP] | pos->pieces[BLACK][BISHOP])
                  + 5 * __builtin_popcountll(pos->pieces[WHITE][ROOK] | pos->pieces[BLACK][ROOK])
                  + 9 * __builtin_popcountll(pos->pieces[WHITE][QUEEN] | pos->pieces[BLACK][QUEEN]);

    const float scale = 0.75f + ((1.06f - 0.75f) * (float)mat) / 62.0f;
    return (int32_t)((float)eval_value * scale);
}

#define NNUE_SCORE_LIMIT (SYZYGY_WIN_SCORE - 1000 - 1)
int nnue_to_search(int scaled_eval) {
    int v = scaled_eval * 113 / 100;
    if (v >  NNUE_SCORE_LIMIT) v =  NNUE_SCORE_LIMIT;
    if (v < -NNUE_SCORE_LIMIT) v = -NNUE_SCORE_LIMIT;
    return v;
}

#define COUNT_EVENT(x) ((void)0)

static pthread_key_t nnue_tls_key;
static pthread_once_t nnue_tls_key_once = PTHREAD_ONCE_INIT;
#ifdef _WIN32
static void nnue_thread_free(void* p) { _aligned_free(p); }
#else
static void nnue_thread_free(void* p) { free(p); }
#endif
static void nnue_tls_key_create(void) { (void)pthread_key_create(&nnue_tls_key, nnue_thread_free); }

NnueThread* nnue_thread_new(void) {
    void* p = NULL;
#ifdef _WIN32
    p = _aligned_malloc(sizeof(NnueThread), 64);
    if (!p) return NULL;
#else
    if (posix_memalign(&p, 64, sizeof(NnueThread)) != 0 || !p) return NULL;
#endif
    NnueThread* w = (NnueThread*)p;

    for (int c = 0; c < 2; c++)
        for (int l = 0; l < 2; l++)
            for (int k = 0; k < NNUE_BUCKETS; k++) {
                memcpy(w->refresh[c][l][k].acc, net.hidden_bias[c], sizeof(w->refresh[c][l][k].acc));
                memset(w->refresh[c][l][k].bb, 0, sizeof(w->refresh[c][l][k].bb));
            }
    for (int j = 0; j < NNUE_STACK_SIZE; j++) w->stack[j].state = NNUE_STALE;
#if NNUE_LIGHT
    w->net_gen = net_generation;
#endif
    pthread_once(&nnue_tls_key_once, nnue_tls_key_create);
    (void)pthread_setspecific(nnue_tls_key, w);
    return w;
}

void nnue_root(NnueThread* w, const Position* pos) {
#if NNUE_LIGHT

    if (w->net_gen != net_generation) nnue_refresh_reset(w);
#endif
    NnueEntry* e = &w->stack[0];
    memcpy(e->bb[WHITE], pos->pieces[WHITE], sizeof(e->bb[WHITE]));
    memcpy(e->bb[BLACK], pos->pieces[BLACK], sizeof(e->bb[BLACK]));
    nnue_recompute(pos, &e->a);
    e->state = NNUE_CURRENT;
}

#if defined(__ARM_NEON)

#define NNUE_TILE_NEURONS 128
#define NNUE_TILE_REGS (NNUE_TILE_NEURONS / 8)
_Static_assert(NNUE_HIDDEN % NNUE_TILE_NEURONS == 0, "NNUE_HIDDEN must be a multiple of the tile (128 neurons)");
#if defined(__clang__)
#define NNUE_UNROLL _Pragma("clang loop unroll(full)")
#else
#define NNUE_UNROLL
#endif
#endif

static void nnue_refresh(NnueThread* w, int c, NnueEntry* dest) {
    const int l = dest->a.mirror[c] ? 1 : 0, bucket_idx = dest->a.bucket[c], m = l ? 7 : 0;
    NnueRefreshEntry* o = &w->refresh[c][l][bucket_idx];
#if defined(__ARM_NEON)
    const int16_t* uw[64];
    const int16_t* dw[64];
    int removed_n = 0, added_n = 0;
    for (int piece_color = 0; piece_color < 2; piece_color++) {
        for (int ptype = PAWN; ptype <= KING; ptype++) {
            const Bitboard old_bb = o->bb[piece_color][ptype], new_bb = dest->bb[piece_color][ptype];
            if (old_bb == new_bb) continue;
            Bitboard removed = old_bb & ~new_bb, added = new_bb & ~old_bb;
            const int feat_base = piece_color * 384 + nnue_net_piece_type[ptype] * 64;
            while (removed) {
                const int piece_sq = __builtin_ctzll(removed);
                removed &= removed - 1;
                uw[removed_n++] = net.feature_weights[c][bucket_idx][feat_base + (piece_sq ^ m)];
            }
            while (added) {
                const int piece_sq = __builtin_ctzll(added);
                added &= added - 1;
                dw[added_n++] = net.feature_weights[c][bucket_idx][feat_base + (piece_sq ^ m)];
            }
            o->bb[piece_color][ptype] = new_bb;
        }
    }
    int16_t* restrict oa = o->acc;
    int16_t* restrict ca = dest->a.acc[c];
    for (int j = 0; j < NNUE_HIDDEN; j += NNUE_TILE_NEURONS) {
        int16x8_t r[NNUE_TILE_REGS];
        NNUE_UNROLL
        for (int i = 0; i < NNUE_TILE_REGS; i++) r[i] = vld1q_s16(oa + j + 8 * i);
        for (int f = 0; f < removed_n; f++) {
            const int16_t* q = uw[f] + j;
            NNUE_UNROLL
            for (int i = 0; i < NNUE_TILE_REGS; i++) r[i] = vsubq_s16(r[i], vld1q_s16(q + 8 * i));
        }
        for (int f = 0; f < added_n; f++) {
            const int16_t* q = dw[f] + j;
            NNUE_UNROLL
            for (int i = 0; i < NNUE_TILE_REGS; i++) r[i] = vaddq_s16(r[i], vld1q_s16(q + 8 * i));
        }
        NNUE_UNROLL
        for (int i = 0; i < NNUE_TILE_REGS; i++) {
            vst1q_s16(oa + j + 8 * i, r[i]);
            vst1q_s16(ca + j + 8 * i, r[i]);
        }
    }
#else
    for (int piece_color = 0; piece_color < 2; piece_color++) {
        for (int ptype = PAWN; ptype <= KING; ptype++) {
            const Bitboard old_bb = o->bb[piece_color][ptype], new_bb = dest->bb[piece_color][ptype];
            if (old_bb == new_bb) continue;
            Bitboard removed = old_bb & ~new_bb, added = new_bb & ~old_bb;
            const int feat_base = piece_color * 384 + nnue_net_piece_type[ptype] * 64;
            while (removed) {
                const int piece_sq = __builtin_ctzll(removed);
                removed &= removed - 1;
                acc_sub(o->acc, net.feature_weights[c][bucket_idx][feat_base + (piece_sq ^ m)]);
            }
            while (added) {
                const int piece_sq = __builtin_ctzll(added);
                added &= added - 1;
                acc_add(o->acc, net.feature_weights[c][bucket_idx][feat_base + (piece_sq ^ m)]);
            }
            o->bb[piece_color][ptype] = new_bb;
        }
    }
    memcpy(dest->a.acc[c], o->acc, sizeof(o->acc));
#endif
    COUNT_EVENT(refresh_counter);
}

static void nnue_step(NnueThread* w, const NnueEntry* src_acc, NnueEntry* dest) {
    int removed[32], added[32], removed_n = 0, added_n = 0;
    for (int piece_color = 0; piece_color < 2; piece_color++) {
        for (int ptype = PAWN; ptype <= KING; ptype++) {
            const Bitboard s = src_acc->bb[piece_color][ptype], n = dest->bb[piece_color][ptype];
            if (s == n) continue;
            Bitboard u = s & ~n, d = n & ~s;
            const int feat_base = piece_color * 384 + nnue_net_piece_type[ptype] * 64;
            while (u) { removed[removed_n++]  = feat_base + __builtin_ctzll(u); u &= u - 1; }
            while (d) { added[added_n++] = feat_base + __builtin_ctzll(d); d &= d - 1; }
        }
    }

    for (int c = 0; c < 2; c++) {
        nnue_mirror_bucket(dest->bb[c][KING], dest->bb[c ^ 1][QUEEN], &dest->a.mirror[c], &dest->a.bucket[c]);
        if (dest->a.mirror[c] != src_acc->a.mirror[c] || dest->a.bucket[c] != src_acc->a.bucket[c] || removed_n > 2 || added_n > 2) {
            nnue_refresh(w, c, dest);
            continue;
        }
        const int m = dest->a.mirror[c] ? 7 : 0, bucket_idx = dest->a.bucket[c];
        int16_t* a = dest->a.acc[c];
        const int16_t* p = src_acc->a.acc[c];
#define NNUE_WEIGHTS_AT(f) (net.feature_weights[c][bucket_idx][(f) ^ m])
        if (added_n == 1 && removed_n == 1)      acc_add1_del1(a, p, NNUE_WEIGHTS_AT(added[0]), NNUE_WEIGHTS_AT(removed[0]));
        else if (added_n == 1 && removed_n == 2) acc_add1_del2(a, p, NNUE_WEIGHTS_AT(added[0]), NNUE_WEIGHTS_AT(removed[0]), NNUE_WEIGHTS_AT(removed[1]));
        else if (added_n == 2 && removed_n == 2) acc_add2_del2(a, p, NNUE_WEIGHTS_AT(added[0]), NNUE_WEIGHTS_AT(added[1]), NNUE_WEIGHTS_AT(removed[0]), NNUE_WEIGHTS_AT(removed[1]));
        else {
            memcpy(a, p, sizeof(dest->a.acc[c]));
            for (int i = 0; i < removed_n; i++) acc_sub(a, NNUE_WEIGHTS_AT(removed[i]));
            for (int i = 0; i < added_n; i++) acc_add(a, NNUE_WEIGHTS_AT(added[i]));
        }
#undef NNUE_WEIGHTS_AT
        COUNT_EVENT(step_counter);
    }
}

int nnue_eval_node(NnueThread* w, int ply, const Position* pos) {
    int k = ply;
    while (w->stack[k].state != NNUE_CURRENT) {
        k--;
    }
    const NnueEntry* src_acc = &w->stack[k];
    for (int j = k + 1; j <= ply; j++) {
        NnueEntry* e = &w->stack[j];
        if (e->state == NNUE_NULL_MOVE) continue;
        nnue_step(w, src_acc, e);
        e->state = NNUE_CURRENT;
        src_acc = e;
    }
    return nnue_to_search(nnue_scale_by_material(pos, nnue_eval(&src_acc->a, pos->to_move)));
}
