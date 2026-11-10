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

#define XO_T(o)    ((o) & 15)
#define XO_CAP(o)  ((o) >> 4 & 15)
#define XO_PROF(o) ((o) >> 8 & 3)
#define XO_PRI     0x400u
#define NF 80

typedef struct {
  int c, r;
  sz l1, l2, l3, l4, l8, l16, up, lf, pr, q, up2, tp;
  acm_h pn, ps, pe, pp, rn, rs, re, rq;
  u32 mlb, rb, ln, ls, ln2, dens, se, sp, gap, le, lq, nsu, p, pbs, rnc;
  u32 opt, pp1;
  const u32 * cum;
} pcx;

static void fref(const mx * m, sz k, u8 * f) {
  f[0] = k == NOC ? 98 : m->nt[k];  f[1] = k == NOC ? 0 : m->sm[k];
  f[2] = k == NOC ? 0 : m->ef[k];  f[3] = k == NOC ? 0 : m->pa[k];
}

static void feat(const mx * m, const pcx * x, u8 * f) {
  const sz rf[10] = { x->l1, x->l2, x->l3, x->l4, x->l8, x->l16, x->up,
                      x->lf, x->pr, x->q };
  int k;
  memset(f, 0, NF);
  f[0] = (u8) MIN(x->c, 31);  f[1] = (u8) x->r;
  f[2] = (u8) MIN(m->L.ch, 32);  f[3] = (u8) MIN(x->p, 15);
  Fk(10, fref(m, rf[k], f + 4 + 4 * k));
  f[44] = (u8) x->mlb;  f[45] = (u8) x->rb;  f[46] = (u8) x->ln;
  f[47] = (u8) x->ls;  f[48] = (u8) x->ln2;  f[49] = (u8) x->dens;
  f[50] = (u8) MIN(x->gap, 63);  f[51] = (u8) x->le;  f[52] = (u8) x->lq;
  f[62] = (u8) MIN(x->nsu, 31);  f[65] = (u8) x->pbs;
  fref(m, x->up2, f + 67);  fref(m, x->tp, f + 71);
  f[75] = (u8) MIN(x->rnc, 32);
}

static u32 sclass(const mx * m, u32 smp) {
  const u8 * s;
  if (!smp) return 0;
  if (smp > (u32) m->L.ns) return 1;
  s = m->h + 20 + 30 * (smp - 1);
  if (!s[22] && !s[23]) return 2;
  return (s[28] << 8 | s[29]) > 1 ? 3 : 4;
}

typedef struct {
  int e, h1, h2, ns, nref;
  const u8 * ref;
  const i8 * emb, * t, * w2[5], * x[5], * g[5], * o[6];
  const i32 * tb, * b2[5], * gb[5], * ob[5];
  const i16 * o4b;
  const int * sh;
} pxw;

#include "mod/patxw.h"

#define PX_NT 18
#define PX_NX 32
#define PX_MH 64
#define PX_MS 96
#define PX_NO (97 + 34 + 16 + 16 + 16 + 46 + 1)
static const u16 TOFF[PX_NT + 1] = {
  0, 99, 133, 149, 165, 181, 189, 253, 257, 273, 289, 297, 313, 329, 345, 361,
  366, 368, 385
};
enum { YNT, YSM, YEF, YPH, YPL, YC, YR, YCH, YP, YMLB, YRB, YDN, YGAP, YNSU,
       YRNC, YSCL, YFL, YVOL };
static const u8 RBS[12] = { 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 67, 71 };
static const int PXNC[5] = { 0, 12, 11, 10, 13 };

typedef struct {
  const pxw * w;
  i16 w2[5][PX_MH * PX_MH], wo[PX_NO * PX_MH], * wg[5];
  i32 * ft, * fx[5];
  u32 fo[PX_MS], xo[5][PX_NX];
  u8 ty[PX_MS], xt[5][PX_NX];
  int nx[5], hp1, hp2;
  i16 t[PX_MH];
  u16 row[PX_MS];
} pxm;

#define SL(y, v) (ty[k] = (u8) (y), row[k++] = (u16) (TOFF[y] + (u32) (v)))

static int px_ref(const u8 * r, u8 * ty, u16 * row, int k) {
  SL(YNT, r[0]);  SL(YSM, MIN(r[1], 33));  SL(YEF, r[2]);
  SL(YPH, r[3] >> 4);  SL(YPL, r[3] & 15);
  return k;
}

static int px_ctx(const pxw * w, const u8 * f, u8 * ty, u16 * row) {
  int k = 0, j;
  SL(YC, MIN(f[0], 7));  SL(YR, f[1]);
  SL(YCH, (f[2] > 4) + (f[2] > 6) + (f[2] > 8));  SL(YP, f[3]);
  Fj(w->nref, k = px_ref(f + w->ref[j], ty, row, k));
  SL(YMLB, f[44]);  SL(YRB, f[45]);  SL(YNT, f[46]);  SL(YSM, MIN(f[47], 33));
  SL(YNT, f[48]);  SL(YDN, f[49]);  SL(YGAP, MIN(f[50], 15));  SL(YEF, f[51]);
  SL(YPH, f[52] >> 4);  SL(YPL, f[52] & 15);  SL(YNSU, MIN(f[62], 15));
  SL(YNT, f[65]);  SL(YRNC, MIN(f[75], 15));
  return k;
}

static int px_ext(const u8 * f, int h, u8 * ty, u16 * row) {
  static const u8 r3[4] = { 0, 6, 8, 9 }, r4[8] = { 0, 1, 6, 7, 8, 9, 10, 11 };
  u32 sm = MIN(f[59], 33), e = f[60];
  int k = 0, j;
  if (h >= 2) { SL(YNT, f[58]);  SL(YSM, MIN(f[64], 33)); }
  if (h >= 3) {
    SL(YSM, sm);  SL(YSCL, f[57]);  SL(YEF, f[53]);  SL(YPH, f[54] >> 4);
    SL(YPL, f[54] & 15);
    Fj(4, SL(YFL, MIN(f[RBS[r3[j]] + 1], 33) == sm));
  }
  if (h >= 4) {
    SL(YEF, e);  SL(YPH, f[55] >> 4);  SL(YPL, f[55] & 15);
    SL(YVOL, MIN(f[56] >> 2, 16));  SL(YPH, f[66] >> 4);  SL(YPL, f[66] & 15);
    Fj(8, SL(YFL, f[RBS[r4[j]] + 2] == e));
    SL(YFL, f[53] == e);  SL(YFL, f[51] == e);
  }
  return k;
}

static INLINE i32 rsh(i32 x, int k) { return k >= 0 ? x >> k : x * (1 << -k); }

static i32 * px_fold(const pxw * pw, const i8 * w, const u8 * ty, int n,
                     int nh, int hp, u32 * fo) {
  const int ne = pw->e, ld = n * ne;
  sz tot = 0;
  i32 * f;
  int s, v, j, e;
  for (s = 0; s < n; s++) {
    fo[s] = (u32) tot;
    tot += (sz) (TOFF[ty[s] + 1] - TOFF[ty[s]]) * (sz) hp;
  }
  f = xcalloc(tot, sizeof *f);
  for (s = 0; s < n; s++)
    for (v = 0; v < TOFF[ty[s] + 1] - TOFF[ty[s]]; v++)
      Fj(nh,
        i32 a = 0;
        const i8 * x = pw->emb + (sz) (TOFF[ty[s]] + v) * (sz) ne;
        for (e = 0; e < ne; e++) a += w[(sz) j * ld + s * ne + e] * x[e];
        f[fo[s] + (u32) v * (u32) hp + (u32) j] = a);
  return f;
}

static void px_mat(i16 * d, const i8 * w, int rows, int cols, int hp) {
  int r, c;
  for (r = 0; r < rows; r++)
    for (c = 0; c < cols; c++) d[r * hp + c] = w[r * cols + c];
}

static void px_init(pxm * p, const pxw * w) {
  static const int on[5] = { 97, 34, 16, 16, 16 };
  u8 f[NF];
  u16 row[PX_MS];
  int h, o = 0;
  memset(f, 0, sizeof f);
  memset(p->w2, 0, sizeof p->w2);  memset(p->wo, 0, sizeof p->wo);
  p->w = w;
  p->hp1 = (w->h1 + 7) & ~7;  p->hp2 = (w->h2 + 7) & ~7;
  for (h = 0; h < 5; h++) {
    px_mat(p->w2[h], w->w2[h], w->h2, w->h1, p->hp1);
    px_mat(p->wo + o * p->hp2, w->o[h + 1], on[h], w->h2, p->hp2);
    o += on[h];
  }
  p->wg[0] = NULL;
  for (h = 1; h < 5; h++) {
    p->wg[h] = p->wo + o * p->hp2;
    px_mat(p->wg[h], w->g[h], PXNC[h], w->h2, p->hp2);
    o += PXNC[h];
  }
  px_mat(p->wo + o * p->hp2, w->o[0], 1, w->h2, p->hp2);
  px_ctx(w, f, p->ty, row);
  p->ft = px_fold(w, w->t, p->ty, w->ns, w->h1, p->hp1, p->fo);
  for (h = 0; h < 5; h++) {
    p->nx[h] = px_ext(f, h, p->xt[h], row);
    p->fx[h] = p->nx[h] ? px_fold(w, w->x[h], p->xt[h], p->nx[h], w->h2,
                                  p->hp2, p->xo[h]) : NULL;
  }
}

static void px_free(pxm * p) {
  int h;
  xfree(p->ft);
  for (h = 0; h < 5; h++) xfree(p->fx[h]);
}

static INLINE void px_acc(i32 * a, const i32 * r, int n) {
  int j, k;
  for (j = 0; j < n; j += 8) Fk(8, a[j + k] += r[j + k]);
}

static INLINE i32 px_dot(const i16 * w, const i16 * x, int n) {
  i32 a[8] = { 0 };
  int i, k;
  for (i = 0; i < n; i += 8) Fk(8, a[k] += w[i + k] * x[i + k]);
  return a[0] + a[1] + a[2] + a[3] + a[4] + a[5] + a[6] + a[7];
}

static void px_trunk(pxm * p, const u8 * f) {
  const pxw * w = p->w;
  const int hp = p->hp1;
  i32 a[PX_MH];
  int j, s;
  u8 ty[PX_MS];
  px_ctx(w, f, ty, p->row);
  Fj(hp, a[j] = j < w->h1 ? w->tb[j] : 0);
  for (s = 0; s < w->ns; s++)
    px_acc(a, p->ft + p->fo[s] + (u32) (p->row[s] - TOFF[p->ty[s]]) * (u32) hp,
           hp);
  Fj(hp, p->t[j] = (i16) CLAMP(rsh(a[j], w->sh[0] + w->sh[1] - 7), 0, 1024));
}

static void px_head(const pxm * p, const u8 * f, int h, i16 * g) {
  const pxw * w = p->w;
  const int hp = p->hp2;
  i32 x[PX_MH], z[PX_MH];
  u16 row[PX_NX];
  u8 ty[PX_NX];
  int j, s;
  Fj(hp,
    z[j] = j < w->h2 ? rsh(w->b2[h][j] + px_dot(p->w2[h] + j * p->hp1, p->t,
                                                 p->hp1), w->sh[2 + h]) : 0;
    x[j] = 0);
  if (p->nx[h]) {
    px_ext(f, h, ty, row);
    for (s = 0; s < p->nx[h]; s++)
      px_acc(x, p->fx[h] + p->xo[h][s] +
                (u32) (row[s] - TOFF[p->xt[h][s]]) * (u32) hp, hp);
    Fj(hp, z[j] += rsh(x[j], w->sh[0] + w->sh[7 + h] - 7));
  }
  Fj(hp, g[j] = (i16) CLAMP(z[j], 0, 1024));
}

static void px_out(const pxm * p, const i16 * w, const i32 * b, int n,
                   const i16 * g, int sh, i32 * o) {
  const int hp = p->hp2;
  int j;
  Fj(n, o[j] = rsh((b ? b[j] : 0) + px_dot(w + j * hp, g, hp), sh - 1));
}

static u32 px_bin(const pxm * p, const u8 * f) {
  i16 g[PX_MH];
  i32 o;
  px_head(p, f, 0, g);
  px_out(p, p->wo + (PX_NO - 1) * p->hp2, p->w->ob[0], 1, g, p->w->sh[17],
         &o);
  return (u32) acm_squash(o) * 16 + 8;
}

static void px_cand(const u8 * f, int hd, u32 * cv, u8 * ok) {
  static const u8 a1[9] = { 0, 1, 3, 6, 7, 8, 9, 10, 11 },
                  a3[8] = { 0, 1, 6, 7, 8, 9, 10, 11 };
  int j;
  if (hd == 1) {
    cv[0] = f[46];  cv[1] = f[48];
    Fj(9, cv[2 + j] = f[RBS[a1[j]]]);
    cv[11] = f[65];
    Fj(12, ok[j] = cv[j] > 0 && cv[j] < 98;  cv[j] = ok[j] ? cv[j] - 1 : 0);
  } else if (hd == 2) {
    Fj(9, cv[j] = f[RBS[a1[j]] + 1];  ok[j] = f[RBS[a1[j]]] < 98);
    cv[9] = f[47];  cv[10] = f[64];  ok[9] = ok[10] = 1;
  } else if (hd == 3) {
    Fj(8, cv[j] = f[RBS[a3[j]] + 2];  ok[j] = f[RBS[a3[j]]] < 98);
    cv[8] = f[53];  cv[9] = f[51];  ok[8] = ok[9] = 1;
  } else {
    Fj(8, cv[j] = f[RBS[a3[j]] + 3];  ok[j] = f[RBS[a3[j]]] < 98);
    cv[8] = f[54];  cv[9] = f[55];  cv[10] = f[52];  cv[11] = f[56];
    cv[12] = f[66];
    Fj(5, ok[8 + j] = 1);
  }
}

static INLINE u32 px_w(i32 top, i32 l) {
  u32 d = (u32) MIN(top - l, 65535) * 23637u >> 14, k = d >> 8;
  return k > 22 ? 1 : MAX(PXFR[d & 255] >> k, 1u);
}

static void px_dist(const pxm * p, const u8 * f, int hd, u32 * cum) {
  const pxw * w = p->w;
  const int h2 = p->hp2;
  i16 g[PX_MH];
  i32 lg[256], hi[16], lo[16], gb[16], mxl = -(1 << 30);
  u32 cv[16];
  u8 ok[16];
  int j, n = hd == 1 ? 97 : hd == 3 ? 16 : 256;
  px_head(p, f, hd, g);
  if (hd == 1) px_out(p, p->wo, w->ob[1], 97, g, w->sh[18], lg);
  else if (hd == 2) {
    px_out(p, p->wo + 97 * h2, w->ob[2], 34, g, w->sh[19], lg);
    for (j = 34; j < 256; j++) lg[j] = lg[33];
  } else if (hd == 3)
    px_out(p, p->wo + 131 * h2, w->ob[3], 16, g, w->sh[20], lg);
  else {
    px_out(p, p->wo + 147 * h2, w->ob[4], 16, g, w->sh[21], hi);
    px_out(p, p->wo + 163 * h2, NULL, 16, g, w->sh[22], lo);
    Fj(256, lg[j] = hi[j >> 4] + lo[j & 15] + w->o4b[j]);
  }
  px_cand(f, hd, cv, ok);
  px_out(p, p->wg[hd], w->gb[hd], PXNC[hd], g, w->sh[12 + hd], gb);
  Fj(PXNC[hd], if (ok[j]) lg[cv[j]] += gb[j]);
  Fj(n, mxl = MAX(mxl, lg[j]));
  cum[0] = 0;
  if (hd == 2) {
    u32 w33 = px_w(mxl, lg[33]);
    Fj(34, cum[j + 1] = cum[j] + px_w(mxl, lg[j]));
    for (j = 34; j < 256; j++)
      cum[j + 1] = cum[j] + (lg[j] == lg[33] ? w33 : px_w(mxl, lg[j]));
  } else Fj(n, cum[j + 1] = cum[j] + px_w(mxl, lg[j]));
}

static INLINE int sure(u32 p1, u32 opt) {
  u32 t = XO_T(opt) ? 1u << XO_T(opt) : 0;
  return opt & XO_PRI || p1 < t || p1 > 65536 - t;
}

static u32 pval(mx * m, const pcx * x, u32 v, u32 n, acm_h base) {
  acm_dec * d = m->d;
  const u32 * cum = x->cum;
  u32 lo = 0, hi = n, dep = 0;
  acm_h path = 1, s0 = d->sel[0], s1 = d->sel[1];
  while (hi - lo > 1) {
    u32 mid = lo + (hi - lo) / 2, r = cum[hi] - cum[mid],
        t = cum[hi] - cum[lo], sh = blr_ilog(t) > 16 ? blr_ilog(t) - 16 : 0,
        p1 = CLAMP(((r >> sh) << 16) / MAX(t >> sh, 1), 1, 65535);
    int bit;
    if (sure(p1, x->opt)) bit = mx_bit(m, m->dec ? 0 : v >= mid, 65536 - p1);
    else {
      d->sel[1] = PH2(s1, dep);
      if (XO_PROF(x->opt)) {
        d->sel[2] = PH2(s0, dep);
        d->sel[3] = PH2(base, (acm_str[p1 >> 4] + 2048) >> 8);
        d->sel[4] = PH2(s1, path);
        d->sel[5] = PH3(base, dep, x->c);
        d->sel[6] = PH3(base, dep, x->r & 15);
        d->sel[7] = PH4(base, dep, x->mlb > 0, x->rb > 0);
      }
      d->apm[0] = PH2(base, path);
      bit = acm_bit(m->q, m->dec ? 0 : v >= mid, p1, PH2(base, path));
    }
    if (bit) lo = mid; else hi = mid;
    path = PH2(path, bit);
    dep++;
  }
  return lo;
}

#define NT(k) fld(m->nt, x->k)
#define SM(k) fld(m->sm, x->k)
#define EF(k) fld(m->ef, x->k)
#define PA(k) fld(m->pa, x->k)

static u32 c_pres(mx * m, const pcx * x, u32 sym) {
  acm_dec * d = m->d;
  u32 e1 = NT(l1) == 0, e2 = NT(l2) == 0, e4 = NT(l4) == 0, e8 = NT(l8) == 0,
      eu = NT(up) == 0, ep = x->pn == 0, er = x->rn == 0, el = NT(lf) == 0;
  m->ty = T_NOTE;
  if (sure(x->pp1, x->opt))
    return (u32) mx_bit(m, m->dec ? 0 : sym != 0, 65536 - x->pp1);
  d->cx[0] = PH4(1, e1, e2, PH4(e4, e8, eu, x->r & 3));
  d->cx[1] = PH3(2, x->r, x->c);
  d->cx[2] = PH3(3, ep, x->mlb);
  d->cx[3] = PH3(4, er, x->rb);
  d->cx[4] = PH4(5, eu, NT(l16) == 0, x->r & 15);
  d->cx[5] = PH4(6, el, e1, x->c);
  d->cx[6] = PH4(7, x->dens, x->r & 7, e1);
  d->cx[7] = PH4(8, EF(l1), PA(l1) >> 4, e1);
  d->cx[8] = PH4(9, ep, er, eu);
  use(m, T_NOTE, 9, PH3(e1, ep, er), PH2(x->r & 15, eu), PH2(ep, x->mlb),
      PH2(er, e1));
  d->apm[0] = 0;
  if (XO_PROF(x->opt)) {
    d->sel[2] = PH2(T_NOTE, x->c);
    d->sel[3] = PH2(T_NOTE, (acm_str[x->pp1 >> 4] + 2048) >> 8);
    d->sel[4] = PH2(T_NOTE, x->r);
    d->sel[5] = PH3(T_NOTE, x->c, x->r & 3);
    d->sel[6] = PH3(T_NOTE, x->r & 15, x->dens);
    d->sel[7] = PH3(T_NOTE, x->mlb > 0, x->rb > 0);
  }
  return (u32) acm_bit(m->q, m->dec ? 0 : sym != 0, x->pp1, PH2(T_NOTE, 7));
}

static u32 c_pitch(mx * m, const pcx * x, u32 sym) {
  acm_dec * d = m->d;
  if (!(x->opt & XO_PRI)) {
    d->cx[0] = PH3(11, x->ln, x->ln2);
    d->cx[1] = PH3(12, x->pn, x->mlb);
    d->cx[2] = PH3(13, x->rn, x->rb);
    d->cx[3] = PH3(14, NT(up), x->ln);
    d->cx[4] = PH3(15, NT(l4), NT(l8));
    d->cx[5] = PH3(16, NT(lf), x->ln);
    d->cx[6] = PH3(17, x->ln, x->ls);
    d->cx[7] = PH4(18, x->ln, x->r & 3, NT(l2));
    d->cx[8] = PH4(19, x->pn, x->rn, NT(up));
    d->cx[9] = PH2(20, x->ls);
    use(m, T_PITCH, 10, x->mlb ? 1 + (x->pn == 0) : 0, x->ln == 0, x->pn,
        x->ln);
  }
  m->ty = T_PITCH;
  return 1 + pval(m, x, sym - 1, NSYM - 1, T_PITCH);
}

static u32 c_smp(mx * m, const pcx * x, u32 smp, u32 sym) {
  acm_dec * d = m->d;
  u32 z = sym == 0;
  if (!(x->opt & XO_PRI)) {
    d->cx[0] = PH3(1, sym, x->ls);
    d->cx[1] = PH3(2, z, x->ls);
    d->cx[2] = PH3(3, SM(up), z);
    d->cx[3] = PH4(4, SM(l1), NT(l1) == 0, z);
    d->cx[4] = PH3(5, sym, x->c);
    d->cx[5] = PH3(6, SM(lf), z);
    d->cx[6] = PH4(7, z, SM(l4), SM(l8));
    d->cx[7] = PH3(8, x->ps, sym == x->pn);
    d->cx[8] = PH4(9, x->ps, x->mlb, z);
    d->cx[9] = PH4(10, x->rs, x->rb, sym == x->rn);
    use(m, T_SMP, 10, z, x->c, x->ls, SM(up));
  }
  m->ty = T_SMP;
  return pval(m, x, smp, 256, T_SMP);
}

static u32 c_pidx(mx * m, const pcx * x, u32 sym, u32 per) {
  acm_dec * d = m->d;
  int s = (int) sym - 1;
  u32 a = m->aoff[s], cnt = (u32) m->aoff[s + 1] - a, k = 0;
  if (cnt < 2) return cnt ? m->aper[a] : m->dec ? mod_semi_lo(s) : per;
  if (!m->dec) while (m->aper[a + k] != per) k++;
  d->cx[0] = PH2(1, s);
  d->cx[1] = PH3(2, s, x->ls);
  d->cx[2] = PH3(3, s, NT(up) == sym);
  use(m, T_PIDX, 3, s, x->ls, s, x->c);
  return m->aper[a + val(m, k, cnt, PH2(T_PIDX, s))];
}

static u32 c_eff(mx * m, const pcx * x, u32 eff, u32 sym, u32 smp) {
  acm_dec * d = m->d;
  if (!(x->opt & XO_PRI)) {
    d->cx[0] = PH4(1, EF(l1), sym == 0, smp == 0);
    d->cx[1] = PH3(2, EF(up), sym == 0);
    d->cx[2] = PH3(3, EF(l1), PA(l1));
    d->cx[3] = PH3(4, x->r, x->c);
    d->cx[4] = PH3(5, EF(lf), sym == 0);
    d->cx[5] = PH3(6, sym, smp);
    d->cx[6] = PH4(7, EF(l1), EF(l2), EF(l4));
    d->cx[7] = PH4(8, x->pe, sym == x->pn, smp == x->ps);
    d->cx[8] = PH3(9, x->pe, x->mlb);
    d->cx[9] = PH4(10, x->re, x->rb, smp == x->rs);
    d->cx[10] = PH4(11, smp, x->se, sym == 0);
    use(m, T_EFF, 11, PH2(sym == 0, smp == 0), x->r == 0 || x->r == 63,
        EF(up), EF(l1));
  }
  m->ty = T_EFF;
  return pval(m, x, eff, 16, T_EFF);
}

static u32 c_par(mx * m, const pcx * x, u32 par, u32 eff, u32 sym, u32 smp,
                 u32 lp, u32 vol) {
  acm_dec * d = m->d;
  if (!(x->opt & XO_PRI)) {
    acm_h a1 = x->l1 != NOC && m->ef[x->l1] == eff ? m->pa[x->l1] : 256,
          au = x->up != NOC && m->ef[x->up] == eff ? m->pa[x->up] : 256,
          ap = x->pe == eff ? x->pp : 256, ar = x->re == eff ? x->rq : 256;
    d->cx[0] = PH3(1, eff, a1);
    d->cx[1] = PH3(2, eff, au);
    d->cx[2] = PH4(3, eff, smp, sym);
    d->cx[3] = PH3(4, eff, x->r);
    d->cx[4] = PH3(5, eff, lp);
    d->cx[5] = PH4(6, eff, EF(l1), PA(l1));
    d->cx[6] = PH3(7, eff, eff == 12 ? vol : 0);
    d->cx[7] = PH4(8, eff, ap, x->mlb > 0);
    d->cx[8] = PH4(9, eff, ap, MIN(x->mlb, 3));
    d->cx[9] = PH4(10, eff, ar, x->rb > 0);
    d->cx[10] = PH4(11, eff, smp, x->se == eff ? x->sp : 256);
    use(m, T_PAR, 11, eff, PH2(eff, sym == 0), PH2(eff, smp), PH2(eff, lp));
  }
  m->ty = T_PAR;
  return pval(m, x, par, 256, T_PAR);
}

static int xcb(void * ctx, int bit, u32 p0) { return mx_bit(ctx, bit, p0); }

static u8 * tdup(const mx * m, sz rowc, sz patc) {
  const mod_lay * L = &m->L;
  sz np = L->ncell / patc, p, k, q, r;
  u8 * td = xcalloc((np + 1) * rowc, 1);
  if (!m->in || !getenv("BLR_MOD_STATS")) return td;
  for (p = 0; p < np; p++)
    for (k = 0; k < rowc; k++) {
      const u8 * a = m->in + L->pat + 4 * (p * patc + k);
      int z = 1;
      for (r = 0; r < MOD_ROWS && z; r++)
        z = !memcmp(a + 4 * r * rowc, "\0\0\0\0", 4);
      td[p * rowc + k] = z ? 2 : 0;
      if (z) continue;
      for (q = 0; q < p * rowc + k && !td[p * rowc + k]; q++) {
        const u8 * b = m->in + L->pat + 4 * (q / rowc * patc + q % rowc);
        int same = 1;
        for (r = 0; r < MOD_ROWS && same; r++)
          same = !memcmp(a + 4 * r * rowc, b + 4 * r * rowc, 4);
        if (same) td[p * rowc + k] = q % rowc == k ? 1 : 3;
      }
    }
  return td;
}

void pat_x(mx * m) {
  static const int prof[3] = { 0, ACM_MODX, ACM_VORBIS };
  const mod_lay * L = &m->L;
  const u32 opt = m->cf->opt;
  const int ch = L->ch;
  const sz rowc = (sz) ch, patc = (sz) ch * MOD_ROWS;
  const int mb = CLAMP((int) blr_ilog((u32) L->ncell) + 1, 8, 22);
  acm * q0 = m->q;
  acm_dec * d0 = m->d;
  u8 * ln = xcalloc(rowc, 1), * ls = xcalloc(rowc, 1),
     * lp = xcalloc(rowc * 16, 1), * ln2 = xcalloc(rowc, 1),
     * dn = xcalloc(rowc, 1), * gp = xcalloc(rowc, 1), * le = xcalloc(rowc, 1),
     * lq = xcalloc(rowc, 1), sef[256], spa[256], su[256], pbs[256],
     sbs[NSYM], lpg[16], pf[NF];
  u32 * rt = xcalloc((sz) 1 << mb, sizeof *rt), * hp = xcalloc(rowc, 4),
      * tpt = xcalloc((sz) 1 << mb, 4), rml = 0, rok = 1, nsu = 0, rnc = 0,
      pcum[257];
  pxm * pm = xmalloc(sizeof *pm);
  sz rp = NOC, i;
  mtrk T;
  mt_init(&T, L->ncell, rowc);
  memset(sef, 0, sizeof sef);  memset(spa, 0, sizeof spa);
  memset(su, 0, sizeof su);  memset(lpg, 0, sizeof lpg);
  memset(pbs, 0, sizeof pbs);  memset(sbs, 0, sizeof sbs);
  if (XO_PROF(opt)) {
    m->q = acm_new(prof[MIN(XO_PROF(opt), 2)]);  m->d = acm_d(m->q);
    acm_bind(m->q, xcb, m);
  }
  if (XO_CAP(opt)) {
    int t;
    for (t = T_NOTE; t <= T_PAR; t++)
      acm_class(m->q, t, ACM_EFULL, (int) XO_CAP(opt));
  }
  px_init(pm, PXMODEL[0]);
  m->tdup = tdup(m, rowc, patc);
  Fi(L->ncell,
    const u8 * src = m->in ? m->in + L->pat + 4 * i : NULL;
    pcx X, * x = &X;
    u32 per = 0, sym = 0, smp = 0, eff = 0, par = 0, vol;
    sz pr, q;
    int c = (int) (i % rowc), r = (int) (i / rowc % MOD_ROWS);
    m->cls = m->tdup[(i / patc) * rowc + i % rowc];
    x->c = c;  x->r = r;  x->opt = opt;  x->p = (u32) (i / patc);
    x->l1 = r >= 1 ? i - rowc : NOC;  x->l2 = r >= 2 ? i - 2 * rowc : NOC;
    x->l3 = r >= 3 ? i - 3 * rowc : NOC;  x->l4 = r >= 4 ? i - 4 * rowc : NOC;
    x->l8 = r >= 8 ? i - 8 * rowc : NOC;
    x->l16 = r >= 16 ? i - 16 * rowc : NOC;
    x->up = i >= patc ? i - patc : NOC;  x->lf = c ? i - 1 : NOC;
    x->up2 = i >= 2 * patc ? i - 2 * patc : NOC;
    if (!c && i >= rowc) {
      u32 hk = 0, k;
      for (k = 0; k < (u32) ch; k++) hk = chash(hk, ckey(m, i - rowc + k), 32);
      hk >>= 32 - mb;
      if (rp != NOC && rok) { rp += rowc;  rml++; }
      else { rp = rt[hk] ? rt[hk] - 1 : NOC;  rml = 0; }
      rt[hk] = (u32) i + 1;
      rok = 1;
    }
    { u32 hk = chash(r ? hp[c] : 0x5bd1e995u, (u32) r, mb), t = tpt[hk];
      x->tp = t && t - 1 < i ? t - 1 : NOC;
      tpt[hk] = (u32) i + 1; }
    pr = mt_pred(&T, c, i);
    q = rp != NOC && rp + (sz) c < i ? rp + (sz) c : NOC;
    x->pr = pr;  x->q = q;
    x->pn = fld(m->nt, pr);  x->ps = fld(m->sm, pr);
    x->pe = fld(m->ef, pr);  x->pp = fld(m->pa, pr);
    x->rn = fld(m->nt, q);  x->rs = fld(m->sm, q);
    x->re = fld(m->ef, q);  x->rq = fld(m->pa, q);
    x->mlb = pr == NOC ? 0 : 1 + MIN(T.ml[c], 14);
    x->rb = q == NOC ? 0 : 1 + MIN(rml, 6);
    x->ln = ln[c];  x->ls = ls[c];  x->ln2 = ln2[c];  x->dens = MIN(dn[c], 7);
    x->gap = gp[c];  x->le = le[c];  x->lq = lq[c];  x->nsu = nsu;
    x->pbs = pbs[x->ls];  x->rnc = c ? rnc : 0;  x->cum = pcum;
    if (src) cell_get(src, &per, &sym, &smp, &eff, &par);
    feat(m, x, pf);
    px_trunk(pm, pf);
    x->pp1 = px_bin(pm, pf);
    if (c_pres(m, x, sym)) {
      px_dist(pm, pf, 1, pcum);
      sym = c_pitch(m, x, sym);
      per = c_pidx(m, x, sym, per);
    } else sym = 0;
    pf[58] = (u8) sym;  pf[64] = sbs[sym];
    px_dist(pm, pf, 2, pcum);
    smp = c_smp(m, x, smp, sym);
    x->se = sef[smp ? smp : ls[c]];  x->sp = spa[smp ? smp : ls[c]];
    vol = smp && smp <= (u32) L->ns ? m->h[45 + 30 * (smp - 1)] : 0;
    pf[59] = (u8) smp;  pf[57] = (u8) sclass(m, smp);
    pf[53] = (u8) x->se;  pf[54] = (u8) x->sp;
    px_dist(pm, pf, 3, pcum);
    eff = c_eff(m, x, eff, sym, smp);
    pf[60] = (u8) eff;  pf[55] = lp[c * 16 + (int) eff];
    pf[56] = (u8) vol;  pf[66] = lpg[eff];
    px_dist(pm, pf, 4, pcum);
    par = c_par(m, x, par, eff, sym, smp, lp[c * 16 + (int) eff], vol);
    cell_put(m, i, per, sym, smp, eff, par);
    gp[c] = (u8) (sym ? 0 : MIN(gp[c] + 1, 255));
    hp[c] = chash(r ? hp[c] : 0x5bd1e995u, ckey(m, i), 32);
    if (sym && smp) { pbs[smp] = (u8) sym;  sbs[sym] = (u8) smp; }
    if (eff || par) { le[c] = (u8) eff;  lq[c] = (u8) par; }
    lpg[eff] = (u8) par;
    rnc = x->rnc + (sym != 0);
    if (smp && !su[smp]) { su[smp] = 1;  nsu++; }
    if (sym) { ln2[c] = ln[c];  ln[c] = (u8) sym; }
    dn[c] = (u8) (sym ? MIN(dn[c] + 2, 15) : dn[c] ? dn[c] - 1 : 0);
    if (smp) ls[c] = (u8) smp;
    lp[c * 16 + (int) eff] = (u8) par;
    if (eff || par) {
      sef[smp ? smp : ls[c]] = (u8) eff;  spa[smp ? smp : ls[c]] = (u8) par;
    }
    if (q == NOC || ckey(m, q) != ckey(m, i)) rok = 0;
    mt_next(&T, m, c, i, pr);
    );
  if (XO_PROF(opt)) { acm_free(m->q);  m->q = q0;  m->d = d0; }
  px_free(pm);
  xfree(pm);  xfree(ln);  xfree(ls);  xfree(lp);  xfree(ln2);  xfree(dn);
  xfree(gp);  xfree(le);  xfree(lq);  xfree(rt);  xfree(hp);  xfree(tpt);
  xfree(m->tdup);
  mt_free(&T);
}

