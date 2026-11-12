/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 * Portions of this file are adapted from Starzix 6.0 (zzzzz, WTFPL v2, see LICENSES/STARZIX-WTFPL.txt). Modified by Dariusz Domagała, 2026-11-12.
 */
#include "nnue.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "syzygy.h"
#include <pthread.h>
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

#ifndef NNUE_AVX2
#if defined(__AVX2__)
#define NNUE_AVX2 1
#else
#define NNUE_AVX2 0
#endif
#endif
#if NNUE_AVX2
#include <immintrin.h>
#ifdef _WIN32
#include <malloc.h>
#endif
_Static_assert(NNUE_HIDDEN % 32 == 0, "AVX2: NNUE_HIDDEN must be a multiple of 32 (two ymm registers per step)");
#endif

#define NNUE_FEATURES 768
#define NNUE_OUTPUT_SCALE 400
#define NNUE_QA 255
#define NNUE_QB 64

typedef struct {
    int16_t feature_weights[2][NNUE_BUCKETS][NNUE_FEATURES][NNUE_HIDDEN];
    int16_t hidden_bias[2][NNUE_HIDDEN];
    int16_t output_weights[8][2][NNUE_HIDDEN];
    int16_t output_bias[8];
} NnueNetwork;

#define NNUE_WEIGHT_COUNT (2 * NNUE_BUCKETS * NNUE_FEATURES * NNUE_HIDDEN + 2 * NNUE_HIDDEN + 8 * 2 * NNUE_HIDDEN + 8)
#define NNUE_FILE_SIZE ((long)NNUE_WEIGHT_COUNT * 2)

typedef char nnue_network_size_ok[(sizeof(NnueNetwork) == (size_t)NNUE_FILE_SIZE) ? 1 : -1];

static NnueNetwork nnue_net __attribute__((aligned(64)));
static bool net_loaded = false;

#if !NNUE_LIGHT
#define NNUE_S (embedded_net_u.s)
#else
#define NNUE_S nnue_net
#endif

static const int nnue_bucket_mirror[2][NNUE_BUCKETS] = {
    { 0, 1, 2, 3, 4 }, { 0, 3, 4, 1, 2 }
};

static inline int nnue_feature_mirror(int c, int f) {
    int x = f + (c ? 384 : 0);
    if (x >= NNUE_FEATURES) x -= NNUE_FEATURES;
    return x ^ (c ? 56 : 0);
}
#define NNUE_WC(c, bucket_idx, f) (NNUE_S.feature_weights[0][nnue_bucket_mirror[c][bucket_idx]][nnue_feature_mirror((c), (f))])

static bool nnue_single_table_ok(const unsigned char* d) {
    const size_t w = (size_t)NNUE_HIDDEN * sizeof(int16_t);
    for (int k = 0; k < NNUE_BUCKETS; k++)
        for (int f = 0; f < NNUE_FEATURES; f++)
            if (memcmp(d + ((size_t)k * NNUE_FEATURES + (size_t)f) * w,
                       d + ((size_t)(NNUE_BUCKETS + nnue_bucket_mirror[1][k]) * NNUE_FEATURES + (size_t)nnue_feature_mirror(1, f)) * w, w) != 0)
                return false;
    const size_t b = (size_t)2 * NNUE_BUCKETS * NNUE_FEATURES * w;
    return memcmp(d + b, d + b + w, w) == 0;
}
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

#define NNUE_LK(king_set, queen_set, c, mirror, bucket) nnue_mirror_bucket(king_set, queen_set, mirror, bucket)

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

    const size_t n_bytes_read = fread(&nnue_net, 1, (size_t)NNUE_FILE_SIZE, f);
    const bool excess = (fgetc(f) != EOF);
    fclose(f);
    if (n_bytes_read != (size_t)NNUE_FILE_SIZE || excess) {
        fprintf(stderr, "nnue: wrong network file size (expected %ld B): %s\n", NNUE_FILE_SIZE, net_path);
        memset(&nnue_net, 0, sizeof(nnue_net));
        return false;
    }
    if (!nnue_single_table_ok((const unsigned char*)&nnue_net)) {
        fprintf(stderr, "nnue: network without mirrored perspective tables (single-table build): %s\n", net_path);
        memset(&nnue_net, 0, sizeof(nnue_net));
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

static const union {
    unsigned char b[NNUE_FILE_SIZE];
    NnueNetwork s;
} embedded_net_u __attribute__((aligned(64))) = { .b = {
#embed "net_g5_s1086.bin"
} };
#define embedded_net (embedded_net_u.b)
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
    if (!nnue_single_table_ok(embedded_net)) {
        fprintf(stderr, "nnue: embedded network without mirrored perspective tables (single-table build)\n");
        return false;
    }
    net_loaded = true;
    return true;
}
#endif

long nnue_net_file_size(void) {
    return NNUE_FILE_SIZE;
}
int nnue_feature_count(void) {
    return NNUE_FEATURES;
}

#if NNUE_LIGHT

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
    if (!nnue_single_table_ok(data_buf)) {
        *reason_msg = "network without mirrored perspective tables (single-table build)";
        return false;
    }
    memcpy(&nnue_net, data_buf, sizeof(nnue_net));
    net_loaded = true;
    net_generation++;
    *reason_msg = NULL;
    return true;
}

static void nnue_refresh_reset(NnueThread* w) {
    for (int c = 0; c < 2; c++)
        for (int l = 0; l < 2; l++)
            for (int k = 0; k < NNUE_BUCKETS; k++) {
                memcpy(w->refresh[c][l][k].acc, NNUE_S.hidden_bias[c], sizeof(w->refresh[c][l][k].acc));
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

static inline void nnue_mirror_bucket(Bitboard king_set, Bitboard opp_queens,
                                       bool* mirror, int* bucket) {
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
        NNUE_LK(pos->pieces[c][KING], pos->pieces[c ^ 1][QUEEN], c, &a->mirror[c], &a->bucket[c]);
        memcpy(a->acc[c], NNUE_S.hidden_bias[c], sizeof(a->acc[c]));
    }

    for (int piece_color = 0; piece_color < 2; piece_color++) {
        for (int ptype = PAWN; ptype <= KING; ptype++) {
            Bitboard bb = pos->pieces[piece_color][ptype];
            while (bb) {
                const int piece_sq = __builtin_ctzll(bb);
                bb &= bb - 1;
                for (int c = 0; c < 2; c++) {

                    const int feat_idx = piece_color * 384 + nnue_net_piece_type[ptype] * 64 + (piece_sq ^ (a->mirror[c] ? 7 : 0));
                    acc_add(a->acc[c], NNUE_WC(c, a->bucket[c], feat_idx));
                }
            }
        }
    }
}

static inline int nnue_output_bucket(const Position* pos) {
    int k = (__builtin_popcountll(pos->all_pieces) - 2) / ((32 + 8 - 1) / 8);
    if (k < 0) k = 0;
    if (k > 8 - 1) k = 8 - 1;
    return k;
}

int nnue_eval(const NnueAccumulators* a, Color perspective, int bucket_idx) {
    uint32_t acc_sum = 0;

#if   NNUE_AVX2

    __m256i s0 = _mm256_setzero_si256(), s1 = _mm256_setzero_si256();
    const __m256i zero = _mm256_setzero_si256(), qa = _mm256_set1_epi16(NNUE_QA);
    for (int k = 0; k < 2; k++) {
        const int16_t* acc = a->acc[(int)perspective ^ k];
        const int16_t* w = NNUE_S.output_weights[bucket_idx][k];
        for (int i = 0; i < NNUE_HIDDEN; i += 32) {
            const __m256i x0 = _mm256_min_epi16(_mm256_max_epi16(_mm256_loadu_si256((const __m256i*)(acc + i)), zero), qa);
            const __m256i x1 = _mm256_min_epi16(_mm256_max_epi16(_mm256_loadu_si256((const __m256i*)(acc + i + 16)), zero), qa);
            const __m256i y0 = _mm256_mullo_epi16(x0, _mm256_loadu_si256((const __m256i*)(w + i)));
            const __m256i y1 = _mm256_mullo_epi16(x1, _mm256_loadu_si256((const __m256i*)(w + i + 16)));
            s0 = _mm256_add_epi32(s0, _mm256_madd_epi16(y0, x0));
            s1 = _mm256_add_epi32(s1, _mm256_madd_epi16(y1, x1));
        }
    }
    {
        const __m256i t = _mm256_add_epi32(s0, s1);
        __m128i u = _mm_add_epi32(_mm256_castsi256_si128(t), _mm256_extracti128_si256(t, 1));
        u = _mm_add_epi32(u, _mm_shuffle_epi32(u, 0x4E));
        u = _mm_add_epi32(u, _mm_shuffle_epi32(u, 0xB1));
        acc_sum = (uint32_t)_mm_cvtsi128_si32(u);
    }
#else
    for (int k = 0; k < 2; k++) {
        const int16_t* acc = a->acc[(int)perspective ^ k];
        const int16_t* w = NNUE_S.output_weights[bucket_idx][k];
        for (int i = 0; i < NNUE_HIDDEN; i++) {
            int32_t x = acc[i];
            if (x < 0) x = 0;
            if (x > NNUE_QA) x = NNUE_QA;
            const int16_t y = (int16_t)(x * w[i]);
            acc_sum += (uint32_t)((int32_t)y * x);
        }
    }
#endif

    const int32_t s = (int32_t)acc_sum;

    const int32_t pre_scale = (int32_t)((uint32_t)(s / NNUE_QA + NNUE_S.output_bias[bucket_idx]) *(uint32_t)NNUE_OUTPUT_SCALE);
    return pre_scale / (NNUE_QA * NNUE_QB);
}

int nnue_eval_full(const Position* pos) {
    NnueAccumulators a;
    nnue_recompute(pos, &a);
    return nnue_eval(&a, pos->to_move, nnue_output_bucket(pos));
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
    int v = scaled_eval * 103 / 100;
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
                memcpy(w->refresh[c][l][k].acc, NNUE_S.hidden_bias[c], sizeof(w->refresh[c][l][k].acc));
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

#if defined(__ARM_NEON) || NNUE_AVX2
#define NNUE_KAFLE 1

#define NNUE_TILE_NEURONS 128
#if NNUE_AVX2
#define NNUE_TILE_REGS (NNUE_TILE_NEURONS / 16)
#else
#define NNUE_TILE_REGS (NNUE_TILE_NEURONS / 8)
#endif
_Static_assert(NNUE_HIDDEN % NNUE_TILE_NEURONS == 0, "NNUE_HIDDEN must be a multiple of the tile (128 neurons)");
#if defined(__clang__)
#define NNUE_UNROLL _Pragma("clang loop unroll(full)")
#else
#define NNUE_UNROLL
#endif
#else
#define NNUE_KAFLE 0
#endif

static void nnue_refresh(NnueThread* w, int c, NnueEntry* dest) {
    const int l = dest->a.mirror[c] ? 1 : 0, bucket_idx = dest->a.bucket[c], m = l ? 7 : 0;
    NnueRefreshEntry* o = &w->refresh[c][l][bucket_idx];
#if defined(__ARM_NEON) || NNUE_AVX2
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
                uw[removed_n++] = NNUE_WC(c, bucket_idx, feat_base + (piece_sq ^ m));
            }
            while (added) {
                const int piece_sq = __builtin_ctzll(added);
                added &= added - 1;
                dw[added_n++] = NNUE_WC(c, bucket_idx, feat_base + (piece_sq ^ m));
            }
            o->bb[piece_color][ptype] = new_bb;
        }
    }
    int16_t* restrict oa = o->acc;
    int16_t* restrict ca = dest->a.acc[c];
#if NNUE_AVX2
    for (int j = 0; j < NNUE_HIDDEN; j += NNUE_TILE_NEURONS) {
        __m256i r[NNUE_TILE_REGS];
        NNUE_UNROLL
        for (int i = 0; i < NNUE_TILE_REGS; i++) r[i] = _mm256_loadu_si256((const __m256i*)(oa + j + 16 * i));
        for (int f = 0; f < removed_n; f++) {
            const int16_t* q = uw[f] + j;
            NNUE_UNROLL
            for (int i = 0; i < NNUE_TILE_REGS; i++) r[i] = _mm256_sub_epi16(r[i], _mm256_loadu_si256((const __m256i*)(q + 16 * i)));
        }
        for (int f = 0; f < added_n; f++) {
            const int16_t* q = dw[f] + j;
            NNUE_UNROLL
            for (int i = 0; i < NNUE_TILE_REGS; i++) r[i] = _mm256_add_epi16(r[i], _mm256_loadu_si256((const __m256i*)(q + 16 * i)));
        }
        NNUE_UNROLL
        for (int i = 0; i < NNUE_TILE_REGS; i++) {
            _mm256_storeu_si256((__m256i*)(oa + j + 16 * i), r[i]);
            _mm256_storeu_si256((__m256i*)(ca + j + 16 * i), r[i]);
        }
    }
#else
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
#endif
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
                acc_sub(o->acc, NNUE_WC(c, bucket_idx, feat_base + (piece_sq ^ m)));
            }
            while (added) {
                const int piece_sq = __builtin_ctzll(added);
                added &= added - 1;
                acc_add(o->acc, NNUE_WC(c, bucket_idx, feat_base + (piece_sq ^ m)));
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
        NNUE_LK(dest->bb[c][KING], dest->bb[c ^ 1][QUEEN], c, &dest->a.mirror[c], &dest->a.bucket[c]);
        if (dest->a.mirror[c] != src_acc->a.mirror[c] || dest->a.bucket[c] != src_acc->a.bucket[c] || removed_n > 2 || added_n > 2) {
            nnue_refresh(w, c, dest);
            continue;
        }
        const int m = dest->a.mirror[c] ? 7 : 0, bucket_idx = dest->a.bucket[c];
        int16_t* a = dest->a.acc[c];
        const int16_t* p = src_acc->a.acc[c];
#define NNUE_WEIGHTS_AT(f) (NNUE_WC(c, bucket_idx, (f) ^ m))
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
    return nnue_to_search(nnue_scale_by_material(pos, nnue_eval(&src_acc->a, pos->to_move, nnue_output_bucket(pos))));
}
