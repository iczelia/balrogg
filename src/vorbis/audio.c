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

#define AR_RZCH (2 * 2 * 2 * AR_NBIN)

static const u32 AR_SIZE[A_NTAB] = {
  AR_HIST2,
  VB_MAXFLOOR * AR_HIST2 * VB_MAXPOST,
  VB_MAXFLOOR * AR_PLEN * VB_MAXPOST * 8,
  VB_MAXFLOOR * VB_MAXPOST * AR_TRI * AR_LOW2,
  AR_NRES * AR_NPART * 16,
  AR_NRES * 2 * 2 * 2 * AR_NBIN * AR_NCH,
  AR_NRES * 2 * 2 * 2 * AR_HIST2 * AR_ILOG * AR_NCH,
  AR_NRES * 2 * AR_MCLS * 2 * AR_ILOG * AR_NCH,
  AR_NRES * AR_MCLS * AR_NCH * 8,
  AR_NRES * AR_MAGB * AR_MCLS * AR_NCH * AR_LOW2
};

#define AR_CELLS(i) (AR_SIZE[i] / (AR_NRES * AR_NCH))

static const u8 AR_TRIB[AR_MAGB + 1] = { 0, 0, 1, 3, 6, 10, 15, 21, 28 };

static INLINE u32 atab(const vb_ctx * v, int t, u32 idx) {
  return v->ab[t] + idx;
}

static INLINE int abit(io * z, u32 at, int b) {
  u32 * p = z->v->ar + at;
  if (!z->enc) return rc_dec_bit_packed(z->d[S_BULK], p, VB_FLIM);
  rc_enc_bit_packed(z->e[S_BULK], p, VB_FLIM, b);
  return b;
}

static void arena(vb_ctx * v) {
  u32 i, at = 0;
  if (v->ar) return;
  Fi(A_NGLOB, v->ab[i] = at;  at += AR_SIZE[i]);
  v->aglob = at;
  v->ab[A_RZERO] = 0;  v->astep = AR_SIZE[A_RZERO];
  at = v->aglob + VB_NSLOT * v->astep;
  for (i = A_RZERO + 1; i < A_NTAB; i++) {
    v->ab[i] = at;  at += AR_SIZE[i] * VB_NSLOT;
  }
  v->arn = at;
  bm_alloc(&v->arena, v->arn * sizeof *v->ar);
  v->ar = (u32 *) (void *) v->arena.p;
  memset(v->ai, 0, sizeof v->ai);
  memset(v->ad, 0, sizeof v->ad);
  v->mem = xcalloc(VB_MEMSZ, 1);
  if (v->t.flags & VB_TF_HIST) v->mavg = xcalloc(VB_MEMSZ, sizeof *v->mavg);
}

static void ar_clear(vb_ctx * v, sz at, sz n) {
  memset(v->ar + at, 0, n * sizeof *v->ar);
}

static u32 ar_slot(vb_ctx * v, u32 k) {
  u32 at = v->aglob + k * v->astep, i, j;
  if (!v->ai[k]) {
    if (v->ad[k]) {
      ar_clear(v, at, v->astep);
      for (i = A_RZERO + 1; i < A_NTAB; i++)
        Fj(AR_NRES * AR_NCH,
          ar_clear(v, v->ab[i] + ((sz) j * VB_NSLOT + k) * AR_CELLS(i),
                   AR_CELLS(i)));
    }
    v->ai[k] = 1;  v->ad[k] = 1;
  }
  return at;
}

static u32 * scratch(u32 ** p, sz * have, sz want) {
  return *p = xgrow(*p, have, want, sizeof **p, want);
}

#define BK_FBITS 10
#define BK_NODE 32
static void bk_fast(blr_arena * ar, vb_book * b) {
  u32 i, j, m = 1;
  Fi(b->ent, if (b->len[i] > m) m = b->len[i]);
  b->fbits = MIN(m, BK_FBITS);
  b->fast = ar_take(ar, ((sz) 1 << b->fbits) * sizeof *b->fast);
  Fi((u32) 1 << b->fbits,
    i32 t = 0;
    Fj(b->fbits,
      t = b->nd[2 * (u32) t + (i >> j & 1)];
      if (t <= 0) break);
    b->fast[i] = j == b->fbits ? (u32) t * 2 * BK_NODE | BK_NODE | j
               : t ? (u32) (-t - 1) * 2 * BK_NODE | (j + 1) : 0);
}

static u32 bk_get(io * z, vb_book * b) {
  u32 idx = 0, e;
  i32 t;
  if (z->replay) {
    e = z->v->symbols[z->symbol++];  z->pos += b->len[e];  return e;
  }
  if (!b->fast) bk_fast(&z->v->tbl, b);
  if (z->len * 8 - z->pos >= b->fbits) {
    u32 f = b->fast[bwin(z) & (((u32) 1 << b->fbits) - 1)];
    FATAL_IF_HOT(!f)("vorbis: the packet holds no such codeword");
    z->pos += f & (BK_NODE - 1);
    if (!(f & BK_NODE)) { e = f / (2 * BK_NODE);  goto found; }
    idx = f / (2 * BK_NODE);
  }
  for (;;) {
    if (z->probe && z->pos >= z->len * 8) {
      z->rawpkt = 1;  return 0;
    }
    t = b->nd[2 * idx + bget1(z)];
    FATAL_IF_HOT(t == 0)("vorbis: the packet holds no such codeword");
    if (t < 0) break;
    idx = (u32) t;
  }
  e = (u32) (-t - 1);
found:
  if (z->probe && !z->v->symfull) {
    vb_ctx * v = z->v;
    if (v->nsymbols == v->csymbols) {
      if (v->csymbols == 1UL << 16) { v->symfull = 1;  return e; }
      v->symbols = xgrow(v->symbols, &v->csymbols, v->nsymbols + 1,
                         sizeof *v->symbols, 256);
    }
    v->symbols[v->nsymbols++] = e;
  }
  return e;
}

static void bk_put(io * z, vb_book * b, u32 e) {
  FATAL_IF_HOT(!(e < b->ent && b->len[e]))("vorbis: codebook has no entry %lu",
                                            (unsigned long) e);
  bputn(z, b->len[e], b->rcode[e]);
}

static u32 fl_val(io * z, u32 f, u32 i, u8 * h, u32 v) {
  vb_ctx * n = z->v;
  u32 o, idx, k, a = 0, nb = 0, r;
  u8 * pl = n->fpl + f * VB_MAXPOST + i;
  int b;
#ifdef BLR_PROFILE
  int e_h = *h, e_pl = *pl;
#endif
  o = atab(n, A_FZERO, (f * AR_HIST2 + (u32) *h) * VB_MAXPOST + i);
  PROF(prof_site = P_FZERO);
  b = abit(z, o, v != 0);
  *h = (u8) ((b + *h * 2) & 3);
  if (!b) {
    *pl = 0;  PROF(prof_floor((int) f, (int) prof_ch, e_h, (int) i, e_pl, 0));
    PROF(prof_site = P_VOTHER);  return 0;
  }
  if (z->enc) nb = blr_ilog(v >> 1);
  FATAL_IF_HOT(nb >= AR_MAGB)("vorbis: floor value %lu exceeds 8 bits",
                              (unsigned long) v);
  o = atab(n, A_FLEN, ((f * AR_PLEN + (u32) *pl) * VB_MAXPOST + i) * 8);
  PROF(prof_site = P_FLEN);
  idx = 1;
  for (k = 3; k > 0; k--)
    idx = idx * 2 + (u32) abit(z, o + idx, (int) (nb >> (k - 1) & 1));
  nb = idx - 8;  *pl = (u8) nb;
  o = atab(n, A_FMAG, ((f * VB_MAXPOST + i) * AR_TRI + AR_TRIB[nb]) * AR_LOW2);
  PROF(prof_site = P_FMAG);
  r = 1;
  for (k = nb; k > 0; k--) {
    b = abit(z, o + (nb - k) * AR_LOW2 + a, (int) (v >> (k - 1) & 1));
    r = r * 2 + (u32) b;  a = (a * 2 + (u32) b) & 3;
  }
  PROF(prof_floor((int) f, (int) prof_ch, e_h, (int) i, e_pl, (i32) r));
  PROF(prof_site = P_VOTHER);
  return r;
}

static int fl_get(io * z, vb_floor * f, u32 * y, u32 * classes) {
  vb_setup * s = z->v->cur;
  u32 i, k, post = 2, p, cd, cs, cv;
  if (!bget(z, 1)) return 0;
  p = blr_ilog(f->quant - 1);
  y[0] = bget(z, p);  y[1] = bget(z, p);
  if (z->rawpkt) return 0;
  Fi(f->parts,
    u32 c = f->pcls[i], cv0;
    cd = f->cdim[c];  cs = f->csub[c];  cv = 0;
    if (cs) cv = bk_get(z, s->bk + f->cbook[c]);
    if (z->rawpkt) return 0;
    classes[i] = cv0 = cv;
    Fk(cd,
      i32 b = f->csb[c][cv & ((1UL << cs) - 1)];
      cv >>= cs;
      y[post + k] = b >= 0 ? bk_get(z, s->bk + b) : 0;
      if (z->rawpkt) return 0);
    if (cv0 >> cs * cd) z->choices = 1;
    Fk(cd,
      u32 j, d = cv0 >> k * cs & ((1UL << cs) - 1);
      Fj(d,
        u32 mx = f->csb[c][j] < 0 ? 1 : s->bk[f->csb[c][j]].ent;
        if (y[post + k] < mx) z->choices = 1));
    post += cd);
  return 1;
}

static void fl_put(io * z, vb_floor * f, const u32 * y, int used,
                    const u32 * classes) {
  vb_setup * s = z->v->cur;
  u32 i, k, j, post = 2, p, cd, cs, cv, mx;
  if (!z->enc) bput(z, 1, (u32) used);
  if (!used) return;
  p = blr_ilog(f->quant - 1);
  FATAL_UNLESS(y[0] < 1UL << p && y[1] < 1UL << p,
               "vorbis: floor post does not fit %lu bits", (unsigned long) p);
  if (!z->enc) { bput(z, p, y[0]);  bput(z, p, y[1]); }
  Fi(f->parts,
    u32 c = f->pcls[i];
    cd = f->cdim[c];  cs = f->csub[c];  cv = 0;
    if (cs) {
      Fk(cd,
        Fj(1UL << cs,
          mx = f->csb[c][j] < 0 ? 1 : s->bk[f->csb[c][j]].ent;
          if (y[post + k] < mx) break);
        FATAL_IF_HOT(j >= 1UL << cs)
          ("vorbis: no class %lu book fits value %lu", (unsigned long) c,
           (unsigned long) y[post + k]);
        cv |= j << k * cs);
      if (z->choices)
        cv = mv(z, S_BULK, M_FCLASS, z->enc ? classes[i] : 0, cv);
      if (!z->enc) bk_put(z, s->bk + f->cbook[c], cv);
    }
    Fk(cd,
      i32 b = f->csb[c][cv & ((1UL << cs) - 1)];
      cv >>= cs;
      if (!z->enc && b >= 0) bk_put(z, s->bk + b, y[post + k]));
    post += cd);
}

static int mcls(i32 v) {
  u32 a = (u32) ABS(v);
  return !a ? 0 : a == 1 ? 1 : a <= 3 ? 2 : a <= 7 ? 3 : 4;
}

static const short CM_W0[CM_NST][4] = {
  { 28600, 3700, 3700, 0 }, { 25200, 10000, 0, 0 }, { 28000, 5600, 500, 0 },
  { 30500, 5000, 1400, 800 }, { 31000, 2700, 0, 300 }
};

static void cm_alloc(vb_ctx * v) {
  if (v->cm.live) return;
  cm_new(&v->cm, CM_NST, CM_BITS - v->hsh,
         CM_SEL * (v->t.flags & VB_TF_HIST ? CM_NAVG : 1), v->t.lr);
  if (v->t.flags & VB_TF_GATE) v->gate = xcalloc(VB_GATE_SZ, sizeof *v->gate);
  if (v->t.x & VB_TX_PRIOR) cm_prior(&v->cm, CM_W0, 3 * v->t.lr);
  if (v->t.x & VB_TX_SLOTW) cm_second(&v->cm, (u32) VB_NSLOT * AR_NCH);
  if (v->t.x & VB_TX_DP) {
    bm_alloc(&v->dpmap, VB_DPSZ * sizeof *v->dp);
    v->dp = (u32 *) (void *) v->dpmap.p;  v->cm.dlim = VB_DPLIM;  v->cm.xt = 1;
  }
  if (v->t.x & VB_TX_APM)
    cm_apm(&v->cm, VB_APM0, VB_APM1, (v->t.x & VB_TX_APMS) != 0);
  if (v->t.x & VB_TX_H2) cm_hist2(&v->cm);
}

static void cm_step(vb_ctx * v, u32 ch, u32 c, i32 val) {
  v->nv0[ch] = val;  v->nidx[ch] = c;
  v->nxv = val;  v->npch = (int) ch;  v->nstarted = 1;
}

static u32 rs_cls(io * z, u32 q, u32 ch, u32 p, u32 v) {
  vb_ctx * n = z->v;
  u32 o, idx = 1, k;
  if (p >= AR_NPART) p = AR_NPART - 1;
  p >>= n->csh;
  o = atab(n, A_CLASS, (q * AR_NPART + p) * 16);
  PROF(prof_site = P_CLASS);
  for (k = 4; k > 0; k--)
    idx = idx * 2 + (u32) abit(z, o + idx, (int) (v >> (k - 1) & 1));
  PROF(prof_cls((int) q, (int) ch, (int) p, (i32) (idx - 16)));
  PROF(prof_site = P_VOTHER);
  return idx - 16;
}

static INLINE void rs_pos(u32 g, u32 il, u32 * c, u32 * ch) {
  if (il == 2) { *c = g >> 1;  *ch = g & 1; }
  else if (il > 2) { *c = g / il;  *ch = g % il; }
  else { *c = g;  *ch = 0; }
}

static u32 rs_step(const vb_res * r, const vb_book * b, u32 * n) {
  if (r->type) { *n = (r->psz + b->dim - 1) / b->dim;  return 1; }
  *n = r->psz / b->dim;
  FATAL_IF_HOT(*n * b->dim != r->psz)
    ("vorbis: residue 0 partition %lu not divisible by %lu",
     (unsigned long) r->psz, (unsigned long) b->dim);
  return *n;
}

static void cw_split(const vb_res * r, u32 cw, u32 * cl, u32 pv) {
  for (; pv > 0; pv--) {
    u32 nx = (u32) ((uint64_t) cw * r->cdmul >> r->cdshift);
    cl[pv - 1] = cw - nx * r->ncl;  cw = nx;
  }
  FATAL_IF_HOT(cw)("vorbis: residue classword exceeds its partitions");
}

static void cw_put(io * z, const vb_res * r, vb_book * cb, const u32 * cl,
                   u32 pv) {
  u32 k, cw = 0;
  Fk(pv, cw = cw * r->ncl + cl[k]);
  bk_put(z, cb, cw);
}

typedef struct {
  io * z;
  vb_ctx * n;
  rc_enc * e;
  rc_dec * d;
  u8 * mr, * mw;
  u32 * tab[A_NTAB - A_RZERO];
  u32 cs[A_NTAB - A_RZERO];
  u32 slot, il;
  u32 zs, is;
  vb_gate * gate;
  u32 * dpb;
#ifdef BLR_PROFILE
  u32 q, pass;
#endif
} rs_ctx;

static INLINE void rs_init(rs_ctx * s, io * z, u32 slot, u32 q, u32 pass) {
  vb_ctx * n = z->v;
  u32 i, mb;
  s->z = z;
  if (z->probe) return;
  mb = ar_slot(n, slot);
  s->n = n;  s->e = z->e[S_BULK];  s->d = z->d[S_BULK];
  s->slot = slot;  s->zs = n->zsh;  s->is = n->ish;
  s->gate = n->gate ? n->gate + (sz) slot * VB_GATE_NB : NULL;
  s->dpb = n->dp ? n->dp + (sz) q * VB_DPN * AR_NCH * AR_MAXIDX : NULL;
  s->mr = n->mem + (q * AR_NPASS + (pass ? pass - 1 : 0)) * AR_MAXIDX * AR_NCH;
  s->mw = n->mem + (q * AR_NPASS + pass) * AR_MAXIDX * AR_NCH;
  s->tab[0] = n->ar + mb + q * (AR_SIZE[A_RZERO] / AR_NRES);
  for (i = A_RZERO + 1; i < A_NTAB; i++) {
    s->tab[i - A_RZERO] = n->ar + n->ab[i] +
                          ((sz) q * AR_NCH * VB_NSLOT + slot) * AR_CELLS(i);
    s->cs[i - A_RZERO] = VB_NSLOT * AR_CELLS(i);
  }
#ifdef BLR_PROFILE
  s->q = q;  s->pass = pass;
#endif
}

#if CM_INLINE
#define CM_SIMD 1
#include "coder/cmmix.h"
#endif

static u32 gate_rate[VB_GATE_MAX + 1];

static void gate_init(void) {
  u32 i;
  if (gate_rate[0]) return;
  for (i = 0; i <= VB_GATE_MAX; i++) gate_rate[i] = 131072u / (2 * i + 3);
}

static INLINE int gate_bit(const rs_ctx * s, int st, u32 * p, u32 p0, int bit,
                           int enc) {
  u32 side = p0 < 32768, q = side ? p0 : 65536 - p0, lg = blr_ilog(q), b, p1;
  vb_gate * g;
  b = lg * 2 + (lg >= 2 ? q >> (lg - 2) & 1 : 0);
  g = s->gate + (sz) st * VB_NSLOT * VB_GATE_NB + side * (VB_GATE_NB / 2) + b;
  if (!g->n) g->p = (65536 - p0) << 12;
  p1 = CLAMP(g->p >> 12, 16, 65520);
  if (!enc) bit = rc_dec_bit_raw(s->d, 65536 - p1);
  else rc_enc_bit_raw(s->e, 65536 - p1, bit);
  PROF(prof_hook(NULL, 65536 - p1, bit));
  { i32 dlt = (bit ? (i32) (1 << 28) : 0) - (i32) g->p;
    g->p = (u32) ((i32) g->p + (i32) ((int64_t) dlt * gate_rate[g->n] >> 16)); }
  if (g->n < VB_GATE_MAX) g->n++;
  *p = rc_adapt_packed(*p, RC_ALIM, bit);
  return bit;
}

#define RS_GATED(p0) ((p0) - VB_GATE_T > 65536 - 2 * VB_GATE_T)

#define RS_ENC 1
#define RS_CM 0
#define RS_HIST 0
#define RS_GATE 0
#define RS_DP 0
#define RS_NAME(n) rs_plain_enc_##n
#include "vorbis/res.h"

#define RS_ENC 0
#define RS_CM 0
#define RS_HIST 0
#define RS_GATE 0
#define RS_DP 0
#define RS_NAME(n) rs_plain_dec_##n
#include "vorbis/res.h"

#define RS_ENC -1
#define RS_CM 5
#define RS_HIST -1
#define RS_GATE -1
#define RS_DP -1
#define RS_APM 0
#define RS_NAME(n) rs_zero_one_##n
#include "vorbis/res.h"

#define RS_ENC -1
#define RS_CM 1
#define RS_HIST -1
#define RS_GATE -1
#define RS_DP -1
#define RS_APM 0
#define RS_NAME(n) rs_zero_##n
#include "vorbis/res.h"

#define RS_ENC 0
#define RS_CM 31
#define RS_HIST 1
#define RS_GATE 1
#define RS_DP 0
#define RS_APM 0
#define RS_NAME(n) rs_mixed_dec_hg_##n
#include "vorbis/res.h"

#define RS_ENC 0
#define RS_CM 31
#define RS_HIST 1
#define RS_GATE 1
#define RS_DP 1
#define RS_APM 0
#define RS_NAME(n) rs_mixed_dec_hgd_##n
#include "vorbis/res.h"

#define RS_ENC 0
#define RS_CM 31
#define RS_HIST 1
#define RS_GATE 1
#define RS_DP 1
#define RS_APM 1
#define RS_NAME(n) rs_mixed_dec_hgd_a_##n
#include "vorbis/res.h"

#define RS_ENC 0
#define RS_CM 31
#define RS_HIST 1
#define RS_GATE 1
#define RS_DP 1
#define RS_H2 1
#define RS_APM 0
#define RS_NAME(n) rs_mixed_dec_hgd_h2_##n
#include "vorbis/res.h"

#define RS_ENC 0
#define RS_CM 31
#define RS_HIST 1
#define RS_GATE 1
#define RS_DP 1
#define RS_H2 1
#define RS_APM 1
#define RS_NAME(n) rs_mixed_dec_hgd_h2_a_##n
#include "vorbis/res.h"

#define RS_ENC 1
#define RS_CM 31
#define RS_HIST 1
#define RS_GATE 1
#define RS_DP 0
#define RS_APM 0
#define RS_NAME(n) rs_mixed_enc_hg_##n
#include "vorbis/res.h"

#define RS_ENC 1
#define RS_CM 31
#define RS_HIST 1
#define RS_GATE 1
#define RS_DP 1
#define RS_APM 0
#define RS_NAME(n) rs_mixed_enc_hgd_##n
#include "vorbis/res.h"

#define RS_ENC 1
#define RS_CM 31
#define RS_HIST 1
#define RS_GATE 1
#define RS_DP 1
#define RS_APM 1
#define RS_NAME(n) rs_mixed_enc_hgd_a_##n
#include "vorbis/res.h"

#define RS_ENC 1
#define RS_CM 31
#define RS_HIST 1
#define RS_GATE 1
#define RS_DP 1
#define RS_H2 1
#define RS_APM 0
#define RS_NAME(n) rs_mixed_enc_hgd_h2_##n
#include "vorbis/res.h"

#define RS_ENC 1
#define RS_CM 31
#define RS_HIST 1
#define RS_GATE 1
#define RS_DP 1
#define RS_H2 1
#define RS_APM 1
#define RS_NAME(n) rs_mixed_enc_hgd_h2_a_##n
#include "vorbis/res.h"

typedef void rs_fn(io * z, vb_res * r, vb_book * b, u32 q, u32 pass, u32 g,
                   u32 il);

static rs_fn * const RS_MIXED[10] = {
  rs_mixed_dec_hg_part, rs_mixed_dec_hgd_part, rs_mixed_dec_hgd_a_part,
  rs_mixed_dec_hgd_h2_part, rs_mixed_dec_hgd_h2_a_part,
  rs_mixed_enc_hg_part, rs_mixed_enc_hgd_part, rs_mixed_enc_hgd_a_part,
  rs_mixed_enc_hgd_h2_part, rs_mixed_enc_hgd_h2_a_part
};

static void zf_part(io * z, vb_res * r, vb_book * b, u32 pass, u32 g, u32 il,
                    int jch, const u8 * cmap);

static void rs_part(io * z, vb_res * r, vb_book * b, u32 q, u32 pass, u32 g,
                    u32 il) {
  vb_ctx * v = z->v;
  switch (v->cm_mask) {
  case 0:
    if (v->zc && !z->probe && vbz_fast(v->zc, b)) {
      zf_part(z, r, b, pass, g, il, z->rch, z->cmap);  break;
    }
    if (z->enc) rs_plain_enc_part(z, r, b, q, pass, g, il);
    else rs_plain_dec_part(z, r, b, q, pass, g, il);
    break;
  case 1:
    rs_zero_part(z, r, b, q, pass, g, il);
    break;
  case 5:
    rs_zero_one_part(z, r, b, q, pass, g, il);
    break;
  case 31:
    RS_MIXED[(z->enc ? 5 : 0) +
             (v->dp ? (v->cm.h2on ? 3 : 1) + (v->cm.apm != NULL) : 0)]
      (z, r, b, q, pass, g, il);
    break;
  default: FATAL("vorbis: unsupported residue stage mask");
  }
}

static void residue(io * z, u32 rno, const u8 * nz, u32 nch, u32 n) {
  vb_setup * s = z->v->cur;
  vb_res * r = s->rs + rno;
  vb_book * cb = s->bk + r->cbook;
  u32 q = rno & 3, il = 0, vch, end, np, pv, pc, i, j, k, cw, w;
  u32 * cl;
  u32 nc = 0;
  int jmap[VB_MAXCH];
  Fi(nch, if (nz[i]) jmap[nc++] = z->cmap ? z->cmap[i] : (int) i);
  if (!nc) return;
  if (r->type == 2) { il = nch;  vch = 1;  end = MIN(r->end, n * nch); }
  else { vch = nc;  end = MIN(r->end, n); }
  if (end <= r->beg) return;
  np = (end - r->beg) / r->psz;
  if (!np) return;
  pv = cb->dim;
  w = np + pv;
  cl = scratch(&z->v->cs, &z->v->csn, (sz) vch * w);
  Fi(r->npass,
    pc = 0;
    while (pc < np) {
      if (!i)
        Fj(vch,
          if (z->enc) {
            cw = bk_get(z, cb);
            if (z->rawpkt) return;
            cw_split(r, cw, cl + j * w + pc, pv);
          }
          PROF(prof_ch = j);
          if (!z->probe)
            Fk(pv,
              cl[j * w + pc + k] =
                rs_cls(z, q, j, pc + k, z->enc ? cl[j * w + pc + k] : 0));
          if (!z->enc) cw_put(z, r, cb, cl + j * w + pc, pv));
      Fk(pv,
        if (pc >= np) break;
        Fj(vch,
          i32 bn;
          FATAL_IF_HOT(cl[j * w + pc] >= r->ncl)
            ("vorbis: residue class %lu, limit %lu",
             (unsigned long) cl[j * w + pc], (unsigned long) r->ncl);
          bn = r->book[cl[j * w + pc]][i];
          PROF(prof_rcls = cl[j * w + pc];  prof_rpart = pc);
          z->rch = il ? 0 : jmap[j];
          if (bn >= 0)
            rs_part(z, r, s->bk + bn, q, i, r->beg + pc * r->psz, il);
          if (z->rawpkt) return);
        pc++);
    });
}

void vb_rule_begin(vb_rule * p, vb_res * r, u32 cls, int last, u32 g, u32 il) {
  u32 j;
  p->on = 0;
  if (!last) return;
  p->r = r;  p->cls = cls;  p->n = r->psz;  p->done = 0;  p->g = g;
  p->il = il ? il : 1;
  p->sum = r->type != 2;  p->a = p->b = 0;  p->jm = 0;
  for (j = 0; j < cls && j < 64; j++)
    if (r->th[j][0] >= 0) p->jm |= (uint64_t) 1 << j;
  p->on = 1;
}

void vb_rule_end(vb_rule * p, vb_res * r, u32 cls) {
  int64_t a = p->on ? p->a : 0, b = p->on ? p->b : 0;
  if (r->th[cls][0] < a) r->th[cls][0] = a;
  if (r->th[cls][1] < b) r->th[cls][1] = b;
  p->on = 0;
}

int vb_rule_state(vb_rule * p, int64_t acc, int64_t del, int * fa) {
  const vb_res * r = p->r;
  u32 o = p->done, j, t;
  int same = 0, rem, rb;
  int64_t need = -1, x = ABS(acc), q;
  *fa = 1;
  if (!p->on || o >= p->n) return 0;
  *fa = p->sum || (p->g + o) % p->il == 0;
  for (j = 0; p->jm >> j; j++)
    if (p->jm >> j & 1) {
      int64_t nj;
      if (p->a > r->th[j][0] || p->b > r->th[j][1]) {
        p->jm &= ~((uint64_t) 1 << j);  continue;
      }
      nj = p->sum ? MIN(r->th[j][0], r->th[j][1] - p->b)
         : *fa ? r->th[j][0] : r->th[j][1];
      if (nj > need) need = nj;
    }
  if (!p->jm) return 1;
  rem = (int) (p->n - 1 - o);
  if (p->sum) same = rem > 0;
  else
    for (t = o + 1; t < p->n && t <= o + p->il; t++)
      if (((p->g + t) % p->il == 0) == *fa) { same = 1;  break; }
  rb = rem < 3 ? rem : rem < 5 ? 3 : rem < 9 ? 4 : 5;
  q = need + 1 - x;
  q = q <= 0 ? 0 : (q + del - 1) / del;
  return 2 + (rb * 2 + same) * 5 + (int) MIN(q, 4);
}

void vb_rule_put(vb_rule * p, int64_t ax, int fa) {
  if (p->done < p->n) {
    if (fa && ax > p->a) p->a = ax;
    if (p->sum) p->b += ax;
    else if (!fa && ax > p->b) p->b = ax;
  }
  p->done++;
}

static void mx_floor_put(io * z, vb_floor * f, const u32 * y, int used,
                         const u32 * classes) {
  vb_setup * s = z->v->cur;
  u32 i, k, post = 2, p;
  bput(z, 1, (u32) used);
  if (!used) return;
  p = blr_ilog(f->quant - 1);
  bput(z, (int) p, y[0]);  bput(z, (int) p, y[1]);
  Fi(f->parts,
    u32 c = f->pcls[i], cd = f->cdim[c], cs = f->csub[c], cv = classes[i];
    if (cs) bk_put(z, s->bk + f->cbook[c], cv);
    Fk(cd,
      i32 b = f->csb[c][cv & ((1UL << cs) - 1)];
      cv >>= cs;
      if (b >= 0) bk_put(z, s->bk + b, y[post + k]));
    post += cd);
}

static void mx_part(io * z, vb_res * r, vb_book * b, u32 pass, u32 g, u32 il,
                    int jch, int cls, int nbc, const u8 * cmap) {
  u32 i, st, n, nnz = 0;
  st = rs_step(r, b, &n);
  Fi(n,
    u32 gg = r->type == 0 ? g + i : g + i * b->dim, k, e = 0, c, ch, pre = 0,
        pw = 1;
    if (z->enc) e = bk_get(z, b);
    rs_pos(gg, il, &c, &ch);
    Fk(b->dim,
      u32 x = vba_digit(z->v->acm, b, k, pre, z->enc ? e / pw % b->nv : 0,
                        z->enc, pass, cls, il ? cmap[ch] : jch, c,
                        (gg - g) / (r->psz / 4 + 1), &nnz, nbc, &z->v->rp);
      pre += x * pw;  pw *= b->nv;
      if (il) { ch += st;  while (ch >= il) { ch -= il;  c++; } }
      else c += st);
    if (!z->enc) bk_put(z, b, pre));
}

static void zf_part(io * z, vb_res * r, vb_book * b, u32 pass, u32 g, u32 il,
                    int jch, const u8 * cmap) {
  u32 i, st, n;
  st = rs_step(r, b, &n);
  Fi(n,
    u32 gg = r->type == 0 ? g + i : g + i * b->dim, e = 0, c, ch;
    if (z->enc) e = bk_get(z, b);
    rs_pos(gg, il, &c, &ch);
    e = vbz_entry(z->v->zc, b, e, z->enc, pass, c, ch, st, il, jch, cmap,
                  i == 0);
    if (!z->enc) bk_put(z, b, e));
}

static void zc_part(io * z, vb_res * r, vb_book * b, u32 q, u32 pass, u32 g,
                    u32 il, int jch, const u8 * cmap) {
  u32 i, st, n;
  if (vbz_fl(z->v->zc)) {
    z->rch = jch;  z->cmap = cmap;  rs_part(z, r, b, q, pass, g, il);
    z->cmap = NULL;  return;
  }
  st = rs_step(r, b, &n);
  Fi(n,
    u32 gg = r->type == 0 ? g + i : g + i * b->dim, k, e = 0, c, ch, pre = 0,
        pw = 1;
    if (z->enc) e = bk_get(z, b);
    rs_pos(gg, il, &c, &ch);
    Fk(b->dim,
      u32 next = (u32) ((uint64_t) e * b->divmul >> b->divshift);
      u32 x = vbz_digit(z->v->zc, b, k, pre, e - next * b->nv, z->enc, pass,
                        il ? cmap[ch] : jch, c);
      e = next;  pre += x * pw;  pw *= b->nv;
      if (il) { ch += st;  while (ch >= il) { ch -= il;  c++; } }
      else c += st);
    if (!z->enc) bk_put(z, b, pre));
}

static void mx_residue(io * z, u32 rno, const u8 * nz, u32 nch, u32 n,
                       const u8 * cmap) {
  vb_setup * s = z->v->cur;
  vb_res * r = s->rs + rno;
  vb_book * cb = s->bk + r->cbook;
  u32 il = 0, vch, end, np, pv, pc, i, j, k, w, nc = 0, known = 0;
  u32 * cl;
  int jmap[VB_MAXCH], lp[VB_MAXRCL], rule = z->v->rule;
  Fi(nch, if (nz[i]) jmap[nc++] = cmap[i]);
  if (!nc) return;
  if (r->type == 2) { il = nch;  vch = 1;  end = MIN(r->end, n * nch); }
  else { vch = nc;  end = MIN(r->end, n); }
  if (end <= r->beg) return;
  np = (end - r->beg) / r->psz;
  if (!np) return;
  pv = cb->dim;
  w = np + pv;
  cl = scratch(&z->v->cs, &z->v->csn, (sz) vch * w);
  Fi(r->ncl, lp[i] = -1;  Fj(8, if (r->book[i][j] >= 0) lp[i] = (int) j));
  Fi(r->npass,
    pc = 0;
    while (pc < np) {
      if (!i)
        Fj(vch,
          u32 pre = 0;
          if (z->enc) cw_split(r, bk_get(z, cb), cl + j * w + pc, pv);
          Fk(pv,
            u32 x = z->v->zc
              ? vbz_class(z->v->zc, r, cb, rno, j, pc + k, k, pre,
                          z->enc ? cl[j * w + pc + k] : 0, il,
                          il ? 0 : jmap[j], cmap)
              : vba_class(z->v->acm, r, cb, rno, j, pc + k, k, pre,
                          z->enc ? cl[j * w + pc + k] : 0, il,
                          il ? 0 : jmap[j], cmap);
            cl[j * w + pc + k] = x;  pre = pre * r->ncl + x);
          if (!z->enc) cw_put(z, r, cb, cl + j * w + pc, pv);
          known = pc + pv);
      Fk(pv,
        if (pc >= np) break;
        Fj(vch,
          i32 bn;
          FATAL_IF_HOT(cl[j * w + pc] >= r->ncl)
            ("vorbis: residue class %lu, limit %lu",
             (unsigned long) cl[j * w + pc], (unsigned long) r->ncl);
          bn = r->book[cl[j * w + pc]][i];
          if (bn >= 0) {
            u32 c = cl[j * w + pc];
            int nbc = (pc > 0 ? (int) cl[j * w + pc - 1] : 16) * 17 +
                      (pc + 1 < (i ? np : known) ? (int) cl[j * w + pc + 1]
                                                 : 16);
            if (rule)
              vb_rule_begin(&z->v->rp, r, c, lp[c] == (int) i,
                            r->beg + pc * r->psz, il);
            if (z->v->zc)
              zc_part(z, r, s->bk + bn, rno & 3, i, r->beg + pc * r->psz, il,
                      il ? 0 : jmap[j], cmap);
            else
              mx_part(z, r, s->bk + bn, i, r->beg + pc * r->psz, il,
                      il ? 0 : jmap[j], (int) c, nbc, cmap);
            if (rule && lp[c] == (int) i) vb_rule_end(&z->v->rp, r, c);
          } else if (rule && !i && lp[cl[j * w + pc]] < 0)
            vb_rule_end(&z->v->rp, r, cl[j * w + pc]));
        pc++);
    });
}

static void mx_payload(io * z, u32 mode) {
  vb_ctx * v = z->v;
  vb_setup * s = v->cur;
  vb_map * mp = s->mp + s->mdmap[mode];
  u32 n = vb_half(v, mode);
  u32 ch = v->i.ch, k, i, j, m;
  u8 nz[VB_MAXCH], sub[VB_MAXCH], cmap[VB_MAXCH];
  u32 * y = scratch(&v->ys, &v->ysn, (sz) ch * VB_MAXPOST);
  memset(nz, 0, sizeof nz);
  if (v->zc) Fi(mp->nstep, vbz_couple(v->zc, mp->mag[i], mp->ang[i]));
  Fk(ch,
    u32 fno = mp->fl[mp->mux[k]];
    vb_floor * f = s->fl + fno;
    u32 * yc = y + k * VB_MAXPOST, classes[VB_MAXPART];
    int u = 0;
    if (z->enc) u = fl_get(z, f, yc, classes);
    u = v->zc ? vbz_floor(v->zc, s, k, fno, f, yc, classes, u, z->choices)
              : vba_floor(v->acm, s, k, fno, f, yc, classes, u);
    nz[k] = (u8) u;
    if (!z->enc) mx_floor_put(z, f, yc, u, classes));
  Fi(mp->nstep,
    if (nz[mp->mag[i]] || nz[mp->ang[i]]) nz[mp->mag[i]] = nz[mp->ang[i]] = 1);
  if (v->zc) vbz_ready(v->zc);
  else {
    Fi(mp->nstep, vba_couple(v->acm, mp->mag[i], mp->ang[i]));
    vba_ready(v->acm);
  }
  Fi(mp->sub,
    m = 0;
    Fj(ch, if (mp->mux[j] == i) { cmap[m] = (u8) j;  sub[m++] = nz[j]; });
    if (m) mx_residue(z, mp->rs[i], sub, m, n, cmap));
}

static void payload(io * z, u32 mode) {
  vb_ctx * v = z->v;
  vb_setup * s = v->cur;
  vb_map * mp = s->mp + s->mdmap[mode];
  u32 n = vb_half(v, mode);
  u32 ch = v->i.ch, k, i, j, m;
  u8 nz[VB_MAXCH], sub[VB_MAXCH], cmap[VB_MAXCH];
  u32 * y = scratch(&v->ys, &v->ysn, (sz) ch * VB_MAXPOST);

  memset(nz, 0, sizeof nz);

  Fk(ch,
    u32 fno = mp->fl[mp->mux[k]];
    vb_floor * f = s->fl + fno;
    u32 * yc = y + k * VB_MAXPOST, classes[VB_MAXPART];
    u8 h = 0;
    int u = 0;
    PROF(prof_ch = k);
    if (z->enc) u = fl_get(z, f, yc, classes);
    if (z->rawpkt) return;
    if (z->probe) { nz[k] = (u8) u;  continue; }
    PROF(prof_site = P_USED);
    u = abit(z, atab(v, A_USED, v->hu), u);
    PROF(prof_site = P_VOTHER);
    v->hu = (u8) ((u + v->hu * 2) & 3);
    nz[k] = (u8) u;
    if (u)
      Fi(f->posts, u32 p = f->srt[i];
                     yc[p] = fl_val(z, fno, p, &h, z->enc ? yc[p] : 0));
    if (!z->enc || z->choices) fl_put(z, f, yc, u, classes));
  Fi(mp->nstep, if (nz[mp->mag[i]] || nz[mp->ang[i]])
                  nz[mp->mag[i]] = nz[mp->ang[i]] = 1);
  Fi(mp->sub,
    m = 0;
    Fj(ch, if (mp->mux[j] == i) { cmap[m] = (u8) j;  sub[m++] = nz[j]; });
    z->cmap = cmap;
    if (m) residue(z, mp->rs[i], sub, m, n);
    z->cmap = NULL;
    if (z->rawpkt) return);
}

static int audio_type(io * z) {
  io p = *z;
  u32 md;
  p.probe = 1;  z->v->nsymbols = 0;  z->v->symfull = 0;
  FATAL_UNLESS(z->v->cur && z->v->cur->nmd, "vorbis: audio before setup");
  FATAL_UNLESS(bget(&p, 1) == 0, "vorbis: header on audio path");
  md = bget(&p, (int) blr_ilog(z->v->cur->nmd - 1));
  FATAL_UNLESS(md < z->v->cur->nmd, "vorbis: invalid audio mode");
  if (z->v->cur->blockflag[md]) bget(&p, 2);
  if (p.rawpkt) return 1;
  payload(&p, md);
  if (p.rawpkt) return 1;
  return (p.choices ? 2 : 0) |
    (p.len * 8 - p.pos >= 8 || bget(&p, (int) (p.len * 8 - p.pos)) ? 4 : 0);
}

static void open_len(io * z, sz at) {
  if (z->enc) {
    mdl_enc(&z->v->apl, z->e[S_TYPE], (u32) (z->len - at));  return;
  }
  { sz n = at + mdl_dec(&z->v->apl, z->d[S_TYPE]);
    FATAL_UNLESS(n <= z->len,
                 "vorbis: an audio packet of %lu bytes, room for %lu",
                 (unsigned long) n, (unsigned long) z->len);
    z->len = n; }
}

static sz packet_tail(io * z) {
  u32 prev = 0;
  while (z->pos < z->len * 8) {
    int n = (int) MIN((sz) 8, z->len * 8 - z->pos), k;
    u32 val = z->enc ? bget(z, n) : 0, idx = 1;
    u32 bank = MIN(prev >> 4, VB_TBANK - 1) * VB_TSIZE;
    for (k = n - 1; k >= 0; k--)
      idx = idx * 2 + (u32) cbit(z, S_BULK, z->v->tailp + bank + idx,
                                 z->v->tailc + bank + idx, val >> k & 1);
    prev = idx - (1U << n);
    if (!z->enc) bput(z, n, prev);
  }
  return z->pos;
}

static sz audio(io * z, int cont) {
  vb_ctx * v = z->v;
  u32 md, bank, type;
  int d, w[2], i;

  FATAL_UNLESS(v->cur && v->cur->nmd > 0, "vorbis: audio before setup");
  d = (int) blr_ilog(v->cur->nmd - 1);
  PROF(prof_pkt++);
  type = z->enc ? (u32) audio_type(z) : mdl_dec(&v->apt, z->d[S_TYPE]);
  FATAL_UNLESS(type <= 6 && type != 3 && type != 5,
               "vorbis: invalid audio packet type");
  if (z->enc) mdl_enc(&v->apt, z->e[S_TYPE], type);
  if (type == 1) {
    sz bits;
    if (z->open) open_len(z, 0);
    bits = packet_tail(z);
    FATAL_UNLESS(z->len && !(z->b[0] & 1),
                 "vorbis: invalid peeled audio packet");
    return bits;
  }
  z->choices = (type & 2) != 0;
  if (z->enc) {
    z->replay = !v->symfull;
    FATAL_UNLESS(bget(z, 1) == 0, "vorbis: header on audio path");
  } else bput(z, 1, 0);
  arena(v);
  cm_bind(&v->cm, z->e[S_BULK], z->d[S_BULK]);

  if (v->acm) {
    vba_bind(v->acm, vba_code, z);
    md = z->enc ? bget(z, d) : 0;
    md = vba_mode(v->acm, md, v->cur->nmd);
    if (!z->enc) bput(z, d, md);
    FATAL_UNLESS(md < v->cur->nmd, "vorbis: mode %lu of %lu",
                 (unsigned long) md, (unsigned long) v->cur->nmd);
    vba_frame(v->acm, v->cur->blockflag[md] != 0,
              vb_half(v, md), v->i.ch, md);
    w[0] = w[1] = 0;
    if (v->cur->blockflag[md])
      Fi(2,
        w[i] = vba_window(v->acm, z->enc ? (int) bget(z, 1) : 0, i, md, w[0]);
        if (!z->enc) bput(z, 1, (u32) w[i]));
    mx_payload(z, md);
    vba_frame_end(v->acm);
  } else {
    bank = (u32) (cont & 1) + (u32) (v->pm & 1) * 2 + (u32) v->pw[1] * 4;
    md = z->enc ? bget(z, d) : 0;
    md = tv(z, S_MODE, v->am + bank * VB_MSTEP,
            v->amc + bank * VB_MSTEP, d, md);
    if (!z->enc) bput(z, d, md);
    FATAL_UNLESS(md < v->cur->nmd, "vorbis: mode %lu of %lu",
                 (unsigned long) md, (unsigned long) v->cur->nmd);

    w[0] = w[1] = 0;
    if (v->cur->blockflag[md])
      Fi(2,
        w[i] = cbit(z, S_MODE, v->aw[i] + v->pw[!i],
                    v->awc[i] + v->pw[!i],
                    z->enc ? (int) bget(z, 1) : 0);
        if (!z->enc) bput(z, 1, (u32) w[i]));
    v->pm = (u8) md;  v->pw[0] = (u8) w[0];  v->pw[1] = (u8) w[1];
    if (v->zc) {
      vbz_bind(v->zc, z->enc ? z->e[S_BULK] : NULL,
               z->enc ? NULL : z->d[S_BULK]);
      vbz_frame(v->zc, v->cur->blockflag[md] != 0,
                vb_half(v, md), v->i.ch);
      if (vbz_reg(v->zc)) payload(z, md);
      else mx_payload(z, md);
      vbz_frame_end(v->zc);
    } else payload(z, md);
  }
  FATAL_IF_HOT(z->replay && z->symbol != v->nsymbols)
    ("internal: Vorbis symbol replay mismatch");
  if (type & 4) {
    if (z->open) open_len(z, (z->pos + 7) / 8);
    return packet_tail(z);
  }
  if (z->open && !z->enc) z->len = (z->pos + 7) / 8;
  FATAL_UNLESS(z->len * 8 - z->pos < 8, "vorbis: %lu unparsed audio bits",
               (unsigned long) (z->len * 8 - z->pos));
  { sz used = z->pos;
    if (z->enc)
      FATAL_UNLESS(bget(z, (int) (z->len * 8 - z->pos)) == 0,
                   "vorbis: nonzero audio padding");
    return used; }
}

sz vb_aud_enc(vb_ctx * v, rc_enc * eb, rc_enc * em, rc_enc * ep,
              const u8 * pkt, sz len, int cont, int open) {
  io z;
  ioinit(&z, v, 1, (u8 *) pkt, len);  z.open = open;
  z.e[S_BULK] = eb;  z.e[S_MODE] = em;  z.e[S_TYPE] = ep;
  return audio(&z, cont);
}

sz vb_aud_dec(vb_ctx * v, rc_dec * db, rc_dec * dm, rc_dec * dp,
              u8 * pkt, sz len, int cont, sz * open) {
  io z;
  sz r;
  ioinit(&z, v, 0, pkt, len);  z.open = open != NULL;  z.zero = 1;
  z.d[S_BULK] = db;  z.d[S_MODE] = dm;  z.d[S_TYPE] = dp;
  r = audio(&z, cont);
  if (open) *open = z.len;
  return r;
}
