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

#include "it/ii.h"
#include "coder/squash.h"
#include "sys/cpu.h"
#include <string.h>

#if defined(HAVE_SSE2) && defined(__SSE2__)
#include <emmintrin.h>
#define S6_SSE2 1
#else
#define S6_SSE2 0
#endif

#if defined(BLR_CM_AVX2)
#include <immintrin.h>
#define S6_AVX2 1
#define SEG s16_seg_avx2
#else
#define S6_AVX2 0
#define SEG segc
#endif

typedef int64_t i64;

#define NM 32
#define NX 3
#define NA (NM + NX)
#define RS 20
#define GN 5
#define GFS 8
#define NNMAX 1024
#define NNWIN 1024
#define HN 64
#define NQ 44
#define NB 18
#define NI 6
#define WMX (1 << 23)
#define MHB 18
#define MN 6
#define SFE 4096
#define NP 40
#define LV 16
#define SFB 1024

enum {
  O_RANK = 1, O_MIX = 2, O_XCH = 4, O_SGN = 8, O_MM = 16, O_MR = 32,
  O_MG = 64, O_DQ = 128, O_UB = 256
};

typedef struct {
  u8 on, kk, fs, xm;
  u16 nn[3];
  u8 ns[3], gn, gk;
  u32 o;
} s6cf;

#define F1 (O_RANK | O_XCH | O_UB)
#define F2 (F1 | O_MM)
#define F4 (O_RANK | O_XCH | O_MM | O_MR | O_SGN | O_MIX | O_MG | O_DQ)

#if !S6_AVX2
static const s6cf cft[] = {
  [1] = { 0, 0, 0, 0, { 16, 0, 0 }, { 11, 0, 0 }, 0, 0, F1 },
  [2] = { 4, 5, 9, 0, { 16, 0, 0 }, { 11, 0, 0 }, 0, 0, F1 },
  [10] = { 4, 6, 9, 3, { 256, 16, 0 }, { 13, 11, 0 }, 0, 0, F2 },
  [11] = { 8, 6, 9, 3, { 256, 16, 0 }, { 13, 11, 0 }, 0, 0, F2 },
  [12] = { 8, 6, 9, 3, { 512, 256, 16 }, { 14, 13, 11 }, 0, 0, F2 },
  [15] = { 8, 6, 9, 3, { 512, 256, 16 }, { 14, 13, 11 }, 5, 3, F4 },
  [16] = { 16, 6, 10, 3, { 1024, 256, 16 }, { 15, 13, 11 }, 5, 3, F4 },
  [17] = { 32, 7, 11, 3, { 1024, 256, 16 }, { 15, 13, 11 }, 5, 4, F4 },
  [18] = { 32, 7, 11, 3, { 1024, 256, 16 }, { 15, 13, 11 }, 5, 2, F4 },
};
#define NCF ((int) ARRAY_LEN(cft))
#endif

typedef struct { i64 m;  int e; } sf;

typedef struct {
  int n, m, kk, fs, cnt, rp;
  i64 r[NM + 1], rh[NM][NM + 1], c[NM][NX], g[NX][NX], bx[NX];
  i32 w[NA];
} ols;

typedef struct {
  int n, cnt, k;
  i64 r[GN][GN], b[GN];
  i32 w[GN];
} gls;

typedef struct {
  i16 w[NNMAX], h[NNWIN + NNMAX], ad[NNWIN + NNMAX];
  int q, avg, n, sh, dt;
} nlf;

typedef struct {
  ols o;
  gls g;
  nlf * f[3];
  i32 hx[2 * HN];
  int pos;
} prd;

typedef struct {
  u32 bt[NQ][32], b1[NQ][NB + 1][32], b2[NQ][16][32], b3[NB][16][32],
      mt[NB + 1][NQ][4], ub[NQ][2][8], sg[5][16][3], rk[2], sm[2],
      mm[16][17][2][4];
  i32 mw[NQ][32][NI];
} rcm;

typedef struct {
  s6cf c;
  int lim, ef, es, eu, b1, s1, u, ml, mhit, mq, avx;
  prd p[2];
  rcm r;
  i64 A[NA][NA];
  sf sL[NA][NA];
  i64 lm[NA][NP], le[NA][NP];
  u16 * nr, * rk;
  i32 * av;
  i16 * mb;
  u32 * mh, * ct;
  sz mc, mn, mp;
} s6;

void s16_seg_avx2(mx * m, s6 * z, int rl, const i16 * s, i16 * d, sz n,
                  const i16 * ref, sz rn, int dm);

#if !S6_AVX2
typedef struct {
  int cfg;
  s6 * e;
} s16t;

int s16_config(int c) {
  return c > IT_S16C0 && c - IT_S16C0 < NCF && cft[c - IT_S16C0].o;
}

static void wset(i32 * w, int n, int k) {
  int i;
  Fi(n, w[i] = i < k ? 65536 / k : 0);
}

static s6 * s6_new(int ci) {
  s6 * z = xcalloc(1, sizeof (s6));
  const s6cf * c = cft + ci;
  int i, j, k;
  z->c = *c;  z->lim = 32767;
  Fk(2,
    prd * p = z->p + k;
    p->o.n = c->on;  p->o.m = k ? c->xm : 0;
    p->o.kk = c->kk;  p->o.fs = c->fs;
    p->g.n = c->gn;  p->g.k = c->gk;
    if (c->gn) p->g.w[0] = 65536;
    Fi(3, if (c->nn[i]) {
      p->f[i] = xcalloc(1, sizeof (nlf));
      p->f[i]->n = c->nn[i];
      p->f[i]->sh = c->ns[i];  p->f[i]->q = NNWIN;
    }));
  Fi(NQ, Fj(32, wset(z->r.mw[i][j], NI, 3)));
#if defined(HAVE_AVX2)
  z->avx = blr_cpu_avx2();
#endif
  return z;
}

static void s6_free(s6 * z) {
  int i, k;
  if (!z) return;
  Fk(2, Fi(3, xfree(z->p[k].f[i])));
  xfree(z->nr);  xfree(z->rk);  xfree(z->av);
  xfree(z->mb);  xfree(z->mh);  xfree(z->ct);
  xfree(z);
}

void * s16_new(int c) {
  s16t * t = xcalloc(1, sizeof (s16t));
  rc_adapt_init();
  acm_prepare();
  t->cfg = c;
  return t;
}

void s16_free(void * st) {
  s16t * t = st;
  s6_free(t->e);
  xfree(t);
}
#endif

static i64 cl64(i64 v, i64 b) { return v < -b ? -b : v > b ? b : v; }

static INLINE sf sfn(i64 m, int e) {
  sf r;
  int s;
  r.m = 0;  r.e = -SFE;
  if (!m) return r;
#if defined(__GNUC__) && !defined(BLR_NO_ATTRS)
  s = 2 - __builtin_clzll(m < 0 ? 0 - (uint64_t) m : (uint64_t) m);
#else
  s = (int) blr_ilog64(m < 0 ? 0 - (uint64_t) m : (uint64_t) m) - 62;
#endif
  if (s > 0) m >>= s;
  else m *= (i64) 1 << -s;
  if (e + s < -SFE) return r;
  r.m = m;  r.e = MIN(e + s, SFE);
  return r;
}

static INLINE sf sfmul(sf a, sf b) {
  i64 p = (a.m >> 31) * (b.m >> 31);
  int e = a.e + b.e + 62;
  sf r;
  r.m = 0;  r.e = -SFE;
  if (!p) return r;
  if (p > -((i64) 1 << 61) && p < (i64) 1 << 61) { p *= 2;  e--; }
  else if (p == (i64) 1 << 62) { p >>= 1;  e++; }
  if (e < -SFE) return r;
  r.m = p;  r.e = MIN(e, SFE);
  return r;
}

static INLINE sf sfsub(sf a, sf b) {
  int d;
  b.m = -b.m;
  if (!b.m) return a;
  if (!a.m) return b;
  if (a.e < b.e) { sf t = a;  a = b;  b = t; }
  d = a.e - b.e;
  if (d > 62) return a;
  return sfn(a.m + (b.m >> d), a.e);
}

static INLINE sf sfinv(sf a) {
  return sfn(((i64) 1 << 62) / (a.m >> 30), -92 - a.e);
}

static INLINE int sfgt(sf a, sf b) {
  if (a.m <= 0 || b.m <= 0) return a.m > b.m;
  return a.e != b.e ? a.e > b.e : a.m > b.m;
}

static INLINE i64 sffix(sf a, int q, i64 lim) {
  int sh = a.e + q;
  if (!a.m || sh < -62) return 0;
  if (sh >= 0) return a.m < 0 ? -lim : lim;
  return cl64(a.m >> -sh, lim);
}

#if S6_AVX2
typedef __m256i vq;

#define VZ _mm256_setzero_si256()
#define VC(x) _mm256_set1_epi64x(x)
#define VB _mm256_blendv_epi8
#define VGT _mm256_cmpgt_epi64
#define VLZ(k) c = _mm256_cmpeq_epi64(_mm256_srli_epi64(y, 62 - k), VZ); \
  y = VB(y, _mm256_slli_epi64(y, k), c); \
  l = _mm256_sub_epi64(l, _mm256_and_si256(c, VC(-k)))

static INLINE void vfn(vq x, vq e, vq * rm, vq * re) {
  vq g = VGT(VZ, x), a = _mm256_sub_epi64(_mm256_xor_si256(x, g), g), y = a,
     l = _mm256_srli_epi64(a, 58), r = _mm256_srli_epi64(a, 62), c;
  if (_mm256_movemask_epi8(_mm256_cmpeq_epi64(l, VZ))) {
    l = VZ;
    VLZ(32);  VLZ(16);  VLZ(8);  VLZ(4);  VLZ(2);  VLZ(1);
  } else
    l = _mm256_shuffle_epi8(_mm256_setr_epi8(0, 3, 2, 2, 1, 1, 1, 1, 0, 0, 0,
          0, 0, 0, 0, 0, 0, 3, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0),
          _mm256_min_epi32(l, VC(15)));
  *rm = _mm256_xor_si256(_mm256_srlv_epi64(_mm256_xor_si256(
          _mm256_sllv_epi64(x, l), g), r), g);
  e = _mm256_sub_epi64(_mm256_add_epi64(e, r), l);
  *re = VB(e, VC(-SFE), _mm256_cmpeq_epi64(a, VZ));
}

static INLINE void vmul(vq am, vq ae, vq bm, vq be, vq * rm, vq * re) {
  vq p = _mm256_mul_epi32(_mm256_srli_epi64(am, 31),
                          _mm256_srli_epi64(bm, 31)),
     e = _mm256_add_epi64(_mm256_add_epi64(ae, be), VC(62)),
     lo = _mm256_and_si256(VGT(p, VC(-((i64) 1 << 61))),
                           VGT(VC((i64) 1 << 61), p)),
     hi = _mm256_cmpeq_epi64(p, VC((i64) 1 << 62));
  p = VB(VB(p, _mm256_add_epi64(p, p), lo), VC((i64) 1 << 61), hi);
  e = _mm256_sub_epi64(_mm256_add_epi64(e, lo), hi);
  *rm = p;
  *re = VB(e, VC(-SFE), _mm256_cmpeq_epi64(p, VZ));
}

static INLINE void vsub(vq * tm, vq * te, vq pm, vq pe) {
  vq bm = _mm256_sub_epi64(VZ, pm), sw = VGT(pe, *te),
     am = VB(*tm, bm, sw), ae = VB(*te, pe, sw), sm = VB(bm, *tm, sw),
     d = _mm256_sub_epi64(ae, VB(pe, *te, sw)), far = VGT(d, VC(62)),
     g = VGT(VZ, sm), rm, re;
  sm = _mm256_xor_si256(_mm256_srlv_epi64(_mm256_xor_si256(sm, g), d), g);
  vfn(_mm256_add_epi64(am, sm), ae, &rm, &re);
  *tm = VB(rm, am, far);  *te = VB(re, ae, far);
}

#define VL(p) _mm256_loadu_si256((const void *) (p))
#define VS(p, x) _mm256_storeu_si256((void *) (p), x)

static INLINE void vrow(const i64 * lm, const i64 * le, i64 * tm, i64 * te,
                 int n, sf v) {
  vq bm = VC(v.m), be = VC(v.e), pm, pe, m, e;
  int i;
  for (i = 0; i < n; i += 4) {
    m = VL(tm + i);  e = VL(te + i);
    vmul(VL(lm + i), VL(le + i), bm, be, &pm, &pe);
    vsub(&m, &e, pm, pe);
    VS(tm + i, m);  VS(te + i, e);
  }
}

static INLINE void vprod(const i64 * am, const i64 * ae, const i64 * bm,
                  const i64 * be, int n, i64 * rm, i64 * re) {
  vq m, e;
  int i;
  for (i = 0; i < n; i += 4) {
    vmul(VL(am + i), VL(ae + i), VL(bm + i), VL(be + i), &m, &e);
    VS(rm + i, m);  VS(re + i, e);
  }
}

static INLINE sf sfat(i64 m, i64 e) {
  sf r;
  r.m = m;  r.e = (int) e;
  return r;
}

static INLINE int sfok(i64 m, i64 e) { return !m || ABS(e) <= SFB; }

static int ldlv(s6 * z, const i64 * b, int n, i32 * w) {
  i64 (*A)[NA] = z->A, (*M)[NP] = z->lm, (*E)[NP] = z->le, em = 1,
      tm[NP], te[NP], pm[NP], pe[NP];
  sf D[NA], ID[NA], v[NA], d, t, reg, dmin;
  int i, j, k, ok = 1;
  Fi(n, em = MAX(em, A[i][i]));
  reg = sfn(em, -RS);  dmin = sfn(em, -RS - 2);
  Fi(NP, tm[i] = te[i] = 0);
  Fj(n,
    Fk(j, v[k] = sfmul(sfat(M[k][j], E[k][j]), D[k]);
          ok &= sfok(v[k].m, v[k].e));
    d = sfsub(sfn(A[j][j], 0), sfn(-reg.m, reg.e));
    tm[j] = d.m;  te[j] = d.e;
    for (i = j + 1; i < n; i++) {
      t = sfn(A[i][j], 0);
      tm[i] = t.m;  te[i] = t.e;
    }
    Fk(j, vrow(M[k] + j, E[k] + j, tm + j, te + j, n - j, v[k]));
    d = sfat(tm[j], te[j]);
    D[j] = sfgt(d, dmin) ? d : dmin;
    ID[j] = sfinv(D[j]);
    ok &= sfok(ID[j].m, ID[j].e);
    {
      vq im = VC(ID[j].m), ie = VC(ID[j].e), m, e;
      for (i = j + 1; i < n; i += 4) {
        vmul(VL(tm + i), VL(te + i), im, ie, &m, &e);
        VS(M[j] + i, m);  VS(E[j] + i, e);
      }
    }
    for (i = j + 1; i < n; i++) ok &= sfok(M[j][i], E[j][i]);
    if (!ok) return 0);
  Fi(n,
    t = sfn(b[i], 0);
    tm[i] = t.m;  te[i] = t.e);
  Fi(NP - n, tm[n + i] = te[n + i] = 0);
  Fk(n,
    if (!sfok(tm[k], te[k])) return 0;
    vrow(M[k] + k + 1, E[k] + k + 1, tm + k + 1, te + k + 1, n - k - 1,
         sfat(tm[k], te[k])));
  Fi(n,
    t = sfmul(sfat(tm[i], te[i]), ID[i]);
    tm[i] = t.m;  te[i] = t.e);
  for (i = n - 1; i >= 0; i--) {
    vprod(M[i] + i + 1, E[i] + i + 1, tm + i + 1, te + i + 1, n - i - 1,
          pm, pe);
    t = sfat(tm[i], te[i]);
    Fk(n - i - 1, t = sfsub(t, sfat(pm[k], pe[k])));
    if (!sfok(t.m, t.e)) return 0;
    tm[i] = t.m;  te[i] = t.e;
  }
  Fi(n, if (tm[i] && te[i] >= -51) return 1);
  Fi(n, w[i] = (i32) sffix(sfat(tm[i], te[i]), 16, (i64) 1 << 26));
  return 1;
}
#endif

static void ldls(s6 * z, const i64 * b, int n, i32 * w) {
  i64 (*A)[NA] = z->A, em = 1;
  sf (*L)[NA] = z->sL, D[NA], ID[NA], v[NA], y[NA], d, t, reg, dmin;
  int i, j, k;
  Fi(n, em = MAX(em, A[i][i]));
  reg = sfn(em, -RS);  dmin = sfn(em, -RS - 2);
  Fj(n,
    d = sfn(A[j][j], 0);
    d = sfsub(d, sfn(-reg.m, reg.e));
    Fk(j, v[k] = sfmul(L[j][k], D[k]);  d = sfsub(d, sfmul(L[j][k], v[k])));
    D[j] = sfgt(d, dmin) ? d : dmin;
    ID[j] = sfinv(D[j]);
    for (i = j + 1; i < n; i++) {
      t = sfn(A[i][j], 0);
      Fk(j, t = sfsub(t, sfmul(L[i][k], v[k])));
      L[i][j] = sfmul(t, ID[j]);
    });
  Fi(n,
    t = sfn(b[i], 0);
    Fk(i, t = sfsub(t, sfmul(L[i][k], y[k])));
    y[i] = t);
  Fi(n, y[i] = sfmul(y[i], ID[i]));
  for (i = n - 1; i >= 0; i--) {
    t = y[i];
    for (k = i + 1; k < n; k++) t = sfsub(t, sfmul(L[k][i], y[k]));
    y[i] = t;
  }
  Fi(n, if (y[i].m && y[i].e >= -51) return);
  Fi(n, w[i] = (i32) sffix(y[i], 16, (i64) 1 << 26));
}

static void ldl(s6 * z, const i64 * b, int n, i32 * w) {
#if S6_AVX2
  if (n >= LV && ldlv(z, b, n, w)) return;
#endif
  ldls(z, b, n, w);
}

static void solve(s6 * z, ols * o) {
  i64 (*A)[NA] = z->A, b[NA];
  int n = o->n, m = o->m, i, j;
  Fi(n,
    const i64 * h = o->rh[(o->rp + n - 1 - i) % n];
    for (j = i; j < n; j++) A[j][i] = h[j - i];
    b[i] = o->r[i + 1]);
  Fj(m,
    Fi(n, A[n + j][i] = o->c[i][j]);
    Fi(j + 1, A[n + j][n + i] = o->g[i][j]);
    b[n + j] = o->bx[j]);
  ldl(z, b, n + m, o->w);
}

static void ols_upd(s6 * z, ols * o, const i32 * h, const i32 * g) {
  int n = o->n, m = o->m, k, j, x = h[0], fs = o->fs;
  Fj(m,
    Fk(n, o->c[k][j] += (i64) h[k + 1] * g[j] - (o->c[k][j] >> fs));
    Fk(j + 1, o->g[k][j] += (i64) g[k] * g[j] - (o->g[k][j] >> fs));
    o->bx[j] += (i64) x * g[j] - (o->bx[j] >> fs));
  memcpy(o->rh[o->rp], o->r, (sz) (n + 1) * sizeof (i64));
  o->rp = (o->rp + 1) % n;
  Fk(n + 1, o->r[k] += (i64) x * h[k] - (o->r[k] >> fs));
  if (++o->cnt >= 1 << o->kk) { o->cnt = 0;  solve(z, o); }
}

static INLINE i64 ols_pred(const ols * o, const i32 * h, const i32 * g) {
  i64 p = 0;
  int j;
  Fj(o->n, p += (i64) o->w[j] * h[j]);
  Fj(o->m, p += (i64) o->w[o->n + j] * g[j]);
  return p;
}

static void gls_upd(s6 * z, gls * g, const i32 * u, i32 x) {
  int i, j, n = g->n;
  i64 b[GN];
  Fi(n,
    Fj(i + 1, g->r[i][j] += (i64) u[i] * u[j] - (g->r[i][j] >> GFS));
    g->b[i] += (i64) u[i] * x - (g->b[i] >> GFS));
  if (++g->cnt < 1 << g->k) return;
  g->cnt = 0;
  Fi(n, Fj(i + 1, z->A[i][j] = g->r[i][j]);  b[i] = g->b[i]);
  ldl(z, b, n, g->w);
}

static INLINE i64 gls_pred(const gls * g, const i32 * u) {
  i64 p = 0;
  int i;
  Fi(g->n, p += (i64) g->w[i] * u[i]);
  return p >> 16;
}

#if S6_AVX2
static INLINE int vsum(__m256i t) {
  __m128i s = _mm_add_epi32(_mm256_castsi256_si128(t),
                            _mm256_extracti128_si256(t, 1));
  s = _mm_add_epi32(s, _mm_shuffle_epi32(s, 0x4E));
  s = _mm_add_epi32(s, _mm_shuffle_epi32(s, 0xB1));
  return _mm_cvtsi128_si32(s);
}
#endif

static INLINE int vdot(const i16 * w, const i16 * h, const int n) {
  int j;
#if S6_AVX2
  __m256i t = _mm256_setzero_si256();
  for (j = 0; j < n; j += 16)
    t = _mm256_add_epi32(t,
      _mm256_madd_epi16(_mm256_loadu_si256((const void *) (w + j)),
                        _mm256_loadu_si256((const void *) (h + j))));
  return vsum(t);
#elif S6_SSE2
  __m128i t = _mm_setzero_si128();
  for (j = 0; j < n; j += 8)
    t = _mm_add_epi32(t,
      _mm_madd_epi16(_mm_loadu_si128((const void *) (w + j)),
                     _mm_loadu_si128((const void *) (h + j))));
  t = _mm_add_epi32(t, _mm_shuffle_epi32(t, 0x4E));
  t = _mm_add_epi32(t, _mm_shuffle_epi32(t, 0xB1));
  return _mm_cvtsi128_si32(t);
#else
  u32 t = 0;
  Fj(n, t += (u32) (w[j] * h[j]));
  return (int) t;
#endif
}

static INLINE int vupd(i16 * w, const i16 * d, const i16 * h, const int n,
                       int sg) {
  int j;
#if S6_AVX2
  __m256i k = _mm256_set1_epi16((short) sg), t = _mm256_setzero_si256(), x;
  for (j = 0; j < n; j += 16) {
    x = _mm256_add_epi16(_mm256_loadu_si256((const void *) (w + j)),
          _mm256_mullo_epi16(_mm256_loadu_si256((const void *) (d + j)), k));
    _mm256_storeu_si256((void *) (w + j), x);
    t = _mm256_add_epi32(t,
          _mm256_madd_epi16(x, _mm256_loadu_si256((const void *) (h + j))));
  }
  return vsum(t);
#elif S6_SSE2
  __m128i k = _mm_set1_epi16((short) sg), t = _mm_setzero_si128(), x;
  for (j = 0; j < n; j += 8) {
    x = _mm_add_epi16(_mm_loadu_si128((const void *) (w + j)),
          _mm_mullo_epi16(_mm_loadu_si128((const void *) (d + j)), k));
    _mm_storeu_si128((void *) (w + j), x);
    t = _mm_add_epi32(t,
          _mm_madd_epi16(x, _mm_loadu_si128((const void *) (h + j))));
  }
  t = _mm_add_epi32(t, _mm_shuffle_epi32(t, 0x4E));
  t = _mm_add_epi32(t, _mm_shuffle_epi32(t, 0xB1));
  return _mm_cvtsi128_si32(t);
#else
  u32 t = 0;
  Fj(n, w[j] = (i16) (w[j] + d[j] * sg);  t += (u32) (w[j] * h[j]));
  return (int) t;
#endif
}

static INLINE int nl_pred(const nlf * f) {
  return (int) ((u32) f->dt + (1u << (f->sh - 1))) >> f->sh;
}

static INLINE void nl_upd(nlf * f, int u, int r) {
  int q = f->q, p = q, au = ABS(u), c, sg = (r > 0) - (r < 0);
  c = au > 3 * f->avg ? 32 : 3 * au > 4 * f->avg ? 16 : au ? 8 : 0;
  f->avg += (au - f->avg) / 16;
  if (!q) {
    memcpy(f->h + NNWIN, f->h, (sz) f->n * sizeof (i16));
    memcpy(f->ad + NNWIN, f->ad, (sz) f->n * sizeof (i16));
    q = NNWIN;
  }
  f->q = --q;
  f->h[q] = (i16) CLAMP(u, -32768, 32767);
  f->dt = sg ? vupd(f->w, f->ad + p, f->h + q, f->n, sg)
             : vdot(f->w, f->h + q, f->n);
  f->ad[q] = (i16) (u > 0 ? c : -c);
  f->ad[q + 1] = (i16) (f->ad[q + 1] >> 1);
  f->ad[q + 2] = (i16) (f->ad[q + 2] >> 1);
  f->ad[q + 8] = (i16) (f->ad[q + 8] >> 1);
}

static INLINE int qlog(i32 e) {
  u32 v = (u32) e + 1, b = blr_ilog(v);
  int q = b < 2 ? 0 : (int) (2 * b - 3 + (v >> (b - 2) & 1));
  return MIN(q, NQ - 1);
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

static INLINE int lbp(mx * m, lrc * c, const int dec, u32 p, int bit) {
  u32 sp;
  if (dec) {
    lnorm(m, c, dec);
    sp = (c->rg >> 16) * p;
    bit = c->cd >= sp;
    c->cd -= bit ? sp : 0;
    c->rg = bit ? c->rg - sp : sp;
  } else {
    u32 lo;
    sp = (c->rg >> 16) * p;
    lo = c->lo + (bit ? sp : 0);
    c->cy |= lo < c->lo;
    c->lo = lo;
    c->rg = bit ? c->rg - sp : sp;
    lnorm(m, c, dec);
  }
  return bit;
}

static INLINE int lbit(mx * m, lrc * c, const int dec, u32 * s, int bit) {
  bit = lbp(m, c, dec, rc_packed_prob(*s), bit);
  *s = rc_adapt_packed(*s, 60, bit);
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
  {
    u32 lo = c->lo + r * v;
    c->cy |= lo < c->lo;
    c->lo = lo;
  }
  c->rg = r;
  lnorm(m, c, dec);
  return v;
}

static INLINE int lmix(mx * m, lrc * c, const int dec, u32 ** cs, int nc,
                       i32 * w, int bit) {
  int st[NI], p, err, i, d;
  i64 dot = 0;
  Fi(nc, st[i] = acm_str[(65536 - rc_packed_prob(*cs[i])) >> 4]);
  st[nc] = 256;
  Fi(nc + 1, dot += (i64) w[i] * st[i]);
  d = (int) cl64(dot >> 16, 2047);
  p = acm_squash(d);
  bit = lbp(m, c, dec, (u32) CLAMP(65536 - p * 16, 1, 65535), bit);
  err = ((bit << 12) - p) * 2;
  Fi(nc + 1, w[i] = CLAMP(w[i] + ((st[i] * err) >> 14), -WMX, WMX));
  Fi(nc, *cs[i] = rc_adapt_packed(*cs[i], i ? 255 : 60, bit));
  return bit;
}

#if !S6_AVX2
static void s6_alpha(mx * m, s6 * z, const i16 * src, sz n) {
  int lim = z->lim, W = 2 * lim + 2, lo = W, hi = -1, u = 0, j, use = 0,
      k, d, pg = 0, last;
  u8 * sn = xcalloc((sz) W, 1);
  z->u = 0;
  if (src) {
    u32 * cnt = z->ct ? z->ct : (z->ct = xcalloc((sz) W, sizeof *cnt));
    i64 a = 0, c0, c1;
    sz i;
    Fi(n, { int v = src[i] + lim + 1;
            cnt[v]++;  lo = MIN(lo, v);  hi = MAX(hi, v); });
    for (j = lo; j <= hi; j++) if (cnt[j]) { u++;  sn[j] = 1; }
    if (u > 1) {
      int q0 = lo, q1 = lo ? lo : hi, uc = 0;
      for (j = lo; j <= hi; j++) {
        a += cnt[j];
        if (a * 4 < (i64) n) q0 = j + 1;
        if (a * 4 < 3 * (i64) n) q1 = j + 1;
      }
      q1 = MAX(q1, q0);
      for (j = q0; j <= q1; j++) uc += sn[j];
      c0 = (i64) n * (i64) (blr_ilog((u32) (q1 - q0 + 1)) -
                            blr_ilog((u32) uc));
      c1 = (i64) (hi - lo + 1) / 2 + 64;
      use = c0 > c1 && 4 * uc < 3 * (q1 - q0 + 1);
    }
    for (j = lo; j <= hi; j++) cnt[j] = 0;
  }
  if (!pbit(m, &z->r.rk[0], 30, use)) { xfree(sn);  return; }
  lo = src ? lo : 0;  hi = src ? hi : 0;
  d = 0;
  for (j = 15; j >= 0; j--) d = d * 2 + mx_bit(m, lo >> j & 1, 32768);
  lo = MIN(d, W - 1);  d = 0;
  for (j = 15; j >= 0; j--) d = d * 2 + mx_bit(m, (hi - lo) >> j & 1, 32768);
  hi = MIN(lo + d, W - 1);
  if (!src) memset(sn, 0, (sz) W);
  sn[lo] = sn[hi] = 1;
  last = lo;
  {
    u32 bc[16 * 3 * 4];
    memset(bc, 0, sizeof bc);
    for (j = lo + 1; j < hi; j++) {
      int g = j - last, c = g < pg ? 0 : g == pg ? 1 : 2,
          ctx = (MIN(g, 15) * 3 + c) * 4 + sn[j - 1] * 2 +
                (j - 2 >= lo ? sn[j - 2] : 0);
      sn[j] = (u8) pbit(m, &bc[ctx], 60, sn[j]);
      if (sn[j]) { pg = g;  last = j; }
    }
  }
  if (!z->nr) {
    z->nr = xmalloc((sz) W * sizeof *z->nr);
    z->rk = xmalloc((sz) W * sizeof *z->rk);
    z->av = xmalloc((sz) W * sizeof *z->av);
  }
  u = 0;
  Fj(W, if (sn[j]) { z->rk[j] = (u16) u;  z->av[u++] = j - lim - 1; }
        else z->rk[j] = 0);
  z->u = u;
  k = 0;
  Fj(W,
    while (k + 1 < u && z->av[k + 1] - (j - lim - 1) <= (j - lim - 1) -
                                                        z->av[k])
      k++;
    z->nr[j] = (u16) k);
  xfree(sn);
}
#endif

static INLINE u32 mhash(const i16 * b) {
  u32 h = 0;
  int i;
  Fi(MN, h = (h + (u16) b[i] + 1) * 0x2F0F3A35u);
  return h >> (32 - MHB);
}

static INLINE void mput(s6 * z, int v, const u32 op) {
  u32 c, hh;
  int k = 0;
  if (z->ml) {
    if (z->mb[z->mp] == v) {
      z->ml = MIN(z->ml + 1, 65535);
      z->mq -= z->mq > 0;
    } else if (op & O_MR && z->ml >= 16 && z->mq < 8) z->mq += 2;
    else z->ml = z->mq = 0;
    z->mp += z->ml > 0;
  }
  z->mb[z->mn++] = (i16) v;
  if (z->mn < MN) return;
  hh = mhash(z->mb + z->mn - MN);
  c = z->mh[hh];
  z->mh[hh] = (u32) z->mn;
  if ((z->ml && !z->mq) || !c || c == z->mp) return;
  while (k < 32 && (u32) k < c && z->mb[c - 1 - k] == z->mb[z->mn - 1 - k])
    k++;
  if (k >= MN) { z->mp = c;  z->ml = k;  z->mq = 0; }
}

static INLINE void segx(mx * m, s6 * z, int role, const i16 * src, i16 * dst,
                        sz n, const i16 * ref, sz rn, int dm, const u32 op,
                        const int dec) {
  prd * pr = z->p + role;
  ols * o = &pr->o;
  rcm * r = &z->r;
  const int lim = dm ? 2 * z->lim + 1 : z->lim, rk = z->u > 0,
            xm = role ? z->c.xm : 0;
  int j, ef = z->ef, es = z->es, eu = z->eu, b1 = z->b1, s1 = z->s1;
  lrc c;
  sz i;
  memset(pr->hx, 0, sizeof pr->hx);
  pr->pos = 0;
  if (op & O_MM) {
    if (!z->mh) z->mh = xcalloc((sz) 1 << MHB, sizeof *z->mh);
    z->mb = xgrow(z->mb, &z->mc, z->mn + n + 1, sizeof *z->mb, 65536);
  }
  if (dec) { c.rg = m->r.range;  c.cd = m->r.code;  c.lo = 0;  c.cy = 0; }
  else { c.lo = m->e.low;  c.rg = m->e.range;  c.cy = m->e.carry;  c.cd = 0; }
  Fi(n,
    const i32 * h = pr->hx + pr->pos;
    i32 g[NX], gu[GN];
    i64 po;
    int P16, P, f, fl, e = 0, a = 0, b, x = 0, q1 = qlog(ef), q2 = qlog(es),
        q3 = qlog(eu), base, pn[3], u, jP = 0, sg = 0, hit = 0;
    Fj(xm, { sz t = i + 1 - (sz) j;
             g[j] = t < rn && i + 1 >= (sz) j ? ref[t] : 0; });
    po = o->n ? ols_pred(o, h, g) : (i64) h[0] * 65536;
    po = cl64(po, (i64) 1 << 36);
    base = (int) CLAMP((po + 32768) >> 16, -65536, 65535);
    P16 = (int) (po >> 12);
    Fj(3, pn[j] = pr->f[j] ? nl_pred(pr->f[j]) : 0;
          P16 += pn[j] * 16);
    if (z->c.gn) {
      gu[0] = P16;  gu[1] = (int) (po >> 12);  gu[2] = h[0] * 16;
      gu[3] = h[1] * 16;
      if (op & O_MG)
        gu[4] = op & O_MM && z->ml >= MN
                ? (z->mb[z->mp] - (dm ? ref[i] : 0)) * 16 : P16;
      P16 = (int) cl64(gls_pred(&pr->g, gu), (i64) 1 << 22);
    }
    P16 = CLAMP(P16, -16 * (lim + 1), 16 * lim);
    P = (P16 + 8) >> 4;
    if (rk) { jP = z->nr[P + lim + 1];  P = z->av[jP]; }
    f = CLAMP(P16 - 16 * P, -127, 127);
    fl = f < 0;
    if (!dec) {
      x = src[i] - (dm ? ref[i] : 0);
      e = rk ? z->rk[x + lim + 1] - jP : x - P;
      if (fl) e = -e;
      a = ABS(e);
    }
    if (op & O_MM && z->ml >= MN) {
      int xm1 = z->mb[z->mp] - (dm ? ref[i] : 0);
      hit = lbit(m, &c, dec, &r->mm[MIN(blr_ilog((u32) z->ml), 15)]
                                   [MIN(blr_ilog((u32) ABS(xm1 - P)), 16)]
                                   [z->mhit][MIN(z->mq, 3)],
                 dec ? 0 : x == xm1);
      if (hit) {
        x = CLAMP(xm1, -lim - 1, lim);
        e = rk ? z->rk[x + lim + 1] - jP : x - P;
        b = (int) blr_ilog((u32) ABS(e));
      }
    }
    if (!hit) {
      b = (int) blr_ilog((u32) a);
      if (op & O_MIX) {
        u32 node = 1, * cs[4];
        int lo = 0, hi = NB, nc = op & O_DQ ? 4 : 3,
            dq = MIN((int) blr_ilog((u32) ABS(P16 - (int) (po >> 12)) >> 4),
                     NB - 1);
        while (hi - lo > 1) {
          int mid = lo + (hi - lo) / 2, bit;
          cs[0] = &r->bt[q1][node];
          cs[1] = &r->b1[q2][b1][node];
          cs[2] = &r->b2[q3][MIN((int) blr_ilog((u32) ABS(f)), 15)][node];
          cs[3] = &r->b3[dq][MIN(q1 / 3, 15)][node];
          bit = lmix(m, &c, dec, cs, nc, r->mw[q1][node], b >= mid);
          if (bit) lo = mid; else hi = mid;
          node = node * 2 + (u32) bit;
        }
        b = lo;
      } else if (op & O_UB) {
        int k0 = MIN((int) blr_ilog((u32) ef >> 5), NB - 1);
        u32 * t = r->ub[q1][0];
        if (lbit(m, &c, dec, t, b >= k0)) {
          j = k0;
          while (j < NB - 1 && lbit(m, &c, dec, t + 1 + MIN(j - k0, 6), b > j))
            j++;
        } else {
          t = r->ub[q1][1];
          j = k0 - 1;
          while (j > 0 && lbit(m, &c, dec, t + MIN(k0 - 1 - j, 7), b < j)) j--;
        }
        b = MAX(j, 0);
      } else {
        u32 * t = r->bt[q1], node = 1;
        int lo = 0, hi = NB;
        while (hi - lo > 1) {
          int mid = lo + (hi - lo) / 2,
              bit = lbit(m, &c, dec, t + node, b >= mid);
          if (bit) lo = mid; else hi = mid;
          node = node * 2 + (u32) bit;
        }
        b = lo;
      }
      if (op & O_UB) {
        if (b > 1) {
          int nb = b - 1;
          u32 v = 2 + (u32) lbit(m, &c, dec, &r->mt[b][q1][0],
                                 a >> (nb - 1) & 1), w;
          w = lraw(m, &c, dec, ((u32) a & ((1u << (nb - 1)) - 1)) << 1 |
                               (u32) (e < 0), nb);
          sg = (int) (w & 1);
          a = (int) (v << (nb - 1) | w >> 1);
        } else {
          a = b;
          if (a) sg = (int) lraw(m, &c, dec, (u32) (e < 0), 1);
        }
      } else if (b > 1) {
        int nb = b - 1, v = 2, bit;
        bit = lbit(m, &c, dec, &r->mt[b][q1][0], a >> (nb - 1) & 1);
        v += bit;
        if (nb > 1) {
          bit = lbit(m, &c, dec, &r->mt[b][q1][2 + bit], a >> (nb - 2) & 1);
          v = v * 2 + bit;
          if (nb > 2)
            v = (int) ((u32) v << (nb - 2) |
                       lraw(m, &c, dec, (u32) a & ((1u << (nb - 2)) - 1),
                            nb - 2));
        }
        a = v;
      } else if (!(op & O_UB)) a = b;
      if (a && !(op & O_UB)) {
        if (op & O_SGN)
          sg = lbit(m, &c, dec, &r->sg[MIN(b, 4)][MIN(ABS(f) >> 3, 15)][s1],
                    e < 0);
        else sg = (int) lraw(m, &c, dec, (u32) (e < 0), 1);
      }
      e = sg ? -a : a;
      if (fl) e = -e;
      if (rk) x = z->av[CLAMP(jP + e, 0, z->u - 1)];
      else x = CLAMP(P + e, -lim - 1, lim);
    }
    if (dst) dst[i] = (i16) (x + (dm ? ref[i] : 0));
    a = ABS(e);
    ef += (a * 32 - ef) >> 2;
    es += (a * 32 - es) >> 4;
    eu += (a * 32 - eu) >> 7;
    b1 = b;  s1 = e ? 1 + (e < 0) : 0;
    if (op & O_MM) { mput(z, x + (dm ? ref[i] : 0), op);  z->mhit = hit; }
    pr->pos = (pr->pos + HN - 1) & (HN - 1);
    pr->hx[pr->pos] = pr->hx[pr->pos + HN] = x;
    h = pr->hx + pr->pos;
    if (o->n) ols_upd(z, o, h, g);
    u = x - base;
    Fj(3, if (pr->f[j]) {
      nl_upd(pr->f[j], u, u - pn[j]);
      u -= pn[j];
    });
    if (z->c.gn) gls_upd(z, &pr->g, gu, x * 16));
  if (dec) { m->r.range = c.rg;  m->r.code = c.cd; }
  else { m->e.low = c.lo;  m->e.range = c.rg;  m->e.carry = c.cy; }
  z->ef = ef;  z->es = es;  z->eu = eu;  z->b1 = b1;  z->s1 = s1;
}

#define SX(O) do { if (m->dec) segx(m, z, rl, s, d, n, ref, rn, dm, O, 1); \
                   else segx(m, z, rl, s, d, n, ref, rn, dm, O, 0); } while (0)

#if S6_AVX2
void
#else
static void
#endif
SEG(mx * m, s6 * z, int rl, const i16 * s, i16 * d, sz n, const i16 * ref,
    sz rn, int dm) {
  switch (z->c.o) {
  case F1: SX(F1);  break;
  case F2: SX(F2);  break;
  default: SX(F4);
  }
}

#if !S6_AVX2
static void seg(mx * m, s6 * z, int rl, const i16 * s, i16 * d, sz n,
                const i16 * ref, sz rn, int dm) {
#if defined(HAVE_AVX2)
  if (z->avx) {
    s16_seg_avx2(m, z, rl, s, d, n, ref, rn, dm);
    return;
  }
#endif
  segc(m, z, rl, s, d, n, ref, rn, dm);
}

static int smode(mx * m, s6 * z, const i16 * src, const i16 * ref, sz n) {
  int md = 0;
  if (src) {
    i64 a = 0, b = 0;
    sz i;
    for (i = 1; i < n; i++) {
      int d = src[i] - src[i - 1], e = d - ref[i] + ref[i - 1];
      a += ABS(d);  b += ABS(e);
    }
    md = 1;
    Fi(n, if (src[i] != ref[i]) { md = 0;  break; });
    if (!md) md = 10 * b < 7 * a ? 2 : 0;
  }
  if (pbit(m, &z->r.sm[0], 30, md == 1)) return 1;
  return pbit(m, &z->r.sm[1], 30, md == 2) ? 2 : 0;
}

static void s6_smp(mx * m, s6 * z, const i16 * src, i16 * dst, sz nv,
                   sz fr) {
  sz o = 0;
  int c = 0;
  while (o < nv) {
    sz k = MIN(fr, nv - o), i;
    const i16 * ref = c && z->c.o & O_XCH ? (dst ? dst : src) + o - fr : NULL;
    int md = ref ? smode(m, z, src ? src + o : NULL, ref, k) : 0;
    if (md == 1) {
      if (dst) Fi(k, dst[o + i] = ref[i]);
    } else {
      if (z->c.o & O_RANK && k >= 256 && !md)
        s6_alpha(m, z, src ? src + o : NULL, k);
      else z->u = 0;
      seg(m, z, ref ? 1 : 0, src ? src + o : NULL, dst ? dst + o : NULL, k,
          ref, ref ? fr : 0, md == 2);
    }
    o += k;  c++;
  }
}

void s16_smp(mx * m, void * st, const i16 * src, i16 * dst, sz nv, sz fr) {
  s16t * t = st;
  if (!fr) fr = nv;
  if (!t->e) t->e = s6_new(t->cfg - IT_S16C0);
  s6_smp(m, t->e, src, dst, nv, fr);
}
#endif
