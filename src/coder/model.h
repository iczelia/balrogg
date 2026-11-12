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

#ifndef BLR_MODEL_H
#define BLR_MODEL_H

#include "coder/rc.h"

typedef struct {
  u8 depth;
  u8 sgn;
  u8 shist;
  u8 freeze;
  u8 bank;
  u8 order;
} mdl_cfg;

typedef struct {
  mdl_cfg c;
  u16 * p;
  u8 * cn;
  u32 n;
  u32 bsg, blen, bmant;
  u32 wlen, wmant;
  u8 h0, h1, hl;
  u32 m0, m1;
} model;

void mdl_adapt(void);

void mdl_init(model * m, const mdl_cfg * c);
void mdl_init_in(model * m, const mdl_cfg * c, blr_arena * a);
void mdl_free(model * m);
void mdl_reset(model * m);

HOT FLATTEN void mdl_enc(model * m, rc_enc * e, u32 v);
HOT FLATTEN u32 mdl_dec(model * m, rc_dec * d);

#endif
