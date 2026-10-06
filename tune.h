/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#ifndef TUNE_H
#define TUNE_H

#define TUNE_LIST(X)                                                                  \
             \
    X(NMP_R_BASE,     855,   400,  1400,  50)                                          \
    X(NMP_R_DEPTH,        61,    20,   120,   8)                                          \
    X(NMP_EVAL_DIV,    111,    60,   220,  12)                                          \
    X(NMP_EVAL_MAX,     5,     2,     8,   1)                                          \
                                  \
    X(RAZOR_MARGIN_CORE,       144,    60,   300,  15)                                          \
                          \
    X(RFP_MULT,        88,    40,   160,   8)                                          \
    X(RFP_D,            8,     4,    12,   1)                                          \
      \
    X(LMP_D,           10,     6,    14,   1)                                          \
    X(LMP_N_A,         13,     4,    40,   3)                                          \
    X(LMP_N_B,          4,     2,    12,   1)                                          \
    X(LMP_I_A,         70,    30,   150,   8)                                          \
    X(LMP_I_B,          7,     3,    16,   1)                                          \
                                      \
    X(SEE_Q,           48,    20,   100,   6)                                          \
    X(SEE_B,           23,    10,    50,   3)                                          \
                             \
    X(CHP_A,           24,  -100,   150,  15)                                          \
    X(CHP_B,          152,    60,   300,  15)                                          \
      \
    X(LMR_N_A,       1060,   600,  1500,  60)                                          \
    X(LMR_N_B,        410,     0,   900,  60)                                          \
    X(LMR_Q_A,       1970,  1200,  2800, 100)                                          \
    X(LMR_Q_B,       1114,   400,  1800, 100)                                          \
    X(LMR_K0,        -400, -1200,   400,  60)                                          \
    X(LMR_NIMP,       504,     0,  1200,  60)                                          \
    X(LMR_HDIV,       362,   150,   800,  40)                                          \
                         \
    X(SING_MARGIN,       10,     4,    24,   2)                                          \
    X(SING_D,           7,     5,    10,   1)                                          \
                                                          \
    X(PC_MARGIN,      152,    80,   300,  15)                                          \
                                                          \
    X(QS_MARGIN_BETA,   125,    60,   250,  12)                                          \
    X(QS_MARGIN_ALPHA,    85,    40,   200,  10)                                          \
                                \
    X(HB_A,            27,    10,    60,   3)                                          \
    X(HB_B,            32,    10,    70,   4)                                          \
    X(HB_C,            34,     0,   100,   6)                                          \
       \
    X(ASP_DELTA0,       8,     4,    30,   2)                                          \
    X(ASP_DIV,         81,    30,   200,  10)                                          \
    X(ASP_GROW,        76,    30,   200,  10)

#  define TUNE_X_ENUM(n, d, a, b, c) n = (d),
enum { TUNE_LIST(TUNE_X_ENUM) };
#  undef TUNE_X_ENUM

#endif
