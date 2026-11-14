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

#if defined(HAVE_SSE2) && defined(__SSE2__)
#include <emmintrin.h>

#define ML_P ((ML_NI + ML_H) / 2)

static i16 mlwi[ML_P][2 * ML_G];

static void ml_init(void) {
  static int done = 0;
  int q, g;
  if (done) return;
  done = 1;
  for (q = 0; q < ML_P; q++)
    for (g = 0; g < 2 * ML_G; g++) {
      int j = 2 * q + (g & 1);
      mlwi[q][g] = j < ML_NI ? ml_w[j * ML_G + g / 2]
                             : ml_u[(j - ML_NI) * ML_G + g / 2];
    }
}

static void ml_pairs(__m128i * a, const i32 * v, int q0, int nq) {
  int q, k;
  for (q = 0; q < nq; q++) {
    const __m128i * w = (const __m128i *) (void *) mlwi[q0 + q];
    __m128i b;
    if (!(v[2 * q] | v[2 * q + 1])) continue;
    b = _mm_unpacklo_epi16(_mm_set1_epi16((short) v[2 * q]),
                           _mm_set1_epi16((short) v[2 * q + 1]));
    Fk(ML_G / 4,
      a[k] = _mm_add_epi32(a[k], _mm_madd_epi16(_mm_loadu_si128(w + k), b)));
  }
}

static void ml_gates(const mlstm * s, const i32 * x, i32 * z) {
  __m128i a[ML_G / 4];
  i32 r = ml_s > 0 ? 1 << (ml_s - 1) : 0;
  int k;
  Fk(ML_G / 4, a[k] = _mm_setzero_si128());
  ml_pairs(a, x, 0, ML_NI / 2);
  Fk(ML_G / 4,
    __m128i b = _mm_set_epi32(ml_b[4 * k + 3], ml_b[4 * k + 2],
                              ml_b[4 * k + 1], ml_b[4 * k]);
    a[k] = _mm_add_epi32(_mm_slli_epi32(a[k], ml_sw),
                         _mm_add_epi32(b, _mm_set1_epi32(r))));
  ml_pairs(a, s->h, ML_NI / 2, ML_H / 2);
  Fk(ML_G / 4, _mm_storeu_si128((__m128i *) (void *) (z + 4 * k),
                                _mm_srai_epi32(a[k], ml_s)));
}

void ml_reset(mlstm * s) { ml_init();  memset(s, 0, sizeof *s); }
#else
static void ml_gates(const mlstm * s, const i32 * x, i32 * z) {
  i32 r = ml_s > 0 ? 1 << (ml_s - 1) : 0;
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
}

void ml_reset(mlstm * s) { memset(s, 0, sizeof *s); }
#endif

void ml_step(mlstm * s, const i32 * x) {
  i32 z[ML_G];
  int j;
  ml_gates(s, x, z);
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
