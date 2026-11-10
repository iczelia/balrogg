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

typedef int64_t i64;

#define NM 128
#define HN 4096
#define NQ 24
#define QS 24
#define RS 15
#define NC 4
#define NI (7 + NC + 1)
#define NS (3 + NC)
#define MHB 18
#define MN 10
#define MT 16
#define GN 4
#define LN 16
#define LMX ((i64) 1 << 24)
#define WMX (1 << 23)

enum {
  O_MIX = 1 << 15, O_MM = 1 << 16, O_LT = 1 << 17, O_GN = 1 << 18,
  O_NC = 1 << 19, O_MX = 1 << 20, O_AP = 1 << 21, O_LM = 1 << 22,
  O_LW = 1 << 26, O_N2 = 1 << 27, O_F2 = 1 << 28
};

typedef struct {
  int n, kk, fs, cnt, rp, lp;
  i64 r[NM + 1], rh[NM][NM + 1], q[NM + 3], qh[NM][NM + 3], xl[3][3];
  i32 w[NM + 3];
} ols;

typedef struct {
  int cnt;
  i64 r[GN][GN], b[GN];
  i32 w[GN];
} gls;

typedef struct {
  u32 opt;
  int nc, ncd, pos, b1, b2, s1, run, u, mlen;
  ols o;
  gls g;
  i32 hx[2 * HN], ef, es, eu, av[256], lw[LN], lu[LN];
  i64 A[NM][NM], L[NM][NM], lpw;
  u8 nr[256], rk[256], * kb;
  u32 * kh;
  sz kl, kc, kp;
  u32 cu[9][NQ], cv[9][NQ][10], cw[9][NQ][4], cx[9][NQ][16], cy[9][10][10],
      cz[9][NQ][NQ], cd[9][16][8], ce[NC][9][9][6], ch[9][4][9];
  u32 mt[10][NQ][16], ml[10][256], mq[10][8][NQ], mc[NC][10][8][4];
  u32 g0[5][4][4], g1[5][8][4], g2[5][3][4], cs[NC][5][3][3];
  u32 fu[9][NQ][NQ], fv[9][NQ][9], fh[5][3][3], fg[5][4][3], mm[16][2][4],
      au, ab[4];
  i32 mw[9][NQ][NI + 1], sw[5][4][NS + 1], mz[10][8][NI + 1];
  u16 pu[9][NQ][33], pm[10][8][6][33], ps[5][4][4][33];
} sls;

static int qlog(i32 e) {
  u32 v = (u32) e + 1, b = blr_ilog(v);
  int q = b < 2 ? 0 : (int) (2 * b - 3 + (v >> (b - 2) & 1));
  return MIN(q, NQ - 1);
}

static void wset(i32 * w, int n, int k) {
  int i;
  Fi(n + 1, w[i] = i < k ? 65536 / k : 0);
}

static void aset(u16 * a, sz n) {
  sz i;
  Fi(n, a[i] = (u16) (acm_squash(((int) (i % 33) - 16) * 128) * 16));
}

void * smp_ls_new(mx * m) {
  sls * z = xcalloc(1, sizeof (sls));
  u32 o = m->sc->opt;
  int i, j, ni;
  rc_adapt_init();
  acm_prepare();
  z->opt = o;
  z->o.n = MIN((int) (o & 127), NM - 4);
  z->o.kk = (int) (o >> 7 & 15);
  z->o.fs = MAX((int) (o >> 11 & 15), 1);
  z->nc = (int) (o >> 23 & 7);
  if (!z->nc) z->nc = 7;
  z->ncd = o & O_NC ? o & O_N2 ? 2 : NC : 0;
  ni = z->nc + z->ncd + (z->ncd > 0);
  Fi(9, Fj(NQ, wset(z->mw[i][j], NI, ni)));
  Fi(5, Fj(4, wset(z->sw[i][j], NS, 3 + z->ncd)));
  Fi(10, Fj(8, wset(z->mz[i][j], NI, 2 + z->ncd)));
  aset(z->pu[0][0], sizeof z->pu / sizeof (u16));
  aset(z->pm[0][0][0], sizeof z->pm / sizeof (u16));
  aset(z->ps[0][0][0], sizeof z->ps / sizeof (u16));
  z->g.w[0] = 65536;
  if (o & O_MM) z->kh = xcalloc((sz) 1 << MHB, sizeof *z->kh);
  return z;
}

void smp_ls_free(void * st) {
  sls * z = st;
  xfree(z->kb);  xfree(z->kh);  xfree(z);
}

static i64 cl64(i64 v, i64 b) { return v < -b ? -b : v > b ? b : v; }

static void ldl(sls * z, i64 * b, int n, i32 * w) {
  i64 (*A)[NM] = z->A, (*L)[NM] = z->L, D[NM], v[NM], y[NM], s, d,
      reg = (i64) 1 << (QS - RS), dmin = MAX(reg >> 2, 1);
  int i, j, k;
  Fj(n,
    d = A[j][j] + reg;
    s = 0;
    Fk(j,
      v[k] = cl64(L[j][k] * D[k] >> 16, (i64) 1 << 26);
      s += L[j][k] * v[k]);
    d -= s >> 16;
    D[j] = d = MAX(d, dmin);
    for (i = j + 1; i < n; i++) {
      i64 t = A[i][j];
      s = 0;
      Fk(j, s += L[i][k] * v[k]);
      t = cl64(t - (s >> 16), (i64) 1 << 40);
      L[i][j] = cl64(t * 65536 / d, LMX);
    });
  Fi(n,
    s = b[i];
    Fk(i, s -= L[i][k] * y[k] >> 16);
    y[i] = cl64(s, (i64) 1 << 38));
  Fi(n, y[i] = cl64(y[i] * 65536 / D[i], (i64) 1 << 34));
  for (i = n - 1; i >= 0; i--) {
    s = y[i];
    for (k = i + 1; k < n; k++) s -= L[k][i] * y[k] >> 16;
    y[i] = cl64(s, (i64) 1 << 34);
  }
  Fi(n, w[i] = (i32) cl64(y[i], (i64) 1 << 20));
}

static int nshift(i64 em, int * sl) {
  int sh = (int) blr_ilog64((uint64_t) em) - QS;
  *sl = sh < 0 ? -sh : 0;
  return sh < 0 ? 0 : sh;
}

static i64 nrm(i64 v, int sh, int sl) { return (v >> sh) * ((i64) 1 << sl); }

static void solve(sls * z) {
  ols * o = &z->o;
  i64 (*A)[NM] = z->A, b[NM], em = 1;
  int n = o->n, i, j, sh, sl;
  Fi(n, em = MAX(em, o->rh[i][0]));
  em = MAX(em, o->r[0]);
  if (o->lp) Fi(3, em = MAX(em, o->xl[i][i]));
  sh = nshift(em, &sl);
  Fi(n,
    const i64 * h = o->rh[(o->rp + n - 1 - i) % n];
    for (j = i; j < n; j++) A[j][i] = nrm(h[j - i], sh, sl);
    b[i] = nrm(o->r[i + 1], sh, sl));
  if (o->lp)
    Fj(3,
      Fi(n, A[n + j][i] = nrm(o->qh[(o->rp + n - 1 - i) % n][n - 1 + j - i],
                              sh, sl));
      Fi(j + 1, A[n + j][n + i] = nrm(o->xl[i][j], sh, sl));
      b[n + j] = nrm(o->q[n + j], sh, sl));
  ldl(z, b, n + (o->lp ? 3 : 0), o->w);
}

static void ols_upd(sls * z, const i32 * h) {
  ols * o = &z->o;
  int n = o->n, k, j, x = h[0], fs = o->fs;
  if (o->lp) {
    const i32 * g = h + o->lp - 1, * d = h + o->lp - n - 1;
    Fj(3,
      Fk(j + 1,
        o->xl[k][j] += (i64) (g[k] * g[j]) * 256 - (o->xl[k][j] >> fs)));
    Fk(n + 3, o->qh[o->rp][k] = o->q[k]);
    Fk(n + 3, o->q[k] += (i64) (x * d[k]) * 256 - (o->q[k] >> fs));
  }
  Fk(n + 1, o->rh[o->rp][k] = o->r[k]);
  o->rp = (o->rp + 1) % n;
  Fk(n + 1, o->r[k] += (i64) (x * h[k]) * 256 - (o->r[k] >> fs));
  if (++o->cnt >= 1 << o->kk) { o->cnt = 0;  solve(z); }
}

static i64 ols_pred(const ols * o, const i32 * h) {
  i64 p = 0;
  int j;
  Fj(o->n, p += (i64) o->w[j] * h[j]);
  if (o->lp) Fj(3, p += (i64) o->w[o->n + j] * h[o->lp - 2 + j]);
  return p;
}

static void gls_upd(sls * z, const i32 * u, i32 x) {
  gls * g = &z->g;
  int i, j, sh, sl;
  i64 em = 1, b[GN];
  Fi(GN,
    Fj(i + 1, g->r[i][j] += (i64) u[i] * u[j] - (g->r[i][j] >> 6));
    g->b[i] += (i64) u[i] * x - (g->b[i] >> 6));
  if (++g->cnt < 4) return;
  g->cnt = 0;
  Fi(GN, em = MAX(em, g->r[i][i]));
  sh = nshift(em, &sl);
  Fi(GN,
    Fj(i + 1, z->A[i][j] = nrm(g->r[i][j], sh, sl));
    b[i] = nrm(g->b[i], sh, sl));
  ldl(z, b, GN, g->w);
}

static i64 gls_pred(const gls * g, const i32 * u) {
  i64 p = 0;
  int i;
  Fi(GN, p += (i64) g->w[i] * u[i]);
  return p >> 4;
}

static int bitm(mx * m, u32 ** c, int nc, i32 * w, u16 * a, int bit) {
  int st[NI + 1], p, err, i, q = 0, wt = 0, pf, d;
  i64 dot = 0;
  Fi(nc, st[i] = acm_str[(65536 - rc_packed_prob(*c[i])) >> 4]);
  st[nc] = 256;
  Fi(nc + 1, dot += (i64) w[i] * st[i]);
  d = (int) cl64(dot >> 16, 2047);
  p = acm_squash(d);
  pf = p * 16;
  if (a) {
    q = (d + 2048) >> 7;  wt = (d + 2048) & 127;
    pf = (pf + 3 * ((a[q] * (128 - wt) + a[q + 1] * wt) >> 7)) >> 2;
  }
  bit = mx_bit(m, bit, (u32) CLAMP(65536 - pf, 1, 65535));
  err = ((bit << 12) - p) * 2;
  Fi(nc + 1, w[i] = CLAMP(w[i] + ((st[i] * err) >> 14), -WMX, WMX));
  Fi(nc, *c[i] = rc_adapt_packed(*c[i], i == 1 ? 255 : 60, bit));
  if (a) {
    int g = bit ? 65535 : 0;
    a[q] = (u16) (a[q] + ((g - a[q]) * (128 - wt) >> 14));
    a[q + 1] = (u16) (a[q + 1] + ((g - a[q + 1]) * wt >> 14));
  }
  return bit;
}

static int sbit(mx * m, u32 * c0, u32 * c1, int bit) {
  int d = (acm_str[(65536 - rc_packed_prob(*c0)) >> 4] +
           acm_str[(65536 - rc_packed_prob(*c1)) >> 4]) >> 1;
  bit = mx_bit(m, bit, (u32) CLAMP(65536 - acm_squash(d) * 16, 1, 65535));
  *c0 = rc_adapt_packed(*c0, 60, bit);
  *c1 = rc_adapt_packed(*c1, 60, bit);
  return bit;
}

static u32 mhash(const u8 * b) {
  u32 h = 0;
  int i;
  Fi(MN, h = (h + b[i] + 1) * 0x2F0F3A35u);
  return h >> (32 - MHB);
}

static void mput(sls * z, int x) {
  u32 c, hh;
  int k = 0;
  if (z->mlen) {
    if (z->kb[z->kp] == (u8) x) {
      z->mlen = MIN(z->mlen + 1, 65535);  z->kp++;
    } else z->mlen = 0;
  }
  z->kb[z->kl++] = (u8) x;
  if (z->kl < MN) return;
  hh = mhash(z->kb + z->kl - MN);
  c = z->kh[hh];
  z->kh[hh] = (u32) z->kl;
  if (z->mlen || !c) return;
  while (k < 32 && (u32) k < c && z->kb[c - 1 - k] == z->kb[z->kl - 1 - k])
    k++;
  if (k >= MN) { z->kp = c;  z->mlen = k; }
}

static void alpha(mx * m, sls * z, const u8 * src, sz n) {
  u8 sn[256];
  int lo = 255, hi = 0, u = 0, j, use = 0, b = 0, k;
  sz i;
  memset(sn, 0, sizeof sn);
  if (src) {
    Fi(n, sn[(u8) (src[i] ^ 128)] = 1);
    Fj(256, if (sn[j]) { u++;  lo = MIN(lo, j);  hi = MAX(hi, j); });
    use = u && (i64) n * (hi - lo + 1 - u) > (i64) 32 * (hi - lo + 1);
  }
  if (pbit(m, &z->au, 30, use)) {
    for (j = 7; j >= 0; j--) b = b * 2 + mx_bit(m, lo >> j & 1, 32768);
    lo = b;  b = 0;
    for (j = 7; j >= 0; j--)
      b = b * 2 + mx_bit(m, (hi - lo) >> j & 1, 32768);
    hi = MIN(lo + b, 255);
    sn[lo] = sn[hi] = 1;
    for (j = lo + 1; j < hi; j++) {
      k = sn[j - 1] * 2 + (j - 2 >= lo ? sn[j - 2] : 0);
      sn[j] = (u8) pbit(m, &z->ab[k], 30, sn[j]);
    }
    Fj(256, if (j < lo || j > hi) sn[j] = 0);
  } else memset(sn, 1, sizeof sn);
  u = 0;
  Fj(256, if (sn[j]) { z->rk[j] = (u8) u;  z->av[u++] = j - 128; });
  z->u = u;
  k = 0;
  Fj(256,
    while (k + 1 < u && z->av[k + 1] - (j - 128) <= (j - 128) - z->av[k])
      k++;
    z->nr[j] = (u8) k);
}

static i64 ncor(const i32 * x, const i64 * e, int w, int P) {
  i64 num = 0;
  uint64_t q;
  int t;
  for (t = P; t < w; t++) num += x[t] * x[t - P];
  q = blr_isqrt64((uint64_t) (e[w] - e[P]) * (uint64_t) e[w - P]);
  return q && num > 0 ? num * 32768 / (i64) q : 0;
}

static int lag(sls * z, const u8 * src, sz n) {
  int w = (int) MIN(n, (sz) (z->opt & O_LW ? 2048 : 8192)),
      lo = z->o.n + 2, hi = MIN(2048, w / 2),
      best = 0, t, P, wd = w / 4, bq[4] = { 0, 0, 0, 0 }, j, k;
  i64 bc[4] = { 0, 0, 0, 0 }, c, * e, cs[516];
  i32 * x;
  if (hi <= lo + 8) return 0;
  x = xmalloc((sz) (w + 1) * sizeof *x);
  e = xmalloc((sz) (w + 1) * sizeof *e);
  for (t = 0; t < wd; t++) {
    x[t] = 0;
    Fk(4, x[t] += (i8) src[4 * t + k]);
  }
  e[0] = 0;
  for (t = 0; t < wd; t++) e[t + 1] = e[t] + (i64) x[t] * x[t];
  for (P = lo / 4; P <= hi / 4 + 1; P++) cs[P] = ncor(x, e, wd, P);
  for (P = lo / 4 + 1; P <= hi / 4; P++) {
    c = cs[P];
    if (c <= cs[P - 1] || c < cs[P + 1]) continue;
    for (j = 0; j < 4 && c <= bc[j]; j++) ;
    if (j < 4) {
      for (k = 3; k > j; k--) { bc[k] = bc[k - 1];  bq[k] = bq[k - 1]; }
      bc[j] = c;  bq[j] = P;
    }
  }
  for (t = 0; t < w; t++) x[t] = (i8) src[t];
  for (t = 0; t < w; t++) e[t + 1] = e[t] + (i64) x[t] * x[t];
  c = 4000;
  Fj(4,
    if (bq[j])
      for (P = MAX(lo, 4 * bq[j] - 4); P <= MIN(hi, 4 * bq[j] + 4); P++) {
        i64 v = ncor(x, e, w, P);
        if (v > c) { c = v;  best = P; }
      });
  xfree(x);  xfree(e);
  return best;
}

static void setlag(mx * m, sls * z, const u8 * src, sz n) {
  ols * o = &z->o;
  int P = src ? lag(z, src, n) : 0, q = 0, j;
  Fj(12, q = q * 2 + mx_bit(m, P >> (11 - j) & 1, 32768));
  P = q < o->n + 2 || q > HN - 8 ? 0 : q;
  if (P == o->lp) return;
  memset(o->q, 0, sizeof o->q);  memset(o->qh, 0, sizeof o->qh);
  memset(o->xl, 0, sizeof o->xl);  memset(o->w + o->n, 0, 3 * sizeof *o->w);
  o->lp = P;
}

static i64 lms(sls * z) {
  i64 a = 0;
  int j;
  Fj(LN, a += (i64) z->lw[j] * z->lu[j]);
  return cl64(a >> 16, 1 << 14);
}

static void lms_upd(sls * z, i32 tg, i32 lp) {
  i64 rc = ((i64) 1 << 24) / (z->lpw + 64);
  i32 er = tg - lp;
  int j;
  Fj(LN,
    z->lw[j] += (i32) ((i64) er * z->lu[j] * rc >> 15);
    z->lw[j] = CLAMP(z->lw[j], -(1 << 20), 1 << 20));
  z->lpw += (i64) tg * tg - (i64) z->lu[LN - 1] * z->lu[LN - 1];
  for (j = LN - 1; j > 0; j--) z->lu[j] = z->lu[j - 1];
  z->lu[0] = tg;
}

void smp_ls_one(mx * m, void * st, int si, const u8 * src, u8 * dst, sz n) {
  sls * z = st;
  const u32 o = z->opt;
  sz i;
  int j, k;
  (void) si;
  if (o & O_MM) z->kb = xgrow(z->kb, &z->kc, z->kl + n, 1, 65536);
  alpha(m, z, src, n);
  memset(z->hx, 0, sizeof z->hx);
  if (o & O_LT) setlag(m, z, src, n);
  Fi(n,
    i64 p, po, dd;
    i32 gu[GN], lp = 0;
    int co[NC], P, jP, f, fq, fl, lo, hi, t, M, e = 0, b, x, q1 = qlog(z->ef),
        q2 = qlog(z->es), q3 = qlog(z->eu), qa, dqa, dsg, mg = 0, sg;
    const i32 * h = z->hx + z->pos;
    qa = (int) blr_ilog((u32) ABS(h[0]) + (u32) ABS(h[0] - h[1]));
    qa = MIN(qa, 15);
    p = ols_pred(&z->o, h);
    p = po = cl64(p, (i64) 1 << 30);
    if (o & O_LM) {
      lp = (i32) lms(z);
      p = cl64(p + (i64) lp * 4096, (i64) 1 << 30);
    }
    if (o & O_GN) {
      gu[0] = (i32) (p >> 12);
      gu[1] = h[0] * 16;  gu[2] = h[1] * 16;
      gu[3] = (z->o.lp ? h[z->o.lp - 1] : h[2]) * 16;
      p = cl64(gls_pred(&z->g, gu), (i64) 1 << 30);
    }
    P = (int) CLAMP((p + 32768) >> 16, -128, 127);
    jP = z->nr[P + 128];
    f = (int) cl64(p - (i64) z->av[jP] * 65536, 32767);
    P = z->av[jP];
    fl = f < 0;
    fq = MIN((fl ? -f : f) >> 13, 3);
    dd = (i64) (2 * h[0] - h[1] - P) * 65536;
    if (fl) dd = -dd;
    dsg = dd > 0;
    dqa = (int) blr_ilog((u32) MIN(ABS(dd) >> 14, 65535));
    dqa = MIN(dqa, 15);
    lo = fl ? jP - (z->u - 1) : -jP;
    hi = fl ? jP : z->u - 1 - jP;
    t = MIN(hi, -lo);
    M = MAX(hi, -lo);
    if (!z->ncd) {
      co[0] = z->nr[h[0] + 128] - jP;
      if (fl) co[0] = -co[0];
    } else {
      int cv[NC];
      cv[0] = h[0];  cv[1] = CLAMP(2 * h[0] - h[1], -128, 127);
      cv[2] = z->mlen ? (i8) z->kb[z->kp]
                      : CLAMP(3 * h[0] - 3 * h[1] + h[2], -128, 127);
      cv[3] = z->o.lp ? h[z->o.lp - 1] : h[1];
      Fj(z->ncd, co[j] = z->nr[cv[j] + 128] - jP;  if (fl) co[j] = -co[j]);
    }
    if (src) {
      e = z->rk[src[i] ^ 128] - jP;
      if (fl) e = -e;
      mg = ABS(e);
    }
    if (z->mlen >= MT) {
      int xm = (i8) z->kb[z->kp];
      if (pbit(m, &z->mm[MIN(blr_ilog((u32) z->mlen), 15)][xm == P]
               [MIN(q1 / 6, 3)], 255, src ? (i8) src[i] == xm : 0)) {
        x = xm;
        e = z->nr[(x + 128) & 255] - jP;
        if (fl) e = -e;
        mg = ABS(e);
        b = (int) blr_ilog((u32) mg);
        goto done;
      }
    }
    b = (int) blr_ilog((u32) mg);
    if (!(o & O_MIX) && o & O_F2) {
      int b0 = (int) blr_ilog((u32) ABS(co[0]));
      for (k = 0; k < 9 && 1 << k <= M; k++)
        if (!sbit(m, &z->fu[k][q1][q2], &z->fv[k][q1][b0], b > k)) break;
    } else
      for (k = 0; k < 9 && 1 << k <= M; k++) {
        u32 * c[NI];
        int nc = z->nc;
        c[0] = &z->cu[k][q1];  c[1] = &z->cv[k][q2][z->b1];
        c[2] = &z->cw[k][q1][fq];  c[3] = &z->cx[k][q3][qa];
        c[4] = &z->cy[k][z->b1][z->b2];  c[5] = &z->cz[k][q1][q2];
        c[6] = &z->cd[k][dqa][MIN(q1 / 3, 7)];
        Fj(z->ncd,
          c[nc++] = &z->ce[j][k][blr_ilog((u32) ABS(co[j]))][MIN(q1 / 4, 5)]);
        if (z->ncd) c[nc++] = &z->ch[k][z->run][blr_ilog((u32) ABS(co[0]))];
        if (!bitm(m, c, nc, z->mw[k][q1], o & O_AP ? z->pu[k][q2] : NULL,
                  b > k))
          break;
      }
    b = k;
    if (b > 1 && o & O_MX) {
      int v = 1 << (b - 1), r;
      for (j = b - 2; j >= 0; j--) {
        u32 * c[NI];
        int nc = 2, mid = v + (1 << j);
        c[0] = &z->ml[b][(v >> j) & 255];
        c[1] = &z->mq[b][j][q1];
        Fk(z->ncd,
          r = ABS(co[k]);
          r = r < v ? 0 : r < mid ? 1 : r < mid + (1 << j) ? 2 : 3;
          c[nc++] = &z->mc[k][b][j][r]);
        if (bitm(m, c, nc, z->mz[b][j], o & O_AP ? z->pm[b][j][q1 / 4] : NULL,
                 mg >= mid))
          v = mid;
      }
      mg = v;
    } else if (b > 1) {
      u32 node = 1;
      node = node * 2 + (u32) pbit(m, &z->mt[b][q1][dqa], 60,
                                   (int) (mg >> (b - 2) & 1));
      for (j = b - 3; j >= 0; j--)
        node = node * 2 + (u32) pbit(m, &z->ml[b][node & 255], 60,
                                     (int) (mg >> j & 1));
      mg = (int) ((1u << (b - 1)) | (node & ((1u << (b - 1)) - 1)));
    } else mg = b;
    mg = MIN(mg, M);
    sg = 0;
    if (mg > t) sg = hi <= -lo;
    else if (mg) {
      int bb = MIN(b, 4), s1 = z->s1 == 0 ? 0 : (z->s1 > 0) == !fl ? 1 : 2;
      if (!(o & O_MIX) && o & O_F2) {
        int a = ABS(co[0]);
        sg = sbit(m, &z->fg[bb][fq][s1],
                  &z->fh[bb][co[0] > 0 ? 1 : co[0] < 0 ? 2 : 0]
                        [a < mg ? 0 : a == mg ? 1 : 2], e < 0);
      } else {
        u32 * c[NS];
        i64 am = (i64) mg * 65536, ad = ABS(dd);
        int rl = ad >= am ? 2 : 2 * ad >= am ? 1 : 0;
        c[0] = &z->g0[bb][fq][0];
        c[1] = &z->g1[bb][dsg * 3 + rl][0];
        c[2] = &z->g2[bb][s1][0];
        Fj(z->ncd,
          int a = ABS(co[j]);
          c[3 + j] = &z->cs[j][bb][co[j] > 0 ? 1 : co[j] < 0 ? 2 : 0]
                           [a < mg ? 0 : a == mg ? 1 : 2]);
        sg = bitm(m, c, 3 + z->ncd, z->sw[bb][fq],
                  o & O_AP ? z->ps[bb][fq][z->run] : NULL, e < 0);
      }
    }
    e = sg ? -mg : mg;
    if (fl) e = -e;
    x = z->av[CLAMP(jP + e, 0, z->u - 1)];
  done:
    if (dst) dst[i] = (u8) x;
    if (o & O_MM) mput(z, x);
    z->ef += (mg * 32 - z->ef) >> 2;
    z->es += (mg * 32 - z->es) >> 4;
    z->eu += (mg * 32 - z->eu) >> 7;
    z->b2 = z->b1;  z->b1 = b;
    z->s1 = x - P;
    z->run = x == h[0] ? MIN(z->run + 1, 3) : 0;
    z->pos = (z->pos + HN - 1) & (HN - 1);
    z->hx[z->pos] = z->hx[z->pos + HN] = x;
    h = z->hx + z->pos;
    ols_upd(z, h);
    if (o & O_GN) gls_upd(z, gu, x * 16);
    if (o & O_LM) lms_upd(z, (i32) cl64(x * 16 - (po >> 12), 1 << 14), lp));
}
