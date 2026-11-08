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

#ifndef BLR_CMMIX_H
#define BLR_CMMIX_H

#include "coder/cm.h"
#include "sys/prof.h"

#if CM_SIMD
#include <emmintrin.h>

typedef __m128i mixin;

static INLINE mixin mix_in6(int a, u32 bd, int e, int f) {
  u32 ac = ((u32) a & 0xFFFFu) | 256u << 16;
  u32 ef = ((u32) e & 0xFFFFu) | ((u32) f << 16);
  return _mm_unpacklo_epi64(_mm_unpacklo_epi16(_mm_cvtsi32_si128((int) ac),
                                                 _mm_cvtsi32_si128((int) bd)),
                            _mm_cvtsi32_si128((int) ef));
}

static INLINE mixin mix_put6(mixin t, int v) {
  return _mm_insert_epi16(t, v, 6);
}
static INLINE mixin mix_put7(mixin t, int v) {
  return _mm_insert_epi16(t, v, 7);
}

static INLINE i32 mix_dot(mixin t, const short * w, int plain) {
  __m128i weights = plain ? _mm_loadl_epi64((const __m128i *) (const void *) w)
                         : *(const __m128i *) (const void *) w;
  __m128i s = _mm_madd_epi16(t, weights);
  s = _mm_srai_epi32(s, 8);
  if (!plain) s = _mm_add_epi32(s, _mm_srli_si128(s, 8));
  s = _mm_add_epi32(s, _mm_srli_si128(s, 4));
  return (i32) _mm_cvtsi128_si32(s);
}

static INLINE void mix_train(mixin t, short * w, int e, int plain) {
  __m128i tmp, twice;
  int high = (e + 32768) >> 16;
  twice = _mm_add_epi16(t, t);
  tmp = _mm_mulhi_epi16(twice, _mm_set1_epi16((short) e));
  tmp = _mm_add_epi16(tmp,
                      _mm_mullo_epi16(twice, _mm_set1_epi16((short) high)));
  tmp = _mm_add_epi16(tmp, _mm_set1_epi16(1));
  tmp = _mm_srai_epi16(tmp, 1);
  if (plain) {
    tmp = _mm_adds_epi16(tmp,
                         _mm_loadl_epi64((const __m128i *) (const void *) w));
    _mm_storel_epi64((__m128i *) (void *) w, tmp);
  } else
    *(__m128i *) (void *) w =
      _mm_adds_epi16(tmp, *(const __m128i *) (const void *) w);
}

#else
typedef struct { short v[CM_NI]; } mixin;

static INLINE mixin mix_in6(int a, u32 bd, int e, int f) {
  mixin t;
  t.v[0] = (short) a;  t.v[1] = (short) bd;
  t.v[2] = 256;  t.v[3] = (short) (bd >> 16);
  t.v[4] = (short) e;  t.v[5] = (short) f;
  t.v[6] = 0;  t.v[7] = 0;
  return t;
}

static INLINE mixin mix_put6(mixin t, int v) { t.v[6] = (short) v;  return t; }
static INLINE mixin mix_put7(mixin t, int v) { t.v[7] = (short) v;  return t; }

static INLINE i32 mix_dot(mixin t, const short * w, int plain) {
  i32 s = 0;
  int n;
  for (n = 0; n < (plain ? 4 : CM_NI); n += 2)
    s += ((i32) t.v[n] * w[n] + (i32) t.v[n + 1] * w[n + 1]) >> 8;
  return s;
}

static INLINE void mix_train(mixin t, short * w, int e, int plain) {
  int i;
  Fi(plain ? 4 : CM_NI,
    i32 v = w[i] + ((((i32) t.v[i] * e * 2 >> 16) + 1) >> 1);
    if (v < -32768) v = -32768;
    if (v > 32767) v = 32767;
    w[i] = (short) v);
}
#endif

#define CM_DEC(c, dec) ((dec) < 0 ? (c)->d != NULL : (dec))
static INLINE int mix_bit0(cm * restrict c, int st, int sel, u32 h,
                           u32 * restrict p, int bit, int plain, int dec) {
  cm_stage * s = c->st + st;
  u8 * restrict sp = s->hist + (h & c->hmask);
  int state = *sp;
  short * restrict w = s->w + sel * CM_NI;
  u32 pst = *p, prob = rc_packed_prob(pst);
  uint64_t * restrict smp = s->sm + state;
  uint64_t sm = *smp;
  u32 x = (u32) sm, ps = x >> 16, bd = (u32) (sm >> 32), pr, nv;
  mixin in = mix_in6(cm_str16[65536u - prob], bd, 0, 0);
  { int dot = (int) (mix_dot(in, w, plain) >> 7);
    if (dot < -2047) dot = -2047;
    if (dot > 2047) dot = 2047;
    pr = cm_squash16[dot + 2048];    }
  if (CM_DEC(c, dec)) bit = rc_dec_bit_ready(c->d, 65536u - pr);
  PROF(prof_hook(NULL, 65536u - pr, bit));
  *sp = cm_nex[state][bit];
  nv = rc_adapt_prob(ps, rc_divt[x & 0xFFFF], !bit);
  *smp = cm_sm(nv, (x & 0xFFFF) + ((int) (x & 0xFFFF) < RC_ALIM), state);
  mix_train(in, w, ((bit << 12) - (int) (pr >> 4)) * c->lr, plain);
  *p = rc_adapt_packed(pst, RC_ALIM, bit);
  if (!CM_DEC(c, dec)) rc_enc_bit_raw(c->e, 65536u - pr, bit);
  return bit;
}

static INLINE int mix_bit(cm * restrict c, int st, int sel, u32 h,
                          u32 * restrict p, int bit, int plain, int dec,
                          int lean, int h2, int apm) {
  if (CM_DEC(c, dec)) rc_dec_norm(c->d);
  if (lean && plain && LIKELY(!c->xt))
    return mix_bit0(c, st, sel, h, p, bit, plain, dec);
  cm_stage * s = c->st + st;
  u8 * restrict sp = s->hist + (h & c->hmask);
  int state = *sp;
  short * restrict w = s->w + sel * CM_NI;
  u32 pst = *p, prob = rc_packed_prob(pst);
  uint64_t * restrict smp = s->sm + state;
  uint64_t sm = *smp;
  u32 x = (u32) sm, ps = x >> 16, bd = (u32) (sm >> 32), pr, nv;
  mixin in;
  u16 * a0 = NULL, * a1 = NULL;
  u32 pf, ak = 0, aw = 0;
  short * restrict w2 = NULL;
  u8 * restrict sp2 = NULL;
  uint64_t * restrict smp2 = NULL;
  uint64_t sm2 = 0;
  int state2 = 0;
  in = mix_in6(cm_str16[65536u - prob], bd, 0, 0);
  if (!plain && c->dq)
    in = mix_put6(in, cm_str16[65536u - rc_packed_prob(*c->dq)]);
  if (!plain && (h2 > 0 || (h2 && c->h2on))) {
    sp2 = s->hist2 + (cm_hst(c->h2b, c->h2x, (u32) st) & c->hmask);
    state2 = *sp2;  smp2 = s->sm2 + state2;  sm2 = *smp2;
    in = mix_put7(in, (short) (sm2 >> 32));
  }
  if (c->w2) w2 = c->w2 + ((sz) st * c->nsel2 + c->sel2) * CM_NI;
  { int dot = (int) (mix_dot(in, w, plain) >> 7);
    if (w2) dot = (dot + (int) (mix_dot(in, w2, plain) >> 7)) >> 1;
    if (dot < -2047) dot = -2047;
    if (dot > 2047) dot = 2047;
    pr = pf = cm_squash16[dot + 2048];
    if (apm > 0 || (apm && c->apm)) {
      u32 u = (u32) (dot + 2048);
      ak = u >> 7;  aw = u & 127;
      a0 = c->apm + ((u32) st * c->apmn + c->actx[0]) * CM_APMN + ak;
      a1 = c->apm + ((sz) c->nst * c->apmn + c->actx[1] * (u32) c->nst +
                     (u32) st) * CM_APMN + ak;
      pf = (cm_apm_at(a0, ak, aw) + cm_apm_at(a1, ak, aw) + 1) >> 1;
      if (pf < 16) pf = 16;
      if (pf > 65504) pf = 65504;
    } }
  if (CM_DEC(c, dec)) bit = rc_dec_bit_ready(c->d, 65536u - pf);
  PROF(prof_hook(NULL, 65536u - pf, bit));
  if (apm > 0 || (apm && a0)) {
    int r = c->apms && c->apmu >= CM_APMS ? CM_APMR + 1 : CM_APMR;
    c->apmu++;
    cm_apm_up(a0, ak, aw, bit, r);  cm_apm_up(a1, ak, aw, bit, r);
  }
  *sp = cm_nex[state][bit];
  nv = rc_adapt_prob(ps, rc_divt[x & 0xFFFF], !bit);
  *smp = cm_sm(nv, (x & 0xFFFF) + ((int) (x & 0xFFFF) < RC_ALIM), state);
  if (sp2) {
    u32 x2 = (u32) sm2;
    *sp2 = cm_nex[state2][bit];
    *smp2 = cm_sm(rc_adapt_prob(x2 >> 16, rc_divt[x2 & 0xFFFF], !bit),
                  (x2 & 0xFFFF) + ((int) (x2 & 0xFFFF) < RC_ALIM), state2);
  }
  if (c->lr0 != c->lr && !(++c->nmix & 1023)) {
    c->lrt = c->lr + (int) ((uint64_t) (c->lr0 - c->lr) * CM_LRH /
                            (CM_LRH + c->nmix));
    if (c->lrt == c->lr) c->lr0 = c->lr;
  }
  mix_train(in, w, ((bit << 12) - (int) (pr >> 4)) * c->lrt, plain);
  if (w2) mix_train(in, w2, ((bit << 12) - (int) (pr >> 4)) * c->lrt, plain);
  *p = rc_adapt_packed(pst, RC_ALIM, bit);
  if (!plain && c->dq) *c->dq = rc_adapt_packed(*c->dq, c->dlim, bit);
  if (!CM_DEC(c, dec)) rc_enc_bit_raw(c->e, 65536u - pf, bit);
  return bit;
}

#endif
