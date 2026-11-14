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

#ifndef BLR_MODLSTM_H
#define BLR_MODLSTM_H

#include "sys/common.h"

#define ML_H  16
#define ML_NI 34
#define ML_G  (4 * ML_H)

extern const i8 ml_w[ML_NI * ML_G], ml_u[ML_H * ML_G], ml_v[255 * ML_H];
extern const i16 ml_b[ML_G], ml_bv[255];
extern const int ml_sw, ml_s, ml_t;

typedef struct { i32 h[ML_H], c[ML_H]; } mlstm;

void ml_reset(mlstm * s);
void ml_step(mlstm * s, const i32 * x);
u32 ml_p1(const mlstm * s, u32 node);

#endif
