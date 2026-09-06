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

/*  The per-bit mixer kernel.  This file is compiled once as the portable
    scalar kernel and, on x86 hosts, again with -msse2 and BLR_CM_SSE2 as the
    vector kernel.  cm.c picks one at run time.  Both round identically, so
    archives do not depend on the kernel.  */

#include "cm.h"
#include "prof.h"

#if defined(BLR_CM_SSE2)
#include <emmintrin.h>
#define CM_BIT  cm_bit_sse2

typedef __m128i mixin;

/*  Pack the inputs before moving them into the vector.  */
static INLINE mixin mix_in6(int a, int b, int c, int d, int e, int f) {
  u32 ab = ((u32) a & 0xFFFFu) | ((u32) b << 16);
  u32 cd = ((u32) c & 0xFFFFu) | ((u32) d << 16);
  u32 ef = ((u32) e & 0xFFFFu) | ((u32) f << 16);
  return _mm_unpacklo_epi64(_mm_unpacklo_epi32(_mm_cvtsi32_si128((int) ab),
                                                 _mm_cvtsi32_si128((int) cd)),
                            _mm_cvtsi32_si128((int) ef));
}

static INLINE i32 mix_dot(mixin t, const short * w) {
  __m128i s = _mm_madd_epi16(t, *(const __m128i *) (const void *) w);
  s = _mm_srai_epi32(s, 8);
  s = _mm_add_epi32(s, _mm_srli_si128(s, 8));
  s = _mm_add_epi32(s, _mm_srli_si128(s, 4));
  return (i32) _mm_cvtsi128_si32(s);
}

static INLINE void mix_train(mixin t, short * w, int e) {
  __m128i tmp;
  if (!e) return;
  tmp = _mm_adds_epi16(t, t);
  tmp = _mm_mulhi_epi16(tmp, _mm_set1_epi16((short) e));
  tmp = _mm_adds_epi16(tmp, _mm_set1_epi16(1));
  tmp = _mm_srai_epi16(tmp, 1);
  *(__m128i *) (void *) w = _mm_adds_epi16(tmp, *(const __m128i *) (const void *) w);
}

#else
#define CM_BIT  cm_bit_scalar

typedef struct { short v[CM_NI]; } mixin;

static INLINE mixin mix_in6(int a, int b, int c, int d, int e, int f) {
  mixin t;
  t.v[0] = (short) a;  t.v[1] = (short) b;
  t.v[2] = (short) c;  t.v[3] = (short) d;
  t.v[4] = (short) e;  t.v[5] = (short) f;
  t.v[6] = 0;  t.v[7] = 0;
  return t;
}

static INLINE i32 mix_dot(mixin t, const short * w) {
  i32 s = 0;
  int n;
  for (n = 0; n < CM_NI; n += 2)
    s += ((i32) t.v[n] * w[n] + (i32) t.v[n + 1] * w[n + 1]) >> 8;
  return s;
}

static INLINE void mix_train(mixin t, short * w, int e) {
  int i;
  if (!e) return;
  Fi(CM_NI,
    i32 v = w[i] + (((((i32) t.v[i] * e * 2) >> 16) + 1) >> 1);
    if (v < -32768) v = -32768;
    if (v > 32767) v = 32767;
    w[i] = (short) v);
}
#endif

HOT int CM_BIT(cm * restrict c, int st, int sel, u32 h, u16 * restrict p,
               u8 * restrict cnt, int exp, int bit) {
  cm_stage * s = c->st + st;
  u8 * sp = s->hist + (h & c->hmask);
  int state = *sp;
  short * w = s->w + sel * CM_NI;
  u32 x = s->sm[state], ps = x >> 16, pr, d;
  i32 nv;
  int mi = 0;
  mixin in;
  /*  The state map stays in 1..0xFFFE and cm_squash returns 16..65504, so
      neither probability needs clamping.  The caller's `p` is P(0).  */
  if (exp < 0)
    in = mix_in6(cm_str16[65536u - *p], cm_str16[ps], 256,
                 cm_str16[ps] * cm_nexd[state], 0, 0);
  else {
    /*  Add learned match accuracy and capped match length, both signed by
        the expected bit.  */
    u32 lb = c->mlen < CM_MLB ? c->mlen : CM_MLB - 1;
    int sg = exp ? 1 : -1;
    mi = st * CM_MLB + (int) lb;
    in = mix_in6(cm_str16[65536u - *p], cm_str16[ps], 256,
                 cm_str16[ps] * cm_nexd[state],
                 sg * cm_str16[c->mp[mi]],
                 sg * (int) (c->mlen < 32 ? c->mlen : 32) * 64);
  }
  {
    int dot = (int) (mix_dot(in, w) >> 7);
    if (dot < -2047) dot = -2047;
    if (dot > 2047) dot = 2047;
    pr = cm_squash16[dot + 2048];                       /*  P(1)  */
  }
  if (c->d) bit = rc_dec_bit_raw(c->d, 65536u - pr);
  /*  Account for mixed bits because rc_*_bit_raw does not report them.  */
  PROF(prof_hook(NULL, 65536u - pr, bit));
  *sp = cm_nex[state][bit];
  d = rc_divt[x & 0xFFFF];
  nv = bit ? (i32) ps + (i32) (((0xFFFF - ps) * d) >> 16)
           : (i32) ps - (i32) ((ps * d) >> 16);
  if (nv < 1) nv = 1;
  if (nv > 0xFFFE) nv = 0xFFFE;
  s->sm[state] = ((u32) nv << 16) | ((x & 0xFFFF) + ((int) (x & 0xFFFF) < c->lim));
  mix_train(in, w, ((bit << 12) - (int) (pr >> 4)) * c->lr);
  *p = rc_adapt(*p, cnt, c->lim, bit);
  if (exp >= 0) c->mp[mi] = rc_adapt(c->mp[mi], c->mpc + mi, c->lim, bit == exp);
  if (!c->d) rc_enc_bit_raw(c->e, 65536u - pr, bit);
  return bit;
}
