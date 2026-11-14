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


#include "mod/mi.h"
#include "coder/squash.h"
#include "sys/cpu.h"
#include <string.h>

#if defined(BLR_CM_AVX2)
#include <immintrin.h>
#define SF_ONE smp_fast_one_avx2
#else
#define SF_ONE fast_one
#endif

#if defined(HAVE_SSE2) && defined(__SSE2__)
#include <emmintrin.h>
#define SF_SSE2 1
#else
#define SF_SSE2 0
#endif

#define NQ 24
#define NNMAX 256
#define NNWIN 512
#define NNLONG 64
#define HTB 16
#define CT 32768
#define CMIN 4
#define CTOT (CT - 9 * CMIN)

typedef struct {
  i16 w[NNMAX], h[NNWIN + NNMAX], ad[NNWIN + NNMAX];
  int q, avg;
} nnr;

typedef struct {
  int af, as, s1, ee[3];
  u32 gp, mp, ml, cap, * ht;
  u8 * hb;
  nnr g[3];
  u16 cdf[NQ][NQ][16];
  u8 cnt[NQ][NQ];
  u32 bk[NQ][NQ][16], bk2[NQ][NQ][16], mt[NQ][9][2], mf[6][9][256];
  u32 sg[24][3][9], mh[16][4];
  i32 mw[NQ][16][3];
  u8 qt[4096];
  u32 opt;
} sfz;

#if !defined(BLR_CM_AVX2)
void * smp_fast_new(mx * m) {
  sfz * z;
  int i, j;
  rc_adapt_init();
  acm_prepare();
  z = xcalloc(1, sizeof (sfz));
  z->opt = m->sc->opt;
  Fi(3, z->g[i].q = NNWIN);
  Fi(NQ, Fj(16, z->mw[i][j][0] = z->mw[i][j][1] = 32768));
  Fi(4096,
    u32 b = blr_ilog((u32) i), q = b < 2 ? b : 2 * b - 2 + (i >> (b - 2) & 1);
    z->qt[i] = (u8) MIN(q, NQ - 1));
  return z;
}

void smp_fast_free(void * st) {
  sfz * z = st;
  xfree(z->hb);  xfree(z->ht);
  xfree(st);
}

#endif

static INLINE int clip8(int v) { return CLAMP(v, -128, 127); }

enum {
  F_SEL = 1, F_MT = 2, F_SG = 4, F_FR = 8, F_SD = 16, F_MM = 32, F_MS = 64,
  F_MG = 128, F_MR = 256, F_CD = 512, F_MX = 1024, F_S4 = 2048
};

#if SF_SSE2
#define LD4(p) _mm_loadu_si128((const void *) (p))

static INLINE u32 hsum4(__m128i t) {
  t = _mm_add_epi32(t, _mm_shuffle_epi32(t, 0x4E));
  t = _mm_add_epi32(t, _mm_shuffle_epi32(t, 0xB1));
  return (u32) _mm_cvtsi128_si32(t);
}
#endif

#if defined(BLR_CM_AVX2)
static INLINE u32 hsum8(__m256i t) {
  return hsum4(_mm_add_epi32(_mm256_castsi256_si128(t),
                             _mm256_extracti128_si256(t, 1)));
}

#define LD8(p) _mm256_loadu_si256((const void *) (p))
#endif

static INLINE int vdot(const i16 * w, const i16 * h, const int n) {
  int j;
#if SF_SSE2
  __m128i t = _mm_setzero_si128();
  for (j = 0; j < n; j += 8)
    t = _mm_add_epi32(t, _mm_madd_epi16(LD4(w + j), LD4(h + j)));
  return (int) hsum4(t);
#else
  u32 t = 0;
  Fj(n, t += (u32) (w[j] * h[j]));
  return (int) t;
#endif
}

static INLINE int vlong(i16 * w, const i16 * d, const i16 * h, const int n,
                        int sg) {
  int j = 0;
  u32 t = 0;
#if defined(BLR_CM_AVX2)
  {
    __m256i k = _mm256_set1_epi16((short) sg), a = _mm256_setzero_si256();
    for (; j + 16 <= n; j += 16) {
      __m256i x = _mm256_add_epi16(LD8(w + j),
                                   _mm256_mullo_epi16(LD8(d + j), k));
      _mm256_storeu_si256((void *) (w + j), x);
      a = _mm256_add_epi32(a, _mm256_madd_epi16(x, LD8(h + j)));
    }
    t = hsum8(a);
  }
#endif
#if SF_SSE2
  {
    __m128i k = _mm_set1_epi16((short) sg), a = _mm_setzero_si128();
    for (; j < n; j += 8) {
      __m128i x = _mm_add_epi16(LD4(w + j), _mm_mullo_epi16(LD4(d + j), k));
      _mm_storeu_si128((void *) (w + j), x);
      a = _mm_add_epi32(a, _mm_madd_epi16(x, LD4(h + j)));
    }
    t += hsum4(a);
  }
#else
  Fj(n,
    w[j] = (i16) (w[j] + d[j] * sg);
    t += (u32) (w[j] * h[j]));
#endif
  return (int) t;
}

static INLINE int vshort(i16 * w, const i16 * d, const i16 * h, const int n,
                         int sg) {
  int j = 0;
  u32 t = 0, e = 0;
#if defined(BLR_CM_AVX2)
  {
    __m256i k = _mm256_set1_epi16((short) sg), a = _mm256_setzero_si256(),
            b = a;
    for (; j + 16 <= n; j += 16) {
      __m256i x = LD8(w + j), y = LD8(d + j), z = LD8(h + j);
      a = _mm256_add_epi32(a, _mm256_madd_epi16(x, z));
      b = _mm256_add_epi32(b, _mm256_madd_epi16(y, z));
      _mm256_storeu_si256((void *) (w + j),
                          _mm256_add_epi16(x, _mm256_mullo_epi16(y, k)));
    }
    t = hsum8(a);  e = hsum8(b);
  }
#endif
#if SF_SSE2
  {
    __m128i k = _mm_set1_epi16((short) sg), a = _mm_setzero_si128(), b = a;
    for (; j < n; j += 8) {
      __m128i x = LD4(w + j), y = LD4(d + j), z = LD4(h + j);
      a = _mm_add_epi32(a, _mm_madd_epi16(x, z));
      b = _mm_add_epi32(b, _mm_madd_epi16(y, z));
      _mm_storeu_si128((void *) (w + j),
                       _mm_add_epi16(x, _mm_mullo_epi16(y, k)));
    }
    t += hsum4(a);  e += hsum4(b);
  }
#else
  Fj(n,
    t += (u32) (w[j] * h[j]);
    e += (u32) (d[j] * h[j]);
    w[j] = (i16) (w[j] + d[j] * sg));
#endif
  return (int) (t + (u32) sg * e);
}

static INLINE void vpush(i16 * h, int q, int v) {
#if defined(BLR_CM_AVX2)
  __m256i x = LD8(h + q + 1);
  x = _mm256_alignr_epi8(x, _mm256_permute2x128_si256(x, x, 0x08), 14);
  _mm256_storeu_si256((void *) (h + q), _mm256_insert_epi16(x, (short) v, 0));
#elif SF_SSE2
  __m128i x = _mm_slli_si128(_mm_loadu_si128((const void *) (h + q + 1)), 2);
  _mm_storeu_si128((void *) (h + q), _mm_insert_epi16(x, v, 0));
#else
  h[q] = (i16) v;
#endif
}

static INLINE int nn_step(nnr * f, int * e, int pk, const int n,
                          const int mu) {
  int u = *e, pe = mu < 0 ? pk >> 2 : pk, am = ABS(mu),
      r = CLAMP(u - ((int) ((u32) pe + 2048) >> 12), -2048, 2047),
      q = f->q, ai = ABS(u), c, sg = (r > 0) - (r < 0), t;
  c = ai * 16 > 3 * f->avg ? 4 * am : ai * 48 > 4 * f->avg ? 2 * am :
      ai ? am : 0;
  f->avg += ai - (f->avg >> 4);
  if (!q) {
    memcpy(f->h + NNWIN, f->h, (sz) n * sizeof (i16));
    memcpy(f->ad + NNWIN, f->ad, (sz) n * sizeof (i16));
    q = NNWIN;
  }
  f->q = --q;
  vpush(f->h, q, u);
  *e = r;
  t = n >= NNLONG ? vlong(f->w, f->ad + q + 1, f->h + q, n, sg)
                  : vshort(f->w, f->ad + q + 1, f->h + q, n, sg);
  vpush(f->ad, q, u > 0 ? c : -c);
  return t;
}

typedef struct { u32 lo, rg, cd;  int cy; } lrc;

static INLINE void lnorm(mx * m, lrc * c, const int dec) {
  if (dec)
    while (c->rg < RC_TOP) { c->cd = c->cd << 8 | rc_get(&m->r);  c->rg <<= 8; }
  else
    while (c->rg < RC_TOP) {
      m->e.low = c->lo;  m->e.carry = c->cy;
      rc_shift(&m->e);
      c->lo = m->e.low;  c->cy = m->e.carry;
      c->rg <<= 8;
    }
}

static INLINE void laddlo(lrc * c, u32 v) {
  u32 lo = c->lo + v;
  c->cy |= lo < c->lo;
  c->lo = lo;
}

static INLINE int lbit(mx * m, lrc * c, const int dec, u32 * s, int bit) {
  u32 st = *s, p = rc_packed_prob(st), sp;
  if (dec) {
    lnorm(m, c, dec);
    sp = (c->rg >> 16) * p;
    bit = c->cd >= sp;
    c->cd -= bit ? sp : 0;
    c->rg = bit ? c->rg - sp : sp;
  } else {
    sp = (c->rg >> 16) * p;
    laddlo(c, bit ? sp : 0);
    c->rg = bit ? c->rg - sp : sp;
    lnorm(m, c, dec);
  }
  *s = rc_adapt_packed(st, 127, bit);
  return bit;
}

static INLINE u32 lraw(mx * m, lrc * c, const int dec, u32 v, int k) {
  u32 r, q;
  if (dec) {
    lnorm(m, c, dec);
    r = c->rg >> k;  q = c->cd / r;
    if (q >> k) q = (1u << k) - 1;
    c->cd -= q * r;  c->rg = r;
    return q;
  }
  r = c->rg >> k;
  laddlo(c, r * v);
  c->rg = r;
  lnorm(m, c, dec);
  return v;
}

static INLINE u32 ltree(mx * m, lrc * c, const int dec, u32 * t, u32 v,
                        int nb) {
  u32 node = 1;
  int j;
  for (j = nb - 1; j >= 0; j--)
    node = node * 2 + (u32) lbit(m, c, dec, &t[node], (int) (v >> j & 1));
  return node - (1u << nb);
}

static INLINE int lmix(mx * m, lrc * c, const int dec, u32 * s1, u32 * s2,
                       i32 * w, int bit) {
  u32 q1 = rc_packed_prob(*s1), q2 = rc_packed_prob(*s2), t;
  int x1 = acm_str[(65535 - q1) >> 4], x2 = acm_str[(65535 - q2) >> 4], pm,
      er;
  i32 d = (i32) (((int64_t) w[0] * x1 + (int64_t) w[1] * x2 +
                  (int64_t) w[2] * 256) >> 16);
  pm = acm_squash(CLAMP(d, -2047, 2047));
  t = ((u32) CLAMP(65536 - pm * 16, 64, 65472) ^ RC_PINIT) | 2u << 16;
  bit = lbit(m, c, dec, &t, bit);
  *s1 = rc_adapt_packed(*s1, 127, bit);
  *s2 = rc_adapt_packed(*s2, 127, bit);
  er = ((bit ? 4095 : 0) - pm) * 6;
  w[0] = CLAMP(w[0] + ((x1 * er) >> 14), -(1 << 22), 1 << 22);
  w[1] = CLAMP(w[1] + ((x2 * er) >> 14), -(1 << 22), 1 << 22);
  w[2] = CLAMP(w[2] + ((256 * er) >> 14), -(1 << 22), 1 << 22);
  return bit;
}

#define T9(B) \
  int k; \
  if (!B(1, v >= 4)) { \
    k = B(2, v >= 2); \
    return (u32) (2 * k + B(4 + k, (int) (v & 1))); \
  } \
  if (!B(3, v >= 6)) return 4u + (u32) B(6, v == 5); \
  if (!B(7, v >= 7)) return 6; \
  return 7u + (u32) B(8, v == 8);

static INLINE u32 ltree9(mx * m, lrc * c, const int dec, u32 * t, u32 v) {
#define B1(j, x) lbit(m, c, dec, &t[j], x)
  T9(B1)
#undef B1
}

static INLINE u32 ltree9m(mx * m, lrc * c, const int dec, u32 * t, u32 * u,
                          i32 (*w)[3], u32 v) {
#define B2(j, x) lmix(m, c, dec, &t[j], &u[j], w[j], x)
  T9(B2)
#undef B2
}

static INLINE void cdf_init(u16 * f) {
  int j;
  Fj(10, f[j] = (u16) (CTOT * j / 9));
}

static INLINE void cdf_upd(u16 * f, int s, int rt) {
#if SF_SSE2
  __m128i x = _mm_loadu_si128((const void *) (f + 1)),
          ix = _mm_set_epi16(8, 7, 6, 5, 4, 3, 2, 1),
          mk = _mm_cmpgt_epi16(ix, _mm_set1_epi16((short) s)),
          cr = _mm_cvtsi32_si128(rt),
          up = _mm_srl_epi16(_mm_sub_epi16(_mm_set1_epi16(CTOT), x), cr),
          dn = _mm_srl_epi16(x, cr);
  x = _mm_add_epi16(x, _mm_and_si128(mk, up));
  x = _mm_sub_epi16(x, _mm_andnot_si128(mk, dn));
  _mm_storeu_si128((void *) (f + 1), x);
#else
  int j;
  for (j = 1; j < 9; j++)
    f[j] = (u16) (j > s ? f[j] + ((CTOT - f[j]) >> rt) : f[j] - (f[j] >> rt));
#endif
}

static INLINE int lsym(mx * m, lrc * c, const int dec, u16 * f, u8 * n,
                       int s) {
  u32 r, ql, qh;
  if (dec) {
    u32 v;
    lnorm(m, c, dec);
    r = c->rg >> 15;
    v = c->cd / r;
    s = 0;
    while (s < 8 && f[s + 1] + (u32) (s + 1) * CMIN <= v) s++;
    ql = f[s] + (u32) s * CMIN;  qh = f[s + 1] + (u32) (s + 1) * CMIN;
    c->cd -= r * ql;  c->rg = r * (qh - ql);
  } else {
    ql = f[s] + (u32) s * CMIN;  qh = f[s + 1] + (u32) (s + 1) * CMIN;
    r = c->rg >> 15;
    laddlo(c, r * ql);
    c->rg = r * (qh - ql);
    lnorm(m, c, dec);
  }
  cdf_upd(f, s, *n < 3 ? 2 : *n < 10 ? 3 : *n < 40 ? 4 : *n < 160 ? 5 : 6);
  if (*n < 255) ++*n;
  return s;
}

static INLINE u32 mm_hash(const u8 * h) {
  return ((((h[0] + 1u) * 0x9E3779B1u + h[-1] + 1u) * 0x9E3779B1u + h[-2] +
           1u) * 0x9E3779B1u) >> (32 - HTB);
}

static INLINE void eng(mx * m, sfz * z, const u8 * src, u8 * dst, sz n,
                       const int dec, const int fl, const int n1,
                       const int m1, const int n2, const int m2,
                       const int n3, const int m3) {
  const int AS = fl & F_S4 ? 4 : 3;
  int x1 = 0, x2 = 0, x3 = 0, af = z->af, as = z->as, s1 = z->s1, j,
      p1 = 0, p2 = 0, p3 = 0;
  u32 gp = z->gp, mp = z->mp, ml = z->ml;
  u8 * restrict hb;
  lrc c;
  sz i;
  if (fl & F_MM) {
    if (!z->ht) {
      z->ht = xmalloc(((sz) 1 << HTB) * sizeof (u32));
      Fj(1 << HTB, z->ht[j] = 8);
      z->hb = xcalloc(z->cap = 8 + (u32) n, 1);
      gp = 8;
    }
    if (gp + n > z->cap) {
      z->cap = gp + (u32) n + (z->cap >> 1);
      z->hb = xrealloc(z->hb, z->cap);
    }
  }
  hb = z->hb;
  if (n1) p1 = vdot(z->g[0].w, z->g[0].h + z->g[0].q, n1);
  if (n2) p2 = vdot(z->g[1].w, z->g[1].h + z->g[1].q, n2);
  if (n3) p3 = vdot(z->g[2].w, z->g[2].h + z->g[2].q, n3);
  if (dec) { c.rg = m->r.range;  c.cd = m->r.code;  c.lo = 0;  c.cy = 0; }
  else { c.lo = m->e.low;  c.rg = m->e.range;  c.cy = m->e.carry;  c.cd = 0; }
  Fi(n,
    int p, r = 0, a, b, s, v = 0, c1 = z->qt[MIN(af >> AS, 4095)], c2 = 0,
        fr = 0, e, hit = 0;
    if (n1) {
      int t = (int) ((u32) x1 * 4096 + 2048 + (u32) (m1 < 0 ? p1 >> 2 : p1) +
                     (u32) p2 + (u32) p3);
      p = clip8(t >> 12);
      fr = (t & 4095) >> 9;
    } else if (fl & F_SEL) {
      int k = z->ee[0] <= z->ee[1] ? 0 : 1;
      if (z->ee[2] < z->ee[k]) k = 2;
      p = k == 0 ? x1 : k == 1 ? clip8(2 * x1 - x2) :
          clip8(3 * x1 - 3 * x2 + x3);
    } else p = x1;
    if ((fl & F_MM) && ml >= 6) {
      int pv = (i8) hb[mp], mc = (int) MIN(ml, 31) >> 1,
          dq = MIN((int) blr_ilog((u32) ABS(pv - p)), 3);
      hit = lbit(m, &c, dec, &z->mh[mc][dq], dec ? 0 : (i8) src[i] == pv);
      v = pv;
    }
    if (hit) { a = ABS(v - p);  b = (int) blr_ilog((u32) a);  s = v < p; }
    else {
      if (!dec) r = (i8) (u8) (src[i] - (u8) p);
      a = r < 0 ? -r : r;
      b = (int) blr_ilog((u32) a);
      if (fl & F_CD) c2 = z->qt[MIN(ABS(p - x1) * 16, 4095)];
      if (fl & F_MS) {
        if (!z->cnt[c1][c2]) cdf_init(z->cdf[c1][c2]);
        b = lsym(m, &c, dec, z->cdf[c1][c2], &z->cnt[c1][c2], b);
      } else if (fl & F_MX)
        b = (int) ltree9m(m, &c, dec, z->bk[c1][c2],
                          z->bk2[c1][z->qt[MIN(as >> 6, 4095)]], z->mw[c1],
                          (u32) b);
      else b = (int) ltree9(m, &c, dec, z->bk[c1][c2], (u32) b);
      s = r < 0;
      if (b > 1) {
        int nb = b - 1;
        u32 mm = (u32) a - (1u << nb), hi, lw;
        if (fl & F_MR) {
          lw = lraw(m, &c, dec, mm << 1 | (u32) s, nb + 1);
          s = (int) (lw & 1);  mm = lw >> 1;
        } else if (fl & F_MT) mm = ltree(m, &c, dec, z->mf[c1 >> 2][b], mm, nb);
        else {
          hi = (u32) lbit(m, &c, dec, &z->mt[c1][b][1],
                          (int) (mm >> (nb - 1) & 1));
          if (fl & F_MG) {
            lw = lraw(m, &c, dec, (mm & ((1u << (nb - 1)) - 1)) << 1 |
                                  (u32) s, nb);
            s = (int) (lw & 1);  lw >>= 1;
          } else if (nb > 1)
            lw = lraw(m, &c, dec, mm & ((1u << (nb - 1)) - 1), nb - 1);
          else lw = 0;
          mm = hi << (nb - 1) | lw;
        }
        a = (int) ((1u << nb) | mm);
      } else a = b;
      if (fl & (F_MG | F_MR)) {
        if (b == 1) s = (int) lraw(m, &c, dec, (u32) s, 1);
      } else if (a) {
        if (fl & F_SG)
          s = lbit(m, &c, dec, &z->sg[(fl & F_FR ? fr : 0) +
                                      (fl & F_SD ? 8 + 8 * ((p > x1) -
                                                            (p < x1)) : 0)]
                                     [s1][b], s);
        else s = (int) lraw(m, &c, dec, (u32) s, 1);
      }
      v = (i8) (u8) (p + (s ? -a : a));
    }
    if (dec) dst[i] = (u8) v;
    if (fl & F_MM) {
      u32 hh, q, l, k;
      k = ml && (i8) hb[mp] == v;
      ml = k ? ml + 1 : 0;
      mp += k;
      hb[gp++] = (u8) v;
      hh = mm_hash(hb + gp - 1);
      q = z->ht[hh];
      k = hb[q - 1] == hb[gp - 1];  l = k;
      k &= hb[q - 2] == hb[gp - 2];  l += k;
      k &= hb[q - 3] == hb[gp - 3];  l += k;
      k &= hb[q - 4] == hb[gp - 4];  l += k;
      k &= hb[q - 5] == hb[gp - 5];  l += k;
      k &= hb[q - 6] == hb[gp - 6];  l += k;
      k &= hb[q - 7] == hb[gp - 7];  l += k;
      k &= hb[q - 8] == hb[gp - 8];  l += k;
      k = !ml && l >= 3;
      ml = k ? l : ml;
      mp = k ? q : mp;
      z->ht[hh] = gp;
    }
    if (n1) {
      e = v - x1;
      p1 = nn_step(&z->g[0], &e, p1, n1, m1);
      if (n2) p2 = nn_step(&z->g[1], &e, p2, n2, m2);
      if (n3) p3 = nn_step(&z->g[2], &e, p3, n3, m3);
    } else if (fl & F_SEL) {
      z->ee[0] += ABS(v - x1) * 16 - (z->ee[0] >> 4);
      z->ee[1] += ABS(v - clip8(2 * x1 - x2)) * 16 - (z->ee[1] >> 4);
      z->ee[2] += ABS(v - clip8(3 * x1 - 3 * x2 + x3)) * 16 -
                  (z->ee[2] >> 4);
    }
    af += a * 16 - (af >> AS);
    if (fl & F_MX) as += a * 16 - (as >> 6);
    s1 = a ? 1 + s : 0;
    x3 = x2;  x2 = x1;  x1 = v);
  if (dec) { m->r.range = c.rg;  m->r.code = c.cd; }
  else { m->e.low = c.lo;  m->e.range = c.rg;  m->e.carry = c.cy; }
  z->af = af;  z->as = as;  z->s1 = s1;  z->gp = gp;  z->mp = mp;  z->ml = ml;
}

#define ENG(...) do { if (m->dec) eng(m, z, src, dst, n, 1, __VA_ARGS__); \
                      else eng(m, z, src, dst, n, 0, __VA_ARGS__); } while (0)
#define FH (F_CD | F_SG | F_FR | F_SD | F_MT | F_MM)
#define FM (F_MS | F_MG | F_CD | F_S4)

void smp_fast_one_avx2(mx * m, void * st, int si, const u8 * src, u8 * dst,
                       sz n);

#if !defined(BLR_CM_AVX2)
static
#endif
void SF_ONE(mx * m, void * st, int si, const u8 * src, u8 * dst, sz n) {
  sfz * z = st;
  (void) si;
  switch (z->opt) {
  case 3: ENG(FM | F_SEL, 0, 0, 0, 0, 0, 0);  break;
  case 5: ENG(FM, 32, 6, 0, 0, 0, 0);  break;
  case 6: ENG(FM | F_MM, 64, 4, 0, 0, 0, 0);  break;
  case 8: ENG((FM ^ F_MG) | F_SG | F_FR | F_SD | F_MM, 128, -6, 32, 8, 0, 0);
    break;
  case 9: ENG(FH, 128, -6, 32, 8, 0, 0);  break;
  case 10: ENG(FH | F_MX, 128, -6, 32, 8, 0, 0);  break;
  case 11: ENG(FH | F_MX, 256, -3, 32, 8, 0, 0);  break;
  default: ENG(F_MS | F_MG | F_MR, 0, 0, 0, 0, 0, 0);
  }
}

#if !defined(BLR_CM_AVX2)
void smp_fast_one(mx * m, void * st, int si, const u8 * src, u8 * dst,
                  sz n) {
#if defined(HAVE_AVX2)
  if (blr_cpu_avx2()) { smp_fast_one_avx2(m, st, si, src, dst, n);  return; }
#endif
  fast_one(m, st, si, src, dst, n);
}
#endif
