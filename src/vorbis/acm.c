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

#include "vorbis/acm.h"
#include "sys/cpu.h"

#define NCH   8
#define NBIN  4096
#define NPART 1024
#define NR    VBA_NR
#define NA    (NR * (NR + 1) / 2)
#define MM_N  (1u << 24)
#define MM_HB 22
#define MM_MIN 8
#define TBUDGET ((sz) 64 << 20)

#define VLITE_CX 0x12004028CULL
#define VZ_CX   0x1A056A28CULL

enum {
  D_MODE, D_WIN, D_USED, D_Y01, D_FCLS, D_FVAL, D_RCLS, D_RZ, D_RSG, D_RMG,
  D_HDR, D_HBIT, D_N
};

static const uint64_t VSET[] = { 0, 0x1E4D6A6EFULL, VZ_CX, VLITE_CX };
enum { S0, S21, S13, S7 };

typedef struct { u32 eng;  u8 zs, ss, gs; } vpt;
#define VE(ty, e) ((u32) (e) << 2 * (ty))
#define VLC (VE(D_Y01, ACM_ELITE) | VE(D_FCLS, ACM_ELITE) | \
             VE(D_FVAL, ACM_ELITE) | VE(D_RCLS, ACM_ELITE))
static const vpt VPT[] = {
  [0] = { 0, S0, S0, S0 }, [1] = { 0, S21, S0, S21 },
  [2] = { 0, S21, S0, S13 }, [7] = { VLC, S7, S7, S7 }
};
#define NVPT ((int) ARRAY_LEN(VPT))

typedef struct {
  acm_h sig;
  u32 nv, dim, pw[25];
  uint64_t * m[25];
  uint64_t * pmf;
  i32 del;
} vbook;

typedef struct {
  acm_h sig;
  u32 ncl, pv, pw[25];
  uint64_t * m[25];
} vres;

typedef struct {
  int valid, n, mode;
  u8 used[NCH];
  int fno[NCH];
  short fy[NCH][VB_MAXPOST];
  u8 fv[NCH][VB_MAXPOST];
  u8 fcl[NCH][VB_MAXPART];
  u8 flo[NCH][NBIN];
  i32 rec[NCH][NBIN];
  signed char dg[3][NCH][NBIN];
  u8 rcl[4][NCH][NPART];
  int role[NCH], partner[NCH];
  i32 lr[NCH][NBIN];
} frame;

#define LSL   VBA_LSL

typedef struct {
  int64_t a[NA][LSL], b[NR][LSL], te[LSL], tx[LSL];  i32 w[NR][LSL];
} lsq;

struct vba {
  acm * q;
  int lite;
  uint64_t cxm;
  uint64_t cxz, cxs, cxg;
  frame f[7];
  int ci, pi[2], p2[2], p3[2], lastbf, bf, pm[2], nch;
  i32 avf[2][NCH][NBIN], avs[2][NCH][NBIN];
  lsq * ls[2][NCH];
  i32 pc[NCH][NBIN];
  i32 rg[NCH][NBIN / LSL][NR][LSL];
  int pcok;
  signed char * mmb;  u32 * mmt;  u32 mmn, mmp, mml;  uint64_t mmh;
  u32 hl[64][2];  acm_h hh[4];
  sz tbl;
};

#define CUR(m) (&(m)->f[(m)->ci])
#define PRV(m) (&(m)->f[(m)->pi[(m)->bf]])
#define PRV2(m) (&(m)->f[(m)->p2[(m)->bf]])
#define PRV3(m) (&(m)->f[(m)->p3[(m)->bf]])
#define LST(m) (&(m)->f[(m)->pi[(m)->lastbf < 0 ? 0 : (m)->lastbf]])
#define CX(h) (d->cx[d->ncx++] = (acm_h) (h))
#define CXON(k) (m->cxm >> (k) & 1)
#define CXK(k, h) do { if (cur_ >> (k) & 1) CX(h); } while (0)

static int lq(int64_t x, int64_t del, int lim) {
  static const int K[7] = { 1, 3, 5, 9, 17, 33, 65 };
  int64_t a = ABS(x) * 2;
  int b = 0, i;
  if (del < 1) del = 1;
  Fi(7, b += a >= K[i] * del);
  if (b > lim) b = lim;
  return x < 0 ? -b : b;
}

static void sel_all(acm_dec * d, acm_h a, acm_h b, acm_h c, acm_h e, acm_h f,
                    acm_h g, acm_h h) {
  d->sel[0] = a;  d->sel[1] = b;  d->sel[2] = c;  d->sel[3] = e;
  d->sel[4] = f;  d->sel[5] = g;  d->sel[6] = h;
  d->sel[7] = 0;  d->xon = 0;
}

sz vba_bytes(void) {
  return acm_bytes() + sizeof (vba) + MM_N + ((sz) 1 << MM_HB) * sizeof (u32) +
         TBUDGET;
}

vba * vba_new(int prof, int zset, int pt) {
  vba * m = xmalloc(sizeof *m);
  int i;
  FATAL_UNLESS(pt >= 0 && pt < NVPT && (!pt || VPT[pt].zs),
               "vorbis: no ACM engine assignment %d", pt);
  memset(m, 0, sizeof *m);
  m->lite = prof == ACM_VLITE;  m->cxm = m->lite ? VLITE_CX : ~(uint64_t) 0;
  m->cxz = VPT[pt].zs ? VSET[VPT[pt].zs] : zset ? m->cxm & VZ_CX : m->cxm;
  m->cxs = VPT[pt].ss ? VSET[VPT[pt].ss] : ~(uint64_t) 0;
  m->cxg = VPT[pt].gs ? VSET[VPT[pt].gs] : m->cxz;
  m->q = acm_new(prof);
  Fi(D_N,
    int e = (int) (VPT[pt].eng >> 2 * i & 3);
    if (e) acm_class(m->q, i, e, 0));
  m->mmb = xcalloc(MM_N, 1);
  m->mmt = xcalloc((sz) 1 << MM_HB, sizeof *m->mmt);
  m->ci = 6;  m->pi[0] = 0;  m->pi[1] = 1;  m->p2[0] = 2;  m->p2[1] = 3;
  m->p3[0] = 4;  m->p3[1] = 5;
  m->lastbf = -1;
  return m;
}

void vba_free(vba * m) {
  int i, j;
  if (!m) return;
  acm_free(m->q);
  xfree(m->mmb);  xfree(m->mmt);
  Fi(2, Fj(NCH, xfree(m->ls[i][j])));
  xfree(m);
}

void vba_bind(vba * m, acm_coder c, void * ctx) {
  acm_bind(m->q, c, ctx);
}

static vbook * book(vba * m, vb_book * b) {
  vbook * p = b->acm;
  u32 i;
  if (p) return p;
  p = xcalloc(1, sizeof *p);
  p->sig = PH4(b->dim, b->ent, b->nv, PH3(b->dm, b->de, b->ds));
  Fi(b->ent, p->sig = PH2(p->sig, b->len[i]));
  p->pmf = xmalloc((b->ent ? b->ent : 1) * sizeof *p->pmf);
  Fi(b->ent, p->pmf[i] = vb_mass(b->len[i]));
  if (b->look) {
    uint64_t sum = vb_gsum(b->nv, b->dim);
    p->nv = b->nv;  p->dim = b->dim;  p->del = (i32) vb_bdel(b);
    if (sum && m->tbl + sum * sizeof (uint64_t) <= TBUDGET) {
      m->tbl += sum * sizeof (uint64_t);
      vb_gfill(p->m, p->pw, b, b->nv, b->dim, 0);
    }
  }
  b->acm = p;
  return p;
}

void vba_book_free(void * q) {
  vbook * p = q;
  u32 k;
  if (!p) return;
  Fk(25, xfree(p->m[k]));
  xfree(p->pmf);  xfree(p);
}

static vres * res(vba * m, vb_res * r, vb_book * cb) {
  vres * p = r->acm;
  u32 pv = cb->dim;
  uint64_t sum;
  if (p) return p;
  p = xcalloc(1, sizeof *p);
  p->ncl = r->ncl;  p->pv = pv;
  p->sig = PH6(r->type, r->beg, r->end, r->psz, r->ncl, book(m, cb)->sig);
  sum = vb_gsum(r->ncl, pv);
  if (sum && m->tbl + sum * sizeof (uint64_t) <= TBUDGET) {
    m->tbl += sum * sizeof (uint64_t);
    vb_gfill(p->m, p->pw, cb, r->ncl, pv, 1);
  }
  r->acm = p;
  return p;
}

void vba_res_free(void * q) {
  vres * p = q;
  u32 k;
  if (!p) return;
  Fk(25, xfree(p->m[k]));
  xfree(p);
}

static void hpush(vba * m, acm_h f) {
  m->hh[3] = PH2(m->hh[2], f);  m->hh[2] = PH2(m->hh[1], f);
  m->hh[1] = PH2(m->hh[0], f);  m->hh[0] = f;
}

static acm_dec * hctx(vba * m, int k, acm_h tag) {
  acm_dec * d = acm_d(m->q);
  u32 l1 = m->hl[k & 63][0], l2 = m->hl[k & 63][1];
  d->ncx = 0;
  CX(PH2(tag, k));
  CX(PH3(tag, k, l1));
  CX(PH4(tag, k, l1, l2));
  CX(PH3(tag, k, m->hh[0]));
  CX(PH3(tag, k, m->hh[1]));
  CX(PH3(tag, k, m->hh[2]));
  CX(PH3(tag, k, m->hh[3]));
  sel_all(d, PH2(1, k), PH3(2, k, MIN(l1, 16)), PH2(3, m->hh[0] & 0xFF),
          4, 5, 6, 7);
  d->apm[1] = PH3(0x50, k, l1);  d->apm[2] = PH3(0x51, k, m->hh[1]);
  return d;
}

u32 vba_field(vba * m, int k, u32 r) {
  acm_dec * d = hctx(m, k, 0x5000);
  i32 s = (i32) r;
  u32 a = s < 0 ? 0u - (u32) s : (u32) s, nb, i, v = 0;
  d->ty = D_HDR;
  if (acm_bit(m->q, r != 0, 32768, PH2(0x5001, 0))) {
    int neg = acm_bit(m->q, s < 0, 32768, PH2(0x5001, 1));
    nb = blr_ilog(a);
    for (i = 1; i < 32; i++)
      if (!acm_bit(m->q, nb > i, 32768, PH3(0x5002, i, 0))) break;
    nb = i;  v = 1;
    for (i = nb - 1; i > 0; i--)
      v = v * 2 + (u32) acm_bit(m->q, (int) (a >> (i - 1) & 1), 32768,
                                PH4(0x5003, nb, i, nb - i <= 3 ? v : 0));
    if (neg) v = 0u - v;
  }
  m->hl[k & 63][1] = m->hl[k & 63][0];  m->hl[k & 63][0] = v;
  hpush(m, PH3(0x5004, k, v));
  return v;
}

u32 vba_tbits(vba * m, int slot, int dep, u32 v) {
  acm_dec * d = hctx(m, 100 + slot, 0x5100);
  u32 idx = 1;
  int i;
  d->ty = D_HBIT;
  for (i = dep - 1; i >= 0; i--)
    idx = idx * 2 + (u32) acm_bit(m->q, (int) (v >> i & 1), 32768,
                                  PH2(0x5101, idx));
  idx -= 1UL << dep;
  m->hl[(100 + slot) & 63][1] = m->hl[(100 + slot) & 63][0];
  m->hl[(100 + slot) & 63][0] = idx;
  hpush(m, PH3(0x5102, slot, idx));
  return idx;
}

void vba_frame(vba * m, int bf, u32 n, u32 nch, u32 md) {
  frame * F;
  int i;
  m->bf = bf;
  F = CUR(m);
  F->valid = 1;  F->mode = (int) md;
  m->nch = (int) MIN(nch, NCH);
  memset(F->role, 0, sizeof F->role);
  memset(F->partner, 0, sizeof F->partner);
  F->n = (int) MIN(n, NBIN);
  memset(F->used, 0, sizeof F->used);
  Fi(NCH, F->fno[i] = -1);
  memset(F->fy, 0xFF, sizeof F->fy);
  memset(F->fv, 0, sizeof F->fv);  memset(F->fcl, 0, sizeof F->fcl);
  memset(F->rcl, 0, sizeof F->rcl);
  Fi(m->nch,
    memset(F->flo[i], 0, (sz) F->n);
    memset(F->rec[i], 0, (sz) F->n * sizeof **F->rec);
    memset(F->dg[0][i], 0, (sz) F->n);  memset(F->dg[1][i], 0, (sz) F->n);
    memset(F->dg[2][i], 0, (sz) F->n));
  m->pcok = 0;
}

void vba_couple(vba * m, u32 mag, u32 ang) {
  frame * F = CUR(m);
  if (mag >= NCH || ang >= NCH) return;
  F->role[mag] = 1;  F->partner[mag] = (int) ang;
  F->role[ang] = 2;  F->partner[ang] = (int) mag;
}

static void regs(const vba * m, int c, int j, int fl, i32 * r) {
  const frame * P = &m->f[m->pi[m->bf]], * P2 = &m->f[m->p2[m->bf]],
              * P3 = &m->f[m->p3[m->bf]];
  const frame * S[3];
  static const int OFF[5] = { 0, -1, 1, -2, 2 };
  int k, i, o = m->nch == 2 ? c ^ 1 : -1;
  S[0] = P;  S[1] = P2;  S[2] = P3;
#define RG(X, ch, jj) ((X)->valid && (jj) >= 0 && (jj) < (X)->n \
    ? vb_rat((X)->lr[ch][jj], (X)->flo[ch][jj] - fl) : 0)
#define RGI(X, ch, jj) vb_rat((X)->lr[ch][jj], (X)->flo[ch][jj] - fl)
  int64_t v[NR];
  if (P->valid && P2->valid && j >= 2 && j + 2 < P->n && j + 2 < P2->n)
    for (k = 0, i = 0; i < 5; i++) {
      v[k++] = RGI(P, c, j + OFF[i]);  v[k++] = RGI(P2, c, j + OFF[i]);
    }
  else
    for (k = 0, i = 0; i < 5; i++) {
      v[k++] = RG(S[0], c, j + OFF[i]);  v[k++] = RG(S[1], c, j + OFF[i]);
    }
  v[10] = RG(S[2], c, j);
  v[11] = o >= 0 ? RG(S[0], o, j) : 0;
#undef RG
#undef RGI
  Fk(NR, r[k * LSL] = (i32) vb_clip24(v[k]));
}

static lsq * lsrow(vba * m, int c) {
  if (!m->ls[m->bf][c])
    m->ls[m->bf][c] = xcalloc(NBIN / LSL, sizeof (lsq));
  return m->ls[m->bf][c];
}

static void lspred_c(lsq * s, const i32 (*r)[LSL], i32 * out) {
  int64_t A[LSL][NR][NR], B[LSL][NR], W[LSL][NR], lam[LSL];
  int j, b, k, i, nl = 0, ix[LSL];
  Fi(LSL,
    int64_t tr = 0, md = 0;
    int sh = 0;
    out[i] = 0;
    Fj(NR,
      tr += s->a[j * (j + 3) / 2][i];
      if (s->a[j * (j + 3) / 2][i] > md) md = s->a[j * (j + 3) / 2][i]);
    if (tr <= 0) continue;
    Fj(NR, if (ABS(s->b[j][i]) > md) md = ABS(s->b[j][i]));
    while ((md >> sh) >= ((int64_t) 1 << 36)) sh++;
    lam[nl] = (tr >> sh) >> 10;
    k = 0;
    Fj(NR,
      for (b = 0; b <= j; b++, k++)
        A[nl][j][b] = A[nl][b][j] = s->a[k][i] >> sh);
    Fj(NR, B[nl][j] = (s->b[j][i] >> sh) * 16384;  W[nl][j] = s->w[j][i]);
    ix[nl++] = i);
  Fk(2,
    Fj(NR,
      Fi(nl,
        const int64_t * Ar = A[i][j], * Wl = W[i];
        int64_t den = Ar[j] + lam[i], w;
        int64_t n0 = B[i][j] + Ar[j] * Wl[j], n1 = 0, n2 = 0;
        for (b = 0; b < NR; b += 3) {
          n0 -= Ar[b] * Wl[b];  n1 -= Ar[b + 1] * Wl[b + 1];
          n2 -= Ar[b + 2] * Wl[b + 2];
        }
        n0 += n1 + n2;
        w = den > 0 ? n0 / den : 0;
        W[i][j] = CLAMP(w, -131072, 131072))));
  Fk(nl,
    int64_t o = 0, mx = 0;
    i = ix[k];
    Fj(NR,
      s->w[j][i] = (i32) W[k][j];  o += (int64_t) r[j][i] * W[k][j];
      if (ABS(r[j][i]) > mx) mx = ABS(r[j][i]));
    o >>= 14;
    out[i] = (i32) CLAMP(o, -3 * mx, 3 * mx));
}

static void lsacc_c(int64_t (*a)[LSL], int64_t (*b)[LSL], int64_t * te,
                    int64_t * tx, const i32 (*r)[LSL], const i32 * x0,
                    const i32 * e) {
  int i, j, k, l;
  for (l = 0; l < LSL; l++) {
    k = 0;
    Fi(NR,
      for (j = 0; j <= i; j++, k++)
        a[k][l] += (int64_t) r[i][l] * r[j][l] - (a[k][l] >> 5));
    Fi(NR, b[i][l] += (int64_t) r[i][l] * x0[l] - (b[i][l] >> 5));
    te[l] += (int64_t) e[l] * e[l] - (te[l] * 3 >> 5);
    tx[l] += (int64_t) x0[l] * x0[l] - (tx[l] * 3 >> 5);
  }
}

static void lspred(lsq * s, const i32 (*r)[LSL], i32 * out) {
#if defined(HAVE_AVX2)
  static int av = -1;
  if (av < 0) av = blr_cpu_avx2();
  if (av) { vba_lspred_avx2(s->a[0], s->b[0], s->w[0], r[0], out);  return; }
#endif
  lspred_c(s, r, out);
}

static void lsacc(int64_t (*a)[LSL], int64_t (*b)[LSL], int64_t * te,
                  int64_t * tx, const i32 (*r)[LSL], const i32 * x0,
                  const i32 * e) {
#if defined(HAVE_AVX2)
  static int av = -1;
  if (av < 0) av = blr_cpu_avx2();
  if (av) { vba_lsacc_avx2(a[0], b[0], te, tx, r[0], x0, e);  return; }
#endif
  lsacc_c(a, b, te, tx, r, x0, e);
}

void vba_ready(vba * m) {
  frame * F = CUR(m), * P = PRV(m), * P2 = PRV2(m);
  int i, j;
  if (!P->valid || !P2->valid || P->n != F->n || P2->n != F->n) return;
  Fi(m->nch,
    lsq * s = lsrow(m, i);
    Fj(F->n,
      regs(m, i, j, F->flo[i][j], &m->rg[i][j / LSL][0][j % LSL]);
      if (j % LSL == LSL - 1) {
        if (F->used[i])
          lspred(s + j / LSL, (const i32 (*)[LSL]) m->rg[i][j / LSL],
                 m->pc[i] + j + 1 - LSL);
        else memset(m->pc[i] + j + 1 - LSL, 0, LSL * sizeof (i32));
      }));
  m->pcok = 1;
}

static int predr(const vba * m, int c, int j, int64_t * x) {
  const frame * F = &m->f[m->ci];
  int mg = c, a;
  int64_t l, r;
  if (!m->pcok || j < 0 || j >= F->n) return 0;
  if (!F->role[c]) { if (!F->used[c]) return 0;  *x = m->pc[c][j];  return 1; }
  if (F->role[c] == 2) mg = F->partner[c];
  a = F->partner[mg];
  l = m->pc[mg][j];  r = m->pc[a][j];
  if (c == mg) *x = ABS(l) > ABS(r) ? l : r;
  else *x = ABS(l) > ABS(r) ? (l > 0 ? l - r : r - l) : (r > 0 ? l - r : r - l);
  return 1;
}

void vba_frame_end(vba * m) {
  frame * F = CUR(m), * P = PRV(m), * P2 = PRV2(m);
  int old = m->p3[m->bf], i, j;
  Fi(m->nch,
    if (F->role[i] != 2)
      Fj(F->n,
        i32 lv = F->rec[i][j], rv = 0;
        if (F->role[i] == 1) {
          int a = F->partner[i];
          vb_uncouple(F->rec[i][j], F->rec[a][j], &lv, &rv);
          F->lr[a][j] = F->used[a] ? rv : 0;
        }
        F->lr[i][j] = F->used[i] ? lv : 0));
  if (P->valid && P2->valid && P->n == F->n && P2->n == F->n)
    Fi(m->nch,
      lsq * s = lsrow(m, i);
      for (j = 0; j < F->n; j += LSL, s++) {
        i32 x0[LSL], e[LSL];
        int k;
        Fk(LSL,
          i32 v = F->lr[i][j + k];
          if (!m->pcok)
            regs(m, i, j + k, F->flo[i][j + k], &m->rg[i][j / LSL][0][k]);
          x0[k] = CLAMP(v, -(1 << 24), 1 << 24);
          e[k] = x0[k] - (m->pcok ? m->pc[i][j + k] : 0));
        lsacc(s->a, s->b, s->te, s->tx,
              (const i32 (*)[LSL]) m->rg[i][j / LSL], x0, e);
      });
  Fi(m->nch,
    Fj(F->n,
      i32 a = ABS(F->rec[i][j]);
      m->avf[m->bf][i][j] = (m->avf[m->bf][i][j] + a) >> 1;
      m->avs[m->bf][i][j] += (a - m->avs[m->bf][i][j]) >> 3));
  m->p3[m->bf] = m->p2[m->bf];  m->p2[m->bf] = m->pi[m->bf];
  m->pi[m->bf] = m->ci;  m->ci = old;
  m->lastbf = m->bf;
}

u32 vba_mode(vba * m, u32 md, u32 nmd) {
  acm_dec * d = acm_d(m->q);
  d->ty = D_MODE;  d->ncx = 0;
  CX(PH2(0x1000, nmd));
  CX(PH3(0x1001, nmd, m->pm[0]));
  CX(PH4(0x1002, nmd, m->pm[0], m->pm[1]));
  CX(PH3(0x1003, nmd, m->lastbf));
  sel_all(d, PH2(1, m->pm[0]), PH2(2, m->pm[1]), 3, 4, 5, 6, 7);
  d->apm[1] = PH2(0x10, m->pm[0]);  d->apm[2] = PH3(0x11, m->pm[0], m->pm[1]);
  md = acm_utree(m->q, md, nmd, PH2(0x1010, nmd));
  m->pm[1] = m->pm[0];  m->pm[0] = (int) md;
  return md;
}

int vba_window(vba * m, int w, int which, u32 md, int w0) {
  acm_dec * d = acm_d(m->q);
  d->ty = D_WIN;  d->ncx = 0;
  CX(PH3(0x1100, which, md));
  CX(PH4(0x1101, which, m->lastbf, which ? w0 : 2));
  CX(PH4(0x1102, which, m->pm[1], md));
  sel_all(d, PH2(1, which), PH3(2, which, m->lastbf), 3, 4, 5, 6, 7);
  d->apm[0] = PH2(0x12, which);  d->apm[1] = PH3(0x13, which, m->lastbf);
  d->apm[2] = PH3(0x14, which, w0);
  return acm_bit(m->q, w, 32768, PH2(0x1110, which));
}

static int point(int x0, int y0, int x1, int y1, int x) {
  int dy = y1 - y0, adx = x1 - x0, ady = ABS(dy), off;
  if (adx <= 0) return y0;
  off = ady * (x - x0) / adx;
  return dy < 0 ? y0 - off : y0 + off;
}

int vba_floor(vba * m, const vb_setup * s, u32 k, u32 fno, const vb_floor * f,
              u32 * y, u32 * classes, int u) {
  acm_dec * d = acm_d(m->q);
  frame * F = CUR(m), * P = PRV(m), * L = LST(m), * P2 = PRV2(m);
  int kc = k < NCH ? (int) k : NCH - 1, oc = kc > 0 ? kc - 1 : -1;
  int pok = P->valid && P->fno[kc] == (int) fno && P->used[kc];
  int lok = L->valid && L->fno[kc] == (int) fno && L->used[kc];
  int ook = oc >= 0 && F->fno[oc] == (int) fno && F->used[oc];
  int p2ok = P2->valid && P2->fno[kc] == (int) fno && P2->used[kc];
  int range = (int) f->quant, i, j;
  u32 post = 2, p;
  u8 step2[VB_MAXPOST];
  d->ty = D_USED;  d->ncx = 0;
  CX(PH3(0x2000, fno, kc));
  CX(PH4(0x2001, fno, P->valid ? P->used[kc] : 2, oc >= 0 ? F->used[oc] : 2));
  CX(PH4(0x2002, F->mode, P->valid ? P->used[kc] : 2,
         L->valid ? L->used[kc] : 2));
  sel_all(d, PH2(1, fno), PH2(2, P->used[kc]), 3, 4, 5, 6, 7);
  d->apm[0] = PH2(0x20, kc);  d->apm[1] = PH2(0x21, P->used[kc]);
  d->apm[2] = PH2(0x22, oc >= 0 ? F->used[oc] : 2);
  u = acm_bit(m->q, u, 32768, 0x2010);
  F->used[kc] = (u8) u;  F->fno[kc] = (int) fno;
  if (!u) return 0;
  p = blr_ilog(f->quant - 1);
  memset(step2, 0, sizeof step2);
  Fi(2,
    int py = pok ? P->fy[kc][i] : -1, oy = ook ? F->fy[oc][i] : -1,
        ly = lok ? L->fy[kc][i] : -1;
    d->ty = D_Y01;  d->ncx = 0;
    CX(PH3(0x2100, fno, i));
    CX(PH4(0x2101, fno, i, py));
    CX(PH4(0x2102, fno, i, py >> 2));
    CX(PH4(0x2103, fno, i, oy));
    CX(PH5(0x2104, fno, i, py, oy));
    CX(PH4(0x2105, fno, i, i ? F->fy[kc][0] : -1));
    CX(PH4(0x2106, fno, i, ly));
    CX(PH5(0x2107, fno, i, py >> 3, oy >> 3));
    sel_all(d, PH2(1, i), PH3(2, i, py >> 3), PH3(3, i, oy >> 3), PH2(4, fno),
            5, PH2(6, kc), 7);
    d->apm[1] = PH3(0x23, i, py);  d->apm[2] = PH3(0x24, i, oy);
    y[i] = acm_utree(m->q, y[i], 1U << p, PH3(0x2110, fno, i));
    F->fy[kc][i] = (short) y[i];  F->fv[kc][i] = (u8) MIN(y[i], 255);
    step2[i] = 1);
  Fi((int) f->parts,
    u32 c = f->pcls[i], cd = f->cdim[c], cs = f->csub[c], cv = 0;
    int pcl = pok ? P->fcl[kc][i] : 0, lcl = lok ? L->fcl[kc][i] : 0,
        p2cl = p2ok ? P2->fcl[kc][i] : 0;
    int ocl = ook ? F->fcl[oc][i] : 0, c1 = i > 0 ? F->fcl[kc][i - 1] : 0,
        c2 = i > 1 ? F->fcl[kc][i - 2] : 0;
    if (cs) {
      vb_book * cb = s->bk + f->cbook[c];
      vbook * B = book(m, cb);
      int pya = -1;
      if (pok) {
        int sum = 0, nn = 0;
        Fj((int) cd,
          if (P->fy[kc][post + j] >= 0) { sum += P->fy[kc][post + j];  nn++; });
        pya = nn ? sum / nn / 8 : -1;
      }
      d->ty = D_FCLS;  d->ncx = 0;
      CX(PH3(0x2200, B->sig, i));
      CX(PH3(0x2201, B->sig, pcl));
      CX(PH4(0x2202, B->sig, i, pcl));
      CX(PH3(0x2203, B->sig, ocl));
      CX(PH4(0x2204, B->sig, pcl, ocl));
      CX(PH3(0x2205, B->sig, c1));
      CX(PH4(0x2206, B->sig, c1, c2));
      CX(PH5(0x2207, B->sig, i, c1, pcl));
      CX(PH3(0x2208, B->sig, lcl));
      CX(PH4(0x2209, B->sig, pya, i));
      CX(PH5(0x220A, B->sig, i, pcl, ocl));
      CX(PH5(0x220B, B->sig, i, pcl, p2cl));
      CX(PH5(0x220C, B->sig, ocl, c1, pcl));
      sel_all(d, PH2(1, B->sig), PH2(2, i), PH2(3, pcl), PH2(4, ocl),
              PH2(5, c1), 0, PH2(7, pya));
      d->apm[1] = PH3(0x25, B->sig, pcl);  d->apm[2] = PH3(0x26, B->sig, ocl);
      cv = acm_qtree(m->q, classes[i], B->pmf, cb->ent,
                     PH3(0x2210, B->sig, i), 5, 0);
    }
    classes[i] = cv;
    F->fcl[kc][i] = (u8) (cv > 254 ? 255 : cv + 1);
    { u32 cvv = cv;
      Fj((int) cd,
        i32 bn = f->csb[c][cvv & ((1UL << cs) - 1)];
        u32 pi = post + (u32) j, lo = f->lo[pi], hi = f->hi[pi];
        int pr, val = 0;
        cvv >>= cs;
        pr = point((int) f->x[lo], F->fy[kc][lo], (int) f->x[hi],
                   F->fy[kc][hi], (int) f->x[pi]);
        if (bn >= 0) {
          vb_book * vb = s->bk + bn;
          vbook * B = book(m, vb);
          int hr = range - pr, room = MIN(hr, pr) * 2;
          int py = pok ? P->fy[kc][pi] : -1, oy = ook ? F->fy[oc][pi] : -1,
              ly = lok ? L->fy[kc][pi] : -1;
          int vs = py >= 0 ? vb_fold(pr, py, range) : -1,
              vo = oy >= 0 ? vb_fold(pr, oy, range) : -1;
          int vl = ly >= 0 ? vb_fold(pr, ly, range) : -1,
              vp = pok ? P->fv[kc][pi] : -1;
          int v1 = pi > 2 ? F->fv[kc][pi - 1] : -1;
          int dps = py >= 0 ? vb_sq(py - pr, 12) : 99,
              dos = oy >= 0 ? vb_sq(oy - pr, 12) : 99;
          int p2y = p2ok ? P2->fy[kc][pi] : -1, t, vtr = -1, vm = -1, vlx = -1;
          if (py >= 0 && p2y >= 0) {
            t = CLAMP(2 * py - p2y, 0, range - 1);
            vtr = vb_fold(pr, t, range);
          }
          if (py >= 0 && oy >= 0) vm = vb_fold(pr, (py + oy + 1) / 2, range);
          if (L->valid && L->used[kc] && L->n && F->n) {
            u32 lx = (u32) ((uint64_t) f->x[pi] * (u32) L->n / (u32) F->n);
            t = lx < (u32) L->n
              ? (L->flo[kc][lx] + (int) f->mult / 2) / (int) f->mult : -1;
            vlx = t >= 0 ? vb_fold(pr, MIN(t, range - 1), range) : -1;
          }
          d->ty = D_FVAL;  d->ncx = 0;
          CX(PH3(0x2300, B->sig, pi));
          CX(PH3(0x2301, B->sig, vs));
          CX(PH4(0x2302, B->sig, pi, vs));
          CX(PH3(0x2303, B->sig, vo));
          CX(PH4(0x2304, B->sig, vs, vo));
          CX(PH4(0x2305, B->sig, vp, pi));
          CX(PH3(0x2306, B->sig, v1));
          CX(PH4(0x2307, B->sig, pr >> 2, room >> 3));
          CX(PH3(0x2308, B->sig, vl));
          CX(PH5(0x2309, B->sig, pi, vp, v1));
          CX(PH4(0x230A, B->sig, dps, dos));
          CX(PH4(0x230B, fno, pi, vs));
          CX(PH5(0x230C, B->sig, vs, v1, dos));
          CX(PH4(0x2311, B->sig, vlx, vs));
          CX(PH4(0x230D, B->sig, vtr, vs));
          CX(PH3(0x230E, B->sig, vm));
          CX(PH5(0x230F, B->sig, vo, dps, pi));
          sel_all(d, PH2(1, B->sig), PH2(2, pi), PH2(3, vb_sq(vs, 16)),
                  PH2(4, vb_sq(vo, 16)), PH2(5, pr >> 4), 0,
                  PH2(7, vb_sq(v1, 16)));
          d->apm[1] = PH3(0x27, B->sig, vs);  d->apm[2] = PH3(0x28, B->sig, vo);
          val = (int) acm_qtree(m->q, y[pi], B->pmf, vb->ent,
                                PH3(0x2310, B->sig, dps == 99 ? 99 : 0), 5, 0);
        }
        y[pi] = (u32) val;
        F->fv[kc][pi] = (u8) MIN(val, 255);
        F->fy[kc][pi] = (short) vb_unfold(pr, val, range);
        if (val) step2[lo] = step2[hi] = step2[pi] = 1); }
    post += cd);
  { int lx = 0, ly = F->fy[kc][0] * (int) f->mult, x, n = F->n;
    for (i = 1; i < (int) f->posts; i++) {
      u32 pi = f->srt[i];
      int hx, hy;
      if (!step2[pi]) continue;
      hx = (int) f->x[pi];  hy = F->fy[kc][pi] * (int) f->mult;
      for (x = lx; x < hx && x < n; x++) {
        int t = point(lx, ly, hx, hy, x);
        F->flo[kc][x] = (u8) CLAMP(t, 0, 255);
      }
      lx = hx;  ly = hy;
    }
    for (x = lx; x < n; x++)
      F->flo[kc][x] = (u8) CLAMP(ly, 0, 255); }
  return 1;
}

u32 vba_class(vba * m, vb_res * r, vb_book * cb, u32 rno, u32 jv, u32 pp,
              u32 k, u32 pre, u32 val, u32 il, int rch, const u8 * cmap) {
  acm_dec * d = acm_d(m->q);
  frame * F = CUR(m), * P = PRV(m), * L = LST(m), * P2 = PRV2(m);
  vres * R = res(m, r, cb);
  uint64_t pmf[VB_MAXRCL];
  u32 i, ncl = R->ncl, j0, j1, b, nn = 0, q = rno & 3;
  int pcl, pcll, pclr, c1, c2, ocl, lcl = 0, fm = 0, pfm = 0, pe = 0, p2cl, qm,
      qs2;
  int64_t psum = 0, pmax = 0, pes = 0;
  int jc = jv < NCH ? (int) jv : NCH - 1;
  u32 ppc = pp < NPART ? pp : NPART - 1;
  Fi(ncl, pmf[i] = R->m[0] ? R->m[k + 1][pre * ncl + i] : 1);
  pcl = P->valid ? P->rcl[q][jc][ppc] : 0;
  pcll = P->valid && ppc > 0 ? P->rcl[q][jc][ppc - 1] : 0;
  pclr = P->valid && ppc + 1 < NPART ? P->rcl[q][jc][ppc + 1] : 0;
  p2cl = P2->valid ? P2->rcl[q][jc][ppc] : 0;
  c1 = ppc > 0 ? F->rcl[q][jc][ppc - 1] : 0;
  c2 = ppc > 1 ? F->rcl[q][jc][ppc - 2] : 0;
  ocl = jc > 0 ? F->rcl[q][jc - 1][ppc] : 0;
  if (L->valid && L->n && F->n) {
    u32 lp = (u32) ((uint64_t) ppc * (u32) L->n / (u32) F->n);
    if (lp < NPART) lcl = L->rcl[q][jc][lp];
  }
  j0 = r->beg + pp * r->psz;  j1 = j0 + r->psz;
  if (il) { j0 /= il;  j1 /= il; }
  if (j1 > (u32) F->n) j1 = (u32) F->n;
  for (b = j0; b < j1; b++) {
    u32 c, c0 = il ? 0 : (u32) rch, cn = il ? MIN(il, NCH) : (u32) rch + 1;
    for (c = c0; c < cn; c++) {
      int ch = il ? cmap[c] : (int) c;
      int64_t xr;
      if (ch >= NCH) ch = NCH - 1;
      fm += F->flo[ch][b];  nn++;
      if (predr(m, ch, (int) b, &xr)) {
        xr = ABS(xr);  psum += xr;  if (xr > pmax) pmax = xr;
      }
      if (P->valid && (int) b < P->n) {
        pfm += P->flo[ch][b];  pes += ABS(P->rec[ch][b]);
      }
    }
  }
  if (nn) {
    fm /= (int) nn;  pfm /= (int) nn;
    pe = lq(4 * pes / (int64_t) nn, 1 << VB_RQ, 7);
  }
  qm = lq(pmax, 1 << VB_RQ, 7);
  qs2 = nn ? lq(4 * psum / (int64_t) nn, 1 << VB_RQ, 7) : 9;
  d->ty = D_RCLS;  d->ncx = 0;
  CX(PH3(0x3000, R->sig, pp));
  CX(PH3(0x3001, R->sig, pcl));
  CX(PH4(0x3002, R->sig, pp, pcl));
  CX(PH3(0x3003, R->sig, c1));
  CX(PH4(0x3004, R->sig, c1, c2));
  CX(PH4(0x3005, R->sig, pcl, c1));
  CX(PH3(0x3006, R->sig, ocl));
  CX(PH3(0x3007, R->sig, fm >> 2));
  CX(PH4(0x3008, R->sig, fm >> 3, pcl));
  CX(PH4(0x3009, R->sig, k, pre));
  CX(PH5(0x300A, R->sig, pcll, pcl, pclr));
  CX(PH4(0x300B, R->sig, pe, fm >> 4));
  CX(PH4(0x300C, R->sig, vb_sq((fm - pfm) >> 2, 6), pcl));
  CX(PH3(0x300D, R->sig, lcl));
  CX(PH5(0x300E, R->sig, ocl, pcl, c1));
  CX(PH5(0x300F, R->sig, pp, pcl, p2cl));
  CX(PH5(0x3011, R->sig, pcl, p2cl, c1));
  CX(PH4(0x3012, R->sig, qm, qs2));
  CX(PH4(0x3013, R->sig, qm, pcl));
  CX(PH5(0x3014, R->sig, qs2, c1, ocl));
  sel_all(d, PH2(1, R->sig), PH2(2, pp < 64 ? pp : 64 + pp / 16), PH2(3, pcl),
          PH2(4, c1), PH2(5, fm >> 4), 0, PH2(7, ocl));
  d->apm[1] = PH3(0x30, R->sig, pcl);  d->apm[2] = PH3(0x31, ocl, c1);
  i = acm_qtree(m->q, val, pmf, ncl, PH2(0x3010, R->sig), 5, 0);
  if (pp < NPART) F->rcl[q][jc][pp] = (u8) (i + 1);
  return i;
}

static int ncdf(int64_t t2, int64_t del, int64_t mu, int64_t sd) {
  int64_t z = (t2 * del - 2 * mu) * 436 / (2 * sd);
  return acm_squash((int) CLAMP(z, -2047, 2047));
}

static int gst(int64_t num, int64_t den) {
  int64_t p = den > 0 ? num * 4096 / den : 2048;
  return acm_stretch((int) CLAMP(p, 1, 4095));
}

static void mm_put(vba * m, int d) {
  if (m->mml && m->mmp != m->mmn && m->mmb[m->mmp & (MM_N - 1)] == d) {
    m->mml++;  m->mmp++;
  }
  else m->mml = 0;
  m->mmb[m->mmn & (MM_N - 1)] = (signed char) d;
  m->mmn++;
  m->mmh = (m->mmh * 32 + (uint64_t) (d + 64)) &
           ((((uint64_t) 1) << (5 * MM_MIN)) - 1);
  if (m->mmn >= MM_MIN) {
    u32 h = (u32) (PH2(m->mmh, 0x77) >> (64 - MM_HB));
    if (!m->mml && m->mmt[h] && m->mmn - m->mmt[h] < MM_N - 1) {
      m->mmp = m->mmt[h];  m->mml = 1;
    }
    m->mmt[h] = m->mmn;
  }
}

#define DIGIT_CX(cur) do { \
  uint64_t cur_ = (cur); \
  if (!cur_) break; \
  CXK(0, PH3(0x4000, bs, k));                                                  \
  CXK(1, PH4(0x4002, bs, d1, d2));                                             \
  CXK(2, PH3(0x4003, bs, dx));                                                 \
  CXK(3, PH4(0x4005, bs, qa, dp));                                             \
  CXK(4, PH5(0x4007, bs, qr, qrl, qrr));                                       \
  CXK(5, PH3(0x4008, bs, fl >> 2));                                            \
  CXK(6, PH4(0x400A, bs, cls, jp));                                            \
  CXK(7, PH3(0x400B, bs, c));                                                  \
  CXK(8, PH4(0x400C, bs, c, pd));                                              \
  CXK(9, PH4(0x400D, bs, en, MIN(*nnz, 7)));                                   \
  CXK(10, PH4(0x400E, bs, k, pre));                                            \
  CXK(11, PH4(0x4012, bs, mexp, mlq));                                         \
  CXK(12, PH5(0x4013, bs, qa, qr, fl >> 4));                                   \
  CXK(13, PH6(0x4014, bs, d1, d2, d3, d4));                                    \
  CXK(14, PH4(0x4015, bs, qr, dx));                                            \
  CXK(15, PH4(0x4016, bs, qx, d1));                                            \
  CXK(16, PH4(0x4018, bs, qr, q2));                                            \
  CXK(17, PH5(0x4019, bs, qa, qp1, qn1));                                      \
  CXK(18, PH4(0x401A, bs, pe, qx));                                            \
  CXK(19, PH4(0x401B, bs, c, qs));                                             \
  if (m->cxm >> 20 & 3) {                                                      \
    int qvs = lq(m->avs[m->bf][ch][c], del, 6),                                \
        qvf = lq(m->avf[m->bf][ch][c], del, 6);                                \
    CXK(20, PH4(0x401C, bs, qvs, qvf));                                        \
    CXK(21, PH5(0x401D, bs, qvs, qr, d1));                                     \
  }                                                                            \
  CXK(22, PH4(0x401E, bs, cls, nbc));                                          \
  CXK(23, PH3(0x401F, bs, qt));                                                \
  CXK(24, PH4(0x4020, bs, qt, qr));                                            \
  CXK(25, PH5(0x4021, bs, c & 1, qr, q2));                                     \
  CXK(26, PH5(0x4022, bs, c, s1, s2));                                         \
  CXK(27, PH4(0x4023, bs, qtr, g));                                            \
  CXK(28, PH5(0x4024, bs, ql1, qt, qr1));                                      \
  CXK(29, PH4(0x4025, bs, qtr, e1));                                           \
  CXK(30, PH4(0x4028, bs, qf, g));                                             \
  CXK(31, PH5(0x4029, bs, qf, k, pre));                                        \
  CXK(32, PH4(0x402A, bs, qf, ex));                                            \
} while (0)

#define RULE_CX() CX(PH4(0x4031, bs, cls, rs))

u32 vba_digit(vba * m, vb_book * b, u32 k, u32 pre, u32 idx, int enc,
              u32 pass, int cls, int rch, u32 c, u32 jp, u32 * nnz, int nbc,
              vb_rule * rp) {
  acm_dec * d = acm_d(m->q);
  frame * F = CUR(m), * P = PRV(m), * P2 = PRV2(m);
  vbook * B = book(m, b);
  uint64_t ms[3], wm[256];
  u32 nv = b->nv, i;
  int dd[256], pidx = pass < 2 ? (int) pass : 2, och = rch ^ 1,
      ch = rch < NCH ? rch : NCH - 1;
  int d1, d2, d3, d4, dx, dp, pd, qa, qr, qrl, qrr, fl, en, mexp, mlq, qx, qs,
      q2, qp1, qn1, pe;
  int qt = 9, qtr = 9, qf = 99, g = 9, ql1 = 9, qr1 = 9, e1 = 9, ex = 9, s1, s2,
      gok = 0, target = -999, dv, mag, sgn, nz;
  int64_t del = B->del, acc, gmu = 0, gsd = 1, xh;
  acm_h bs = B->sig, node;
  int nzc, sext = !(m->cxz & ~m->cxs), gnew = m->cxg != m->cxz, rs = 0, rfa = 1;
  FATAL_IF_HOT(nv > 256)("vorbis: %lu multiplicands", (unsigned long) nv);
  if (och >= NCH) och = NCH - 1;
  if (c >= NBIN) c = NBIN - 1;
  Fi(nv,
    dd[i] = (int) b->mult[i] - (int) b->off;
    wm[i] = B->m[0] ? B->m[k + 1][pre + i * B->pw[k]] : 1;
    if (enc && i == idx) target = dd[i]);
  d1 = c >= 1 ? F->dg[pidx][ch][c - 1] : 0;
  d2 = c >= 2 ? F->dg[pidx][ch][c - 2] : 0;
  d3 = c >= 3 && CXON(13) ? F->dg[pidx][ch][c - 3] : 0;
  d4 = c >= 4 && CXON(13) ? F->dg[pidx][ch][c - 4] : 0;
  dx = F->dg[pidx][och][c];
  dp = pass > 0 ? F->dg[MIN(pass - 1, 2)][ch][c] : 9;
  acc = F->rec[ch][c];
  if (rp->on) rs = vb_rule_state(rp, acc, del, &rfa);
  qa = pass > 0 ? lq(acc, del, 6) : 9;
  pd = P->valid && (int) c < P->n ? P->dg[pidx][ch][c] : 9;
  qr = P->valid && (int) c < P->n ? lq(P->rec[ch][c], del, 6) : 9;
  qrl = CXON(4) && P->valid && c >= 1 && (int) c - 1 < P->n
      ? lq(P->rec[ch][c - 1], del, 4) : 9;
  qrr = CXON(4) && P->valid && (int) c + 1 < P->n
      ? lq(P->rec[ch][c + 1], del, 4) : 9;
  fl = F->flo[ch][c];
  en = ABS(d1) + ABS(d2) + ABS(dx);
  en = MIN(en, 7);
  mexp = m->mml ? m->mmb[m->mmp & (MM_N - 1)] : 99;
  mlq = m->mml == 0 ? 0 : m->mml < 16 ? 1 : m->mml < 32 ? 2 : 3;
  qx = lq(F->rec[och][c], del, 6);
  qs = m->cxm >> 19 & 1 && P->valid && (int) c < P->n
     ? lq(vb_rat(P->rec[ch][c], P->flo[ch][c] - fl), del, 6) : 9;
  q2 = (CXON(16) || CXON(25) || CXON(26)) && P2->valid && (int) c < P2->n
     ? lq(P2->rec[ch][c], del, 6) : 9;
  qp1 = CXON(17) && c >= 1 ? lq(F->rec[ch][c - 1], del, 4) : 9;
  qn1 = CXON(17) && (int) c + 1 < F->n ? lq(F->rec[ch][c + 1], del, 4) : 9;
  if (P->valid) {
    int lo = c >= 2 ? (int) c - 2 : 0, hi = (int) c + 2, j;
    int64_t se = 0;
    if (hi >= P->n) hi = P->n - 1;
    for (j = lo; j <= hi; j++) se += ABS(P->rec[ch][j]);
    pe = lq(se, del, 7);
  } else pe = 9;
  s1 = qr == 9 ? 2 : qr < 0 ? -1 : qr > 0;
  s2 = q2 == 9 ? 2 : q2 < 0 ? -1 : q2 > 0;
  if (m->ls[m->bf][ch] && predr(m, ch, (int) c, &xh)) {
    const lsq * L = m->ls[m->bf][ch] + c / LSL;
    int64_t te = L->te[c % LSL], tx = L->tx[c % LSL];
    qt = lq(xh, del, 6);
    qtr = pass > 0 ? lq(xh - acc, del, 6) : qt;
    qf = vb_sq((int) blr_fdiv(4 * (xh - acc) + del, 2 * del), 12);
    gmu = xh - acc;
    gsd = (int64_t) blr_isqrt64((uint64_t) (te * 3 >> 5));
    if (gsd < del / 4) gsd = del / 4;
    if (gsd < 1) gsd = 1;
    gok = 1;
    g = tx <= 0 ? 8 : te >= tx ? 0 : te < tx / 64 ? 7 : te < tx / 16 ? 6
      : te < tx / 8 ? 5 : te < tx / 4 ? 4 : te < tx / 2 ? 3 : 2;
    if (predr(m, ch, (int) c - 1, &xh)) {
      ql1 = lq(xh, del, 4);  e1 = lq(F->rec[ch][c - 1] - xh, del, 4);
    }
    if (m->cxm >> 28 & 1 && predr(m, ch, (int) c + 1, &xh))
      qr1 = lq(xh, del, 4);
  }
  if (predr(m, och, (int) c, &xh)) ex = lq(F->rec[och][c] - xh, del, 4);
  d->ncx = 0;
  DIGIT_CX(m->cxm & m->cxz);
  if (rs) RULE_CX();
  nzc = d->ncx;
  if (m->lite) {
    d->sel[0] = PH2(1, bs);  d->sel[1] = d->sel[7] = PH3(9, qf, g);
    d->xon = 0;
  } else {
    sel_all(d, PH2(1, bs), PH3(2, pass, cls), PH2(3, qr),
            PH3(4, vb_sq(d1, 3), vb_sq(dx, 3)), PH2(5, fl >> 4),
            PH3(6, qa, vb_sq(dp, 3)), PH3(7, mexp, mlq));
    d->sel[7] = PH3(9, qf, g);
    if (rs) { d->sel[0] = PH3(1, bs, rs);  d->sel[1] = PH4(2, pass, cls, rs); }
  }
  d->apm[0] = rs && !m->lite ? PH5(0x44, bs, pd, d1, rs)
                             : PH4(0x40, bs, pd, d1);
  d->apm[1] = PH3(0x41, qa, qr);  d->apm[2] = PH3(0x42, bs, c);
  ms[0] = ms[1] = ms[2] = 0;
  Fi(nv, ms[dd[i] < 0 ? 0 : dd[i] > 0 ? 2 : 1] += wm[i]);
  nz = target != 0;
  d->xon = gok;
  if (ms[1] && (ms[0] || ms[2])) {
    d->ty = D_RZ;  node = PH2(0x4100, 1);
    if (gok)
      d->xin = gst(4096 - (ncdf(1, del, gmu, gsd) - ncdf(-1, del, gmu, gsd)), 4096);
    nz = acm_bit(m->q, nz, vb_pfrac(ms[0] + ms[2], ms[1]), node);
  } else nz = !ms[1];
  dv = 0;
  if (nz) {
    sgn = target < 0;
    if (ms[0] && ms[2]) {
      d->ty = D_RSG;  node = PH2(0x4100, 2);
      if (!sext) {
        d->ncx = 0;  DIGIT_CX(m->cxm & m->cxs);  if (rs) RULE_CX();  gnew = 1;
      }
      else if (m->cxs & m->cxm & ~m->cxz) DIGIT_CX(m->cxm & m->cxs & ~m->cxz);
      if (gok) {
        int pn = ncdf(-1, del, gmu, gsd), pp = 4096 - ncdf(1, del, gmu, gsd);
        d->xin = gst(pn, pn + pp);
      }
      sgn = acm_bit(m->q, sgn, vb_pfrac(ms[0], ms[2]), node);
    } else sgn = ms[0] != 0;
    mag = 1;
    for (;;) {
      uint64_t ge = 0, gt = 0;
      Fi(nv,
        if ((sgn ? dd[i] < 0 : dd[i] > 0) && wm[i]) {
          if (ABS(dd[i]) >= mag) ge += wm[i];
          if (ABS(dd[i]) > mag) gt += wm[i];
        });
      if (!gt) break;
      if (ge > gt) {
        if (gnew) {
          d->ncx = 0;  DIGIT_CX(m->cxm & m->cxg);
          if (rs) RULE_CX();
          nzc = d->ncx;  gnew = 0;
        }
        d->ty = D_RMG;  node = PH3(0x4100, 3, MIN(mag, 15));  d->ncx = nzc;
        if (gok) {
          int a = sgn ? ncdf(-(2 * mag + 1), del, gmu, gsd)
                      : 4096 - ncdf(2 * mag + 1, del, gmu, gsd);
          int e = sgn ? ncdf(-(2 * mag - 1), del, gmu, gsd)
                      : 4096 - ncdf(2 * mag - 1, del, gmu, gsd);
          d->xin = gst(a, e);
        }
        if (!acm_bit(m->q, ABS(target) > mag, vb_pfrac(gt, ge - gt), node))
          break;
      }
      mag++;
    }
    dv = sgn ? -mag : mag;
  }
  d->xon = 0;
  (*nnz) += dv != 0;
  Fi(nv, if (dd[i] == dv && wm[i]) break);
  FATAL_IF_HOT(i >= nv)("vorbis: decoded digit %d is not a multiplicand", dv);
  F->dg[pidx][ch][c] = (signed char) vb_sq(dv, 127);
  { int64_t r = acc + (int64_t) dv * del;
    F->rec[ch][c] = (i32) CLAMP(r, -0x3FFFFFFF, 0x3FFFFFFF);
    if (rp->on) vb_rule_put(rp, ABS((int64_t) F->rec[ch][c]), rfa); }
  mm_put(m, vb_sq(dv, 127));
  return i;
}
