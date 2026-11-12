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

#include "vorbis/vb.h"
#include "fmt/cmt.h"
#include "sys/prof.h"
#include "vorbis/acm.h"
#include "vorbis/zc.h"
#include "vorbis/dict.h"

static void cm_alloc(vb_ctx * v);
static void gate_init(void);
static void ar_clear(vb_ctx * v, sz at, sz n);

static const mdl_cfg CFG[M_N] = {
  {   3,     1,  1,    8,     0,   1 },
  {   5,     1,  1,    8,     0,   1 },
  {   5,     1,  1,    8,     0,   1 },
  {   5,     1,  1,    8,     0,   1 },
  {   5,     1,  1,    8,     0,   1 },
  {   3,     1,  1,    8,     0,   1 },
  {   3,     1,  1,    8,     0,   1 },
  {   3,     1,  1,    8,     1,   1 },
  {   4,     1,  2,    8,     0,   1 },
  {   5,     0,  0,    32,    1,   0 },
  {   3,     0,  0,    8,     0,   0 },
  {   5,     0,  0,    8,     1,   0 },
  {   3,     1,  1,    8,     1,   1 },
  {   2,     1,  1,    2,     1,   1 },
  {   3,     1,  1,    8,     0,   0 },
  {   3,     1,  1,    8,     0,   1 },
  {   3,     1,  1,    8,     0,   1 },
  {   3,     1,  1,    8,     0,   1 },
  {   2,     1,  1,    2,     1,   1 },
  {   3,     1,  1,    8,     0,   1 },
  {   3,     1,  1,    8,     1,   0 },
  {   4,     1,  1,    8,     0,   1 },
  {   3,     1,  1,    8,     0,   1 },
  {   5,     1,  1,    8,     0,   1 },
  {   5,     1,  1,    8,     0,   1 },
  {   5,     1,  1,    1,     1,   1 },
  {   3,     1,  1,    8,     0,   1 },
  {   3,     1,  1,    8,     0,   1 },
  {   4,     1,  1,    8,     1,   0 },
  {   3,     1,  1,    8,     0,   1 },
  {   3,     1,  1,    8,     0,   1 },
  {   4,     1,  1,    8,     0,   1 },
  {   3,     1,  1,    8,     0,   1 },
  {   4,     1,  1,    8,     0,   1 },
  {   3,     1,  1,    8,     0,   1 },
  {   3,     1,  1,    8,     0,   1 },
  {   3,     1,  1,    8,     0,   1 },
  {   3,     1,  1,    8,     0,   1 },
  {   3,     1,  1,    8,     0,   1 },
  {   5,     1,  1,    32,    0,   1 },
  {   4,     1,  1,    2,     0,   1 },
  {   4,     1,  1,    2,     0,   1 },
  {   5,     1,  1,    8,     0,   0 },
  {   5,     1,  1,   32,     0,   1 }
};

static const mdl_cfg APLCFG = { 4, 0, 0, 32, 1, 0 };

void vb_init(vb_ctx * v) {
  sz i;
  memset(&v->tbl, 0, sizeof v->tbl);
  Fi(M_N, mdl_init_in(v->m + i, CFG + i, &v->tbl));
  rc_probs_init(v->f, F_NSLOT);
  v->tailp = xmalloc(VB_TBANK * VB_TSIZE * sizeof *v->tailp);
  v->tailc = NULL;
  memset(v->fc, 0, sizeof v->fc);  memset(v->amc, 0, sizeof v->amc);
  memset(v->awc, 0, sizeof v->awc);
  rc_probs_init(v->tailp, VB_TBANK * VB_TSIZE);
  v->pb = 0;  memset(&v->i, 0, sizeof v->i);
  rc_probs_init(v->am, VB_MBANK * VB_MSTEP);
  rc_probs_init(&v->aw[0][0], 4);
  mdl_init_in(&v->apt, CFG + M_CH, &v->tbl);
  mdl_init_in(&v->apl, &APLCFG, &v->tbl);
  v->pm = v->pw[0] = v->pw[1] = 0;
  v->su = NULL;  v->nsu = v->csu = 0;  v->cur = NULL;
  memset(v->sl, 0, sizeof v->sl);  memset(v->st, 0, sizeof v->st);
  v->nsl = 0;
  v->ar = NULL;  v->arn = 0;  v->mem = NULL;
  memset(&v->arena, 0, sizeof v->arena);
  v->mavg = NULL;
  memset(v->ab, 0, sizeof v->ab);  v->aglob = v->astep = 0;
  memset(&v->cm, 0, sizeof v->cm);  v->cm_mask = 0;  v->gate = NULL;
  memset(v->nv0, 0, sizeof v->nv0);
  memset(v->nidx, 0, sizeof v->nidx);
  memset(v->nrun, 0, sizeof v->nrun);
  v->nxv = 0;  v->npch = -1;  v->nstarted = 0;
  v->dp = NULL;
  v->symbols = NULL;  v->nsymbols = v->csymbols = 0;  v->symfull = 0;
  v->ys = NULL;  v->ysn = 0;  v->cs = NULL;  v->csn = 0;
  memset(v->fpl, 0, sizeof v->fpl);
  v->hu = 0;  v->psl = 0;  v->acm = NULL;  v->zc = NULL;  v->olen = 0;
  v->zsh = v->ish = v->csh = v->hsh = 0;
  v->rule = 0;  memset(&v->rp, 0, sizeof v->rp);
  rc_probs_init(v->vq, VQ_N);  memset(v->vqc, 0, sizeof v->vqc);
  memset(v->fh, 0, 4);  memset(v->sh, 0, 4);  memset(v->mh, 0, 4);
  vb_tune_default(&v->t);
}

void vb_tune_default(vb_tune * t) {
  t->lr = 7;  t->flags = 0;  t->x = 0;  t->x2 = 0;  t->x3 = 0;
}

void vb_tune_set(vb_ctx * v, const vb_tune * t) {
  FATAL_UNLESS(!v->cm.live, "vorbis: CM is already built");
  v->t = *t;
  v->rule = (t->x2 & VB_T2_RULE) != 0;
}

static const u8 PRESET[VB_NPRESET][3] = {
  [1] = { 20, VB_TF_GATE, VB_TX_PRIOR | VB_TX_SLOTW },
  [2] = { 20, VB_TF_GATE | VB_TF_HIST, VB_TX_PRIOR | VB_TX_SLOTW },
  [3] = { 20, VB_TF_GATE | VB_TF_HIST, VB_TX_PRIOR | VB_TX_SLOTW | VB_TX_DP },
  [4] = { 20, VB_TF_GATE | VB_TF_HIST,
          VB_TX_PRIOR | VB_TX_SLOTW | VB_TX_DP | VB_TX_APM },
  [10] = { 7, VB_TF_ZMAG, 0 },
  [12] = { 20, VB_TF_GATE | VB_TF_ZMAG, VB_TX_PRIOR | VB_TX_SLOTW },
  [13] = { 20, VB_TF_GATE | VB_TF_HIST | VB_TF_ZMAG,
           VB_TX_PRIOR | VB_TX_SLOTW },
  [14] = { 20, VB_TF_GATE | VB_TF_HIST | VB_TF_ZMAG,
           VB_TX_PRIOR | VB_TX_SLOTW | VB_TX_DP },
  [15] = { 20, VB_TF_GATE | VB_TF_HIST | VB_TF_ZMAG,
           VB_TX_PRIOR | VB_TX_SLOTW | VB_TX_DP | VB_TX_APM | VB_TX_APMS },
  [18] = { 20, VB_TF_GATE | VB_TF_HIST | VB_TF_ZMAG,
           VB_TX_PRIOR | VB_TX_SLOTW | VB_TX_H2 },
  [19] = { 20, VB_TF_GATE | VB_TF_HIST | VB_TF_ZMAG,
           VB_TX_PRIOR | VB_TX_SLOTW | VB_TX_DP | VB_TX_H2 },
  [20] = { 20, VB_TF_GATE | VB_TF_HIST | VB_TF_ZMAG,
           VB_TX_PRIOR | VB_TX_SLOTW | VB_TX_DP | VB_TX_APM | VB_TX_H2 |
           VB_TX_APMS },
  [21] = { 20, VB_TF_GATE | VB_TF_ZMAG | VB_TF_ZONLY,
           VB_TX_PRIOR | VB_TX_SLOTW }
};

void vb_tune_preset(vb_tune * t, int k) {
  FATAL_UNLESS(k >= 0 && k < VB_NPRESET && PRESET[k][0],
               "internal: no Vorbis preset %d", k);
  vb_tune_default(t);
  t->lr = PRESET[k][0];  t->flags = PRESET[k][1];  t->x = PRESET[k][2];
}

int vb_zcopt(const vb_tune * t) {
  return (t->x3 & VB_T3_OPT) >> 1 | (t->x2 & VB_T2_ZC) << 6 |
         (t->x2 & VB_T2_ZC2) << 5;
}

static void bk_free(vb_book * b) {
  vba_book_free(b->acm);  vbz_book_free(b->zc);
}

const i32 VB_RAT[129] = {
  73, 77, 82, 88, 93, 100, 106, 113, 120, 128, 136, 145, 155, 165, 176, 187,
  199, 212, 226, 241, 256, 273, 291, 309, 330, 351, 374, 398, 424, 452, 481,
  512, 546, 581, 619, 659, 702, 748, 796, 848, 903, 962, 1024, 1091, 1162, 1237,
  1318, 1404, 1495, 1592, 1696, 1806, 1923, 2048, 2181, 2323, 2474, 2635, 2807,
  2989, 3184, 3391, 3611, 3846, 4096, 4362, 4646, 4948, 5270, 5613, 5978, 6366,
  6780, 7221, 7691, 8191, 8723, 9291, 9895, 10538, 11224, 11953, 12731, 13558,
  14440, 15379, 16379, 17444, 18579, 19787, 21073, 22444, 23903, 25458, 27113,
  28876, 30754, 32754, 34883, 37152, 39568, 42141, 44881, 47799, 50908, 54218,
  57744, 61498, 65497, 69757, 74293, 79124, 84269, 89748, 95585, 101800, 108420,
  115470, 122979, 130976, 139493, 148563, 158224, 168513, 179471, 191141,
  203570, 216808, 230906
};

int64_t vb_bdel(const vb_book * b) {
  int sh = b->de + VB_RQ;
  int64_t v = sh >= 31 ? (int64_t) 1 << 30
            : sh >= 0 ? (int64_t) b->dm << sh
            : -sh >= 32 ? 0 : b->dm >> -sh;
  return CLAMP(v, 1, 1 << 30);
}

uint64_t vb_gsum(u32 n, u32 dim) {
  uint64_t tot = 1, sum = 0;
  u32 k;
  for (k = 0; k <= dim && k < 25 && tot <= (1u << 22); k++) {
    sum += tot;  tot *= n;
  }
  return dim < 25 && k == dim + 1 ? sum : 0;
}

void vb_gfill(uint64_t ** m, u32 * pw, const vb_book * b, u32 n, u32 dim,
              int res) {
  u32 k, e;
  pw[0] = 1;
  for (k = 1; k <= dim; k++) pw[k] = pw[k - 1] * n;
  for (k = 0; k <= dim; k++) m[k] = xcalloc(pw[k], sizeof *m[k]);
  for (e = 0; e < b->ent && e < pw[dim]; e++)
    if (b->len[e])
      for (k = 0; k <= dim; k++)
        m[k][res ? e / pw[dim - k] : e % pw[k]] += vb_mass(b->len[e]);
}

void vb_free(vb_ctx * v) {
  sz i, j;
  xfree(v->tailp);  v->tailp = NULL;
  xfree(v->tailc);  v->tailc = NULL;
  cm_free(&v->cm);
  xfree(v->gate);  v->gate = NULL;
  Fi(v->nsu,
    Fj(v->su[i]->nbk, bk_free(v->su[i]->bk + j));
    Fj(v->su[i]->nrs,
      vba_res_free(v->su[i]->rs[j].acm);  vbz_res_free(v->su[i]->rs[j].zc));
    xfree(v->su[i]->bk);  xfree(v->su[i]->fl);  xfree(v->su[i]->rs);
    xfree(v->su[i]->mp);  xfree(v->su[i]));
  xfree(v->su);  v->su = NULL;  v->nsu = 0;  v->cur = NULL;
  ar_free(&v->tbl);
  Fi(VB_NSLOT, xfree(v->sl[i].len));
  memset(v->sl, 0, sizeof v->sl);  v->nsl = 0;
  bm_free(&v->arena);  v->ar = NULL;
  xfree(v->mem);  v->mem = NULL;
  xfree(v->mavg);  v->mavg = NULL;
  if (v->dp) bm_free(&v->dpmap);
  v->dp = NULL;
  xfree(v->symbols);  v->symbols = NULL;
  xfree(v->ys);  v->ys = NULL;  xfree(v->cs);  v->cs = NULL;
  v->ysn = v->csn = 0;
  vba_free(v->acm);  v->acm = NULL;
  vbz_free(v->zc);  v->zc = NULL;
}

#define VB_SMALL0 ((uint64_t) 16384)
#define VB_SMALL1 ((uint64_t) 65536)
#define VB_SMALL2 ((uint64_t) 262144)
#define VB_FRAMES0 ((uint64_t) 96 << 10)
#define VB_FRAMES1 ((uint64_t) 1 << 19)
#define VB_FRAMES2 ((uint64_t) 1 << 20)
#define VB_FRAMESN ((uint64_t) 1 << 24)

static void vb_tier(vb_ctx * v, int t, int hist) {
  static const u8 ZS[2][3] = { { 5, 3, 3 }, { 2, 2, 0 } },
                  IS[2][3] = { { 1, 1, 1 }, { 1, 1, 0 } },
                  CS[2][3] = { { 1, 0, 0 }, { 1, 0, 0 } },
                  HS[2][3] = { { 0, 0, 0 }, { 4, 2, 0 } };
  int c = v->cm_mask != 0;
  v->zsh = t >= 0 ? ZS[c][t] : 0;  v->ish = t >= 0 ? IS[c][t] : 0;
  v->csh = t >= 0 ? CS[c][t] : 0;
  if (hist) v->hsh = t >= 0 ? HS[c][t] : 0;
}

static int vb_btier(const vb_ctx * v) {
  return !v->olen || v->olen >= VB_SMALL2 ? -1
       : v->olen < VB_SMALL0 ? 0 : v->olen < VB_SMALL1 ? 1 : 2;
}

static void vb_ftier(vb_ctx * v, u32 rate, i32 nominal, u32 bs1) {
  uint64_t f;
  if (!v->olen || nominal <= 0) return;
  if (v->olen >= VB_FRAMESN) { vb_tier(v, -1, 0);  return; }
  f = (v->olen * 16384 / bs1) * MIN(rate, 1u << 24) / (uint64_t) nominal;
  vb_tier(v, f < VB_FRAMES0 ? 0 : f < VB_FRAMES1 ? 1
           : f < VB_FRAMES2 ? 2 : -1, 0);
}

static const u8 CM_LEVMASK[CM_NLEV] = { 0x00, 0x05, 0x00, 0x1F };

void vb_level(vb_ctx * v, int lev) {
  FATAL_UNLESS(lev >= 0 && lev < CM_NLEV && lev != 2,
               "vorbis: effort %d is out of range", lev);
  FATAL_UNLESS(!v->ar, "vorbis: the arena is already in use");
  rc_adapt_init();
  if (!v->tailc) v->tailc = xcalloc(VB_TBANK * VB_TSIZE, 1);
  v->cm_mask = CM_LEVMASK[lev];
  if (v->t.flags & VB_TF_ZONLY) v->cm_mask &= 1;
  if (v->t.x & VB_TX_ACM ||
      (v->t.x3 & VB_T3_ZC && !(vb_zcopt(&v->t) & VBZ_O_FL))) v->cm_mask = 0;
  vb_tier(v, vb_btier(v), 1);
  if (v->cm_mask) cm_alloc(v);
  if (v->t.x & VB_TX_ACM && !v->acm)
    v->acm = vba_new(v->t.x & VB_TX_ACML ? ACM_VLITE : ACM_VORBISG | ACM_W16,
                     (v->t.x2 & VB_T2_ACMZ) != 0,
                     (v->t.x2 & VB_T2_PT) / VB_T2_PT1);
  if (v->t.x3 & VB_T3_ZC && !v->zc) v->zc = vbz_new(vb_zcopt(&v->t), v->olen);
  gate_init();
}

void vb_use(vb_ctx * v, u32 n) {
  FATAL_UNLESS(n < v->nsu, "vorbis: no setup %lu to reuse", (unsigned long) n);
  v->csu = n;  v->cur = v->su[n];
}

void vb_link(vb_ctx * v) {
  v->pm = v->pw[0] = v->pw[1] = 0;
  v->hu = 0;  v->psl = 0;
  memset(v->fh, 0, 4);  memset(v->sh, 0, 4);  memset(v->mh, 0, 4);
  memset(v->fpl, 0, sizeof v->fpl);
  memset(v->st, 0, sizeof v->st);
  if (v->mem) memset(v->mem, 0, VB_MEMSZ);
  if (v->zc) vbz_link(v->zc);
  if (v->mavg) memset(v->mavg, 0, VB_MEMSZ * sizeof *v->mavg);
}

void vb_endlink(vb_ctx * v) {
  sz i;
  Fi(v->nsl, if (!v->st[i] && v->sl[i].use) v->sl[i].use--);
  memset(v->st, 0, sizeof v->st);
}

static const u8 RST[] = {
  M_DIM, M_ENT, M_ORDF, M_ORDR, M_LEN, M_VBITS, M_MULT,
  M_PCLS, M_MBOOK, M_SBOOK, M_FX,
  M_MULTW
};

#define S_BULK  0
#define S_MODE  1
#define S_TYPE  2

typedef struct {
  int enc;
  int replay;  sz symbol;
  int probe, rawpkt, choices;
  int acmh;
  int quiet;
  int open;
  int zero;
  rc_enc * e[3];
  rc_dec * d[3];
  vb_ctx * v;
  u8 * b;
  sz len, pos;
  const u8 * cmap;
  int rch;
} io;

static int vba_code(void * ctx, int bit, u32 p0) {
  io * z = ctx;
  if (!z->enc) return rc_dec_bit_raw(z->d[S_BULK], p0);
  rc_enc_bit_raw(z->e[S_BULK], p0, bit);
  return bit;
}

static INLINE int cbit(io * z, int s, u16 * p, u8 * c, int b) {
  if (!z->enc) return rc_dec_bit_ad(z->d[s], p, c, RC_ALIM);
  rc_enc_bit_ad(z->e[s], p, c, RC_ALIM, b);  return b;
}

static INLINE uint64_t bwin(const io * z) {
  sz at = z->pos >> 3, i;
  const u8 * p = z->b + at;
  uint64_t w = 0;
  if (at + 8 <= z->len) w = rd64le(p);
  else for (i = 0; at + i < z->len; i++) w |= (uint64_t) p[i] << 8 * i;
  return w >> (z->pos & 7);
}

static u32 bget(io * z, int n) {
  u32 r;
  if (z->probe && z->pos + (sz) n > z->len * 8) {
    z->rawpkt = 1;  return 0;
  }
  FATAL_IF_HOT(z->pos + (sz) n > z->len * 8)
    ("vorbis: packet ends inside a field");
  r = (u32) (bwin(z) & (((uint64_t) 1 << n) - 1));
  z->pos += (sz) n;
  return r;
}

static INLINE void bputn(io * z, u32 n, u32 v) {
  sz at = z->pos >> 3;
  uint64_t w = (uint64_t) v << (z->pos & 7);
  int left = (int) (n + (z->pos & 7));
  FATAL_IF_HOT(z->pos + n > z->len * 8)
    ("vorbis: packet ends inside a codeword");
  if (z->zero && at + 8 <= z->len) wr64le(z->b + at, w | z->b[at]);
  else for (; left > 0; left -= 8) { z->b[at++] |= (u8) w;  w >>= 8; }
  z->pos += n;
}

static void bput(io * z, int n, u32 v) {
  FATAL_IF_HOT(z->pos + (sz) n > z->len * 8)
    ("vorbis: packet ends inside a field");
  bputn(z, (u32) n, (u32) (v & (((uint64_t) 1 << n) - 1)));
}

static INLINE u32 bget1(io * z) {
  u32 r;
  FATAL_IF_HOT(z->pos >= z->len * 8)("vorbis: packet ends inside a codeword");
  r = z->b[z->pos >> 3] >> (z->pos & 7) & 1;
  z->pos++;
  return r;
}

static u32 mv(io * z, int s, int k, u32 v, u32 p) {
  if (z->quiet) return v;
  if (z->acmh) return vba_field(z->v->acm, k, z->enc ? v - p : 0) + p;
  if (z->enc) { mdl_enc(z->v->m + k, z->e[s], v - p);  return v; }
  return mdl_dec(z->v->m + k, z->d[s]) + p;
}

static u32 tv(io * z, int st, u16 * s, u8 * c, int d, u32 v) {
  u32 idx = 1;
  int i;
  FATAL_IF_HOT(z->enc && v >= 1UL << d)("vorbis: %lu does not fit a %d-bit "
                                         "tree", (unsigned long) v, d);
  if (z->quiet) return v;
  if (z->acmh && s >= z->v->f && s < z->v->f + F_NSLOT)
    return vba_tbits(z->v->acm, (int) (s - z->v->f), d, v);
  for (i = d - 1; i >= 0; i--)
    idx = idx * 2 + (u32) cbit(z, st, s + idx, c + idx, (int) (v >> i & 1));
  return idx - (1UL << d);
}

static u32 fld(io * z, int k, int n, u32 p) {
  u32 v = z->enc ? bget(z, n) : 0;
  v = mv(z, S_BULK, k, v, p);
  if (!z->enc) {
#ifndef BLR_NO_FLD
    FATAL_IF_HOT(n < 32 && v >> n)("vorbis: field %d value %lu exceeds %d bits",
                                   k, (unsigned long) v, n);
#endif
    bput(z, n, v);
  }
  return v;
}

static u32 tfld(io * z, int k, int d, int n) {
  u32 v = z->enc ? bget(z, n) : 0;
  v = tv(z, S_BULK, z->v->f + k, z->v->fc + k, d, v);
  if (!z->enc) bput(z, n, v);
  return v;
}

static void cst(io * z, int n, u32 k) {
  if (z->enc) {
    u32 v = bget(z, n);
    FATAL_UNLESS(v == k, "vorbis: fixed field %lu, expected %lu",
                 (unsigned long) v, (unsigned long) k);
  } else bput(z, n, k);
}

static u32 lk1(u32 entries, u32 dim) {
  u32 v = 0, i, p;
  for (;;) {
    p = 1;
    Fi(dim,
      if (p > entries / (v + 1)) { p = entries + 1;  break; }
      p *= v + 1);
    if (p > entries) return v;
    v++;
  }
}

#include "vorbis/setup.c"
#include "vorbis/audio.c"
