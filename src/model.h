/*  Copyright (C) 2026 Kamila Szewczyk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, version 3.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program. If not, see <http://www.gnu.org/licenses/>.  */

#ifndef BLR_MODEL_H
#define BLR_MODEL_H

#include "rc.h"

/*  Adaptive integer coder using zero, sign, length, and mantissa models.  */

typedef struct {
  u8 depth;             /*  length-tree bits: 2..5  */
  u8 sgn;               /*  signed: a sign bit follows a nonzero flag  */
  u8 shist;             /*  sign history width in bits, 1 or 2  */
  u8 freeze;            /*  mantissa index stops moving at this value  */
  u8 bank;              /*  1 banks the mantissa tree by the bit-length,
                            0 shares one tree across all of them  */
  u8 order;             /*  predictor: 0 none, 1 first, 2 second  */
} mdl_cfg;

typedef struct {
  mdl_cfg c;
  u16 * p;              /*  `n` probability slots  */
  u8 * cn;              /*  observation count per slot, or NULL  */
  u32 n;                /*  slots in the block  */
  u32 bsg, blen, bmant; /*  sign, length and mantissa table bases  */
  u32 wlen, wmant;      /*  length and mantissa bank widths  */
  u8 h0, h1, hl;        /*  zero, sign and bit-length histories  */
  u32 m0, m1;           /*  predictor state  */
} model;

/*  Build the adaptation table before mdl_init.  */
void mdl_adapt(void);

void mdl_init(model * m, const mdl_cfg * c);
void mdl_free(model * m);
/*  Clear histories and predictors while retaining probabilities.  */
void mdl_reset(model * m);

HOT FLATTEN void mdl_enc(model * m, rc_enc * e, u32 v);
HOT FLATTEN u32 mdl_dec(model * m, rc_dec * d);

#endif
