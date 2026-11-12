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

#include "vorbis/zc.h"
#include "coder/acm.h"
#ifdef VBZ_STATS
#include <math.h>
#include <stdio.h>
#endif

#define NCH   8
#define NBIN  4096
#define NPART 1024
#define TBUDGET ((sz) 32 << 20)
#define NBK   64
#define KB    8
#define NWS   64
#define PNEAR 96
#define VBZ_SMALL ((uint64_t) 65536)
#define VBZ_SMALLD ((uint64_t) 16384)

typedef struct { u16 p, n; } zc_t;
typedef zc_t zs_t;
typedef struct { i32 w[3]; } zm_t;

enum { R60, R255, R1023, NRT };
static u16 RT[NRT][1024];
static const u16 RL[NRT] = { 60, 255, 1023 };
static u8 VBKT[257], V8T[257];
static void vbz_prepare(void) {
  int i, j, x;
  if (RT[0][0]) return;
  Fi(NRT,
    Fj(1024, RT[i][j] = (u16) (655360u / (10u * (u32) MIN(j, RL[i]) + 16u))));
  acm_prepare();
  for (x = -1; x < 256; x++) {
    VBKT[x + 1] =
      (u8) (x < 0 ? 31 : x < 24 ? x : x < 48 ? 24 + (x - 24) / 8 : 30);
    V8T[x + 1] =
      (u8) (x < 0 ? 7 : x < 3 ? x : x < 5 ? 3 : x < 9 ? 4 : x < 17 ? 5 : 6);
  }
}

static INLINE u32 cp(const zc_t * c) { return c->p ^ 0x8000u; }

static INLINE void cup(zc_t * c, int bit, const u16 * rt, int lim) {
  u32 n = c->n, r = rt[n], p = cp(c);
  if ((int) n < lim) c->n = (u16) (n + 1);
  if (bit) p += (65535 - p) * r >> 16;
  else p -= p * r >> 16;
  p = CLAMP(p, 32, 65503);
  c->p = (u16) (p ^ 0x8000);
}

typedef struct {
  zc_t * a, * c, * p, * e;
  zs_t * b;
  zm_t x[NWS];
  int la, lb, lc, pk;
#ifdef VBZ_STATS
  double bits, n;
#endif
} zdc;

static void dc_new(zdc * d, sz na, sz nb, sz nc, int la, int lb) {
  memset(d, 0, sizeof *d);
  d->a = xcalloc(na, sizeof *d->a);
  d->b = nb ? xcalloc(nb * (KB + 1), sizeof *d->b) : NULL;
  d->c = nc ? xcalloc(nc, sizeof *d->c) : NULL;
  d->la = la;  d->lb = lb;  d->lc = R255;
}

static void dc_parent(zdc * d, sz np, int pk) {
  d->p = xcalloc(np, sizeof *d->p);  d->pk = pk;
}

static void dc_free(zdc * d) {
  xfree(d->a);  xfree(d->b);  xfree(d->c);  xfree(d->p);  xfree(d->e);
}

typedef struct zab { u32 tot, mark;  u16 v[1]; } zab;
#define ZCUM(a) ((a)->v)
#define ZCNT(a, n) ((a)->v + (n) + 1)
#define ZBKT(a, n) ((a)->v + 2 * (n) + 1)

typedef struct {
  u32 np, ns;
  u16 * epat, * erank;
  u32 * voff, * vent;
  u16 * vcum;
  u8 * vuni, * vnz;
  u16 * prior;
  i16 * dig;
  zab ** ab;  u32 nab, zbb;
} zfast;

#define RF_Z   1
#define RF_NZ  2
#define RF_S   4
#define RF_NEG 8
typedef struct {
  u32 nv, dim, pw[25], id, mm, rs, roff[25];
  u16 * rec;
  int64_t del;
  uint64_t rdel;
  zfast * fz;  int fok;
} zbook;

typedef struct {
  u32 ncl, pv, pw[25];
  uint64_t * m[25];
  u32 nn;
  u16 * nc[25];
} zres;

#define ZN_LO 1
#define ZN_HI 65535

typedef struct {
  int valid, n;
  u8 used[NCH];
  int fno[NCH];
  short fy[NCH][VB_MAXPOST];
  u8 fv[NCH][VB_MAXPOST];
  u8 fd[NCH][VB_MAXPOST];
  u8 flo[NCH][NBIN];
  short fpx[NCH][VB_MAXPOST], fpy[NCH][VB_MAXPOST];
  int nfp[NCH];
  i32 rec[NCH][NBIN];
  signed char dg[3][NCH][NBIN];
  u8 rcl[4][NCH][NPART];
  int role[NCH], partner[NCH];
  i32 lr[NCH][NBIN];
} frame;

#define NLS 6
typedef struct zls {
  int64_t a[NLS * (NLS + 1) / 2], b[NLS];  i32 w[NLS];
} zls;

enum { D_Z, D_S, D_M, D_RC, D_FZ, D_FE, D_FS, D_FL, D_FM, D_N };

struct vbz {
  int opt;
  int small;
  rc_enc * e;  rc_dec * d;
  zdc dc[D_N];
  frame f[5];
  int ci, pi[2], p2[2], bf, nch;
  u32 fzl;
  u32 fo[16];
  u32 nbk;
  u32 zlast[NCH], zrun[NCH];
  int lca[NCH], lcx[NCH];
  zc_t yc[2][2][9][9][40];
  zc_t usd[3][3];
  int64_t tn[2][NCH][NBIN], td[2][NCH][NBIN];
  struct zls * ls[2][NCH];
  i32 lp[NCH][NBIN], pc[NCH][NBIN];
  int64_t nbs[NCH][NBIN];
  i32 av[2][NCH][NBIN];
  u8 sh[3][NCH];
  long ie;
  int ws;
  int pcok, nbok;
  sz tbl;
  u32 pdist[NCH][NCH];
  signed char fpt[NCH];
  u8 cj[4][NCH];
  uint64_t * rcp;
};

#define CUR(m) (&(m)->f[(m)->ci])
#define PRV(m) (&(m)->f[(m)->pi[(m)->bf]])
#define PRV2(m) (&(m)->f[(m)->p2[(m)->bf]])

static const u8 LQB[66] = {
  0, 1, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
  6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 7
};

static INLINE int lq(const zbook * B, int64_t x, int lim) {
  uint64_t a = (uint64_t) ABS(x), q;
  int b;
  if (a > ((uint64_t) 1 << 30)) a = (uint64_t) 1 << 30;
  q = 2 * a * B->rdel >> 32;
  b = LQB[MIN(q, 65)];
  if (b > lim) b = lim;
  return x < 0 ? -b : b;
}

#define SZ_ZA ((sz) NBK * 16 * 16 * 8 * 4 * 8)
#define SZ_ZB ((sz) 8 * 8 * 32 * 8 * 8)
#define SZ_ZC ((sz) 32 * 8)
#define SZ_SA ((sz) 16 * 16 * 16 * 8)
#define SZ_SB ((sz) NBK)
#define SZ_SC ((sz) 16 * 16 * 4)
#define SZ_MA ((sz) NBK * 16 * 8 * 16 * 8 * 8)
#define SZ_MB ((sz) 16 * 4)
#define SZ_MC ((sz) 16 * 8 * 8 * 8)
#define SZ_BIN ((sz) 2 * NCH * NBIN)
#define SZ_RA ((sz) 4 * 16 * 32 * 256)
#define SZ_RB ((sz) 16 * 32 * 16 * 8)
#define SZ_RC ((sz) 16)
#define SZ_ZP ((sz) 16 * 16 * 8 * 4)
#define SZ_SP ((sz) 16 * 16 * 8)
#define SZ_MP ((sz) 16 * 8 * 16 * 8 * 8)

vbz * vbz_new(int opt, uint64_t len) {
  vbz * m = xcalloc(1, sizeof *m);
  int mix = (opt & VBZ_O_MIX) != 0, sse = !(opt & VBZ_O_NOSSE);
  vbz_prepare();
  if (!mix) opt &= ~(VBZ_O_AVG | VBZ_O_BIN);
  if (!(opt & VBZ_O_AVG)) opt &= ~VBZ_O_BIN;
  m->small = len &&
             len < (opt & (VBZ_O_FAST | VBZ_O_FL) ? VBZ_SMALL : VBZ_SMALLD);
  if (m->small) opt |= VBZ_O_NOTP;
  m->opt = opt;
  dc_new(m->dc + D_Z, SZ_ZA, sse ? SZ_ZB : 0, mix ? SZ_ZC : 0, R255, R255);
  dc_new(m->dc + D_S, SZ_SA, sse ? SZ_SB : 0, mix ? SZ_SC : 0, R1023, R255);
  dc_new(m->dc + D_M, SZ_MA, sse ? SZ_MB : 0, opt & VBZ_O_AVG ? SZ_MC : 0,
         R255, R255);
  if (opt & VBZ_O_BIN) {
    m->dc[D_Z].e = xcalloc(SZ_BIN * 8, sizeof (zc_t));
    m->dc[D_S].e = xcalloc(SZ_BIN * 9, sizeof (zc_t));
    m->dc[D_M].e = xcalloc(SZ_BIN * 32, sizeof (zc_t));
  }
  dc_new(m->dc + D_RC, SZ_RA * 3, SZ_RB, mix ? SZ_RC : 0, R255, R60);
  dc_new(m->dc + D_FZ, (sz) 4 * 64 * 8 * 32 * 8, 8, 0, R60, R255);
  dc_new(m->dc + D_FE, (sz) 16 * 8, 64, 0, R255, R255);
  dc_new(m->dc + D_FS, (sz) 16 * 16, 8, 0, R255, R255);
  dc_new(m->dc + D_FL, (sz) 4 * 16 * 16, 32, 0, R255, R255);
  dc_new(m->dc + D_FM, (sz) 16 * 16, 0, 0, R255, R255);
  dc_parent(m->dc + D_Z, SZ_ZP, 32);  dc_parent(m->dc + D_S, SZ_SP, 32);
  dc_parent(m->dc + D_M, SZ_MP, 16);
  m->ci = 4;  m->pi[0] = 0;  m->pi[1] = 1;  m->p2[0] = 2;  m->p2[1] = 3;
  m->ie = -1;
  return m;
}

void vbz_free(vbz * m) {
  int i;
  if (!m) return;
#ifdef VBZ_STATS
  { static const char * NAME[D_N] = {
      "zero", "sign", "magnitude", "class", "floor zero", "floor equal",
      "floor sign", "floor length", "floor bits"
    };
    double tot = 0;
    Fi(D_N, tot += m->dc[i].bits);
    Fi(D_N,
      if (m->dc[i].n)
        fprintf(stderr, "vbz %-12s %12.0f B %6.2f%% %11.0f decisions "
                        "%6.4f bits each\n", NAME[i], m->dc[i].bits / 8,
                100 * m->dc[i].bits / (tot ? tot : 1), m->dc[i].n,
                m->dc[i].bits / m->dc[i].n)); }
#endif
  Fi(D_N, dc_free(m->dc + i));
  Fi(2 * NCH, xfree(m->ls[i / NCH][i % NCH]));
  xfree(m->rcp);  xfree(m);
}

void vbz_bind(vbz * m, rc_enc * e, rc_dec * d) {
  m->e = e;  m->d = d;
}

void vbz_link(vbz * m) {
  int i;
  Fi(5, m->f[i].valid = 0);
}

static INLINE int zbit(vbz * m, int bit, u32 p) {
  if (m->e) { rc_enc_bit_raw(m->e, 65536 - p, bit);  return bit; }
  return rc_dec_bit_raw(m->d, 65536 - p);
}

static INLINE int zcodep(vbz * m, int t, int bit, sz ia, const zs_t * sb,
                         sz ib, long ic, long ip, int xe) {
  zdc * d = m->dc + t;
  zc_t * a = d->a + ia, * c = NULL, * q = ip >= 0 ? d->p + ip : NULL,
       * e = NULL;
  zs_t * lo = NULL, * hi = NULL;
  zm_t * x = d->x + m->ws;
  u32 p = cp(a), pa = p, fr = 0, il = 0, ih = 0;
  int s0 = 0, s1 = 0, s2 = 0;
  long ie = -1;
  if (xe) { ie = m->ie;  m->ie = -1; }
  if (q && a->n < d->pk) {
    pa = (pa * a->n + cp(q) * (u32) (d->pk - a->n)) / (u32) d->pk;  p = pa;
  }
  if (sb) {
    u32 pos = pa * KB, j = pos >> 16;
    fr = pos & 0xFFFF;
    lo = d->b + ib * (KB + 1) + j;  hi = lo + 1;
    il = j << 13;  ih = j == KB - 1 ? 65535 : (j + 1) << 13;
    p = ((u32) (u16) (lo->p + il) * (65536 - fr) +
         (u32) (u16) (hi->p + ih) * fr) >> 16;
    p = (3 * p + pa) >> 2;
  }
  if (ic >= 0) {
    c = d->c + ic;
    s0 = acm_str[p >> 4];  s1 = acm_str[cp(c) >> 4];
    if (ie >= 0) { e = d->e + ie;  s2 = acm_str[cp(e) >> 4]; }
    p = (u32) acm_squash((int) (((int64_t) (x->w[0] + 65536) * s0 +
                                 (int64_t) x->w[1] * s1 +
                                 (int64_t) x->w[2] * s2) >> 16)) << 4;
  }
  p = CLAMP(p, 32, 65503);
  bit = zbit(m, bit, p);
#ifdef VBZ_STATS
  if (m->e) { d->bits -= log2(bit ? p / 65536.0 : 1 - p / 65536.0);  d->n++; }
#endif
  if (c) {
    int err = ((bit << 16) - (int) p) >> 4;
    x->w[0] += (s0 * err) >> 11;  x->w[1] += (s1 * err) >> 11;
    x->w[2] += (s2 * err) >> 11;
    cup(c, bit, RT[d->lc], RL[d->lc]);
    if (e) cup(e, bit, RT[R60], RL[R60]);
  }
  if (lo) {
    u32 tg = bit ? 65535 : 0, rl = RT[d->lb][lo->n], rh = RT[d->lb][hi->n];
    int64_t wl = (int64_t) rl * (65536 - fr) >> 16,
            wh = (int64_t) rh * fr >> 16;
    i32 vl = (u16) (lo->p + il), vh = (u16) (hi->p + ih);
    vl += (i32) (((int64_t) tg - vl) * wl >> 16);
    vh += (i32) (((int64_t) tg - vh) * wh >> 16);
    lo->p = (u16) ((u32) vl - il);  hi->p = (u16) ((u32) vh - ih);
    if (lo->n < RL[d->lb]) lo->n++;
    if (hi->n < RL[d->lb]) hi->n++;
  }
  cup(a, bit, RT[d->la], RL[d->la]);
  if (q) cup(q, bit, RT[d->la], RL[d->la]);
  return bit;
}

static INLINE int zcode(vbz * m, int t, int bit, sz ia, const zs_t * sb,
                        sz ib, long ic) {
  return zcodep(m, t, bit, ia, sb, ib, ic, -1, 0);
}

static INLINE u32 qp16(u32 p) { return (u32) (acm_str[p >> 4] + 2048) >> 8; }
static INLINE u32 qp32(u32 p) { return (u32) (acm_str[p >> 4] + 2048) >> 7; }

static u16 pclamp(u32 p) { return (u16) CLAMP(p, 2, 65534); }

static void mkrec(u16 * r, const uint64_t * w, const int * dd, u32 nv,
                  u32 mm) {
  uint64_t ms[3] = { 0, 0, 0 };
  u32 i, j, k;
  Fi(nv, ms[dd[i] < 0 ? 0 : dd[i] > 0 ? 2 : 1] += w[i]);
  r[0] = (u16) ((ms[1] && (ms[0] || ms[2]) ? RF_Z : 0) | (!ms[1] ? RF_NZ : 0)
              | (ms[0] && ms[2] ? RF_S : 0) | (ms[0] ? RF_NEG : 0));
  r[1] = pclamp(vb_pfrac(ms[0] + ms[2], ms[1]));
  r[2] = pclamp(vb_pfrac(ms[0], ms[2]));
  Fk(2,
    for (j = 1; j <= mm; j++) {
      uint64_t ge = 0, gt = 0;
      Fi(nv,
        if ((k ? dd[i] < 0 : dd[i] > 0) && w[i]) {
          if ((u32) ABS(dd[i]) >= j) ge += w[i];
          if ((u32) ABS(dd[i]) > j) gt += w[i];
        });
      r[3 + k * mm + j - 1] =
        (u16) (!gt ? 0 : ge == gt ? 1 : pclamp(vb_pfrac(gt, ge - gt)));
    });
}

static zbook * book(vbz * m, vb_book * b) {
  zbook * p = b->zc;
  u32 i;
  int dd[256];
  int64_t del;
  if (p) return p;
  FATAL_UNLESS(b->look && b->nv <= 256,
               "vorbis: residue book of %lu multiplicands",
               (unsigned long) b->nv);
  p = xcalloc(1, sizeof *p);
  p->id = m->nbk < NBK - 1 ? m->nbk++ : NBK - 1;
  del = vb_bdel(b);
  p->del = del;  p->rdel = ((uint64_t) 1 << 32) / (uint64_t) del;
  p->nv = b->nv;  p->dim = b->dim;
  Fi(b->nv,
    dd[i] = (int) b->mult[i] - (int) b->off;
    if ((u32) ABS(dd[i]) > p->mm) p->mm = (u32) ABS(dd[i]));
  p->rs = 3 + 2 * p->mm;
  b->zc = p;
  return p;
}

static void brec(vbz * m, vb_book * b, zbook * p) {
  u32 k, i, j, nrec = 0;
  int dd[256];
  uint64_t w[256], * ms[25], sum = vb_gsum(b->nv, b->dim);
  Fi(b->nv, dd[i] = (int) b->mult[i] - (int) b->off);
  if (sum && m->tbl + sum * (sizeof (uint64_t) + p->rs * 2) <= TBUDGET) {
    m->tbl += sum * p->rs * 2;
    vb_gfill(ms, p->pw, b, b->nv, b->dim, 0);
    Fk(b->dim, p->roff[k] = nrec;  nrec += p->pw[k]);
    p->rec = xmalloc((sz) nrec * p->rs * sizeof *p->rec);
    Fk(b->dim,
      Fj(p->pw[k],
        Fi(b->nv, w[i] = ms[k + 1][j + i * p->pw[k]]);
        mkrec(p->rec + (sz) (p->roff[k] + j) * p->rs, w, dd, b->nv, p->mm)));
    for (k = 0; k <= b->dim; k++) xfree(ms[k]);
  } else {
    p->rec = xmalloc((sz) p->rs * sizeof *p->rec);
    Fi(b->nv, w[i] = 1);
    mkrec(p->rec, w, dd, b->nv, p->mm);
  }
}

static void zfast_free(zfast * Z);

void vbz_book_free(void * q) {
  zbook * p = q;
  if (!p) return;
  zfast_free(p->fz);  xfree(p->rec);  xfree(p);
}

static zres * res(vbz * m, vb_res * r, vb_book * cb) {
  zres * p = r->zc;
  u32 pv = cb->dim;
  uint64_t sum;
  if (p) return p;
  p = xcalloc(1, sizeof *p);
  p->ncl = r->ncl;  p->pv = pv;
  for (p->nn = 2; p->nn < 2 * r->ncl; p->nn *= 2) ;
  sum = vb_gsum(r->ncl, pv);
  if (sum && m->tbl + sum * sizeof (uint64_t) <= TBUDGET) {
    m->tbl += sum * sizeof (uint64_t);
    vb_gfill(p->m, p->pw, cb, r->ncl, pv, 1);
  }
  r->zc = p;
  return p;
}

void vbz_res_free(void * q) {
  zres * p = q;
  u32 k;
  if (!p) return;
  Fk(25, xfree(p->m[k]);  xfree(p->nc[k]));
  xfree(p);
}

void vbz_frame(vbz * m, int bf, u32 n, u32 nch) {
  frame * F;
  int i;
  m->bf = bf;
  F = CUR(m);
  F->valid = 1;
  m->nch = (int) MIN(nch, NCH);
  memset(F->role, 0, sizeof F->role);
  memset(F->partner, 0, sizeof F->partner);
  memset(m->fpt, 0xFF, sizeof m->fpt);  memset(m->cj, 0, sizeof m->cj);
  F->n = (int) MIN(n, NBIN);
  memset(F->used, 0, sizeof F->used);
  Fi(NCH, F->fno[i] = -1;  F->nfp[i] = 0;  m->lca[i] = m->lcx[i] = 0);
  if (!(m->opt & VBZ_O_REG))
    Fi(4, memset(F->rcl[i], 0, (sz) m->nch * sizeof *F->rcl[i]));
  Fi(m->nch,
    memset(F->rec[i], 0, (sz) F->n * sizeof **F->rec);
    if (m->opt & (VBZ_O_REG | VBZ_O_LITE)) continue;
    memset(F->flo[i], 0, (sz) F->n);
    memset(F->dg[0][i], 0, (sz) F->n);  memset(F->dg[1][i], 0, (sz) F->n);
    memset(F->dg[2][i], 0, (sz) F->n));
  memset(m->zlast, 0xFF, sizeof m->zlast);
}

void vbz_couple(vbz * m, u32 mag, u32 ang) {
  frame * F = CUR(m);
  if (mag >= NCH || ang >= NCH) return;
  F->role[mag] = 1;  F->partner[mag] = (int) ang;
  F->role[ang] = 2;  F->partner[ang] = (int) mag;
}

static INLINE int64_t scl(const frame * X, const frame * F, int c, int j) {
  return vb_rat(X->lr[c][j], (int) X->flo[c][j] - (int) F->flo[c][j]);
}

static int tpok(const vbz * m) {
  const frame * F = &m->f[m->ci], * P = &m->f[m->pi[m->bf]],
              * P2 = &m->f[m->p2[m->bf]];
  return P->valid && P2->valid && P->n == F->n && P2->n == F->n;
}

static void lsreg(const vbz * m, int c, int j, int64_t * r) {
  const frame * F = &m->f[m->ci], * P = &m->f[m->pi[m->bf]],
              * P2 = &m->f[m->p2[m->bf]];
  int n = F->n, k;
  r[0] = scl(P, F, c, j);  r[1] = scl(P2, F, c, j);
  r[2] = j > 0 ? scl(P, F, c, j - 1) : 0;
  r[3] = j + 1 < n ? scl(P, F, c, j + 1) : 0;
  r[4] = j > 0 ? scl(P2, F, c, j - 1) : 0;
  r[5] = j + 1 < n ? scl(P2, F, c, j + 1) : 0;
  Fk(NLS, r[k] = vb_clip24(r[k]));
}

static i32 lssolve(zls * s, const int64_t * r) {
  int64_t A[NLS][NLS], B[NLS], W[NLS], tr = 0, md = 0, lam, o = 0, mx = 0;
  int i, j, k, sh = 0;
  Fi(NLS,
    tr += s->a[i * (i + 3) / 2];
    if (s->a[i * (i + 3) / 2] > md) md = s->a[i * (i + 3) / 2]);
  if (tr <= 0) return 0;
  Fi(NLS, if (ABS(s->b[i]) > md) md = ABS(s->b[i]));
  while ((md >> sh) >= ((int64_t) 1 << 36)) sh++;
  lam = (tr >> sh) >> 10;
  k = 0;
  Fi(NLS,
    for (j = 0; j <= i; j++, k++) A[i][j] = A[j][i] = s->a[k] >> sh);
  Fi(NLS, B[i] = (s->b[i] >> sh) * 16384;  W[i] = s->w[i]);
  Fi(NLS,
    int64_t den = A[i][i] + lam, nm = B[i] + A[i][i] * W[i], w;
    Fj(NLS, nm -= A[i][j] * W[j]);
    w = den > 0 ? nm / den : 0;
    W[i] = CLAMP(w, -131072, 131072));
  Fi(NLS,
    s->w[i] = (i32) W[i];  o += r[i] * W[i];
    if (ABS(r[i]) > mx) mx = ABS(r[i]));
  o >>= 14;
  return (i32) CLAMP(o, -3 * mx, 3 * mx);
}

void vbz_ready(vbz * m) {
  const frame * F = CUR(m), * P = PRV(m), * P2 = PRV2(m);
  int i, j;
  m->nbok = P->valid && P->n == F->n && !(m->opt & VBZ_O_FL);
  if (m->nbok)
    Fi(m->nch,
      const i32 * r = P->rec[i];
      int64_t * o = m->nbs[i], s = 0;
      int n = F->n;
      for (j = 0; j < 2 && j < n; j++) s += ABS(r[j]);
      Fj(n,
        if (j + 2 < n) s += ABS(r[j + 2]);
        if (j - 3 >= 0) s -= ABS(r[j - 3]);
        o[j] = s));
  m->pcok = !(m->opt & VBZ_O_NOTP) && tpok(m);
  if (!m->pcok) return;
  Fi(m->nch,
    const int64_t * tn = m->tn[m->bf][i], * td = m->td[m->bf][i];
    if (!F->used[i]) {
      memset(m->lp[i], 0, (sz) F->n * sizeof **m->lp);  continue;
    }
    if (m->opt & VBZ_O_LS) {
      zls * s = m->ls[m->bf][i];
      if (!s) s = m->ls[m->bf][i] = xcalloc(NBIN, sizeof *s);
      Fj(F->n,
        int64_t r[NLS];
        lsreg(m, i, j, r);  m->lp[i][j] = lssolve(s + j, r));
      continue;
    }
    Fj(F->n,
      int64_t a = td[j] > 0 ? tn[j] / ((td[j] >> 14) + 1) : 0, v;
      a = CLAMP(a, -32768, 32768);
      v = td[j] > 0 ? (a * scl(P, F, i, j) >> 14) - scl(P2, F, i, j) : 0;
      m->lp[i][j] = (i32) CLAMP(v, -(1 << 28), 1 << 28)));
  Fi(m->nch,
    int mg = F->role[i] == 2 ? F->partner[i] : i, a = F->partner[mg];
    if (!F->role[i]) {
      memcpy(m->pc[i], m->lp[i], (sz) F->n * sizeof **m->pc);  continue;
    }
    Fj(F->n,
      i32 l = m->lp[mg][j], r = m->lp[a][j];
      if (i == mg) m->pc[i][j] = ABS(l) > ABS(r) ? l : r;
      else m->pc[i][j] = ABS(l) > ABS(r) ? (l > 0 ? l - r : r - l)
                                         : (r > 0 ? l - r : r - l)));
}

void vbz_frame_end(vbz * m) {
  frame * F = CUR(m), * P = PRV(m), * P2 = PRV2(m);
  int old = m->p2[m->bf], i, j;
  Fi(m->nch,
    if (F->role[i] != 2 && !(m->opt & VBZ_O_NOTP))
      Fj(F->n,
        i32 lv = F->rec[i][j], rv = 0;
        if (F->role[i] == 1) {
          int a = F->partner[i];
          vb_uncouple(F->rec[i][j], F->rec[a][j], &lv, &rv);
          F->lr[a][j] = F->used[a] ? rv : 0;
        }
        F->lr[i][j] = F->used[i] ? lv : 0));
  if (m->opt & VBZ_O_AVG)
    Fi(m->nch,
      Fj(F->n,
        i32 * a = &m->av[m->bf][i][j];
        *a += (ABS(F->rec[i][j]) - *a) >> 3));
  if (!(m->opt & VBZ_O_NOTP) && tpok(m))
    Fi(m->nch,
      int64_t * tn = m->tn[m->bf][i], * td = m->td[m->bf][i];
      if (m->opt & VBZ_O_LS) {
        zls * s = m->ls[m->bf][i];
        if (!s) s = m->ls[m->bf][i] = xcalloc(NBIN, sizeof *s);
        for (j = 0; j < F->n; j++, s++) {
          int64_t r[NLS], x0 = vb_clip24(F->lr[i][j]);
          int a, b, k;
          lsreg(m, i, j, r);
          for (a = 0, k = 0; a < NLS; a++)
            for (b = 0; b <= a; b++, k++)
              s->a[k] += r[a] * r[b] - (s->a[k] >> 5);
          for (a = 0; a < NLS; a++) s->b[a] += r[a] * x0 - (s->b[a] >> 5);
        }
        continue;
      }
      Fj(F->n,
        int64_t x0 = vb_clip24(F->lr[i][j]), x1 = vb_clip24(scl(P, F, i, j)),
                x2 = vb_clip24(scl(P2, F, i, j));
        tn[j] += (x0 + x2) * x1 - (tn[j] >> 2);
        td[j] += x1 * x1 - (td[j] >> 2)));
  m->p2[m->bf] = m->pi[m->bf];  m->pi[m->bf] = m->ci;  m->ci = old;
}

static INLINE int point(int x0, int y0, int y1, int x, uint64_t r) {
  int dy = y1 - y0, off;
  if (!r) return y0;
  off = (int) ((uint64_t) (u32) (ABS(dy) * (x - x0)) * r >> 40);
  return dy < 0 ? y0 - off : y0 + off;
}

static INLINE u32 vbk(int v) { return VBKT[v + 1]; }
static INLINE u32 v8(int v) { return V8T[v + 1]; }

typedef struct {
  u32 pi, fno, fv1, fvo, fvs, froom, fprv, fod, fpd, frd;  int py, p2y, oy;
} fctx;

static INLINE void fnz(fctx * x, int pr) {
  x->fod = (u32) (x->py >= 0 && x->oy >= 0 ? vb_sq(x->oy - x->py, 7) + 7 : 15);
  x->fpd = (u32) (x->p2y >= 0 ? vb_sq(x->py - x->p2y, 3) + 3 : 7);
  x->frd = (u32) (x->py >= 0 ? vb_sq(pr - x->py, 7) + 7 : 15);
}

static int fval(vbz * m, fctx * x, u32 v, int t, int pr, int yh, int range) {
  int d, a, s, nb = 0, k, idx, r;
  if (!zcode(m, D_FZ, v != 0,
             (((x->fno * 64 + x->pi) * 8 + x->fprv) * 32 + x->fvo) * 8 +
             x->froom, m->dc[D_FZ].b, x->fv1, -1)) return 0;
  fnz(x, pr);
  d = t - yh;
  if (yh != pr && zcode(m, D_FE, d == 0, x->fod * 8 + x->fpd, m->dc[D_FE].b,
                        x->pi, -1)) d = 0;
  else {
    s = zcode(m, D_FS, d < 0, x->fod * 16 + x->frd, m->dc[D_FS].b, x->fpd, -1);
    a = ABS(d);
    if (m->e) nb = (int) blr_ilog((u32) a >> 1);
    idx = 1;
    for (k = 4; k > 0; k--)
      idx = idx * 2 + zcode(m, D_FL, nb >> (k - 1) & 1,
                            ((sz) x->fno * 16 + (u32) idx) * 16 + x->fod,
                            m->dc[D_FL].b, x->fvs, -1);
    nb = idx - 16;  r = 1;
    for (k = nb; k > 0; k--)
      r = r * 2 + zcode(m, D_FM, a >> (k - 1) & 1,
                        (k == nb ? (u32) nb : 0) * 16 + x->fod, NULL, 0, -1);
    d = s ? -r : r;
  }
  t = yh + d;
  FATAL_IF_HOT(t < 0 || t >= range || t == pr)
    ("vorbis: floor value %d out of range", t);
  return vb_fold(pr, t, range);
}

static INLINE int zdir(vbz * m, int t, int bit, sz ia) {
  zdc * d = m->dc + t;
  zc_t * a = d->a + ia;
  u32 p = cp(a);
  bit = zbit(m, bit, p);
#ifdef VBZ_STATS
  if (m->e) { d->bits -= log2(bit ? p / 65536.0 : 1 - p / 65536.0);  d->n++; }
#endif
  cup(a, bit, RT[d->la], RL[d->la]);
  return bit;
}

static int fvalq(vbz * m, fctx * x, u32 v, int t, int pr, int yh, int range) {
  int d, a, s, nb = 0, k, r;
  if (!zdir(m, D_FZ, v != 0,
            (((x->fno * 64 + x->pi) * 8 + x->fprv) * 8 + x->fvo) * 8 +
            x->froom)) return 0;
  fnz(x, pr);
  d = t - yh;
  if (yh != pr && zdir(m, D_FE, d == 0, x->fod * 8 + x->fpd)) d = 0;
  else {
    s = zdir(m, D_FS, d < 0, x->fod * 16 + x->frd);
    a = ABS(d);
    if (m->e) nb = (int) blr_ilog((u32) a >> 1);
    for (k = 0; k < 15 && zdir(m, D_FL, nb > k,
                               ((sz) x->fno * 16 + (u32) k) * 16 + x->fod);
         k++) ;
    nb = k;  r = 1;
    for (k = nb; k > 0; k--)
      r = r * 2 + zdir(m, D_FM, a >> (k - 1) & 1,
                       (k == nb ? (u32) nb : 0) * 16 + x->fod);
    d = s ? -r : r;
  }
  t = yh + d;
  FATAL_IF_HOT(t < 0 || t >= range || t == pr)
    ("vorbis: floor value %d out of range", t);
  return vb_fold(pr, t, range);
}

static u32 yval(vbz * m, int w, u32 v, int py, u32 bits, u32 oc, u32 d0) {
  zc_t * t = m->yc[w][py >= 0][oc][d0];
  u32 idx = 1, i;
  if (py >= 0) {
    int d = m->e ? (int) v - py : 0, a, s, nb = 0, b;
    b = zbit(m, d != 0, cp(t));  cup(t, b, RT[R255], 255);
    if (!b) return (u32) py;
    s = zbit(m, d < 0, cp(t + 1));  cup(t + 1, s, RT[R255], 255);
    a = ABS(d);
    if (m->e) nb = (int) blr_ilog((u32) a >> 1);
    for (i = 4; i > 0; i--) {
      int q = zbit(m, nb >> (i - 1) & 1, cp(t + 2 + idx));
      cup(t + 2 + idx, q, RT[R255], 255);  idx = idx * 2 + (u32) q;
    }
    nb = (int) idx - 16;  a = 1;
    for (i = (u32) nb; i > 0; i--) {
      zc_t * c = t + 18 + 2 * MIN(nb, 10) + (i != (u32) nb);
      int q = zbit(m, (ABS(d) >> (i - 1)) & 1, cp(c));
      cup(c, q, RT[R255], 255);  a = a * 2 + q;
    }
    return (u32) (s ? py - a : py + a);
  }
  for (i = bits; i > 0; i--) {
    int q = zbit(m, (int) (v >> (i - 1) & 1), cp(t + (idx < 32 ? idx : 0)));
    if (idx < 32) cup(t + idx, q, RT[R255], 255);
    idx = idx * 2 + (u32) q;
  }
  return idx - (1u << bits);
}

int vbz_floor(vbz * m, const vb_setup * s, u32 k, u32 fno, const vb_floor * f,
              u32 * y, u32 * classes, int u, int choices) {
  frame * F = CUR(m), * P = PRV(m), * P2 = PRV2(m);
  int kc = k < NCH ? (int) k : NCH - 1, oc = kc - 1;
  int pok = P->valid && P->fno[kc] == (int) fno && P->used[kc];
  int p2ok = pok && P2->valid && P2->fno[kc] == (int) fno && P2->used[kc];
  int ook, range = (int) f->quant, i, kk, qf = (m->opt & VBZ_O_QF) != 0;
  u32 post = 2, p;
  u8 step2[VB_MAXPOST];
  fctx x;
  if (k < NCH) {
    u32 best = 0xFFFFFFFFu, j, dd;
    Fj((u32) kc,
      if (F->fno[j] == (int) fno && F->used[j] &&
          (dd = m->pdist[kc][j] ? m->pdist[kc][j] : 256) < best) {
        best = dd;  oc = (int) j;
      });
    m->fpt[kc] = (signed char) oc;
  }
  ook = oc >= 0 && F->fno[oc] == (int) fno && F->used[oc];
  { zc_t * c = &m->usd[P->valid ? P->used[kc] : 2][oc >= 0 ? F->used[oc] : 2];
    u = zbit(m, u, cp(c));  cup(c, u, RT[R60], 60); }
  F->used[kc] = (u8) u;  F->fno[kc] = (int) fno;
  if (!u) return 0;
  FATAL_UNLESS(range >= 2 && range <= 256, "vorbis: floor range %d", range);
  p = blr_ilog(f->quant - 1);
  memset(step2, 0, sizeof step2);
  Fi(2,
    int py = pok ? P->fy[kc][i] : -1;
    u32 ocd = !m->small && oc >= 0 && F->fno[oc] == (int) fno && F->used[oc]
            ? F->fd[oc][i] : 8;
    y[i] = yval(m, i, y[i], py, p, ocd, i && !m->small ? F->fd[kc][0] : 8);
    FATAL_IF_HOT(y[i] >= (u32) range)
      ("vorbis: floor end point %lu out of range", (unsigned long) y[i]);
    F->fy[kc][i] = (short) y[i];  F->fv[kc][i] = (u8) MIN(y[i], 255);
    F->fd[kc][i] = (u8) (py >= 0 ? vb_sq((int) y[i] - py, 3) + 3 : 7);
    step2[i] = 1);
  x.fno = fno & 3;
  Fi((int) f->parts,
    u32 c = f->pcls[i], cd = f->cdim[c], cs = f->csub[c];
    for (kk = 0; kk < (int) cd; kk++) {
      u32 pi = post + (u32) kk, lo, hi;
      int pr, val, py, oy, yh, t, room;
      lo = f->lo[pi];  hi = f->hi[pi];
      pr = point((int) f->x[lo], F->fy[kc][lo], F->fy[kc][hi], (int) f->x[pi],
                 f->rdx[pi]);
      py = pok ? P->fy[kc][pi] : -1;  oy = ook ? F->fy[oc][pi] : -1;
      room = MIN(range - pr, pr) * 2;
      x.pi = pi < 64 ? pi : 63;
      x.py = py;  x.oy = oy;  x.p2y = p2ok ? P2->fy[kc][pi] : -1;
      if (!qf) {
        x.fvo = vbk(oy >= 0 ? vb_fold(pr, oy, range) : -1);
        x.fvs = vbk(py >= 0 ? vb_fold(pr, py, range) : -1);
        x.fv1 = v8(pi > 2 ? F->fv[kc][pi - 1] : -1);
      } else x.fvo = v8(oy >= 0 ? vb_fold(pr, oy, range) : -1);
      if (room < 8) x.froom = 0;
      else { u32 l = blr_ilog((u32) room) - 3;  x.froom = MIN(l, 7); }
      x.fprv = v8(pok ? P->fv[kc][pi] : -1);
      yh = py >= 0 ? py : pr;
      t = m->e ? vb_unfold(pr, (int) y[pi], range) : 0;
      val = qf ? fvalq(m, &x, m->e ? y[pi] : 0, t, pr, yh, range)
               : fval(m, &x, m->e ? y[pi] : 0, t, pr, yh, range);
      y[pi] = (u32) val;
      F->fv[kc][pi] = (u8) MIN(val, 255);
      F->fy[kc][pi] = (short) (val ? vb_unfold(pr, val, range) : pr);
      F->fd[kc][pi] = (u8) (py >= 0 ? vb_sq(F->fy[kc][pi] - py, 3) + 3 : 7);
      if (val) step2[lo] = step2[hi] = step2[pi] = 1;
    }
    if (cs) {
      u32 cv = 0, j, mx;
      if (!choices) {
        for (kk = 0; kk < (int) cd; kk++) {
          Fj(1u << cs,
            mx = f->csb[c][j] < 0 ? 1 : s->bk[f->csb[c][j]].ent;
            if (y[post + (u32) kk] < mx) break);
          FATAL_UNLESS(j < 1u << cs,
                       "vorbis: no class %lu book fits value %lu",
                       (unsigned long) c, (unsigned long) y[post + (u32) kk]);
          cv |= j << kk * cs;
        }
      } else {
        u32 b, nb = blr_ilog(s->bk[f->cbook[c]].ent - 1);
        cv = m->e ? classes[i] : 0;
        for (b = nb; b > 0; b--) {
          int q = zbit(m, (int) (cv >> (b - 1) & 1), 32768);
          if (!m->e) cv = cv * 2 + (u32) q;
        }
        FATAL_IF_HOT(cv >= s->bk[f->cbook[c]].ent)
          ("vorbis: floor classword %lu out of range", (unsigned long) cv);
      }
      classes[i] = cv;
    } else classes[i] = 0;
    post += cd);
  if (k < NCH) {
    u32 j;
    Fj((u32) kc,
      if (F->fno[j] == (int) fno && F->used[j]) {
        u32 sm = 0, d, * q = &m->pdist[kc][j];
        Fi((int) f->posts, sm += (u32) ABS(F->fy[kc][i] - F->fy[j][i]));
        d = sm * 16 / f->posts + 1;
        *q = *q ? *q + (u32) (((int) d - (int) *q) / 4) : d;
      });
  }
  if (m->opt & VBZ_O_LITE) {
    int np = 1;
    F->fpx[kc][0] = 0;  F->fpy[kc][0] = (short) (F->fy[kc][0] * (int) f->mult);
    for (i = 1; i < (int) f->posts; i++) {
      u32 pi = f->srt[i];
      if (!step2[pi] || (int) f->x[pi] <= F->fpx[kc][np - 1]) continue;
      F->fpx[kc][np] = (short) f->x[pi];
      F->fpy[kc][np++] = (short) (F->fy[kc][pi] * (int) f->mult);
    }
    F->nfp[kc] = np;
    return 1;
  }
  { int lx = 0, ly = F->fy[kc][0] * (int) f->mult, xx, n = F->n;
    for (i = 1; i < (int) f->posts; i++) {
      u32 pi = f->srt[i];
      int hx, hy;
      if (!step2[pi]) continue;
      hx = (int) f->x[pi];  hy = F->fy[kc][pi] * (int) f->mult;
      if (hx > lx) {
        int dy = hy - ly, ady = ABS(dy), adx = hx - lx,
            qs = ady / adx, rs = ady % adx, q = 0, r = 0;
        for (xx = lx; xx < hx && xx < n; xx++) {
          int t = dy < 0 ? ly - q : ly + q;
          F->flo[kc][xx] = (u8) CLAMP(t, 0, 255);
          q += qs;  r += rs;
          if (r >= adx) { r -= adx;  q++; }
        }
      }
      lx = hx;  ly = hy;
    }
    for (xx = lx; xx < n; xx++)
      F->flo[kc][xx] = (u8) CLAMP(ly, 0, 255); }
  return 1;
}

u32 vbz_class(vbz * m, vb_res * r, vb_book * cb, u32 rno, u32 jv, u32 pp,
              u32 k, u32 pre, u32 val, u32 il, int rch, const u8 * cmap) {
  frame * F = CUR(m), * P = PRV(m);
  zres * R = res(m, r, cb);
  u16 * nc;
  u32 x, ncl = R->ncl, q = rno & 3, lo = 0, hi = ncl, node = 1, pcl, c1,
      fm = 0, nn = 0, b, j0, j1, ka, kb;
  int jc = jv < NCH ? (int) jv : NCH - 1, ch = il ? cmap[0] : rch,
      mix = (m->opt & VBZ_O_MIX) != 0;
  int qf = (m->opt & VBZ_O_QF) != 0;
  u32 ppc = pp < NPART ? pp : NPART - 1, pcs = 0;
  sz ia;
  if (ch >= NCH) ch = NCH - 1;
  m->ws = 0;
  if (!il && rch < NCH) {
    int pc = m->fpt[rch];
    m->cj[q][rch] = (u8) (jc + 1);
    if (pc >= 0 && m->cj[q][pc]) pcs = F->rcl[q][m->cj[q][pc] - 1][ppc];
  }
  { u32 row = R->m[0] ? k : 0;
    if (!R->nc[row])
      R->nc[row] = xcalloc((sz) (R->m[0] ? R->pw[row] : 1) * R->nn,
                           sizeof (u16));
    nc = R->nc[row] + (sz) (R->m[0] ? pre : 0) * R->nn; }
  pcl = P->valid ? MIN(P->rcl[q][jc][ppc], 31) : 0;
  c1 = ppc > 0 ? MIN(F->rcl[q][jc][ppc - 1], 31) : 0;
  j0 = r->beg + pp * r->psz;  j1 = j0 + r->psz;
  if (il) { j0 /= il;  j1 /= il; }
  if (j1 > (u32) F->n) j1 = (u32) F->n;
  if (m->opt & VBZ_O_LITE) {
    int np = F->nfp[ch], a = 0, z = np, xm = (int) (j0 + j1) / 2, t;
    if (np && j0 < j1) {
      if (xm >= m->lcx[ch])
        for (a = m->lca[ch]; a + 1 < np && F->fpx[ch][a + 1] <= xm; a++) ;
      else
        while (z - a > 1) {
          int md = (a + z) / 2;
          if (F->fpx[ch][md] <= xm) a = md;  else z = md;
        }
      m->lca[ch] = a;  m->lcx[ch] = xm;
      if (a + 1 < np) {
        int dx = F->fpx[ch][a + 1] - F->fpx[ch][a];
        uint64_t rd = 0;
        if (dx > 0) {
          if (!m->rcp) m->rcp = xcalloc(32768, sizeof *m->rcp);
          rd = m->rcp[dx] ? m->rcp[dx] : (m->rcp[dx] = vb_rcp((u32) dx));
        }
        t = point(F->fpx[ch][a], F->fpy[ch][a], F->fpy[ch][a + 1], xm, rd);
      } else t = F->fpy[ch][a];
      fm = (u32) CLAMP(t, 0, 255) >> 4;
    }
  } else {
    for (b = j0; b < j1; b++) { fm += F->flo[ch][b];  nn++; }
    fm = nn ? fm / nn >> 4 : 0;
  }
  ka = (q * 16 * 32 + pcl) * 256 + MIN(ppc, 255);
  kb = (c1 * 16 + fm) * 8 + (k & 7);
  while (hi - lo > 1) {
    u32 mid = (lo + hi + 1) / 2, pr = nc[node];
    int bit;
    if (!pr) {
      const uint64_t * pm = R->m[0] ? R->m[k + 1] + (sz) pre * ncl : NULL;
      uint64_t a = 0, c = 0;
      u32 t;
      for (t = lo; t < mid; t++) a += pm ? pm[t] : 1;
      for (t = mid; t < hi; t++) c += pm ? pm[t] : 1;
      pr = nc[node] = (u16) (!a ? ZN_HI : !c ? ZN_LO : pclamp(vb_pfrac(c, a)));
    }
    if (pr == ZN_HI) { lo = mid;  node = node * 2 + 1;  continue; }
    if (pr == ZN_LO) { hi = mid;  node = node * 2;  continue; }
    ia = ka + (node < 16 ? node : 15) * 32 * 256 +
         (!pcs ? 0 : pcs - 1 < mid ? SZ_RA : 2 * SZ_RA);
    bit = zcode(m, D_RC, val >= mid, ia, m->dc[D_RC].b,
                qf ? qp16(pr) * 16 + fm : qp16(pr) * 32 * 16 * 8 + kb,
                mix ? (long) qp16(pr) : -1);
    if (bit) lo = mid;
    else hi = mid;
    node = node * 2 + (u32) bit;
  }
  x = lo;
  if (pp < NPART) F->rcl[q][jc][pp] = (u8) (x + 1);
  return x;
}

static u32 cbk(u32 c) {
  u32 l;
  if (c < 8) return c;
  l = blr_ilog(c);
  l = 8 + (l - 4) * 2 + (c >> (l - 2) & 1);
  return MIN(l, 31);
}

static INLINE u32 digit_k(vbz * m, vb_book * b, u32 k, u32 pre, u32 idx,
                          int enc, u32 pass, int rch, u32 c, const int mix,
                          const int avg, const int bins) {
  frame * F = CUR(m), * P = PRV(m);
  zbook * B = b->zc ? b->zc : book(m, b);
  const u16 * rc = (B->rec ? 0 : (brec(m, b, B), 0),
                    B->rec + (B->pw[0] ? (sz) (B->roff[k] + pre) * B->rs : 0));
  u32 x, mm = B->mm, bk = B->id, nb, qx, zr, ts, tq, qa, qa2, qr;
  int pidx = pass < 2 ? (int) pass : 2, och = rch ^ 1,
      ch = rch < NCH ? rch : NCH - 1;
  int hp = rch & 1;
  int target = 0, dv, mag, sgn, nz;
  int64_t acc;
  long bin = -1;
  u32 ab = 0, pd = 0;
  int pv;
  if (rch < NCH && F->role[rch]) { och = F->partner[rch];  hp = och < rch; }
  else if (rch < NCH && m->fpt[rch] >= 0 && m->pdist[rch][m->fpt[rch]] &&
           m->pdist[rch][m->fpt[rch]] <= PNEAR) {
    och = m->fpt[rch];  hp = 1;
  }
  if (och >= NCH) och = NCH - 1;
  if (c >= NBIN) c = NBIN - 1;
  if (enc) target = (int) b->mult[idx] - (int) b->off;
  acc = F->rec[ch][c];
  pv = P->valid && (int) c < P->n;
  nb = m->nbok && (int) c < F->n ? (u32) lq(B, m->nbs[ch][c], 7) : 15;
  qx = hp ? (u32) (lq(B, F->rec[och][c], 3) + 3) : 7;
  if (m->zlast[ch] + 1 != c) m->zrun[ch] = 0;
  zr = MIN(m->zrun[ch], 3);
  if (m->pcok && (F->role[ch] || F->used[ch])) {
    int64_t xh = m->pc[ch][c];
    tq = (u32) (lq(B, xh - acc, 7) + 7);  ts = (u32) (lq(B, xh, 3) + 3);
  } else { tq = 15;  ts = 7; }
  if (pass > 0) {
    int q = lq(B, acc, 7);  qa2 = (u32) (q + 7);  qa = (u32) (vb_sq(q, 3) + 3);
  }
  else { qa = 7;  qa2 = 15; }
  qr = pv ? (u32) (lq(B, P->rec[ch][c], 3) + 3) : 7;
  if (avg) {
    ab = (u32) lq(B, m->av[m->bf][ch][c], 7);
    pd = pv ? (u32) ABS(vb_sq(P->dg[pidx][ch][c], 3)) : 0;
    m->ws = (int) ((qp16(rc[1]) >> 1) * 8 + ab);
    if (bins) bin = (long) (((sz) m->bf * NCH + (sz) ch) * NBIN + c);
  }
  nz = target != 0;
  if (rc[0] & RF_Z) {
    u32 pr = rc[1];
    long zp = (long) (((qp16(pr) * 16 + nb) * 8 + qx) * 4 + zr);
    if (bin >= 0) m->ie = (long) (bin * 8 + (long) ab);
    nz = zcodep(m, D_Z, nz, ((sz) bk * SZ_ZP + (sz) zp) * 8 + ts,
                m->dc[D_Z].b,
                (((qr * 8 + qx) * 32 + cbk(c)) * 8 + qa) * 8 +
                (F->flo[ch][c] >> 5),
                mix ? (long) (avg ? qp32(pr) * 8 + ab : qp32(pr)) : -1, zp, bins);
  } else nz = (rc[0] & RF_NZ) != 0;
  dv = 0;
  if (nz) {
    const u16 * pm;
    if (rc[0] & RF_S) {
      u32 pr = rc[2];
      int d2 = c >= 2 ? F->dg[pidx][ch][c - 2] : 0;
      if (bin >= 0) {
        int d1 = c >= 1 ? F->dg[pidx][ch][c - 1] : 0,
            s1 = pv ? vb_sq(P->dg[pidx][ch][c], 1) + 1 : 1;
        m->ie = (long) (bin * 9 + s1 * 3 + vb_sq(d1, 1) + 1);
      }
      sgn = zcodep(m, D_S, target < 0,
                   ((qp16(pr) * 16 + tq) * 16 + qa2) * 8 + qx, m->dc[D_S].b,
                   bk,
                   mix ? (long) (avg ? (qp16(pr) * 16 + tq) * 4 +
                                       m->sh[pidx][ch]
                                     : (u32) (vb_sq(d2, 2) + 2)) : -1,
                   (long) ((qp16(pr) * 16 + qa2) * 8 + qx), bins);
    } else sgn = (rc[0] & RF_NEG) != 0;
    m->sh[pidx][ch] = (u8) ((m->sh[pidx][ch] * 2 + (u32) sgn) & 3);
    pm = rc + 3 + (u32) sgn * mm - 1;
    if (pm[1]) {
      int d1 = c >= 1 ? F->dg[pidx][ch][c - 1] : 0,
          d2 = c >= 2 ? F->dg[pidx][ch][c - 2] : 0;
      int dx = F->dg[pidx][och][c],
          en = ABS(d1) + ABS(d2) + (hp || !F->role[ch] ? ABS(dx) : 0);
      sz ip = (sz) nb * 8 * 8 + qx * 8 + qa, ia = (sz) bk * SZ_MP + ip;
      u32 ib = tq * 4 + (u32) MIN(en, 3);
      for (mag = 1; mag < (int) mm && pm[mag]; mag++) {
        u32 pr = pm[mag];
        if (pr == 1) continue;
        u32 mq = (u32) MIN(mag, 7);
        sz mo = ((sz) qp16(pr) * 8 + mq) * 16 * 8 * 8;
        if (bin >= 0)
          m->ie = (long) ((bin * 8 + (long) mq) * 4 + (long) pd);
        if (!zcodep(m, D_M, ABS(target) > mag, ia + mo, m->dc[D_M].b, ib,
                    avg ? (long) (((qp16(pr) * 8 + mq) * 8 + ab) * 8 + qa)
                        : -1, (long) (ip + mo), bins)) break;
      }
    } else mag = 1;
    dv = sgn ? -mag : mag;
  }
  if (dv) m->zrun[ch] = 0;
  else m->zrun[ch]++;
  m->zlast[ch] = c;
  x = dv + (int) b->off >= 0 && dv + (int) b->off < (int) b->base
    ? b->inv[dv + (int) b->off] : (u32) -1;
  FATAL_IF_HOT(x >= b->nv)
    ("vorbis: decoded digit %d is not a multiplicand", dv);
  F->dg[pidx][ch][c] = (signed char) vb_sq(dv, 127);
  { int64_t r = acc + (int64_t) dv * B->del;
    F->rec[ch][c] = (i32) CLAMP(r, -0x3FFFFFFF, 0x3FFFFFFF); }
  return x;
}

#define DIGIT_K(n, mix, avg, bins) \
  static NOINLINE u32 n(vbz * m, vb_book * b, u32 k, u32 pre, u32 idx, \
                        int enc, u32 pass, int rch, u32 c) { \
    return digit_k(m, b, k, pre, idx, enc, pass, rch, c, mix, avg, bins); \
  }
DIGIT_K(digit_p, 0, 0, 0)
DIGIT_K(digit_m, 1, 0, 0)
DIGIT_K(digit_a, 1, 1, 0)
DIGIT_K(digit_b, 1, 1, 1)
#undef DIGIT_K

u32 vbz_digit(vbz * m, vb_book * b, u32 k, u32 pre, u32 idx, int enc,
              u32 pass, int rch, u32 c) {
  switch (m->opt & (VBZ_O_MIX | VBZ_O_AVG | VBZ_O_BIN)) {
  case 0: return digit_p(m, b, k, pre, idx, enc, pass, rch, c);
  case VBZ_O_MIX: return digit_m(m, b, k, pre, idx, enc, pass, rch, c);
  case VBZ_O_MIX | VBZ_O_AVG:
    return digit_a(m, b, k, pre, idx, enc, pass, rch, c);
  default: return digit_b(m, b, k, pre, idx, enc, pass, rch, c);
  }
}

#define ZT   15
#define ZINC 32
#define ZCAP 60000
#define ZPW  16
#define ZNX  945
#define ZNXS 9
#define ZMAXP 4096

static INLINE void zs_enc(rc_enc * e, u32 lo, u32 f, int sh) {
  u32 r = e->range >> sh;
  rc_addlow(e, r * lo);  e->range = r * f;
  rc_norm(e);
}

static INLINE u32 zs_peek(rc_dec * d, u32 * r, int sh) {
  u32 t;
  rc_dec_norm(d);
  *r = d->range >> sh;  t = d->code / *r;
  return t < (1u << sh) ? t : (1u << sh) - 1;
}

static INLINE void zs_take(rc_dec * d, u32 r, u32 lo, u32 f) {
  d->code -= r * lo;  d->range = r * f;
}

static void zrebuild(zab * a, u32 n, u32 zb) {
  u16 * cum = ZCUM(a), * cnt = ZCNT(a, n), * bk = ZBKT(a, n);
  u32 i, f, sum = 0, big = 0, j;
  u32 sc = (u32) (((uint64_t) ((1u << ZT) - n) << 16) / a->tot);
  Fi(n,
    f = 1 + (u32) ((uint64_t) cnt[i] * sc >> 16);
    cum[i] = (u16) f;  sum += f;
    if (cnt[i] > cnt[big]) big = i);
  cum[big] = (u16) (cum[big] + (1u << ZT) - sum);
  sum = 0;
  Fi(n, f = cum[i];  cum[i] = (u16) sum;  sum += f);
  cum[n] = (u16) sum;
  i = 0;
  Fj(1u << zb,
    while (cum[i + 1] <= j << (ZT - zb)) i++;
    bk[j] = (u16) i);
  a->mark = a->tot + MAX(2 * ZINC, a->tot >> 3);
}

static zab * zab_new(const zfast * Z) {
  u32 i;
  zab * a = xmalloc(sizeof *a +
                    ((sz) 2 * Z->np + ((sz) 1 << Z->zbb)) * sizeof (u16));
  u16 * cnt = ZCNT(a, Z->np);
  a->tot = 0;
  Fi(Z->np, cnt[i] = Z->prior[i];  a->tot += Z->prior[i]);
  zrebuild(a, Z->np, Z->zbb);
  return a;
}

static void zab_add(zab * a, u32 s, u32 n, u32 zb) {
  u16 * cnt = ZCNT(a, n);
  cnt[s] = (u16) (cnt[s] + ZINC);  a->tot += ZINC;
  if (a->tot > ZCAP) {
    u32 i;
    a->tot = 0;
    Fi(n, cnt[i] = (u16) ((cnt[i] + 1) >> 1);  a->tot += cnt[i]);
    zrebuild(a, n, zb);
  } else if (a->tot >= a->mark) zrebuild(a, n, zb);
}

static void zcum(u16 * cum, const uint64_t * w, u32 n) {
  uint64_t t = 0;
  u32 i, f, sum = 0, big = 0, sh = 0;
  Fi(n, t += w[i];  if (w[i] > w[big]) big = i);
  while ((t >> sh) >= ((uint64_t) 1 << 32)) sh++;
  Fi(n,
    f = 1 + (u32) (((w[i] >> sh) * (uint64_t) ((1u << ZT) - n)) / (t >> sh));
    cum[i] = (u16) f;  sum += f);
  cum[big] = (u16) (cum[big] + (1u << ZT) - sum);
  sum = 0;
  Fi(n, f = cum[i];  cum[i] = (u16) sum;  sum += f);
  cum[n] = (u16) sum;
}

static void zfast_free(zfast * Z) {
  u32 i;
  if (!Z) return;
  Fi(Z->nab, xfree(Z->ab[i]));
  xfree(Z->ab);
  xfree(Z->epat);  xfree(Z->erank);  xfree(Z->voff);  xfree(Z->vent);
  xfree(Z->vcum);  xfree(Z->vuni);  xfree(Z->vnz);
  xfree(Z->prior);  xfree(Z->dig);  xfree(Z);
}

static zfast * zfast_new(const vb_book * b, const zbook * B, int small) {
  zfast * Z;
  u32 k, p, mm1 = B->mm + 1, nkey = 1, i, np = 0, * ord, * cnt;
  int32_t * kmap;
  uint64_t * pm, * w;
  if (!b->look || b->nv > 256 || b->ent > 0xFFFE || b->dim > 16 || !b->ent)
    return NULL;
  Fk(b->dim, nkey *= mm1;  if (nkey > (1u << 20)) return NULL);
  Z = xcalloc(1, sizeof *Z);
  Z->dig = xmalloc((sz) b->ent * b->dim * sizeof *Z->dig);
  Z->epat = xmalloc((sz) b->ent * sizeof *Z->epat);
  Z->erank = xmalloc((sz) b->ent * sizeof *Z->erank);
  kmap = xmalloc((sz) nkey * sizeof *kmap);
  Fi(nkey, kmap[i] = -1);
  pm = xcalloc(b->ent, sizeof *pm);
  Fi(b->ent,
    u32 t = i, key = 0, pw = 1;
    Fk(b->dim,
      u32 x = t % b->nv;
      int d = (int) b->mult[x] - (int) b->off;
      t /= b->nv;
      Z->dig[(sz) i * b->dim + k] = (i16) d;
      key += (u32) ABS(d) * pw;  pw *= mm1);
    Z->epat[i] = 0xFFFF;
    if (!b->len[i]) continue;
    if (kmap[key] < 0) kmap[key] = (int32_t) np++;
    Z->epat[i] = (u16) kmap[key];
    pm[kmap[key]] += vb_mass(b->len[i]));
  xfree(kmap);
  if (!np || np > ZMAXP) { xfree(pm);  zfast_free(Z);  return NULL; }
  ord = xmalloc((sz) np * sizeof *ord);
  for (p = 0; p < np; p++) ord[p] = p;
  for (p = 1; p < np; p++) {
    u32 v = ord[p], j = p;
    while (j > 0 && pm[ord[j - 1]] < pm[v]) { ord[j] = ord[j - 1];  j--; }
    ord[j] = v;
  }
  cnt = xcalloc(np, sizeof *cnt);
  { u32 * inv = xmalloc((sz) np * sizeof *inv);
    for (p = 0; p < np; p++) inv[ord[p]] = p;
    Fi(b->ent,
      if (Z->epat[i] != 0xFFFF) {
        Z->epat[i] = (u16) inv[Z->epat[i]];  cnt[Z->epat[i]]++;
      });
    xfree(inv); }
  Z->np = np;  Z->ns = b->ent;
  Z->voff = xmalloc((sz) (np + 1) * sizeof *Z->voff);
  for (Z->voff[0] = 0, p = 0; p < np; p++) Z->voff[p + 1] = Z->voff[p] + cnt[p];
  Z->vent = xmalloc((sz) Z->voff[np] * sizeof *Z->vent);
  Z->vcum = xcalloc((sz) Z->voff[np] + np, sizeof *Z->vcum);
  Z->vuni = xcalloc(np, 1);  Z->vnz = xcalloc(np, 1);
  Z->prior = xmalloc((sz) np * sizeof *Z->prior);
  for (p = 0; p < np; p++) cnt[p] = 0;
  Fi(b->ent,
    u32 msk = 0, nz = 0, j;
    if (Z->epat[i] == 0xFFFF) continue;
    p = Z->epat[i];
    Fk(b->dim,
      int d = Z->dig[(sz) i * b->dim + k];
      if (d) { if (d < 0) msk |= 1u << nz;  nz++; });
    Z->vnz[p] = (u8) nz;
    j = Z->voff[p] + cnt[p]++;
    while (j > Z->voff[p] && Z->erank[Z->vent[j - 1]] > msk) {
      Z->vent[j] = Z->vent[j - 1];  j--;
    }
    Z->vent[j] = i;  Z->erank[i] = (u16) msk);
  w = xmalloc((sz) b->ent * sizeof *w);
  for (p = 0; p < np; p++) {
    u32 o = Z->voff[p], n = Z->voff[p + 1] - o, uni = n == 1u << Z->vnz[p];
    Fi(n,
      w[i] = vb_mass(b->len[Z->vent[o + i]]);
      if (Z->erank[Z->vent[o + i]] != i ||
          b->len[Z->vent[o + i]] != b->len[Z->vent[o]]) uni = 0);
    Fi(n, Z->erank[Z->vent[o + i]] = (u16) i);
    Z->vuni[p] = (u8) uni;
    if (n > 1 && !uni) zcum(Z->vcum + o + p, w, n);
  }
  { uint64_t t = 0;
    u32 sh = 0, tw = ZPW * ZINC;
    for (p = 0; p < np; p++) t += pm[p];
    while ((t >> sh) >= ((uint64_t) 1 << 32)) sh++;
    for (p = 0; p < np; p++) {
      uint64_t c = 1 + ((pm[ord[p]] >> sh) * tw) / (t >> sh);
      Z->prior[p] = (u16) MIN(c, 4096);
    } }
  xfree(w);  xfree(pm);  xfree(ord);  xfree(cnt);
  Z->nab = small ? ZNXS : ZNX;
  Z->ab = xcalloc(Z->nab, sizeof *Z->ab);
  for (Z->zbb = 6; Z->zbb < 10 && 1u << Z->zbb < 2 * np; Z->zbb++) ;
  return Z;
}

int vbz_reg(const vbz * m) { return (m->opt & VBZ_O_REG) != 0; }

int vbz_fl(const vbz * m) { return (m->opt & VBZ_O_FL) != 0; }

int vbz_fast(vbz * m, vb_book * b) {
  zbook * B;
  if (!(m->opt & VBZ_O_FAST)) return 0;
  B = b->zc ? b->zc : book(m, b);
  if (!B->fok) { B->fz = zfast_new(b, B, m->small);  B->fok = 1; }
  return B->fz != NULL;
}

u32 vbz_entry(vbz * m, vb_book * b, u32 e, int enc, u32 pass, u32 c, u32 ch,
              u32 st, u32 il, int jch, const u8 * cmap, int first) {
  frame * F = CUR(m), * P = PRV(m), * P2 = PRV2(m);
  zbook * B = b->zc;
  zfast * Z = B->fz;
  u32 dim = b->dim, k, p, r, n, o, cx, l, * fo = m->fo;
  const i32 * pr = P->rec[0], * pr2 = P2->rec[0];
  i32 * fr = F->rec[0];
  int64_t es = 0, ea = 0, del = B->del;
  int pv = P->valid && P->n == F->n, pv2 = pv && P2->valid && P2->n == F->n;
  zab * a;
  if (il == 2 && st == 1) {
    u32 g = 2 * c + ch, c0 = cmap[0] < NCH ? cmap[0] : NCH - 1,
        c1 = cmap[1] < NCH ? cmap[1] : NCH - 1;
    for (k = 0; k < dim; k++, g++)
      fo[k] = (g & 1 ? c1 : c0) * NBIN + (g >> 1 < NBIN ? g >> 1 : NBIN - 1);
  } else
    Fk(dim,
      u32 h = il ? cmap[ch] : (u32) jch;
      fo[k] = (h < NCH ? h : NCH - 1) * NBIN + (c < NBIN ? c : NBIN - 1);
      if (il) { ch += st;  while (ch >= il) { ch -= il;  c++; } }
      else c += st);
  if (pass && !m->small) Fk(dim, ea += ABS(fr[fo[k]]));
  if (pv2) for (k = 0; k < dim; k++) es += ABS(pr[fo[k]]) + ABS(pr2[fo[k]]);
  else if (pv) Fk(dim, es += 2 * ABS(pr[fo[k]]));
  cx = pv ? (u32) lq(B, (es + 1) >> 1, 7) : 8;
  if (!m->small) {
    cx = cx * 3 + (first ? 2 : m->fzl);
    l = (blr_ilog(fo[0] % NBIN) + 1) >> 1;
    cx = cx * 7 + MIN(l, 6);
    cx = cx * 5 + (pass ? (u32) lq(B, ea, 3) : 4);
  }
  a = Z->ab[cx];
  if (!a) a = Z->ab[cx] = zab_new(Z);
  if (enc) {
    p = Z->epat[e];
    zs_enc(m->e, a->v[p], (u32) a->v[p + 1] - a->v[p], ZT);
  } else {
    u32 t = zs_peek(m->d, &r, ZT);
    p = ZBKT(a, Z->np)[t >> (ZT - Z->zbb)];
    while (a->v[p + 1] <= t) p++;
    zs_take(m->d, r, a->v[p], (u32) a->v[p + 1] - a->v[p]);
  }
  zab_add(a, p, Z->np, Z->zbb);
  o = Z->voff[p];  n = Z->voff[p + 1] - o;
  r = 0;
  if (n > 1) {
    if (Z->vuni[p]) {
      int nz = Z->vnz[p];
      if (enc) { r = Z->erank[e];  zs_enc(m->e, r, 1, nz); }
      else { r = zs_peek(m->d, &k, nz);  zs_take(m->d, k, r, 1); }
    } else {
      const u16 * cum = Z->vcum + o + p;
      if (enc) {
        r = Z->erank[e];
        zs_enc(m->e, cum[r], (u32) cum[r + 1] - cum[r], ZT);
      } else {
        u32 t = zs_peek(m->d, &k, ZT), q = 0;
        while (q + 1 < n && cum[q + 1] <= t) q++;
        zs_take(m->d, k, cum[q], (u32) cum[q + 1] - cum[q]);
        r = q;
      }
    }
  }
  e = Z->vent[o + r];
  m->fzl = Z->vnz[p] != 0;
  if (Z->vnz[p]) {
    const i16 * dg = Z->dig + (sz) e * dim;
    Fk(dim,
      int64_t v = fr[fo[k]] + (int64_t) dg[k] * del;
      fr[fo[k]] = (i32) CLAMP(v, -0x3FFFFFFF, 0x3FFFFFFF));
  }
  return e;
}
