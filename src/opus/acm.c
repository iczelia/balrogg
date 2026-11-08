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

#include "opus/acm.h"
#include "opus/cwrs.h"

#define PB    21
#define PBIN  800
#define EQ    10
#define VN    260
#define VK    132
#define UN    CW_N
#define QN    264

enum {
  D_GEN, D_COARSE, D_FINE, D_FINAL, D_THST, D_THMONO, D_SPLIT, D_SIGN, D_N
};

#define SP_ALL 0x1FFFFFFu
#define SP_FS  0xB1BF57u
#define SP_9   0xB13214u
#define SP_7   0xA03214u

typedef struct { u32 eng;  u8 cap[D_N];  u32 spm; } opt;
#define OE(ty, e) ((u32) (e) << 2 * (ty))
static const opt OPT[] = {
  [0] = { 0, { 0 }, 0 }, [3] = { 0, { 0 }, SP_7 },
  [4] = { OE(D_GEN, ACM_ELITE) | OE(D_COARSE, ACM_ELITE),
          { 0, 0, 8, 0, 8, 8 }, SP_9 }
};
#define NOPT ((int) ARRAY_LEN(OPT))

static const int EB[PB + 1] = {
  0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 14, 16, 20, 24, 28, 34, 40, 48, 60, 78, 100
};

typedef struct {
  int LM, trans, intra, spread, trim, C, flen, cb, inten, dual, silence;
  int pf, pfo, pfp, pfg, pft, tfsel, nskip;
  int qi[2][PB], fv[2][PB], fn[2][PB], fz[2][PB];
  i32 E[2][PB], Eo[2][PB];
  int tfb[PB], tf[PB], dyn[PB];
  u8 dest[PB];
  int dynfs;
  short th[PB][5][64];
  signed char y[4][PBIN];
  u8 hy[4][PB];
  int kband[4][PB];
} fstate;

typedef struct {
  int b, role, W, base, N, K, B, LM, prole, pavail, tilt, bpb, dE, Eq, nsp;
  int sh[16];
  i32 conc;
  fstate * f, * p;
} pvs;

struct opa {
  acm * q;
  fstate F[4];
  int fi, lastfs, tfcur, lastsite[128], sitehist[128];
  i32 prev[2], Eold[2][PB];
  int lastsg, lastsgd, hist[8];
  u32 * U;
  i32 * LV;
  u32 X2[256];
  const opa_pri * ap;
  int xin, pri, ncmax;
  u32 spm;
};

#define CUR(m) (&(m)->F[(m)->fi & 3])
#define PRV(m, k) (&(m)->F[((m)->fi - (k)) & 3])
#define CX(h) (d->cx[d->ncx++] = (acm_h) (h))
#define CXP(k, h) do { if (m->spm >> ((k) - 1) & 1) CX(h); } while (0)

static int qb(int v, int lim) { return CLAMP(v, -lim, lim); }
static int lbkt(int v) { return v > 1 ? (int) blr_ilog((u32) v) - 1 : 0; }
static int fq(int64_t x, int64_t step, int lim) {
  return qb((int) blr_fdiv(2 * x + step, 2 * step), lim);
}

static i32 med3(const i32 * x) {
  i32 a = x[0], b = x[1], c = x[2];
  return a > b ? (b > c ? b : a > c ? c : a) : (a > c ? a : b > c ? c : b);
}

#define SWP(a, b) do { i32 t_ = a;  a = b;  b = t_; } while (0)

static i32 med5(const i32 * x) {
  i32 a = x[0], b = x[1], c = x[2], d = x[3], e = x[4];
  if (a > b) SWP(a, b);
  if (d > e) SWP(d, e);
  if (a > d) { SWP(a, d);  SWP(b, e); }
  if (b > c) SWP(b, c);
  if (b > d) { SWP(b, d);  SWP(c, e); }
  return MIN(c, d);
}

void opa_dynest(const i32 * L0, const i32 * L1, int C, int start, int end,
                int LM, u8 * est) {
  static const i32 NF[PB] = {
    -5321, -5255, -5107, -4971, -4880, -4786, -4673, -4604, -4564, -4474,
    -4396, -4314, -4246, -4127, -3988, -3894, -3797, -3745, -3626, -3391,
    -3105
  };
  const i32 * L[2];
  i32 f[2][PB], t;
  int i, j;
  memset(est, 0, PB);
  if (end > PB) end = PB;
  if (start < 0 || end - start < 1) return;
  L[0] = L0;  L[1] = C == 2 ? L1 : L0;
  Fj(C,
    const i32 * e = L[j];
    i32 * g = f[j];
    int last = start;
    g[start] = e[start];
    for (i = start + 1; i < end; i++) {
      if (e[i] > e[i - 1] + 128) last = i;
      g[i] = MIN(g[i - 1] + 384, e[i]);
    }
    for (i = last - 1; i >= start; i--)
      g[i] = MIN(g[i], MIN(g[i + 1] + 512, e[i]));
    if (end - start >= 5) {
      for (i = start + 2; i < end - 2; i++)
        g[i] = MAX(g[i], med5(e + i - 2) - 256);
      t = med3(e + start) - 256;
      g[start] = MAX(g[start], t);  g[start + 1] = MAX(g[start + 1], t);
      t = med3(e + end - 3) - 256;
      g[end - 2] = MAX(g[end - 2], t);  g[end - 1] = MAX(g[end - 1], t);
    }
    for (i = start; i < end; i++) g[i] = MAX(g[i], NF[i]));
  for (i = start; i < end; i++) {
    i32 x, w = (i32) (C * (EB[i + 1] - EB[i]) << LM);
    if (C == 2) {
      i32 a = MAX(f[1][i], f[0][i] - 1024), b = MAX(f[0][i], f[1][i] - 1024);
      x = (MAX(0, L[0][i] - b) + MAX(0, L[1][i] - a)) / 2;
    } else x = MAX(0, L[0][i] - f[0][i]);
    if (i < 8) x *= 2;
    if (i >= 12) x /= 2;
    x = MIN(x, 1024);
    x = (w < 6 ? x : w > 48 ? x * 8 : x * w / 6) >> 7;
    est[i] = (u8) MIN(x, 31);
  }
}

static i32 ilog2q(uint64_t v) {
  int e, i;
  uint64_t x;
  i32 r;
  if (!v) return 0;
  e = (int) blr_ilog64(v) - 1;
  x = e >= 31 ? v >> (e - 31) : v << (31 - e);
  r = (i32) e << 16;
  for (i = 15; i >= 0; i--) {
    x = (x * x) >> 31;
    if (x >= (uint64_t) 1 << 32) { x >>= 1;  r |= (i32) 1 << i; }
  }
  return r;
}

typedef struct { uint64_t m;  int e; } sfl;

static sfl sadd(sfl a, sfl b) {
  sfl r;
  if (!a.m) return b;
  if (!b.m) return a;
  if (a.e < b.e) { r = a;  a = b;  b = r; }
  r.e = a.e;
  r.m = a.e - b.e >= 63 ? a.m : a.m + (b.m >> (a.e - b.e));
  if (r.m >> 62) { r.m >>= 1;  r.e++; }
  return r;
}

static void tables(opa * m) {
  int i, k;
  sfl * V = xmalloc((sz) VN * VK * sizeof *V);
  m->U = cw_new();
  m->LV = xmalloc((sz) VN * VK * sizeof *m->LV);
  Fi(VN,
    Fk(VK,
      sfl v;
      if (!i || !k) { v.m = i || !k ? (uint64_t) 1 << 61 : 0;  v.e = 0; }
      else v = sadd(sadd(V[(sz) (i - 1) * VK + k], V[(sz) i * VK + k - 1]),
                    V[(sz) (i - 1) * VK + k - 1]);
      V[(sz) i * VK + k] = v;
      m->LV[(sz) i * VK + k] =
        v.m ? ilog2q(v.m) - ((i32) 61 << 16) + ((i32) v.e << 16)
            : INT32_MIN / 2));
  xfree(V);
  cw_x2(m->X2);
}

static i32 lv(const opa * m, int n, int k) {
  if (n < 0 || k < 0) return INT32_MIN / 2;
  if (n >= VN || k >= VK) return INT32_MIN / 2;
  return m->LV[(sz) n * VK + k];
}

static uint64_t xmass(const opa * m, i32 d) {
  uint64_t v;
  if (d >= (i32) 40 << 16) return 1;
  v = ((uint64_t) 1 << (40 - (d >> 16))) * m->X2[(d >> 8) & 255] >> 30;
  return v ? v : 1;
}

opa * opa_new(int flags) {
  opa * m = xmalloc(sizeof *m);
  int pt = OPA_PT(flags), i;
  FATAL_UNLESS(pt < NOPT && (!pt || OPT[pt].spm),
               "opus: no ACM engine assignment %d", pt);
  memset(m, 0, sizeof *m);
  m->q = acm_new(flags & OPA_LITE ? ACM_VLITE : ACM_OPUSG | ACM_W16);
  Fi(D_N,
    if (OPT[pt].eng >> 2 * i & 3 || OPT[pt].cap[i])
      acm_class(m->q, i, (int) (OPT[pt].eng >> 2 * i & 3), OPT[pt].cap[i]));
  m->xin = (flags & OPA_XIN) != 0;  m->pri = (flags & OPA_PRI) != 0;
  m->ncmax = flags & OPA_CAP ? 12 : 0;
  m->spm = OPT[pt].spm ? OPT[pt].spm : flags & OPA_FS ? SP_FS : SP_ALL;
  m->lastfs = -1;
  tables(m);
  return m;
}

void opa_free(opa * m) {
  if (!m) return;
  acm_free(m->q);  xfree(m->U);  xfree(m->LV);  xfree(m);
}

void opa_bind(opa * m, acm_coder c, void * ctx) { acm_bind(m->q, c, ctx); }

static void frame_sync(opa * m) {
  fstate * f;
  if (orec_fstart == m->lastfs) return;
  m->lastfs = orec_fstart;
  m->fi++;
  f = CUR(m);
  memset(f, 0, sizeof *f);
  f->C = orec_C;  f->flen = orec_flen;  f->LM = PRV(m, 1)->LM;
  memcpy(f->Eo, m->Eold, sizeof m->Eold);
  memcpy(f->E, m->Eold, sizeof m->Eold);
  m->prev[0] = m->prev[1] = 0;  m->tfcur = 0;
}

static int bandN(const fstate * f, int b) {
  return (EB[b + 1] - EB[b]) << f->LM;
}

static int mass(const fstate * f, int role, int b, int lo, int hi, int den) {
  int N = bandN(f, b), i, base = EB[b] << f->LM, s = 0;
  if (!f->hy[role][b]) return -1;
  lo = (2 * lo * N + den) / (2 * den);  hi = (2 * hi * N + den) / (2 * den);
  if (hi <= lo) hi = lo + 1;
  if (hi > N) hi = N;
  for (i = lo; i < hi; i++) s += ABS(f->y[role][base + i]);
  return s;
}

static int shareb(int l, int r) {
  if (l < 0 || r < 0) return 15;
  if (l + r <= 0) return 14;
  return (16 * l + (l + r)) / (2 * (l + r));
}

static int kb(int k) { return k < 8 ? k : 8 + lbkt(k); }

static i32 cpred(opa * m, int b, int c) {
  static const i32 PC[4] = { 29440, 26112, 21248, 16384 };
  i32 coef = CUR(m)->intra ? 0 : PC[orec_LM & 3];
  i32 o = MAX(-9 * (1 << EQ), m->Eold[c][b]);
  return (i32) ((int64_t) coef * o >> 15) + m->prev[c];
}

static void coarse_upd(opa * m, int b, int c, int qi) {
  static const i32 BC[4] = { 30147, 22282, 12124, 6554 };
  fstate * f = CUR(m);
  i32 beta = f->intra ? 4915 : BC[orec_LM & 3];
  i32 e = cpred(m, b, c) + (i32) qi * (1 << EQ);
  f->qi[c][b] = qi;
  f->E[c][b] = e;
  m->prev[c] += (i32) qi * (1 << EQ) -
                (i32) ((int64_t) beta * (i32) qi * (1 << EQ) >> 15);
  m->Eold[c][b] = e;
  if (f->C == 1) { f->E[1][b] = e;  m->Eold[1][b] = e; }
}

static void sel_set(acm_dec * d, acm_h a, acm_h b, acm_h c, acm_h e, acm_h g) {
  d->sel[0] = a;  d->sel[1] = b;  d->sel[2] = c;  d->sel[3] = e;
  d->sel[4] = g;  d->sel[5] = 0;  d->sel[6] = 0;  d->sel[7] = 0;
  d->xon = 0;
  d->apm[2] = d->ncx > 1 ? d->cx[1] : 0;
}

static u32 ratio16(uint64_t a, uint64_t b) {
  int sh = 0;
  while ((b >> sh) >= ((uint64_t) 1 << 40)) sh++;
  return b >> sh ? (u32) ((a >> sh) * 65536 / (b >> sh)) : 32768;
}

static u32 p16(uint64_t a, uint64_t t) {
  u32 p = ratio16(a, t);
  return CLAMP(p, 32, 65504);
}

static u32 cum_p1(const u32 * c, u32 lo, u32 s, u32 hi) {
  return p16(c[hi + 1] - c[s + 1], c[hi + 1] - c[lo]);
}

static const u32 * apcum(const opa * m, u32 n) {
  return m->ap && m->ap->kind == OPA_CUM && (u32) m->ap->n == n ? m->ap->cum
                                                                : NULL;
}

static u32 apbit(const opa * m) {
  const u32 * c = apcum(m, 2);
  return c ? cum_p1(c, 0, 0, 1)
           : m->ap && m->ap->kind == OPA_BIT ? p16(m->ap->p1, 65536) : 0;
}

static int xbit0(opa * m, int bit, u32 p1, acm_h node, u32 xi) {
  acm_dec * d = acm_d(m->q);
  if (m->ncmax && d->ncx > m->ncmax) d->ncx = m->ncmax;
  d->xon = xi != 0;
  if (xi) d->xin = acm_stretch((int) (xi >> 4));
  return acm_bit(m->q, bit, p1, node);
}

static int xbit(opa * m, int bit, u32 ps, acm_h node, u32 a1) {
  if (a1 && m->pri) return xbit0(m, bit, a1, node, 0);
  return xbit0(m, bit, ps, node, m->xin ? a1 : 0);
}

static u32 xqtree(opa * m, u32 v, const uint64_t * pmf, u32 n, acm_h base,
                  int dsel, int mid, const u32 * ac) {
  acm_dec * d = acm_d(m->q);
  uint64_t pu[QN];
  const uint64_t * pq = pmf;
  u32 lo = 0, hi = n - 1, dep = 0, i;
  acm_h path = 1;
  if (!m->xin && !m->pri) ac = NULL;
  if (!ac && !m->ncmax) return acm_qtree(m->q, v, pmf, n, base, dsel, mid);
  if (ac && m->pri && n <= QN) {
    Fi(n,
      pu[i] = pmf[i] ? (uint64_t) (ac[i + 1] - ac[i] ? ac[i + 1] - ac[i] : 1)
                     : 0);
    pq = pu;
  }
  while (lo < hi && !pq[lo]) lo++;
  while (hi > lo && !pq[hi]) hi--;
  while (lo < hi) {
    uint64_t tot = 0, acc = 0, left = 0, best = ~(uint64_t) 0;
    u32 s = lo, p1, xi = 0;
    int bit;
    for (i = lo; i <= hi; i++) tot += pq[i];
    if (mid) {
      s = lo + (hi - lo) / 2;
      for (i = lo; i <= s; i++) left += pq[i];
    } else
      for (i = lo; i < hi; i++) {
        uint64_t dd;
        acc += pq[i];
        dd = acc * 2 > tot ? acc * 2 - tot : tot - acc * 2;
        if (dd < best) { best = dd;  s = i;  left = acc; }
      }
    p1 = ratio16(tot - left, tot);
    if (m->xin && ac) xi = cum_p1(ac, lo, s, hi);
    if (dsel >= 0) d->sel[dsel] = PH2(base, MIN(dep, 7));
    d->apm[0] = PH2(base, path);
    bit = xbit0(m, v > s, p1, PH2(base, path), xi);
    if (bit) lo = s + 1; else hi = s;
    while (lo < hi && !pq[lo]) lo++;
    while (hi > lo && !pq[hi]) hi--;
    path = PH2(path, bit);  dep++;
  }
  return lo;
}

static u32 xutree(opa * m, u32 v, u32 n, acm_h base, const u32 * ac) {
  acm_dec * d = acm_d(m->q);
  u32 lo = 0, hi = n;
  acm_h path = 1;
  if (!m->xin && !m->pri) ac = NULL;
  if (!ac && !m->ncmax) return acm_utree(m->q, v, n, base);
  while (hi - lo > 1) {
    u32 mid = lo + (hi - lo) / 2;
    int bit;
    d->apm[0] = PH2(base, path);
    bit = xbit(m, v >= mid, (u32) ((uint64_t) (hi - mid) * 65536 / (hi - lo)),
               PH2(base, path), ac ? cum_p1(ac, lo, mid - 1, hi - 1) : 0);
    if (bit) lo = mid; else hi = mid;
    path = PH2(path, bit);
  }
  return lo;
}

static u32 lap_nz(const u32 * c, int n) {
  int W = (n - 2) / 2;
  return p16(c[n] - (c[W + 1] - c[W]), c[n]);
}
static u32 lap_neg(const u32 * c, int n) {
  int W = (n - 2) / 2;
  uint64_t e = (c[n] - c[n - 1]) / 2, ng = c[W] + e;
  return p16(ng, ng + (c[2 * W + 1] - c[W + 1]) + e);
}
static u32 lap_gt(const u32 * c, int n, int g) {
  int W = (n - 2) / 2;
  uint64_t rem, at;
  if (g > W) return 0;
  rem = c[n] - (c[W + g] - c[W - g + 1]);
  at = (c[W + g + 1] - c[W + g]) + (c[W - g + 1] - c[W - g]);
  return p16(rem - at, rem);
}

static int dyn_est(fstate * f, int b) {
  if (f->dynfs != orec_fstart) {
    i32 L[2][PB];
    int i, j;
    Fj(2, Fi(PB, L[j][i] = (i32) blr_fdiv(f->E[j][i], 1 << (EQ - 8))));
    opa_dynest(L[0], L[1], f->C == 2 ? 2 : 1, orec_bstart, orec_bend, orec_LM,
               f->dest);
    f->dynfs = orec_fstart;
  }
  return f->dest[b];
}

static void cx_generic(opa * m, acm_dec * d, int site, int b, int c) {
  fstate * f = CUR(m), * p = PRV(m, 1);
  int pv, dyne = 0;
  (void) c;
  switch (site) {
  case OREC_S_CELT_TF: pv = p->tfb[b] + 2 * p->tf[b];  break;
  case OREC_S_CELT_DYNALLOC: pv = p->dyn[b];  break;
  case OREC_S_CELT_SPREAD: pv = p->spread;  break;
  case OREC_S_CELT_TRIM: pv = p->trim;  break;
  case OREC_S_CELT_TRANSIENT: pv = p->trans + 2 * PRV(m, 2)->trans;  break;
  case OREC_S_CELT_INTRA: pv = p->intra;  break;
  case OREC_S_CELT_INTENSITY: pv = p->inten;  break;
  case OREC_S_CELT_DUAL: pv = p->dual;  break;
  case OREC_S_CELT_PF_FLAG: pv = p->pf;  break;
  case OREC_S_CELT_PF_OCTAVE: pv = p->pfo;  break;
  case OREC_S_CELT_PF_PITCH: pv = p->pfp;  break;
  case OREC_S_CELT_PF_GAIN: pv = p->pfg;  break;
  case OREC_S_CELT_PF_TAPSET: pv = p->pft;  break;
  case OREC_S_CELT_SKIP: pv = p->cb * 4 + (b >= p->cb);  break;
  default: pv = m->lastsite[site & 127];  break;
  }
  d->ncx = 0;
  CX(PH2(site, 1));
  CX(PH3(site, b, c));
  CX(PH4(site, b, pv, 2));
  CX(PH3(site, m->sitehist[site & 127] & 0xFF, 3));
  CX(PH5(site, b, orec_LM, f->trans, 4));
  CX(PH5(site, b, lbkt(f->flen), f->spread, 5));
  CX(PH5(site, pv, m->sitehist[site & 127] & 0xF, b, 6));
  if (site == OREC_S_CELT_TF) {
    CX(PH5(site, b, m->tfcur, f->trans, 7));
    CX(PH6(site, b, m->tfcur, p->tf[b], f->trans, 8));
    CX(PH5(site, b, fq(f->E[0][b] - f->Eo[0][b], 1 << EQ, 4), f->trans, 9));
  } else if (site == OREC_S_CELT_DYNALLOC) {
    CX(PH5(site, b, f->dyn[b], p->dyn[b], 7));
    CX(PH5(site, b, b > 0 ? f->dyn[b - 1] : 9, f->dyn[b], 8));
    CX(PH5(site, b, fq(f->E[0][b] - (b ? f->E[0][b - 1] : 0), 1 << EQ, 4),
           f->dyn[b], 9));
    CX(PH5(site, b, fq(f->E[0][b] - f->Eo[0][b], 1 << EQ, 4), f->dyn[b], 10));
    if (f->flen < OPA_DYNMIN(orec_LM)) dyne = 10;
    else {
      dyne = dyn_est(f, b) - 2 * f->dyn[b];
      dyne = CLAMP(dyne, -2, 6) + 3;
    }
    CX(PH5(site, dyne, f->trans, orec_LM, 11));
    CX(PH5(site, b, dyne, p->dyn[b], 12));
  } else if (site == OREC_S_CELT_SKIP) {
    CX(PH4(site, b, f->nskip, 7));
    CX(PH5(site, b, p->cb, lbkt(f->flen), 8));
  } else if (site == OREC_S_CELT_TRIM || site == OREC_S_CELT_SPREAD) {
    CX(PH5(site, p->trim, p->spread, f->trans, 7));
    CX(PH5(site, fq(f->E[0][PB - 1] - f->E[0][5], 2 << EQ, 6), f->trans,
           PRV(m, 2)->trim, 8));
  } else if (site == OREC_S_CELT_INTENSITY || site == OREC_S_CELT_DUAL) {
    CX(PH5(site, p->inten, f->cb, p->dual, 7));
  }
  sel_set(d, PH2(site, 1), PH2(site, b),
          dyne ? PH4(site, pv, dyne, 2) : PH3(site, pv, 2),
          PH3(site, m->sitehist[site & 127] & 0xF, 3), PH3(site, f->trans, 4));
  d->apm[1] = PH3(site, b, pv);
}

#define LPW 40
static void lap_pmf(unsigned fs0, int decay, uint64_t * pm) {
  unsigned fs;
  int k;
  pm[0] = fs0;
  fs = ((32768 - 2 * 16 - fs0) * (u32) (16384 - decay) >> 15) + 1;
  for (k = 1; k <= LPW; k++) {
    pm[k] = fs;
    if (fs > 1) fs = (((2 * fs - 2) * (u32) decay) >> 15) + 1;
  }
}

static int code_coarse(opa * m, oprec * op, int b, int c, int dec) {
  acm_dec * d = acm_d(m->q);
  fstate * f = CUR(m), * p = PRV(m, 1), * p2 = PRV(m, 2);
  uint64_t pm[LPW + 1], tot;
  int v = dec ? 0 : op->v, mag, j = 0, bit, an = 0;
  const u32 * ac = NULL;
  i32 pred = cpred(m, b, c);
  int r0 = fq(f->Eo[c][b] - pred, 1 << EQ, 6);
  int r1 = b ? fq((int64_t) f->E[c][b - 1] - f->Eo[c][b - 1] + f->Eo[c][b] -
                  pred, 1 << EQ, 6) : 9;
  int pq0 = qb(p->qi[c][b], 4), pb1 = b ? qb(f->qi[c][b - 1], 4) : 9;
  int pb2 = b > 1 ? qb(f->qi[c][b - 2], 4) : 9;
  int oc = c ? qb(f->qi[0][b], 4) : 9;
  int eo = fq(f->Eo[c][b], 2 << EQ, 10);
  int lr = c ? fq((int64_t) f->E[0][b] - f->Eo[1][b], 1 << EQ, 6) : 9;
  d->ty = D_COARSE;
  if (op->kind != OP_LAPLACE) {
    static const uint64_t SP[3] = { 2, 1, 1 };
    d->ncx = 0;
    CX(PH3(op->site, b, 1));
    CX(PH4(op->site, r0, f->intra, 2));
    CX(PH4(op->site, b, pq0, 3));
    CX(PH4(op->site, oc, r0, 4));
    sel_set(d, PH2(op->site, 1), PH2(op->site, r0), PH2(op->site, b),
            PH2(op->site, oc), 1);
    d->apm[1] = PH3(op->site, r0, b);
    if (op->kind == OP_LOGP) return xbit(m, v, 32768, 0x51, apbit(m));
    return (int) xqtree(m, (u32) v, SP, 3, 0x52, 5, 0, apcum(m, 3));
  }
  lap_pmf(op->aux, (int) op->ftb, pm);
  if (m->ap && m->ap->kind == OPA_CUM && m->ap->n >= 4 && !(m->ap->n & 1)) {
    ac = m->ap->cum;  an = m->ap->n;
  }
  d->ncx = 0;
  CX(PH5(b, c, f->intra, orec_LM, 1));
  CX(PH6(b, c, f->intra, orec_LM, r0, 2));
  CX(PH4(r0, r1, f->intra, 3));
  CX(PH5(b, pq0, pb1, c, 4));
  CX(PH5(c, oc, r0, lr, 5));
  CX(PH5(r0, eo, f->intra, b > 10, 6));
  CX(PH5(pb1, pb2, b > 2 ? qb(f->qi[c][b - 3], 4) : 9, c, 7));
  CX(PH6(b, r0, pq0, oc, f->intra, 8));
  CX(PH5(lbkt(f->flen), f->trans, orec_LM, b >> 2, 9));
  CX(PH5(b, pq0, qb(p2->qi[c][b], 4), r0, 10));
  CX(PH5(b, oc, c ? qb(p->qi[1][b] - p->qi[0][b], 3) : 9, r0, 11));
  CX(PH4(r0, r1, pb1, 12));
  CX(PH5(b, fq((int64_t) f->Eo[c][b] - p->Eo[c][b], 1 << EQ, 5), r0, f->intra,
         13));
  CX(PH6(b, c, b < PB - 1 ? fq((int64_t) f->Eo[c][b + 1] - f->Eo[c][b],
                               1 << EQ, 5) : 9, r0, pb1, 14));
  sel_set(d, PH3(b, c, f->intra), PH2(r0, r1), PH3(pb1, oc, f->intra),
          PH2(pq0, b), PH2(lr, r0));
  d->apm[1] = PH4(b, r0, f->intra, oc);
  tot = pm[0];
  for (j = 1; j <= LPW; j++) tot += 2 * pm[j];
  bit = xbit(m, v != 0, ratio16(tot - pm[0], tot), 0x100,
             ac ? lap_nz(ac, an) : 0);
  if (!bit) return 0;
  { int sg = xbit(m, v < 0, 32768, 0x101, ac ? lap_neg(ac, an) : 0);
    mag = 1;  tot = (tot - pm[0]) / 2;
    if (!dec) j = ABS(v);
    for (;;) {
      if (mag >= 24) break;
      bit = xbit(m, dec ? 0 : j > mag, ratio16(tot - pm[mag], tot),
                 (acm_h) (0x110 + mag), ac ? lap_gt(ac, an, mag) : 0);
      if (!bit) break;
      tot -= pm[mag];  mag++;
    }
    if (mag >= 24)
      mag = 24 + (int) xutree(m, dec ? 0 : (u32) (j - 24), 1 << 12, 0x1F0, NULL);
    return sg ? -mag : mag;
  }
}

static int code_fine(opa * m, oprec * op, int b, int c, int dec) {
  acm_dec * d = acm_d(m->q);
  fstate * f = CUR(m), * p = PRV(m, 1);
  int nb = (int) op->ftb, v = dec ? 0 : op->v;
  int64_t frac = (int64_t) f->Eo[c][b] - f->E[c][b] + (1 << (EQ - 1));
  int imp = (int) blr_fdiv(frac * (1 << nb), 1 << EQ);
  int dlt = fq((int64_t) f->Eo[c][b] - f->E[c][b], 1 << (EQ - 2), 6);
  int pfv = p->fn[c][b] == nb ? p->fv[c][b] : -1;
  int ofv = c && f->fn[0][b] == nb ? f->fv[0][b] : -1;
  int bfv = b && f->fn[c][b - 1] ? f->fv[c][b - 1] * 8 / (1 << f->fn[c][b - 1])
                                 : -1;
  int sub = fq(frac * (1 << nb) - (int64_t) imp * (1 << EQ) - (1 << (EQ - 1)),
               1 << (EQ - 2), 2);
  imp = qb(imp, 1 << nb);
  d->ty = D_FINE;  d->ncx = 0;
  CX(PH4(b, c, nb, 1));
  CX(PH4(nb, imp, dlt, 2));
  CX(PH5(b, nb, imp, c, 3));
  CX(PH4(nb, pfv, imp, 4));
  CX(PH5(nb, ofv, imp, c, 5));
  CX(PH5(nb, qb(f->qi[c][b], 3), imp, 0, 6));
  CX(PH5(nb, bfv, imp, 0, 7));
  CX(PH5(nb, ofv, c ? fq((int64_t) f->E[0][b] - f->E[1][b], 1 << (EQ - 1), 8)
                    : 99, 0, 8));
  CX(PH5(nb, imp, sub, dlt, 9));
  CX(PH6(b, nb, pfv, ofv, imp, 10));
  sel_set(d, PH2(nb, imp), PH3(b, nb, c), PH3(nb, ofv, 1), PH3(nb, pfv, 2),
          PH3(nb, dlt, 3));
  d->apm[1] = PH4(nb, imp, ofv, dlt);
  return (int) xutree(m, (u32) v, 1U << nb, PH2(0xF1, nb), apcum(m, 1U << nb));
}

static int code_final(opa * m, oprec * op, int b, int c, int dec) {
  acm_dec * d = acm_d(m->q);
  fstate * f = CUR(m), * p = PRV(m, 1);
  int v = dec ? 0 : op->v, fqb = orec_fq[b];
  int dq = fq(((int64_t) f->Eo[c][b] - f->E[c][b]) * (2 << fqb), 1 << (EQ - 2), 8);
  int ov = c ? f->fz[0][b] : 9;
  d->ty = D_FINAL;  d->ncx = 0;
  CX(PH3(b, c, 1));
  CX(PH3(dq, fqb, 2));
  CX(PH4(b, dq, c, 3));
  CX(PH4(ov, dq, fqb, 4));
  CX(PH4(p->fz[c][b], dq, b, 5));
  CX(PH4(orec_fpri[b], fqb, dq, 6));
  CX(PH4(f->fn[c][b], f->fn[c][b] ? f->fv[c][b] & 7 : 0, fqb, 7));
  CX(PH5(b, f->fn[c][b], f->fn[c][b] ? f->fv[c][b] & 7 : 0, c, 8));
  CX(PH5(dq, f->fn[c][b], f->fn[c][b] ? f->fv[c][b] & 3 : 0, ov, 9));
  sel_set(d, PH2(dq, fqb), PH2(b, c), PH2(ov, dq), PH2(orec_fpri[b], fqb), 7);
  d->apm[1] = PH3(dq, ov, fqb);
  return xbit(m, v, 32768, 0xF7, apbit(m));
}

static int heap(int dep, int off, int W) {
  int w = W >> dep;
  if (dep > 5 || w <= 0) return 63;
  return (1 << dep) + off / w;
}

static int code_theta(opa * m, oprec * op, int b, int dec) {
  acm_dec * d = acm_d(m->q);
  fstate * f = CUR(m), * p = PRV(m, 1), * p2 = PRV(m, 2);
  int qn = (int) op->nsym - 1, mode = (int) op->ftb, N = (int) op->aux;
  int v = dec ? 0 : op->v, i;
  int st = orec_thst, role = st ? 4 : orec_pvqrole & 3, W = bandN(f, b);
  int hp = st ? 0 : heap(orec_pdep, orec_pvqoff, W);
  int pth = p->th[b][role][hp] - 1, pth2 = p2->th[b][role][hp] - 1;
  int nth = b ? f->th[b - 1][role][st ? 0 : 1] - 1 : -1;
  int part = !st && role < 4 ? f->th[b][role ^ 1][hp] - 1 : -1;
  int pq_ = pth < 0 ? -1 : (pth * qn + 8192) / 16384;
  int pq2 = pth2 < 0 ? -1 : (pth2 * qn + 8192) / 16384;
  int nq = nth < 0 ? -1 : (nth * qn + 8192) / 16384;
  int acm_ = part < 0 ? -1 : (part * qn + 8192) / 16384;
  int tilt = b > 0 && b < PB - 1
           ? fq((int64_t) f->E[0][b + 1] - f->E[0][b - 1], 1 << (EQ - 1), 6)
           : 9;
  int lrd = f->C == 2 ? fq((int64_t) f->E[0][b] - f->E[1][b], 1 << (EQ - 1), 8)
                      : 99;
  int bpb = lbkt(orec_pb / (N > 0 ? N : 1));
  int dE = fq((int64_t) f->E[0][b] - f->Eo[0][b], 1 << (EQ - 1), 6);
  int ps, os;
  uint64_t pmf[260];
  qn = CLAMP(qn, 0, 258);
  for (i = 0; i <= qn; i++)
    pmf[i] = mode == 0 ? (i <= qn / 2 ? 3 : 1)
           : mode == 2 ? (uint64_t) (i <= qn / 2 ? i + 1 : qn + 1 - i) : 1;
  d->ty = st ? D_THST : D_THMONO;  d->ncx = 0;
  CX(PH4(st, mode, qn, 1));
  CX(PH5(st, qn, b, orec_pdep, 2));
  CX(PH4(qn, pq_, st, 3));
  CX(PH5(qn, pq_, pq2, st, 4));
  CX(PH4(qn, nq, st, 5));
  CX(PH5(qn, acm_, role, st, 6));
  CX(PH5(qn, tilt, orec_pB, f->trans, 7));
  CX(PH5(qn, lrd, st, b, 8));
  CX(PH5(qn, bpb, N, st, 9));
  CX(PH6(qn, pq_, b, st, orec_pdep, 10));
  CX(PH5(qn, dE, pq_, st, 11));
  CX(PH5(qn, orec_tfc, orec_pB, orec_pdep, 12));
  CX(PH6(qn, nq, pq_, lrd, st, 13));
  CX(PH5(qn, hp, role, b, 14));
  if (st) {
    ps = shareb(mass(p, 1, b, 0, 1, 1), mass(p, 0, b, 0, 1, 1));
    os = shareb(mass(p2, 1, b, 0, 1, 1), mass(p2, 0, b, 0, 1, 1));
  } else {
    int r0 = orec_pvqoff, r1 = orec_pvqoff + N, r2 = orec_pvqoff + 2 * N;
    ps = shareb(mass(p, role, b, r0, r1, W), mass(p, role, b, r1, r2, W)) +
         (p->LM != f->LM) * 16;
    os = f->hy[role ^ 1][b] ? shareb(mass(f, role ^ 1, b, r0, r1, W),
                                     mass(f, role ^ 1, b, r1, r2, W)) : 15;
  }
  CX(PH5(qn, ps, st, orec_pdep, 15));
  CX(PH5(qn, os, st, ps, 16));
  CX(PH6(qn, ps, pq_, st, f->trans, 17));
  if (!st) {
    int r0 = orec_pvqoff, r1 = orec_pvqoff + N, r2 = orec_pvqoff + 2 * N;
    int a = mass(p, role, b, r0, r1, W), c2 = mass(p2, role, b, r0, r1, W);
    int e = mass(p, role, b, r1, r2, W), e2 = mass(p2, role, b, r1, r2, W);
    CX(PH5(qn, shareb(MAX(a, 0) + MAX(c2, 0), MAX(e, 0) + MAX(e2, 0)),
           orec_pdep, orec_pB, 18));
  }
  sel_set(d, PH3(st, qn, orec_pdep), PH3(qn, pq_, 1), PH3(qn, nq, 2),
          PH3(b, st, 3), PH3(qn, lrd, tilt));
  d->apm[1] = PH5(qn, pq_, ps, st, 0x33);
  d->sel[6] = PH3(qn, st, mode);
  v = (int) xqtree(m, (u32) v, pmf, (u32) qn + 1, PH3(0x7E, st, qn), 5, !st,
                   apcum(m, (u32) qn + 1));
  f->th[b][role][hp] = (short) (1 + v * 16384 / (qn > 0 ? qn : 1));
  return v;
}

static int ysg(const fstate * f, int role, int b, int j) {
  int v;
  if (!f->hy[role][b] || j < 0 || j >= bandN(f, b)) return 3;
  v = f->y[role][(EB[b] << f->LM) + j];
  return v > 0 ? 1 : v < 0 ? 2 : 0;
}

static int code_sign(opa * m, pvs * g, int pos, int mg, int neg) {
  acm_dec * d = acm_d(m->q);
  int pps = g->p->LM == g->LM ? ysg(g->p, g->role, g->b, pos) : 3;
  int ops = g->pavail ? ysg(g->f, g->prole, g->b, pos) : 3;
  int dist = lbkt(pos - m->lastsgd), ob = g->B > 1 ? pos % g->B : 0;
  d->ty = D_SIGN;  d->ncx = 0;
  CX(PH2(0x5A, 1));
  CX(PH4(m->lastsg, dist, MIN(2, mg), 2));
  CX(PH4(pps, mg > 1, m->lastsg, 3));
  CX(PH4(ops, g->role, mg > 1, 4));
  CX(PH5(g->b, pos & 1, ob, g->B, 5));
  CX(PH5(pps, ops, m->lastsg, g->role, 6));
  CX(PH5(m->lastsg, dist, g->B, kb(g->K), 7));
  sel_set(d, PH2(m->lastsg, dist), PH2(pps, ops), PH2(g->b, g->role),
          PH2(g->B, mg > 1), 9);
  d->apm[1] = PH3(pps, ops, m->lastsg);
  d->apm[0] = 0x5A;
  CX(PH6(MIN(3, mg), m->lastsg, (pos - g->base) * 4 / g->N, g->B, orec_spread,
         8));
  CX(PH6(pps, g->p->LM == g->LM ? ysg(g->p, g->role, g->b, pos - 1) * 4 +
                                  ysg(g->p, g->role, g->b, pos + 1) : 99,
         m->lastsg, 0, 0, 9));
  neg = xbit(m, neg, 32768, 0x5A, 0);
  m->lastsg = neg ? 2 : 1;  m->lastsgd = pos;
  return neg;
}

static void pv_tree(opa * m, pvs * g, int off, int n, int k, int dep, int par,
                    int sk, int bp, int * y, int dec) {
  acm_dec * d = acm_d(m->q);
  int n1, n2, k1 = 0, i;
  uint64_t pmf[VK + 1];
  i32 lg[VK + 1], mx = INT32_MIN;
  if (k == 0) { Fi(n, y[off + i] = 0);  return; }
  if (n == 1) {
    int neg = code_sign(m, g, g->base + off, k, dec ? 0 : y[off] < 0);
    y[off] = neg ? -k : k;
    return;
  }
  n1 = n >> 1;  n2 = n - n1;
  FATAL_IF_HOT(k >= VK || n >= VN)
    ("opus: PVQ geometry %d/%d beyond the model's tables", n, k);
  for (i = 0; i <= k; i++) {
    lg[i] = lv(m, n1, i) + lv(m, n2, k - i);
    if (lg[i] > mx) mx = lg[i];
  }
  for (i = 0; i <= k; i++) pmf[i] = xmass(m, mx - lg[i]);
  if (!dec) Fi(n1, k1 += ABS(y[off + i]));
  {
    int lo = g->base + off, md = lo + n1, hi = lo + n, W = g->W;
    int ps = shareb(mass(g->p, g->role, g->b, lo, md, W),
                    mass(g->p, g->role, g->b, md, hi, W)) +
             (g->p->LM == g->LM ? 0 : 16);
    fstate * p2 = PRV(m, 2);
    int ps2 = shareb(mass(p2, g->role, g->b, lo, md, W),
                     mass(p2, g->role, g->b, md, hi, W));
    int os = g->pavail ? shareb(mass(g->f, g->prole, g->b, lo, md, W),
                                mass(g->f, g->prole, g->b, md, hi, W)) : 15;
    int kbk = kb(k), nb = lbkt(n), rp = lo * 8 / W, dp = MIN(6, dep);
    int pk = par < 0 ? 15 : par;
    int lh = m->hist[dp];
    acm_h vh = 0;
    d->ty = D_SPLIT;  d->ncx = 0;
    CXP(1, PH4(n1, n2, k, 1));
    CXP(2, PH6(n1, n2, k, g->b, g->LM, 2));
    CXP(3, PH6(nb, kbk, dp, rp, g->B, 3));
    CXP(4, PH5(n1, n2, k, ps, 4));
    CXP(5, PH5(n1, n2, k, os, 5));
    CXP(6, PH5(nb, kbk, ps, os, 6));
    CXP(7, PH5(n1, n2, k, g->B > 1 ? 99 : g->tilt, 7));
    CXP(8, PH5(n1, n2, k, pk, 8));
    CXP(9, PH5(nb, kbk, lh, dp, 9));
    CXP(10, PH6(nb, kbk, ps, ps2, g->role, 10));
    CXP(11, PH6(n1, n2, k, g->bpb, orec_tfc, 11));
    CXP(12, PH6(nb, kbk, g->dE, g->Eq, dp, 12));
    CXP(13, PH6(n1, n2, k, g->b, g->role, 13));
    CXP(14, PH6(nb, kbk, rp, g->b, ps, 14));
    CXP(15, PH6(n1, n2, k, os, ps, 15));
    CXP(16, PH5(nb, kbk, orec_spread, g->B, 16));
    if (g->p->LM == g->LM && g->p->hy[g->role][g->b]) {
      int base = (EB[g->b] << g->LM) + lo;
      vh = 1;
      for (i = 0; i < n && base + i < PBIN; i++)
        vh = PH2(vh, g->p->y[g->role][base + i]);
    }
    CXP(17, PH5(n1, n2, k, vh, 17));
    CXP(18, PH5(n1, n2, k, sk < 0 ? 99 : MIN(15, sk), 18));
    CXP(19, PH6(nb, kbk, k * 8 / g->K, dp, ps, 19));
    CXP(20, PH6(nb, kbk, qb(g->K - g->p->kband[g->role][g->b], 4), ps,
                g->p->hy[g->role][g->b], 20));
    CXP(21, PH6(n1, n2, k, pk, sk < 0 ? 99 : MIN(7, sk), 21));
    { int gp = dep >= 2 && dep < 16 ? g->sh[dep - 2] : 15;
      int cc = g->nsp ? (int) (8 * g->conc / g->nsp >> 12) : 9;
      int a = mass(g->p, g->role, g->b, lo, md, W);
      int a2 = mass(p2, g->role, g->b, lo, md, W);
      int e = mass(g->p, g->role, g->b, md, hi, W);
      int e2 = mass(p2, g->role, g->b, md, hi, W);
      int p12 = shareb(a < 0 ? a : a + MAX(a2, 0), e < 0 ? e : e + MAX(e2, 0));
      CXP(22, PH6(nb, kbk, pk, gp, dp, 22));
      CXP(23, PH6(nb, kbk, cc, pk, dp, 23));
      CXP(24, PH5(n1, n2, k, p12, 24));
      CXP(25, PH6(n1, n2, k, cc, pk, 25)); }
    sel_set(d, PH3(dp, kbk, nb), PH3(ps, kbk, 1), PH3(os, kbk, 2),
            PH3(g->b, dp, 3), PH3(pk, lh, 4));
    d->apm[1] = PH5(n1, n2, k, ps, os);
    d->sel[6] = PH3(n1, n2, k);
    { const u32 * ac = m->ap && m->ap->split
                     ? m->ap->split(dep, off, n, k, bp) : NULL;
      k1 = (int) xqtree(m, (u32) k1, pmf, (u32) k + 1, PH4(0xB1, n1, n2, k), 5,
                        0, ac);
      if (ac) m->ap->splitk(k1); }
    { int sb = (16 * k1 + k) / (2 * k);
      int pr = k1 * 4 * n > k * n1 * 5 ? 2 : k1 * 5 * n < k * n1 * 4 ? 0 : 1;
      if (k >= 2) {
        g->conc += (i32) ((int64_t) (k1 * n > n1 * k ? k1 * n - n1 * k
                                                     : n1 * k - k1 * n) *
                          4096 / (k * n));
        g->nsp++;
      }
      if (dep < 16) g->sh[dep] = sb;
      m->hist[dp] = sb;
      pv_tree(m, g, off, n1, k1, dep + 1, sb, -1, 1 + pr, y, dec);
      pv_tree(m, g, off + n1, n2, k - k1, dep + 1, sb, k1, 3 - pr, y, dec); }
  }
}

static void code_pvq(opa * m, oprec * op, int b, int dec) {
  int y[UN + 8];
  pvs g;
  fstate * f = CUR(m);
  int N = orec_pvqN, K = orec_pvqK, i;
  FATAL_UNLESS(N >= 1 && N <= UN && K >= 1 && K <= UN &&
               op->nsym == cw_V(m->U, N, K),
               "opus: unexpected PVQ geometry N=%d K=%d", N, K);
  memset(&g, 0, sizeof g);
  g.b = b;  g.role = orec_pvqrole & 3;  g.f = f;  g.p = PRV(m, 1);
  g.LM = orec_LM;  f->LM = orec_LM;
  g.W = bandN(f, b);  g.base = orec_pvqoff;  g.N = N;  g.K = K;  g.B = orec_pB;
  g.prole = g.role ^ 1;  g.pavail = f->hy[g.prole][b];
  g.tilt = b > 0 && b < PB - 1
         ? fq((int64_t) f->E[0][b + 1] - f->E[0][b - 1], 1 << (EQ - 1), 6) : 9;
  g.bpb = lbkt(orec_pb / N);
  g.dE = fq((int64_t) f->E[g.role & 1][b] - f->Eo[g.role & 1][b],
            1 << (EQ - 1), 6);
  g.Eq = fq(f->E[g.role & 1][b], 2 << EQ, 10);
  if (!dec) cw_dec(m->U, N, K, (u32) op->v, y);
  m->lastsg = 0;  m->lastsgd = g.base;
  pv_tree(m, &g, 0, N, K, 0, -1, -1, 0, y, dec);
  if (dec) op->v = (opus_int32) cw_enc(m->U, N, K, y);
  for (i = 0; i < N && g.base + i < g.W && (EB[b] << g.LM) + g.base + i < PBIN;
       i++) {
    int v = y[i];
    f->y[g.role][(EB[b] << g.LM) + g.base + i] =
      (signed char) CLAMP(v, -127, 127);
  }
  f->hy[g.role][b] = 1;  f->kband[g.role][b] += K;
}

static void site_note(opa * m, int site, int v) {
  m->lastsite[site & 127] = v;
  m->sitehist[site & 127] = (int) ((unsigned) m->sitehist[site & 127] << 4 |
                                   (unsigned) (v & 15));
}

void opa_op(opa * m, oprec * op, int dec, const opa_pri * ap) {
  acm_dec * d = acm_d(m->q);
  int b = CLAMP(orec_band, 0, PB - 1);
  int c = orec_ch & 1, v = 0;
  fstate * f;
  m->ap = ap;
  frame_sync(m);
  f = CUR(m);
  memset(d->sel, 0, sizeof d->sel);
  d->apm[0] = 0;  d->xon = 0;
  switch (op->site) {
  case OREC_S_CELT_COARSE: case OREC_S_CELT_COARSE_SMALL:
  case OREC_S_CELT_COARSE_BIT:
    f->LM = orec_LM;
    v = code_coarse(m, op, b, c, dec);
    if (op->kind == OP_ICDF) coarse_upd(m, b, c, (v >> 1) ^ -(v & 1));
    else if (op->kind == OP_LOGP) coarse_upd(m, b, c, -v);
    else coarse_upd(m, b, c, v);
    break;
  case OREC_S_CELT_FINE:
    v = code_fine(m, op, b, c, dec);
    f->E[c][b] += (((2 * v + 1) << EQ) >> (op->ftb + 1)) - (1 << (EQ - 1));
    m->Eold[c][b] = f->E[c][b];
    if (f->C == 1) { f->E[1][b] = f->E[0][b];  m->Eold[1][b] = f->E[0][b]; }
    f->fv[c][b] = v;  f->fn[c][b] = (int) op->ftb;
    break;
  case OREC_S_CELT_FINALISE:
    v = code_final(m, op, b, c, dec);
    f->E[c][b] += (i32) blr_fdiv((2 * v - 1) * (1 << EQ),
                             (int64_t) 1 << (orec_fq[b] + 2));
    m->Eold[c][b] = f->E[c][b];
    if (f->C == 1) { f->E[1][b] = f->E[0][b];  m->Eold[1][b] = f->E[0][b]; }
    f->fz[c][b] = 1 + v;
    break;
  case OREC_S_CELT_THETA:
    v = code_theta(m, op, b, dec);
    break;
  case OREC_S_CELT_PVQ:
    code_pvq(m, op, b, dec);
    orec_pvqK = 0;
    return;
  default:
    d->ty = D_GEN;
    cx_generic(m, d, op->site, b, c);
    if (op->kind == OP_LOGP) {
      v = xbit(m, dec ? 0 : op->v, op->ftb < 16 ? 65536u >> op->ftb : 1,
               PH2(0x10, op->site), apbit(m));
    } else if (op->kind == OP_ICDF || op->kind == OP_ICDF16) {
      uint64_t pmf[64];
      int n = 0, prev = 1 << op->ftb;
      for (;;) {
        int cur = op->kind == OP_ICDF ? ((const u8 *) op->pdf)[n]
                                      : ((const u16 *) op->pdf)[n];
        pmf[n++] = (uint64_t) (prev - cur);
        prev = cur;
        if (!cur || n >= 64) break;
      }
      v = (int) xqtree(m, dec ? 0 : (u32) op->v, pmf, (u32) n,
                       PH3(0x20, op->site, n), 5, 0, apcum(m, (u32) n));
    } else if (op->kind == OP_UINT) {
      v = (int) xutree(m, dec ? 0 : (u32) op->v, op->nsym,
                       PH3(0x30, op->site, op->nsym), apcum(m, op->nsym));
    } else if (op->kind == OP_BITS) {
      v = op->ftb ? (int) xutree(m, dec ? 0 : (u32) op->v, 1U << op->ftb,
                                 PH3(0x40, op->site, op->ftb),
                                 apcum(m, 1U << op->ftb)) : 0;
    } else FATAL("opus: unexpected op kind %d", op->kind);
    switch (op->site) {
    case OREC_S_CELT_SILENCE: f->silence = v;  break;
    case OREC_S_CELT_PF_FLAG: f->pf = v;  break;
    case OREC_S_CELT_PF_OCTAVE: f->pfo = v;  break;
    case OREC_S_CELT_PF_PITCH: f->pfp = v;  break;
    case OREC_S_CELT_PF_GAIN: f->pfg = v;  break;
    case OREC_S_CELT_PF_TAPSET: f->pft = v;  break;
    case OREC_S_CELT_TRANSIENT: f->trans = v;  break;
    case OREC_S_CELT_INTRA: f->intra = v;  f->LM = orec_LM;  break;
    case OREC_S_CELT_TF:
      f->tfb[b] = v;  m->tfcur ^= v;  f->tf[b] = m->tfcur;  break;
    case OREC_S_CELT_TF_SELECT: f->tfsel = v;  break;
    case OREC_S_CELT_SPREAD: f->spread = v;  break;
    case OREC_S_CELT_DYNALLOC: if (v) f->dyn[b]++;  break;
    case OREC_S_CELT_TRIM: f->trim = v;  break;
    case OREC_S_CELT_SKIP: f->nskip++;  f->cb = b + 1;  break;
    case OREC_S_CELT_INTENSITY: f->inten = v;  break;
    case OREC_S_CELT_DUAL: f->dual = v;  break;
    default: break;
    }
    break;
  }
  site_note(m, op->site, v);
  op->v = v;
}
