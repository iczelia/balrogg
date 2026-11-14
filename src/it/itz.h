/*  Copyright (C) 2026 Kamila Szewczyk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, version 3.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#ifndef BLR_ITZ_H
#define BLR_ITZ_H

#include "sys/common.h"

enum {
  ITZ_K_ALG, ITZ_K_MISS, ITZ_K_FLAG, ITZ_K_NVAL, ITZ_K_NEV, ITZ_K_GAP,
  ITZ_K_WID, ITZ_K_LEN, ITZ_K_XB, ITZ_K_PAD, ITZ_K_TBYTE, ITZ_K_NPRE,
  ITZ_K_N
};

enum { ITZ_A_IT, ITZ_A_MPT, ITZ_A_GRD, ITZ_A_NONE, ITZ_A_N };

enum { ITZ_F_EV = 1, ITZ_F_PART = 2, ITZ_F_TRUNC = 4, ITZ_F_TAIL = 8 };

typedef struct {
  u32 pos;
  u8 w;
} itz_ev;

typedef struct {
  u8 flag;
  u32 nval, len, xb, pad, npre, ev0, nev, tb0, ntb;
} itz_blk;

typedef struct {
  u32 frames;
  int ch, b16, it215, alg;
  u32 miss, nblk, cblk, nev, cev, ntb, ctb, mcnt[3];
  itz_blk * blk;
  itz_ev * ev;
  u8 * tb;
  sz used;
} itz_side;

typedef struct {
  void (*put)(void * u, int kind, u32 v);
  int (*get)(void * u, int kind, u32 * v);
  void * u;
} itz_io;

void itz_init(itz_side * si);
void itz_free(itz_side * si);

int itz_decode(const u8 * s, sz n, u32 frames, int ch, int b16, int it215,
               i16 * pcm, itz_side * si);
u8 * itz_encode(const i16 * pcm, const itz_side * si, sz * outn);

int itz_side_put(const itz_side * si, const itz_io * io);
int itz_side_get(itz_side * si, u32 frames, int ch, int b16,
                 int it215, const itz_io * io);

#endif
