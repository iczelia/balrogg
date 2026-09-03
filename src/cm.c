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


#include "cm.h"
#include "prof.h"

/*  Stretch table and bit-history state transitions.  */

static short STR16[65536];
static u8 NEX[256][4];
static i8 NEXD[256];

static const int SQT[33] = {
  1, 2, 3, 6, 10, 16, 27, 45, 73, 120, 194, 310, 488, 747, 1101,
  1546, 2047, 2549, 2994, 3348, 3607, 3785, 3901, 3975, 4022,
  4050, 4068, 4079, 4085, 4089, 4092, 4093, 4094
};

/*  Exact linear interpolation rearranged to use one multiplication.
      SQT[i]*16*(128 - w) + SQT[i+1]*16*w + 64
    = SQT[i]*2048 + 64 + (SQT[i+1] - SQT[i])*16*w  */
static INLINE int cm_squash(int d) {
  u32 u, w, i;
  if (d > 2047) d = 2047;
  if (d < -2047) d = -2047;
  u = (u32) (d + 2048);
  i = u >> 7;  w = u & 127;
  return (int) (((u32) (SQT[i] * 2048 + 64)
                 + (u32) ((SQT[i + 1] - SQT[i]) * 16) * w) >> 7);
}

static void cm_init(void) {
  int x, i, j, pi = 0, st, y, n;
  static int done = 0;
  if (done) return;
  done = 1;

  for (x = -2047; x <= 2047; x++) {
    i = cm_squash(x);
    for (j = pi; j <= i && j < 65536; j++) STR16[j] = (short) x;
    pi = i + 1;
  }
  for (j = pi; j < 65536; j++) STR16[j] = 2047;

  {
    static u8 ilogt[65536];
    static u8 t[64][64][2];
    u32 xx = 14155776;
    int b[5];
    b[0] = 42;  b[1] = 41;  b[2] = 13;  b[3] = 6;  b[4] = 5;
    ilogt[0] = ilogt[1] = 0;
    for (i = 2; i < 65536; i++) {
      xx += 774541002u / (u32) (i * 2 - 1);
      ilogt[i] = (u8) (xx >> 24);
    }
    memset(t, 0, sizeof t);
    st = 0;
    for (i = 0; i < 256; i++) for (y = 0; y <= i; y++) {
      int xv = i - y, a, r;
      /*  num_states(x, y)  */
      int lo = xv < y ? xv : y, hi = xv < y ? y : xv;
      n = 0;
      if (!(hi < 0 || lo < 0 || hi >= 64 || lo >= 64 || lo >= 5 || hi >= b[lo])) {
        if (hi + lo <= 4) {
          r = 1;
          for (a = hi + 1; a <= hi + lo; a++) r *= a;
          for (a = 2; a <= lo; a++) r /= a;
          n = r;
        } else n = 1 + (lo > 0 && hi + lo < 16);
      }
      if (n && xv < 64 && y < 64) {
        t[xv][y][0] = (u8) st;  t[xv][y][1] = (u8) n;  st += n;
      }
    }
    st = 0;
    for (i = 0; i < 64; i++) for (y = 0; y <= i; y++) {
      int xv = i - y, k;
      for (k = 0; k < t[xv][y][1]; k++) {
        int x0 = xv, y0 = y, x1 = xv, y1 = y;
        if (st < 15) {
          x0++;  y1++;
          NEX[st][0] = (u8) (t[x0][y0][0] + st - t[xv][y][0]);
          NEX[st][1] = (u8) (t[x1][y1][0] + st - t[xv][y][0]
                             + (xv > 0 ? t[xv - 1][y + 1][1] : 0));
        } else {
          /*  next_state(x0,y0,0) and next_state(x1,y1,1), inlined  */
          int p, q, bb, sw;
          for (bb = 0; bb < 2; bb++) {
            int * px = bb ? &x1 : &x0, * py = bb ? &y1 : &y0;
            p = *px;  q = *py;  sw = 0;
            if (p < q) { int tmp = p;  p = q;  q = tmp;  sw = 1; }
            if (bb ^ sw) { q++;  if (p > 2) p = ilogt[p] / 6 - 1; }
            else         { p++;  if (q > 2) q = ilogt[q] / 6 - 1; }
            while (!t[p][q][1]) {
              if (q < 2) p--;
              else { p = (p * (q - 1) + (q / 2)) / q;  q--; }
            }
            if (sw) { int tmp = p;  p = q;  q = tmp; }
            *px = p;  *py = q;
          }
          NEX[st][0] = t[x0][y0][0];
          NEX[st][1] = (u8) (t[x1][y1][0] + (t[x1][y1][1] > 1));
        }
        NEX[st][2] = (u8) xv;  NEX[st][3] = (u8) y;
        st++;
      }
    }
    if (st != 253)
      FATAL_CODE(BLR_EXIT_INTERNAL, "internal: bit history came out at %d states, not 253", st);
    for (i = 0; i < 256; i++)
      NEXD[i] = (i8) ((NEX[i][3] == 0) - (NEX[i][2] == 0));
  }
}

/*  Mixer arithmetic.  */
#define NI 8

#if defined(__SSE2__) && !defined(BLR_NO_SIMD)
#include <emmintrin.h>

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

typedef struct { short v[NI]; } mixin;

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
  for (n = 0; n < NI; n += 2)
    s += ((i32) t.v[n] * w[n] + (i32) t.v[n + 1] * w[n + 1]) >> 8;
  return s;
}

static INLINE void mix_train(mixin t, short * w, int e) {
  int n;
  if (!e) return;
  for (n = 0; n < NI; n++) {
    i32 v = w[n] + (((((i32) t.v[n] * e * 2) >> 16) + 1) >> 1);
    if (v < -32768) v = -32768;
    if (v > 32767) v = 32767;
    w[n] = (short) v;
  }
}
#endif

/*  State.  */
#define MMUL  773u

/*  MMUL to the CM_MMIN, the weight of the digit leaving the hash window.  */
static u32 mmul_out(void) {
  u32 m = 1;
  int i;
  for (i = 0; i < CM_MMIN; i++) m *= MMUL;
  return m;
}

void cm_new(cm * c, int nst, int bits, int nsel, int lr, int lim) {
  int i, k;
  FATAL_UNLESS(!c->live, "internal: the context mixer is already built");
  cm_init();
  c->nst = nst;  c->nsel = nsel;  c->lr = lr;  c->lim = lim;
  c->hmask = ((u32) 1 << bits) - 1;
  c->e = NULL;  c->d = NULL;
  c->st = xmalloc((sz) nst * sizeof *c->st);
  for (i = 0; i < nst; i++) {
    cm_stage * s = c->st + i;
    s->hist = xcalloc((sz) 1 << bits, 1);
    /*  The state map starts at the count-based estimate of each state.  */
    s->sm = xmalloc(256 * sizeof *s->sm);
    for (k = 0; k < 256; k++) {
      int n0 = NEX[k][2], n1 = NEX[k][3];
      u32 p;
      if (n0 == 0) n1 *= 64;
      if (n1 == 0) n0 *= 64;
      p = (u32) (65535 * (n1 + 1) / (n0 + n1 + 2));
      if (p < 1) p = 1;
      if (p > 65534) p = 65534;
      s->sm[k] = p << 16;
    }
    /*  Weights align for SSE2 loads; the first input starts at unity.  */
    s->raw = xcalloc((sz) nsel * NI + NI, sizeof(short));
    s->w = s->raw;
    while (((sz) s->w & 15) != 0) s->w++;
    for (k = 0; k < nsel; k++) s->w[k * NI] = 32767;
  }
  c->mbuf = xcalloc((sz) 1 << CM_MHBITS, 1);
  c->mtab = xcalloc((sz) 1 << CM_MTBITS, sizeof *c->mtab);
  c->mpos = 0;  c->mptr = 0;  c->mlen = 0;  c->mhash = 0;  c->mout = mmul_out();
  c->mp = xmalloc((sz) nst * CM_MLB * sizeof *c->mp);
  c->mpc = xcalloc((sz) nst * CM_MLB, 1);
  rc_probs_init(c->mp, (sz) nst * CM_MLB);
  c->live = 1;
}

void cm_free(cm * c) {
  int i;
  if (!c->live) return;
  for (i = 0; i < c->nst; i++) {
    free(c->st[i].hist);  free(c->st[i].sm);  free(c->st[i].raw);
  }
  free(c->st);  c->st = NULL;
  free(c->mbuf);  free(c->mtab);  free(c->mp);  free(c->mpc);
  c->mbuf = NULL;  c->mtab = NULL;  c->mp = NULL;  c->mpc = NULL;
  c->live = 0;
}

/*  The match model.  */

#define MMASK (((u32) 1 << CM_MHBITS) - 1)

void cm_match_push(cm * c, i32 val) {
  u32 h, p, chk;
  u8 d = (u8) ((val < -127 ? -127 : val > 127 ? 127 : val) + 128);
  /*  Update the rolling hash. Unwritten ring entries are zero.  */
  c->mhash = c->mhash * MMUL + d - c->mout * c->mbuf[(c->mpos - CM_MMIN) & MMASK];
  c->mbuf[c->mpos & MMASK] = d;
  c->mpos++;
  if (c->mlen && c->mbuf[c->mptr & MMASK] == d) {
    c->mptr++;
    if (c->mlen < CM_MMAX) c->mlen++;
  } else c->mlen = 0;
  if (c->mpos < CM_MMIN) return;
  h = c->mhash * 0x9E3779B1u;
  /*  Store a hash check above the ring position.  */
  chk = h << CM_MTBITS >> CM_MHBITS << CM_MHBITS;
  h >>= 32 - CM_MTBITS;
  if (!c->mlen) {
    p = c->mtab[h];
    if ((p & ~MMASK) == chk && (p & MMASK) != 0) {
      /*  Resolve the newest position, then verify it against the ring.  */
      p = c->mpos - ((c->mpos - p) & MMASK);
      u32 n = 0;
      while (n < CM_MMAX && n < p &&
             c->mbuf[(p - 1 - n) & MMASK] == c->mbuf[(c->mpos - 1 - n) & MMASK])
        n++;
      if (n >= CM_MMIN) { c->mptr = p;  c->mlen = n; }
    }
  }
  c->mtab[h] = (c->mpos & MMASK) | chk;
}

/*  The per-bit path.  */

int cm_bit(cm * c, int st, int sel, u32 h, u16 * p, u8 * cnt, int exp,
           int bit) {
  cm_stage * s = c->st + st;
  u8 * sp = s->hist + (h & c->hmask);
  int state = *sp;
  short * w = s->w + sel * NI;
  u32 x = s->sm[state], ps = x >> 16, pr, d;
  i32 nv;
  int mi = 0;
  mixin in;
  /*  The state map stays in 1..0xFFFE and cm_squash returns 16..65504, so
      neither probability needs clamping.  The caller's `p` is P(0).  */
  if (exp < 0)
    in = mix_in6(STR16[65536u - *p], STR16[ps], 256, STR16[ps] * NEXD[state],
                 0, 0);
  else {
    /*  Add learned match accuracy and capped match length, both signed by
        the expected bit.  */
    u32 lb = c->mlen < CM_MLB ? c->mlen : CM_MLB - 1;
    int sg = exp ? 1 : -1;
    mi = st * CM_MLB + (int) lb;
    in = mix_in6(STR16[65536u - *p], STR16[ps], 256, STR16[ps] * NEXD[state],
                 sg * STR16[c->mp[mi]],
                 sg * (int) (c->mlen < 32 ? c->mlen : 32) * 64);
  }
  pr = (u32) cm_squash((int) (mix_dot(in, w) >> 7));      /*  P(1)  */
  if (c->d) bit = rc_dec_bit_raw(c->d, 65536u - pr);
  else rc_enc_bit_raw(c->e, 65536u - pr, bit);
  /*  Account for mixed bits because rc_*_bit_raw does not report them.  */
  PROF(prof_hook(NULL, 65536u - pr, bit));
  *sp = NEX[state][bit];
  d = rc_divt[x & 0xFFFF];
  nv = bit ? (i32) ps + (i32) (((0xFFFF - ps) * d) >> 16)
           : (i32) ps - (i32) ((ps * d) >> 16);
  if (nv < 1) nv = 1;
  if (nv > 0xFFFE) nv = 0xFFFE;
  s->sm[state] = ((u32) nv << 16) | ((x & 0xFFFF) + ((int) (x & 0xFFFF) < c->lim));
  mix_train(in, w, ((bit << 12) - (int) (pr >> 4)) * c->lr);
  *p = rc_adapt(*p, cnt, c->lim, bit);
  if (exp >= 0) c->mp[mi] = rc_adapt(c->mp[mi], c->mpc + mi, c->lim, bit == exp);
  return bit;
}
