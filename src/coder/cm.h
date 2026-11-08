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

#ifndef BLR_CM_H
#define BLR_CM_H

#include "coder/rc.h"
#include "coder/squash.h"
#include "sys/map.h"

typedef struct {
  u8 * hist;
  uint64_t * sm;
  short * w, * raw;
  u8 * hist2;
  uint64_t * sm2;
} cm_stage;

typedef struct {
  cm_stage * st;
  int nst, nsel, lr;
  u32 hmask;
  rc_enc * e;
  rc_dec * d;
  blr_map hmap;
  int live;
  int lrt, lr0;
  u32 nmix;
  u32 * dq;
  int dlim;
  u16 * apm;
  blr_map apmmap;
  u32 apmn;
  u32 actx[2];
  u32 apmu;
  int apms;
  short * w2, * w2raw;
  u32 sel2;
  u32 nsel2;
  int xt;
  blr_map hmap2;
  int h2on;
  u32 h2b, h2x;
} cm;

void cm_new(cm * c, int nst, int bits, int nsel, int lr);
void cm_free(cm * c);

#define CM_LRH     65536
void cm_prior(cm * c, const short (*w)[4], int lr0);

void cm_second(cm * c, u32 nsel2);

#define CM_APMN    33
#define CM_APMR    5
#define CM_APMS    65536
void cm_apm(cm * c, u32 n0, u32 n1, int staged);

void cm_hist2(cm * c);

extern u16 cm_apmid[CM_APMN];

static INLINE u32 cm_apm_at(const u16 * a, u32 k, u32 w) {
  u32 a0 = a[0] ^ cm_apmid[k], a1 = a[1] ^ cm_apmid[k + 1];
  return (a0 * (128 - w) + a1 * w) >> 7;
}

static INLINE void cm_apm_up(u16 * a, u32 k, u32 w, int bit, int r) {
  int t = bit ? 65535 : 0;
  int a0 = a[0] ^ cm_apmid[k], a1 = a[1] ^ cm_apmid[k + 1];
  a0 += (t - a0) * (int) (128 - w) >> (r + 7);
  a1 += (t - a1) * (int) w >> (r + 7);
  a[0] = (u16) (a0 ^ cm_apmid[k]);  a[1] = (u16) (a1 ^ cm_apmid[k + 1]);
}

static INLINE void cm_bind(cm * c, rc_enc * e, rc_dec * d) {
  c->e = e;  c->d = d;
}

typedef int (*cm_bit_fn)(cm * restrict c, int st, int sel, u32 h,
                         u32 * restrict p, int bit);
extern cm_bit_fn cm_bit;
typedef int (*cm_plain_fn)(cm * restrict c, int st, int sel, u32 h,
                           u32 * restrict p, int bit);
extern cm_plain_fn cm_plain;
HOT int cm_bit_scalar(cm * restrict c, int st, int sel, u32 h, u32 * restrict p,
                      int bit);
HOT int cm_plain_scalar(cm * restrict c, int st, int sel, u32 h,
                        u32 * restrict p, int bit);
#if defined(HAVE_SSE2)
HOT int cm_bit_sse2(cm * restrict c, int st, int sel, u32 h, u32 * restrict p,
                    int bit);
HOT int cm_plain_sse2(cm * restrict c, int st, int sel, u32 h,
                      u32 * restrict p, int bit);
#endif
#if defined(HAVE_AVX2)
HOT int cm_bit_avx2(cm * restrict c, int st, int sel, u32 h, u32 * restrict p,
                    int bit);
HOT int cm_plain_avx2(cm * restrict c, int st, int sel, u32 h,
                      u32 * restrict p, int bit);
#endif

#if defined(HAVE_SSE2) && defined(__SSE2__)
#define CM_INLINE 1
#else
#define CM_INLINE 0
#endif
extern int cm_inl;

const char * cm_dispatched(void);

void cm_prepare(void);

#define CM_NI 8
extern short cm_str16[65536];
extern u8 cm_nex[256][4];
extern i8 cm_nexd[256];
extern u16 cm_squash16[4096];

static INLINE uint64_t cm_sm(u32 p, u32 count, int state) {
  int b = cm_str16[p];
  u32 bd = (u16) b | (u32) (u16) (b * cm_nexd[state]) << 16;
  return (uint64_t) bd << 32 | p << 16 | count;
}

static INLINE int cm_squash(int d) {
  u32 u, w, i;
  if (d > 2047) d = 2047;
  if (d < -2047) d = -2047;
  u = (u32) (d + 2048);
  i = u >> 7;  w = u & 127;
  return (int) (((u32) (cm_sqt[i] * 2048 + 64)
                 + (u32) ((cm_sqt[i + 1] - cm_sqt[i]) * 16) * w) >> 7);
}

static INLINE u32 cm_hpre(u32 a, u32 b, u32 c, u32 d) {
  return a * 200002979u + b * 30005491u + c * 50004239u + d * 70004807u;
}

static INLINE u32 cm_hpx(u32 a, u32 b, u32 c, u32 d) {
  return a >> 2 ^ b >> 3 ^ c >> 4 ^ d >> 5;
}

static INLINE u32 cm_hst(u32 base, u32 xr, u32 e) {
  u32 h = base + e * 110002499u;
  return h ^ h >> 9 ^ xr ^ e >> 6;
}

#endif
