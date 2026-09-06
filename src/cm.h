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

#ifndef BLR_CM_H
#define BLR_CM_H

#include "rc.h"

/*  Mix a hashed bit-history model with a caller-owned probability.  */

typedef struct {
  u8 * hist;                  /*  one bit-history state per hashed context  */
  u32 * sm;                   /*  state map: (P(1) << 16) | count  */
  short * w, * raw;           /*  nsel rows of eight weights, 16-byte aligned  */
} cm_stage;

/*  Predict digits from the most recent matching ring-buffer context.  */
#define CM_MHBITS  18               /*  ring size, in digits  */
#define CM_MTBITS  16               /*  hash table size  */
#define CM_MMIN    10               /*  minimum match, avoiding noise hits  */
#define CM_MMAX    63               /*  match length cap  */
#define CM_MLB     16               /*  match length buckets per stage  */

typedef struct {
  cm_stage * st;
  int nst, nsel, lr, lim;
  u32 hmask;                  /*  the history index mask  */
  rc_enc * e;                 /*  the coder bound for the current packet ...  */
  rc_dec * d;                 /*  ... exactly one of the two  */
  u8 * mbuf;  u32 mpos;       /*  digit history and the next slot in it  */
  u32 mhash, mout;            /*  rolling hash of the last CM_MMIN digits,
                                  and the weight a digit leaves it with  */
  u32 * mtab;                 /*  context hash -> the position that followed  */
  u32 mptr, mlen;             /*  the match: where its next digit is, and
                                  how many digits it has held for  */
  u16 * mp;  u8 * mpc;        /*  P(the predicted bit is right), by stage
                                  and length bucket, with counts  */
  int live;
} cm;

/*  Build the mixer.  */
void cm_new(cm * c, int nst, int bits, int nsel, int lr, int lim);
void cm_free(cm * c);

static INLINE void cm_bind(cm * c, rc_enc * e, rc_dec * d) { c->e = e;  c->d = d; }

/*  Code one bit. `exp` is the match prediction or -1.  */
typedef int (*cm_bit_fn)(cm * restrict c, int st, int sel, u32 h,
                         u16 * restrict p, u8 * restrict cnt, int exp, int bit);
extern cm_bit_fn cm_bit;
HOT int cm_bit_scalar(cm * restrict c, int st, int sel, u32 h, u16 * restrict p,
                      u8 * restrict cnt, int exp, int bit);
#if defined(HAVE_SSE2)
HOT int cm_bit_sse2(cm * restrict c, int st, int sel, u32 h, u16 * restrict p,
                    u8 * restrict cnt, int exp, int bit);
#endif

/*  Tables shared by both kernels.  */
#define CM_NI 8
extern short cm_str16[65536];
extern u8 cm_nex[256][4];
extern i8 cm_nexd[256];
extern const int cm_sqt[33];
extern u16 cm_squash16[4096];

/*  Exact linear interpolation rearranged to use one multiplication.
      SQT[i]*16*(128 - w) + SQT[i+1]*16*w + 64
    = SQT[i]*2048 + 64 + (SQT[i+1] - SQT[i])*16*w  */
static INLINE int cm_squash(int d) {
  u32 u, w, i;
  if (d > 2047) d = 2047;
  if (d < -2047) d = -2047;
  u = (u32) (d + 2048);
  i = u >> 7;  w = u & 127;
  return (int) (((u32) (cm_sqt[i] * 2048 + 64)
                 + (u32) ((cm_sqt[i + 1] - cm_sqt[i]) * 16) * w) >> 7);
}

/*  Return the next matched digit when available.  */
static INLINE int cm_match(const cm * c, i32 * pred) {
  if (!c->mlen) return 0;
  *pred = (i32) c->mbuf[c->mptr & (((u32) 1 << CM_MHBITS) - 1)] - 128;
  return 1;
}

/*  Record a digit and update the match.  */
void cm_match_push(cm * c, i32 val);

/*  Fold shared inputs before adding the stage number.  */
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
