/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) Dariusz Domagała
 */
#ifndef MAGIC_H
#define MAGIC_H

#include "board.h"

int magic_init(void);

extern int magic_ready;

Bitboard magic_rook_attacks(int square, Bitboard occupied);
Bitboard magic_bishop_attacks(int square, Bitboard occupied);

Bitboard magic_rook_mask(int square);
Bitboard magic_bishop_mask(int square);

unsigned long magic_tries_rook(void);
unsigned long magic_tries_bishop(void);

int magic_corrupt_rook_mask(int square);

#endif
