/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#include <stddef.h>
#include <string.h>
#include "fathom/tbprobe.h"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
#endif
#define TB_ALIGNED __attribute__((aligned(64)))
static const unsigned char KBBvK_wdl[] TB_ALIGNED = {
#embed <KBBvK.rtbw>
};
static const unsigned char KBBvK_dtz[] TB_ALIGNED = {
#embed <KBBvK.rtbz>
};
static const unsigned char KBNvK_wdl[] TB_ALIGNED = {
#embed <KBNvK.rtbw>
};
static const unsigned char KBNvK_dtz[] TB_ALIGNED = {
#embed <KBNvK.rtbz>
};
static const unsigned char KBPvK_wdl[] TB_ALIGNED = {
#embed <KBPvK.rtbw>
};
static const unsigned char KBPvK_dtz[] TB_ALIGNED = {
#embed <KBPvK.rtbz>
};
static const unsigned char KBvK_wdl[] TB_ALIGNED = {
#embed <KBvK.rtbw>
};
static const unsigned char KBvK_dtz[] TB_ALIGNED = {
#embed <KBvK.rtbz>
};
static const unsigned char KBvKB_wdl[] TB_ALIGNED = {
#embed <KBvKB.rtbw>
};
static const unsigned char KBvKB_dtz[] TB_ALIGNED = {
#embed <KBvKB.rtbz>
};
static const unsigned char KBvKN_wdl[] TB_ALIGNED = {
#embed <KBvKN.rtbw>
};
static const unsigned char KBvKN_dtz[] TB_ALIGNED = {
#embed <KBvKN.rtbz>
};
static const unsigned char KBvKP_wdl[] TB_ALIGNED = {
#embed <KBvKP.rtbw>
};
static const unsigned char KBvKP_dtz[] TB_ALIGNED = {
#embed <KBvKP.rtbz>
};
static const unsigned char KNNvK_wdl[] TB_ALIGNED = {
#embed <KNNvK.rtbw>
};
static const unsigned char KNNvK_dtz[] TB_ALIGNED = {
#embed <KNNvK.rtbz>
};
static const unsigned char KNPvK_wdl[] TB_ALIGNED = {
#embed <KNPvK.rtbw>
};
static const unsigned char KNPvK_dtz[] TB_ALIGNED = {
#embed <KNPvK.rtbz>
};
static const unsigned char KNvK_wdl[] TB_ALIGNED = {
#embed <KNvK.rtbw>
};
static const unsigned char KNvK_dtz[] TB_ALIGNED = {
#embed <KNvK.rtbz>
};
static const unsigned char KNvKN_wdl[] TB_ALIGNED = {
#embed <KNvKN.rtbw>
};
static const unsigned char KNvKN_dtz[] TB_ALIGNED = {
#embed <KNvKN.rtbz>
};
static const unsigned char KNvKP_wdl[] TB_ALIGNED = {
#embed <KNvKP.rtbw>
};
static const unsigned char KNvKP_dtz[] TB_ALIGNED = {
#embed <KNvKP.rtbz>
};
static const unsigned char KPPvK_wdl[] TB_ALIGNED = {
#embed <KPPvK.rtbw>
};
static const unsigned char KPPvK_dtz[] TB_ALIGNED = {
#embed <KPPvK.rtbz>
};
static const unsigned char KPvK_wdl[] TB_ALIGNED = {
#embed <KPvK.rtbw>
};
static const unsigned char KPvK_dtz[] TB_ALIGNED = {
#embed <KPvK.rtbz>
};
static const unsigned char KPvKP_wdl[] TB_ALIGNED = {
#embed <KPvKP.rtbw>
};
static const unsigned char KPvKP_dtz[] TB_ALIGNED = {
#embed <KPvKP.rtbz>
};
static const unsigned char KQBvK_wdl[] TB_ALIGNED = {
#embed <KQBvK.rtbw>
};
static const unsigned char KQBvK_dtz[] TB_ALIGNED = {
#embed <KQBvK.rtbz>
};
static const unsigned char KQNvK_wdl[] TB_ALIGNED = {
#embed <KQNvK.rtbw>
};
static const unsigned char KQNvK_dtz[] TB_ALIGNED = {
#embed <KQNvK.rtbz>
};
static const unsigned char KQPvK_wdl[] TB_ALIGNED = {
#embed <KQPvK.rtbw>
};
static const unsigned char KQPvK_dtz[] TB_ALIGNED = {
#embed <KQPvK.rtbz>
};
static const unsigned char KQQvK_wdl[] TB_ALIGNED = {
#embed <KQQvK.rtbw>
};
static const unsigned char KQQvK_dtz[] TB_ALIGNED = {
#embed <KQQvK.rtbz>
};
static const unsigned char KQRvK_wdl[] TB_ALIGNED = {
#embed <KQRvK.rtbw>
};
static const unsigned char KQRvK_dtz[] TB_ALIGNED = {
#embed <KQRvK.rtbz>
};
static const unsigned char KQvK_wdl[] TB_ALIGNED = {
#embed <KQvK.rtbw>
};
static const unsigned char KQvK_dtz[] TB_ALIGNED = {
#embed <KQvK.rtbz>
};
static const unsigned char KQvKB_wdl[] TB_ALIGNED = {
#embed <KQvKB.rtbw>
};
static const unsigned char KQvKB_dtz[] TB_ALIGNED = {
#embed <KQvKB.rtbz>
};
static const unsigned char KQvKN_wdl[] TB_ALIGNED = {
#embed <KQvKN.rtbw>
};
static const unsigned char KQvKN_dtz[] TB_ALIGNED = {
#embed <KQvKN.rtbz>
};
static const unsigned char KQvKP_wdl[] TB_ALIGNED = {
#embed <KQvKP.rtbw>
};
static const unsigned char KQvKP_dtz[] TB_ALIGNED = {
#embed <KQvKP.rtbz>
};
static const unsigned char KQvKQ_wdl[] TB_ALIGNED = {
#embed <KQvKQ.rtbw>
};
static const unsigned char KQvKQ_dtz[] TB_ALIGNED = {
#embed <KQvKQ.rtbz>
};
static const unsigned char KQvKR_wdl[] TB_ALIGNED = {
#embed <KQvKR.rtbw>
};
static const unsigned char KQvKR_dtz[] TB_ALIGNED = {
#embed <KQvKR.rtbz>
};
static const unsigned char KRBvK_wdl[] TB_ALIGNED = {
#embed <KRBvK.rtbw>
};
static const unsigned char KRBvK_dtz[] TB_ALIGNED = {
#embed <KRBvK.rtbz>
};
static const unsigned char KRNvK_wdl[] TB_ALIGNED = {
#embed <KRNvK.rtbw>
};
static const unsigned char KRNvK_dtz[] TB_ALIGNED = {
#embed <KRNvK.rtbz>
};
static const unsigned char KRPvK_wdl[] TB_ALIGNED = {
#embed <KRPvK.rtbw>
};
static const unsigned char KRPvK_dtz[] TB_ALIGNED = {
#embed <KRPvK.rtbz>
};
static const unsigned char KRRvK_wdl[] TB_ALIGNED = {
#embed <KRRvK.rtbw>
};
static const unsigned char KRRvK_dtz[] TB_ALIGNED = {
#embed <KRRvK.rtbz>
};
static const unsigned char KRvK_wdl[] TB_ALIGNED = {
#embed <KRvK.rtbw>
};
static const unsigned char KRvK_dtz[] TB_ALIGNED = {
#embed <KRvK.rtbz>
};
static const unsigned char KRvKB_wdl[] TB_ALIGNED = {
#embed <KRvKB.rtbw>
};
static const unsigned char KRvKB_dtz[] TB_ALIGNED = {
#embed <KRvKB.rtbz>
};
static const unsigned char KRvKN_wdl[] TB_ALIGNED = {
#embed <KRvKN.rtbw>
};
static const unsigned char KRvKN_dtz[] TB_ALIGNED = {
#embed <KRvKN.rtbz>
};
static const unsigned char KRvKP_wdl[] TB_ALIGNED = {
#embed <KRvKP.rtbw>
};
static const unsigned char KRvKP_dtz[] TB_ALIGNED = {
#embed <KRvKP.rtbz>
};
static const unsigned char KRvKR_wdl[] TB_ALIGNED = {
#embed <KRvKR.rtbw>
};
static const unsigned char KRvKR_dtz[] TB_ALIGNED = {
#embed <KRvKR.rtbz>
};
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

typedef struct { const char* param_name; const unsigned char* data_buf; size_t param_size; } TbFile;
#define TB_PAIR(n) { #n ".rtbw", n##_wdl, sizeof(n##_wdl) }, { #n ".rtbz", n##_dtz, sizeof(n##_dtz) }
static const TbFile tb_files[] = {
    TB_PAIR(KBBvK),
    TB_PAIR(KBNvK),
    TB_PAIR(KBPvK),
    TB_PAIR(KBvK),
    TB_PAIR(KBvKB),
    TB_PAIR(KBvKN),
    TB_PAIR(KBvKP),
    TB_PAIR(KNNvK),
    TB_PAIR(KNPvK),
    TB_PAIR(KNvK),
    TB_PAIR(KNvKN),
    TB_PAIR(KNvKP),
    TB_PAIR(KPPvK),
    TB_PAIR(KPvK),
    TB_PAIR(KPvKP),
    TB_PAIR(KQBvK),
    TB_PAIR(KQNvK),
    TB_PAIR(KQPvK),
    TB_PAIR(KQQvK),
    TB_PAIR(KQRvK),
    TB_PAIR(KQvK),
    TB_PAIR(KQvKB),
    TB_PAIR(KQvKN),
    TB_PAIR(KQvKP),
    TB_PAIR(KQvKQ),
    TB_PAIR(KQvKR),
    TB_PAIR(KRBvK),
    TB_PAIR(KRNvK),
    TB_PAIR(KRPvK),
    TB_PAIR(KRRvK),
    TB_PAIR(KRvK),
    TB_PAIR(KRvKB),
    TB_PAIR(KRvKN),
    TB_PAIR(KRvKP),
    TB_PAIR(KRvKR),
};
typedef char tb_files_count_check[(sizeof(tb_files) / sizeof(tb_files[0]) == 70) ? 1 : -1];

const unsigned char* syzygy_embedded_file(const char* param_name, const char* param_suffix, size_t* param_size) {
    const size_t n = strlen(param_name), s = strlen(param_suffix);
    for (size_t i = 0; i < sizeof(tb_files) / sizeof(tb_files[0]); i++) {
        const char* p = tb_files[i].param_name;
        if (strlen(p) == n + s && strncmp(p, param_name, n) == 0 && strcmp(p + n, param_suffix) == 0) {
            *param_size = tb_files[i].param_size;
            return tb_files[i].data_buf;
        }
    }
    return NULL;
}
