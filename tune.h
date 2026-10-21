/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 * Copyright (C) 2004-2026 The Stockfish developers (Stockfish 19, see the AUTHORS file of Stockfish)
 * Portions of this file are adapted from Stockfish 19. Modified by Dariusz Domagała, 2026-10-21.
 */
#ifndef TUNE_H
#define TUNE_H

#define TUNE_LIST(X)                                                                  \
             \
    X(NMP_R_BASE,     850,   400,  1400,  50)                                          \
    X(NMP_R_DEPTH,        62,    20,   120,   8)                                          \
    X(NMP_EVAL_DIV,    113,    60,   220,  12)                                          \
    X(NMP_EVAL_MAX,     5,     2,     8,   1)                                          \
                                  \
    X(RAZOR_MARGIN_CORE,       139,    60,   300,  15)                                          \
                          \
    X(RFP_MULT,        77,    40,   160,   8)                                          \
    X(RFP_D,            8,     4,    12,   1)                                          \
      \
    X(LMP_D,           10,     6,    14,   1)                                          \
    X(LMP_N_A,         14,     4,    40,   3)                                          \
    X(LMP_N_B,          5,     2,    12,   1)                                          \
    X(LMP_I_A,         75,    30,   150,   8)                                          \
    X(LMP_I_B,          7,     3,    16,   1)                                          \
                                      \
    X(SEE_Q,           44,    20,   100,   6)                                          \
    X(SEE_B,           21,    10,    50,   3)                                          \
                             \
    X(CHP_A,           26,  -100,   150,  15)                                          \
    X(CHP_B,          146,    60,   300,  15)                                          \
      \
    X(LMR_N_A,       1056,   600,  1500,  60)                                          \
    X(LMR_N_B,        432,     0,   900,  60)                                          \
    X(LMR_Q_A,       2011,  1200,  2800, 100)                                          \
    X(LMR_Q_B,       1098,   400,  1800, 100)                                          \
    X(LMR_K0,        -372, -1200,   400,  60)                                          \
    X(LMR_NIMP,       530,     0,  1200,  60)                                          \
    X(LMR_HDIV,       337,   150,   800,  40)                                          \
                         \
    X(SING_MARGIN,       11,     4,    24,   2)                                          \
    X(SING_D,           8,     5,    10,   1)                                          \
                                                          \
    X(PC_MARGIN,      155,    80,   300,  15)                                          \
                                                          \
    X(QS_MARGIN_BETA,   134,    60,   250,  12)                                          \
    X(QS_MARGIN_ALPHA,    85,    40,   200,  10)                                          \
          \
    X(QS_FUT_SEE,     201,    80,   400,  20)                                          \
                                \
    X(HB_A,            27,    10,    60,   3)                                          \
    X(HB_B,            31,    10,    70,   4)                                          \
    X(HB_C,            33,     0,   100,   6)                                          \
       \
    X(ASP_DELTA0,       9,     4,    30,   2)                                          \
    X(ASP_DIV,         81,    30,   200,  10)                                          \
    X(ASP_GROW,        78,    30,   200,  10)                                          \
                   \
                     \
    X(T_CORR_WEIGHT_PAWN,    15341,  7000, 31000, 1200)                                      \
    X(T_CORR_WEIGHT_NONPAWN,      12906,  6000, 26000, 1000)                                      \
    X(T_CORR_WEIGHT_CONT,     8761,  4000, 18000,  700)                                      \
         \
    X(T_CORR_UPDATE_NONPAWN,        186,    90,   380,   15)                                      \
    X(T_CORR_UPDATE_CONT2,        130,    60,   260,   10)                                      \
    X(T_CORR_UPDATE_CONT4,         70,    30,   140,    6)                                      \
    X(T_CORR_BONUS_MOVE,       12,     6,    24,    1)                                      \
    X(T_CORR_BONUS_NO_MOVE,       18,     9,    36,    2)                                      \
                     \
    X(T_FF2_A,            60,    20,   140,    6)                                      \
    X(T_FF2_B,            44,    15,   100,    4)                                      \
    X(T_FF2_C,            33,    10,    80,    4)                                      \
    X(T_FF2_LD,           12,     6,    18,    1)                                      \
                \
    X(T_FB_A,             86,    30,   180,    8)                                      \
    X(T_FB_B,             91,    40,   180,    8)                                      \
    X(T_FB_LD,             8,     4,    14,    1)                                      \
      \
    X(T_LMR_EXTEND_THRESHOLD,     -3,    -6,    -1,    1)                                      \
    X(T_LMR_SKIP_RED_THRESHOLD,           3,     1,     6,    1)                                      \
    X(T_LMR_SKIP_NO_TT_ADD,         2,     0,     4,    1)                                      \
          \
    X(T_RED_ST_OFF,       96,    30,   200,   10)                                      \
    X(T_RED_NMP_ST,      480,   200,  1000,   40)                                      \
    X(T_RED_EV_DIVISOR,     16,     8,    32,    1)                                      \
                      \
    X(T_CHECK_SEE_MARGIN,    160,    80,   320,   12)                                      \
              \
    X(T_PS_DEEPER_MARGIN,     20,     6,    50,    3)                                      \
    X(T_PS_SHALLOWER_MARGIN,      3,     0,    12,    1)                                      \
    X(T_PS_PV_DEPTH_MAX,       14,     8,    22,    1)                                      \
    X(T_PS_REDUCTION_MARGIN,      61,    25,   130,    6)                                      \
      \
    X(T_DOUBLE_EXT_MARGIN,     16,     4,    40,    2)                                      \
    X(T_TRIPLE_EXT_MARGIN,     96,    40,   200,    8)                                      \
    X(T_DOUBLE_EXT_LIMIT,       8,     3,    14,    1)                                      \
              \
    X(T_EXT_PHASE_MAX,         10,     4,    20,    1)                                      \
    X(T_PROBCUT_D,         6,     5,    10,    1)

#define PROBCUT_MARGIN PC_MARGIN
#define QS_DELTA_BETA  QS_MARGIN_BETA
#define QS_DELTA_ALPHA  QS_MARGIN_ALPHA
#define QS_FUT_MARGIN        QS_FUT_SEE

#define CORR_WEIGHT_PAWN     T_CORR_WEIGHT_PAWN
#define CORR_WEIGHT_NONPAWN       T_CORR_WEIGHT_NONPAWN
#define CORR_WEIGHT_CONT     T_CORR_WEIGHT_CONT
#define CORR_UPDATE_NONPAWN       T_CORR_UPDATE_NONPAWN
#define CORR_UPDATE_CONT2       T_CORR_UPDATE_CONT2
#define CORR_UPDATE_CONT4       T_CORR_UPDATE_CONT4
#define CORR_BONUS_MOVE     T_CORR_BONUS_MOVE
#define CORR_BONUS_NO_MOVE     T_CORR_BONUS_NO_MOVE
#define FF2_A          T_FF2_A
#define FF2_B          T_FF2_B
#define FF2_C          T_FF2_C
#define FF2_LD         T_FF2_LD
#define FB_A           T_FB_A
#define FB_B           T_FB_B
#define FB_LD          T_FB_LD
#define LMR_EXTEND_THRESHOLD   T_LMR_EXTEND_THRESHOLD
#define LMR_SKIP_RED_THRESHOLD        T_LMR_SKIP_RED_THRESHOLD
#define LMR_SKIP_NO_TT_ADD      T_LMR_SKIP_NO_TT_ADD
#define RED_ST_OFF     T_RED_ST_OFF
#define RED_NMP_ST     T_RED_NMP_ST
#define CHECK_SEE_MARGIN   T_CHECK_SEE_MARGIN
#define PS_DEEPER_MARGIN   T_PS_DEEPER_MARGIN
#define PS_SHALLOWER_MARGIN   T_PS_SHALLOWER_MARGIN
#define PS_PV_DEPTH_MAX     T_PS_PV_DEPTH_MAX
#define PS_REDUCTION_MARGIN    T_PS_REDUCTION_MARGIN
#define DOUBLE_EXT_MARGIN   T_DOUBLE_EXT_MARGIN
#define TRIPLE_EXT_MARGIN   T_TRIPLE_EXT_MARGIN
#define DOUBLE_EXT_LIMIT    T_DOUBLE_EXT_LIMIT
#define EXT_PHASE_MAX       T_EXT_PHASE_MAX
#define PROBCUT_D      T_PROBCUT_D

#  define TUNE_X_ENUM(n, d, a, b, c) n = (d),
enum { TUNE_LIST(TUNE_X_ENUM) };
#  undef TUNE_X_ENUM

#endif
