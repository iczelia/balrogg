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

#include "mod/lstm.h"
#include "coder/squash.h"

void ml_reset(mlstm * s) { memset(s, 0, sizeof *s); }

void ml_step(mlstm * s, const i32 * x) {
  i32 z[ML_G], r = ml_s > 0 ? 1 << (ml_s - 1) : 0;
  int g, j;
  Fj(ML_G, z[j] = 0);
  Fj(ML_NI,
    const i8 * w = ml_w + j * ML_G;
    i32 v = x[j];
    if (v) for (g = 0; g < ML_G; g++) z[g] += w[g] * v);
  Fj(ML_G, z[j] = z[j] * (1 << ml_sw) + ml_b[j] + r);
  Fj(ML_H,
    const i8 * u = ml_u + j * ML_G;
    i32 v = s->h[j];
    if (v) for (g = 0; g < ML_G; g++) z[g] += u[g] * v);
  Fj(ML_G, z[j] >>= ml_s);
  Fj(ML_H,
    i32 i = acm_squash(z[j]), f = acm_squash(z[ML_H + j]),
        c = 2 * acm_squash(2 * z[2 * ML_H + j]) - 4096,
        o = acm_squash(z[3 * ML_H + j]);
    c = (f * s->c[j] + i * c + 2048) >> 12;
    s->c[j] = CLAMP(c, -65535, 65535);
    s->h[j] = (o * (2 * acm_squash(s->c[j] >> 3) - 4096)) >> 16);
}

u32 ml_p1(const mlstm * s, u32 node) {
  const i8 * v = ml_v + (node - 1) * ML_H;
  i32 a = ml_bv[node - 1] + (ml_t > 0 ? 1 << (ml_t - 1) : 0);
  int j;
  Fj(ML_H, a += v[j] * s->h[j]);
  return (u32) acm_squash(a >> ml_t);
}
