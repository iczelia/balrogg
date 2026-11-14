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

typedef u32 ctr;

#define NI 20
#define NM 10
#define NH 10
#define HB 18
#define W0 12288
#define FL 255
#define TL 60

enum {
  O_HASH = 1, O_H2 = 2, O_M2 = 4, O_RH = 8, O_FLAG = 128, O_EF = 256
};

typedef struct { i32 w[NI + 1];  u32 n; } mw;

typedef struct {
  ctr * t[NI], * mt[NM], * ht;
  u32 mv[NM], hh[NH], s2;
  int n, nm, nh;
  mw * w2;
} tin;

typedef struct {
  sz pr, q, up, lf, l1, l2, l4, l8, l16;
  int c, cc, r, mlb, rb, h1f;
  u32 kp, kq, ln, ln2, ls, opt;
} pc;

typedef struct {
  ctr na[32][4], nb[64][32], nc[3][16], nd[3][8], ne[3][2][16];
  ctr nf[3][2][32], ng[16][8][2], nh[17][16][2], ni[3][3][3];
  ctr nj[256][4], nk[256][3];
  ctr ea[32][4], eb[3][3][2][2], ec[256][2], ed[64][2];
  ctr h1a[16][4], h1b[3][3][3], h1c[256], h1d[64], h2a[8][3], h2b[3][2][2];
  ctr za[17][2][2], zb[3][3][3], zc[64][2], zd[256], ze[17][2], zf[256][2];
  ctr pv1[98][128], pv2[32][128], pv3[256][128], pm[6][16][7][3];
  ctr xa[MOD_NSEMI][64], xb[256][64], xc[4096];
  ctr sv1[98][256], sv2[256][256], sv3[2][256], sm[6][8][8][3];
  ctr fv1[17][2][2][16], fv2[17][2][16], fv3[3][17][16], fv4[17][17][16];
  ctr fm[5][8][4][3], qs[2][4][3][2], qs2[16][2], qr[16][3][2], qr2[16][9];
  ctr rv1[16][256], rv2[16][16][256], rv3[16][2][256], rv4[65][256];
  ctr rm[8][16][8][3];
  ctr ht[1 << HB];
  mw wn[3][2], wh1[4], wh2[2], wz[3], we[3], qws[2], qwr[16];
  mw wp[7][4], wx[12][4], ws[8][4], wf[2][4][4], wr[16][8][4];
  mw vn[16][3], vh1[16][3], vz[17][2];
  mw vp[7][16], vs[8][16], vf[4][16], vr[8][16];
  u16 dv[256];
  u8 sef[256], spa[256], sps[98], scv[256];
} pz;

static INLINE u32 cadapt(const pz * z, u32 s, int lim, int bit) {
  u32 n = s >> 16, p = rc_adapt_prob(rc_packed_prob(s), z->dv[n], bit);
  return (p ^ RC_PINIT) | (n + ((int) n < lim)) << 16;
}

static INLINE int cbitn(mx * m, const pz * z, ctr * const * c, int n,
                        mw * const * ws, int nw, int lim, int bit) {
  i32 st[NI + 1], pm[3], err, s = 0, p1;
  int i, j;
  Fi(n, st[i] = acm_str[(65536 - rc_packed_prob(*c[i])) >> 4]);
  st[n] = 256;
  Fj(nw,
    int64_t dot = 0;
    i32 t;
    Fi(n + 1, dot += (int64_t) ws[j]->w[i] * st[i]);
    t = (i32) CLAMP(dot >> 16, -2047, 2047);
    pm[j] = acm_squash(t);  s += t);
  s /= nw;
  p1 = acm_squash(s) * 16;
  bit = mx_bit(m, bit, (u32) CLAMP(65536 - p1, 32, 65504));
  Fj(nw,
    mw * w = ws[j];
    err = ((bit ? 4095 : 0) - pm[j]) *
          (600 + 600000 / (200 + 16 * (int) (w->n >> 4)));
    if (w->n < 65535) w->n++;
    Fi(n + 1, w->w[i] += (i32) (((int64_t) st[i] * err) >> 20)));
  Fi(n, *c[i] = cadapt(z, *c[i], lim, bit));
  return bit;
}

static INLINE int cbit(mx * m, const pz * z, ctr * const * c, int n, mw * w,
                       mw * v, int bit) {
  mw * ws[2];
  ws[0] = w;  ws[1] = v;
  return cbitn(m, z, c, n, ws, v ? 2 : 1, FL, bit);
}

static u32 mtree(mx * m, const pz * z, const tin * x, mw * w, int nb, u32 v,
                 int lim) {
  u32 node = 1;
  int d, i, k, na, nw;
  ctr * c[NI];
  mw * ws[2];
  for (d = 0; d < nb; d++) {
    k = 0;  na = 0;
    Fi(x->n, c[k++] = x->t[i] + node);
    Fi(x->nh,
      c[k++] = x->ht + ((x->hh[i] + node * 0x9E3779B1u) >> (32 - HB)));
    Fi(x->nm,
      if (((1u << d) | x->mv[i] >> (nb - d)) == node) {
        c[k++] = x->mt[i] + d * 3 + (x->mv[i] >> (nb - 1 - d) & 1);  na++;
      } else c[k++] = x->mt[i] + d * 3 + 2);
    na = MIN(na, 3);
    ws[0] = w + d * 4 + na;  nw = 1;
    if (x->w2) ws[nw++] = x->w2 + d * 16 + (x->s2 & 15);
    node = node * 2 +
      (u32) cbitn(m, z, c, k, ws, nw, lim, (int) (v >> (nb - 1 - d) & 1));
  }
  return node - (1u << nb);
}

static INLINE u32 H3(u32 a, u32 b, u32 c) {
  return chash(chash(a, b, 32), c, 32);
}

static int agree(const u32 * v, const int * ok, int n, int j) {
  int k, a = 0;
  for (k = 0; k < n; k++) a += k != j && ok[k] && v[k] == v[j];
  return MIN(a, 3);
}

static void addm(tin * x, ctr * t, u32 v) {
  x->mv[x->nm] = v;  x->mt[x->nm++] = t;
}

#define NT(k) ((k) == NOC ? 0u : m->nt[k])
#define SM(k) ((k) == NOC ? 0u : m->sm[k])
#define PA(k) ((k) == NOC ? 0u : m->pa[k])
#define E17(k) ((k) == NOC ? 16u : m->ef[k])
#define EZ(k) ((k) == NOC ? 2 : cv[k] == 0)
#define NZ(k) ((k) == NOC ? 2 : m->nt[k] == 0)
#define OPT(o, a) (x->opt & (o) ? (a) : NULL)
#define HASNOTE(v) (((v) >> 16 & 0xFFF) != 0)

static void mixers(pz * z, tin * X, const pc * x, mw * w2, u32 s2) {
  X->w2 = OPT(O_M2, w2);  X->s2 = s2;  X->ht = z->ht;
}

static u32 c_pitch(mx * m, pz * z, const pc * x, u32 sym) {
  tin X;
  u32 vv[6], un = NT(x->up), fn = NT(x->lf);
  int ok[6], j, bk;
  m->ty = T_PITCH;
  vv[0] = NT(x->pr);  vv[1] = NT(x->q);  vv[2] = un;  vv[3] = x->ln;
  vv[4] = x->ln2;  vv[5] = fn;
  Fj(6, ok[j] = vv[j] != 0);
  X.n = 3;  X.nm = 0;  X.nh = 0;
  X.t[0] = z->pv1[x->ln];  X.t[1] = z->pv2[x->cc];  X.t[2] = z->pv3[x->ls];
  if (x->opt & O_HASH) {
    X.hh[0] = H3(1, x->ln, x->ln2);  X.hh[1] = H3(2, un, x->ln);
    X.hh[2] = H3(3, fn, x->ln);  X.hh[3] = H3(4, NT(x->l4), NT(x->l8));
    X.hh[4] = H3(5, x->ln, x->ls);  X.hh[5] = H3(6, vv[0], vv[1] << 8 | un);
    X.nh = 6;
  }
  if (x->opt & O_H2) {
    X.hh[X.nh++] = H3(7, vv[0], (u32) x->mlb);
    X.hh[X.nh++] = H3(8, vv[1], (u32) x->rb);
    X.hh[X.nh++] = H3(9, x->ln << 8 | NT(x->l2), (u32) x->r & 3);
  }
  Fj(6,
    if (!ok[j]) continue;
    bk = j == 0 ? MIN(x->mlb - 1, 3) : j == 1 ? MIN(x->rb - 1, 3)
       : j == 2 ? (x->ln == 0) + 2 * (x->pr != NOC) : x->r & 3;
    addm(&X, z->pm[j][agree(vv, ok, 6, j) * 4 + bk][0], vv[j] - 1));
  mixers(z, &X, x, z->vp[0],
         (u32) MIN(x->mlb, 3) * 4 + (x->ln == 0) * 2 + (x->rb > 0));
  sym = 1 + mtree(m, z, &X, z->wp[0], 7, sym - 1, TL);
  return MIN(sym, NSYM - 1);
}

static u32 c_pidx(mx * m, pz * z, const pc * x, u32 sym, u32 per) {
  tin X;
  u32 s = sym - 1, a = m->aoff[s], cnt = (u32) m->aoff[s + 1] - a, k = 0;
  if (cnt < 2) return cnt ? m->aper[a] : m->dec ? mod_semi_lo((int) s) : per;
  m->ty = T_PIDX;
  if (!m->dec) while (m->aper[a + k] != per) k++;
  X.n = 2;  X.nm = 0;  X.nh = 0;  X.w2 = NULL;
  X.t[0] = z->xa[s];  X.t[1] = z->xb[x->ls];
  if (cnt > 64) { X.n = 1;  X.t[0] = z->xc; }
  k = mtree(m, z, &X, z->wx[0], cnt > 64 ? 12 : 6, k, FL);
  return m->aper[a + MIN(k, cnt - 1)];
}

static u32 c_smp(mx * m, pz * z, const pc * x, u32 smp, u32 sym) {
  tin X;
  u32 vv[6], zn = sym == 0;
  int ok[6], j;
  m->ty = T_SMP;
  if (x->opt & O_FLAG) {
    ctr * cx[2];
    int a = (x->pr != NOC) + (x->pr != NOC && m->sm[x->pr] == x->ls);
    cx[0] = &z->qs[zn][MIN(x->mlb, 3)][a][x->h1f];
    cx[1] = &z->qs2[(x->ls == SM(x->up)) * 8 + (x->ls == z->sps[sym]) * 4 +
                    (x->ls == SM(x->lf)) * 2 + !zn][zn];
    if (cbit(m, z, cx, 2, &z->qws[zn], NULL, smp == x->ls))
      return x->ls;
  }
  vv[0] = x->ls;  vv[1] = SM(x->pr);  vv[2] = SM(x->q);  vv[3] = SM(x->up);
  vv[4] = z->sps[sym];  vv[5] = SM(x->lf);
  ok[0] = 1;  ok[1] = x->pr != NOC;  ok[2] = x->q != NOC;  ok[3] = x->up != NOC;
  ok[4] = sym != 0;  ok[5] = x->lf != NOC;
  X.n = 3;  X.nm = 0;  X.nh = 0;
  X.t[0] = z->sv1[sym];  X.t[1] = z->sv2[x->ls];  X.t[2] = z->sv3[zn];
  if (x->opt & O_HASH) {
    X.hh[0] = H3(11, sym, x->ls);  X.hh[1] = H3(12, sym, (u32) x->c);
    X.hh[2] = H3(13, (x->l4 != NOC ? m->sm[x->l4] : 256) << 1 | zn,
                 x->l8 != NOC ? m->sm[x->l8] : 256);
    X.hh[3] = H3(14, vv[1], x->pr != NOC && m->nt[x->pr] == sym);
    X.hh[4] = H3(15, (x->l1 != NOC ? m->sm[x->l1] : 256) << 1 | zn,
                 (u32) NZ(x->l1));
    X.nh = 5;
  }
  if (x->opt & O_H2) {
    X.hh[X.nh++] = H3(16, SM(x->up) << 1 | zn, (u32) (x->up != NOC));
    X.hh[X.nh++] = H3(17, SM(x->lf) << 1 | zn, (u32) (x->lf != NOC));
    X.hh[X.nh++] = H3(18, vv[1] << 8 | vv[2], sym);
  }
  Fj(6,
    if (ok[j]) addm(&X, z->sm[j][agree(vv, ok, 6, j) * 2 + zn][0], vv[j]));
  mixers(z, &X, x, z->vs[0], zn * 12 + (u32) MIN(x->mlb, 3));
  return mtree(m, z, &X, z->ws[0], 8, smp, TL);
}

static u32 c_eff(mx * m, pz * z, const pc * x, u32 eff, u32 zn, u32 smp) {
  tin X;
  u32 vv[5], e1 = E17(x->l1), zs = smp == 0;
  int ok[5], j;
  m->ty = T_EFF;
  vv[0] = e1;  vv[1] = E17(x->pr);  vv[2] = E17(x->q);  vv[3] = E17(x->up);
  vv[4] = z->sef[smp ? smp : x->ls] & 15;
  Fj(5, ok[j] = vv[j] < 16);
  X.n = 4;  X.nm = 0;  X.nh = 0;
  X.t[0] = z->fv1[e1][zn][zs];  X.t[1] = z->fv2[vv[3]][zn];
  X.t[2] = z->fv3[x->r == 0 ? 0 : x->r == 63 ? 1 : 2][e1];
  X.t[3] = z->fv4[E17(x->l2)][e1];
  if (x->opt & O_H2) {
    X.hh[X.nh++] = H3(31, e1 << 8 | E17(x->l2), E17(x->l4));
    X.hh[X.nh++] = H3(32, NT(x->l1) << 8 | smp, zn);
    X.hh[X.nh++] = H3(33, smp << 8 | vv[4], zn);
    X.hh[X.nh++] = H3(34, e1 << 8 | PA(x->l1), zn);
  }
  Fj(5, if (ok[j]) addm(&X, z->fm[j][agree(vv, ok, 5, j) * 2 + zn][0], vv[j]));
  mixers(z, &X, x, z->vf[0], MIN(e1, 15));
  return mtree(m, z, &X, z->wf[zn][0], 4, eff, TL);
}

static u32 c_par(mx * m, pz * z, const pc * x, u32 par, u32 e, u32 zn,
                 u32 smp, u32 sym, u32 lpc, u32 lp2) {
  tin X;
  u32 vv[8], sx = smp ? smp : x->ls,
      vol = sx && sx <= (u32) m->L.ns ? MIN(m->h[45 + 30 * (sx - 1)], 64) : 0;
  int ok[8], j;
  m->ty = T_PAR;
  if (x->opt & O_FLAG) {
    ctr * cx[2];
    cx[0] = &z->qr[e][E17(x->l1) == e ? 1 + (PA(x->l1) == lpc) : 0][zn];
    cx[1] = &z->qr2[e][E17(x->pr) == e ? 1 + (PA(x->pr) == lpc) * 4 +
                       MIN(x->mlb, 3) : 0];
    if (cbit(m, z, cx, 2, &z->qwr[e], NULL, par == lpc))
      return lpc;
  }
  vv[0] = PA(x->l1);  ok[0] = E17(x->l1) == e;
  vv[1] = PA(x->pr);  ok[1] = E17(x->pr) == e;
  vv[2] = PA(x->q);  ok[2] = E17(x->q) == e;
  vv[3] = PA(x->up);  ok[3] = E17(x->up) == e;
  vv[4] = lpc;  ok[4] = 1;
  vv[5] = z->spa[sx];  ok[5] = z->sef[sx] == e;
  vv[6] = vol;  ok[6] = e == 12;
  vv[7] = (u32) CLAMP(2 * (int) lpc - (int) lp2, 0, 255);  ok[7] = e == 12;
  X.n = e == 12 ? 4 : 3;  X.nm = 0;  X.nh = 0;
  X.t[0] = z->rv1[e];  X.t[1] = z->rv2[e][lpc >> 4];
  X.t[2] = z->rv3[e][zn];  X.t[3] = z->rv4[vol];
  if (x->opt & O_HASH) {
    X.hh[0] = H3(21, e, smp << 8 | sym);  X.hh[1] = H3(22, e, (u32) x->r);
    X.hh[2] = H3(23, e, E17(x->l1) << 8 | PA(x->l1));
    X.hh[3] = H3(24, e, lpc << 8 | lp2);  X.hh[4] = H3(25, e, sx << 1 | zn);
    X.nh = 5;
  }
  Fj(8, if (ok[j]) addm(&X, z->rm[j][e][0], vv[j]));
  if (e == 12) addm(&X, z->rm[7][15][0], z->scv[sx]);
  mixers(z, &X, x, z->vr[0], e);
  return mtree(m, z, &X, z->wr[e][0], 8, par, TL);
}

static int c_hit(mx * m, pz * z, const pc * x, const u32 * cv, const u8 * hs,
                 u32 v) {
  ctr * cx[4];
  int qa = x->q == NOC ? 2 : x->kq == x->kp,
      ua = x->up == NOC ? 2 : cv[x->up] == x->kp,
      la = x->l1 == NOC ? 2 : cv[x->l1] == x->kp, h = hs[x->c];
  cx[0] = &z->h1a[x->mlb][h & 3];
  cx[1] = &z->h1b[qa][ua][la];
  cx[2] = &z->h1c[h];
  cx[3] = &z->h1d[x->r];
  return cbit(m, z, cx, 4, &z->wh1[MIN(x->mlb, 3)],
              OPT(O_M2, &z->vh1[h & 15][ua]), v == x->kp);
}

void pat_cm(mx * m) {
  const mod_lay * L = &m->L;
  const int ch = L->ch;
  const sz rowc = (sz) ch, patc = (sz) ch * MOD_ROWS;
  pz * z = xcalloc(1, sizeof *z);
  u8 * ln = xcalloc(rowc, 1), * ls = xcalloc(rowc, 1),
     * lp = xcalloc(rowc * 16, 1), * lp2 = xcalloc(rowc * 16, 1),
     * ln2 = xcalloc(rowc, 1), * dn = xcalloc(rowc, 1),
     * hs = xcalloc(rowc, 1), * nh8 = xcalloc(rowc, 1),
     * eh8 = xcalloc(rowc, 1);
  u32 * cv = xcalloc(L->ncell + 1, sizeof *cv);
  sz i;
  rtrk R;
  mw * w;
  mtrk T;
  mt_init(&T, L->ncell, rowc);
  rt_init(&R, L->ncell, rowc);
  Fi(256, z->dv[i] = (u16) (65536 / (i + 2)));
  for (w = z->wn[0]; w <= &z->vr[7][15]; w++) {
    Fi(NI, w->w[i] = W0);
    w->w[NI] = 0;
  }
  Fi(L->ncell,
    const u8 * src = m->in ? m->in + L->pat + 4 * i : NULL;
    u32 per = 0, sym = 0, smp = 0, eff = 0, par = 0, v = 0;
    int done = 0, nv = -1, nn, n1, np, nq, nu, e1, hit = 0;
    ctr * cx[12], * ncx[12];
    pc X, * x = &X;
    u8 b[4];
    x->opt = m->cf->opt;  x->h1f = 0;
    x->c = (int) (i % rowc);  x->r = (int) (i / rowc % MOD_ROWS);
    x->cc = MIN(x->c, 31);
    x->l1 = x->r >= 1 ? i - rowc : NOC;
    x->l2 = x->r >= 2 ? i - 2 * rowc : NOC;
    x->l4 = x->r >= 4 ? i - 4 * rowc : NOC;
    x->l8 = x->r >= 8 ? i - 8 * rowc : NOC;
    x->l16 = x->r >= 16 ? i - 16 * rowc : NOC;
    x->up = i >= patc ? i - patc : NOC;  x->lf = x->c ? i - 1 : NOC;
    rt_row(&R, m, i);
    x->pr = mt_pred(&T, x->c, i);
    x->q = rt_pred(&R, x->c, i);
    x->mlb = x->pr == NOC ? 0 : 1 + (int) MIN(T.ml[x->c], 14);
    x->rb = x->q == NOC ? 0 : 1 + (int) MIN(R.rml, 6);
    x->kp = x->pr != NOC ? cv[x->pr] : 0;  x->kq = x->q != NOC ? cv[x->q] : 0;
    x->ln = ln[x->c];  x->ln2 = ln2[x->c];  x->ls = ls[x->c];
    if (src) v = rd32be(src);
    n1 = NZ(x->l1);  np = NZ(x->pr);  nq = NZ(x->q);  nu = NZ(x->up);
    e1 = EZ(x->l1) == 1;
    m->ty = T_NOTE;
    ncx[0] = &z->na[(n1 == 1) | (NZ(x->l2) == 1) << 1 |
                    (NZ(x->l4) == 1) << 2 | (NZ(x->l8) == 1) << 3 |
                    (nu == 1) << 4][x->r & 3];
    ncx[1] = &z->nb[x->r][x->cc];
    ncx[2] = &z->nc[np][x->mlb];
    ncx[3] = &z->nd[nq][x->rb];
    ncx[4] = &z->ne[nu][NZ(x->l16) == 1][x->r & 15];
    ncx[5] = &z->nf[NZ(x->lf)][n1 == 1][x->cc];
    ncx[6] = &z->ng[MIN(dn[x->c], 15)][x->r & 7][n1 == 1];
    ncx[7] = &z->nh[E17(x->l1)][PA(x->l1) >> 4][n1 == 1];
    ncx[8] = &z->ni[np][nq][nu];
    ncx[9] = &z->nj[nh8[x->c]][x->r & 3];
    ncx[10] = &z->nk[hs[x->c]][np];
    nn = x->opt & O_RH ? 11 : 9;
    if (!(x->opt & O_EF))
      nv = cbit(m, z, ncx, nn, &z->wn[np][n1 == 1],
                OPT(O_M2, &z->vn[x->r & 15][nu]), HASNOTE(v));
    if (x->kp && (nv < 0 || HASNOTE(x->kp) == nv)) {
      hit = c_hit(m, z, x, cv, hs, v);
      if (hit) { v = x->kp;  done = 1; }
      else x->h1f = 1;
    }
    if (!done && x->kq && x->kq != x->kp && nv >= 0 &&
        HASNOTE(x->kq) == nv) {
      int ua = x->up == NOC ? 2 : cv[x->up] == x->kq;
      cx[0] = &z->h2a[x->rb][ua];
      cx[1] = &z->h2b[ua][x->pr != NOC][x->l1 != NOC && cv[x->l1] == x->kq];
      if (cbit(m, z, cx, 2, &z->wh2[x->pr != NOC], NULL, v == x->kq)) {
        v = x->kq;  done = 1;
      } else x->h1f = 1;
    }
    if (!done && nv < 0) {
      int ep = EZ(x->pr), eu = EZ(x->up);
      cx[0] = &z->ea[e1 | (EZ(x->l2) == 1) << 1 | (EZ(x->l4) == 1) << 2 |
                     (EZ(x->l8) == 1) << 3 | (eu == 1) << 4][x->r & 3];
      cx[1] = &z->eb[ep][eu][e1][x->h1f];
      cx[2] = &z->ec[eh8[x->c] | nh8[x->c]][e1];
      cx[3] = &z->ed[x->r][e1];
      if (cbit(m, z, cx, 4, &z->we[ep], NULL, v == 0)) {
        v = 0;  done = 1;
      } else
        nv = cbit(m, z, ncx, 9, &z->wn[np][n1 == 1], NULL, HASNOTE(v));
    }
    if (!done && !nv && !(x->opt & O_EF)) {
      int ep = EZ(x->pr), eu = EZ(x->up);
      cx[0] = &z->za[E17(x->l1)][e1][x->h1f];
      cx[1] = &z->zb[ep][EZ(x->q)][eu];
      cx[2] = &z->zc[x->r][e1];
      cx[3] = &z->zd[hs[x->c]];
      cx[4] = &z->ze[z->sef[x->ls] & 15][e1];
      cx[5] = &z->zf[eh8[x->c]][x->h1f];
      if (cbit(m, z, cx, x->opt & O_RH ? 6 : 5, &z->wz[ep],
               OPT(O_M2, &z->vz[E17(x->l1)][e1]), v == 0)) {
        v = 0;  done = 1;
      }
    }
    if (!done) {
      if (src) cell_get(src, &per, &sym, &smp, &eff, &par);
      if (nv) {
        sym = c_pitch(m, z, x, sym);
        per = c_pidx(m, z, x, sym, per);
      } else sym = per = 0;
      smp = c_smp(m, z, x, smp, sym);
      eff = c_eff(m, z, x, eff, !nv, smp);
      par = c_par(m, z, x, par, eff, !nv, smp, sym, lp[x->c * 16 + eff],
                  lp2[x->c * 16 + eff]);
      b[0] = (u8) ((smp & 0xF0) | per >> 8);  b[1] = (u8) per;
      b[2] = (u8) ((smp & 15) << 4 | eff);  b[3] = (u8) par;
      v = rd32be(b);
    }
    b[0] = (u8) (v >> 24);  b[1] = (u8) (v >> 16);  b[2] = (u8) (v >> 8);
    b[3] = (u8) v;
    cell_get(b, &per, &sym, &smp, &eff, &par);
    cv[i] = v;
    cell_put(m, i, per, sym, smp, eff, par);
    hs[x->c] = (u8) (hs[x->c] << 1 | (u8) hit);
    nh8[x->c] = (u8) (nh8[x->c] << 1 | (sym != 0));
    eh8[x->c] = (u8) (eh8[x->c] << 1 | (eff || par));
    if (sym) { ln2[x->c] = ln[x->c];  ln[x->c] = (u8) sym; }
    dn[x->c] = (u8) (sym ? MIN(dn[x->c] + 2, 15) : dn[x->c] ? dn[x->c] - 1 : 0);
    if (smp) ls[x->c] = (u8) smp;
    if (sym && smp) z->sps[sym] = (u8) smp;
    lp2[x->c * 16 + eff] = lp[x->c * 16 + eff];
    lp[x->c * 16 + eff] = (u8) par;
    smp = smp ? smp : ls[x->c];
    if (eff == 12) z->scv[smp] = (u8) par;
    if (eff || par) { z->sef[smp] = (u8) eff;  z->spa[smp] = (u8) par; }
    rt_next(&R, m, x->q, i);
    mt_next(&T, m, x->c, i, x->pr));
  mt_free(&T);
  xfree(z);  xfree(ln);  xfree(ls);  xfree(lp);  xfree(lp2);  xfree(ln2);
  xfree(dn);  xfree(hs);  xfree(nh8);  xfree(eh8);  xfree(cv);  xfree(R.rt);
}
