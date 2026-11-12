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

static mctx * icdf_ctx(const oprec * op) {
  int stable = op->site != OREC_S_SILK_SIGN, wide = op->kind == OP_ICDF16;
  okey id = stable ? tab_id(op->pdf, wide, op->ftb)
                   : tab_hash(op->pdf, wide, op->ftb);
  prior * pr = prior_icdf(id, op->pdf, wide, op->ftb);
  okey key = id ^ (okey) op->site << 40 ^ (okey) site_hist[op->site] << 3;
  return ctx_find(key, pr);
}

static INLINE int unzig(int v) { return (v >> 1) ^ -(v & 1); }

static void icdf_note(const oprec * op, int b, int c, int v) {
  if (op->site == OREC_S_CELT_COARSE_SMALL && OM_TRACK)
    en_coarse(b, c, unzig(v));
  site_hist[op->site] = (u8) MIN(7, v);
}

static int dn_ctx(void) {
  int b = CLAMP(orec_band, 0, 20), e;
  if (orec_cframe != dn_frame) {
    dn_frame = orec_cframe;  dn_band = -1;
    dn_short = orec_flen < OPA_DYNMIN(orec_LM & 3);
    if (!dn_short)
      opa_dynest(en_E[0], en_E[1], orec_C == 2 ? 2 : 1, orec_bstart, orec_bend,
                 orec_LM & 3, dn_est);
  }
  if (b != dn_band) { dn_band = b;  dn_k = 0; }
  if (dn_short) return 0;
  e = dn_est[b] - 2 * dn_k;
  return 3 + CLAMP(e, -2, 6) + 9 * (orec_trans & 1);
}

static okey logp_key(const oprec * op, int b) {
  okey key = (okey) op->site << 20 ^ (okey) op->ftb << 4
           ^ (okey) site_hist[op->site];
  if (om_feat & OF_SIDE) {
    if (op->site == OREC_S_CELT_TF)
      key ^= (okey) 0x7101 << 32 ^ (okey) b << 48 ^ (okey) pf_tf[b] << 56;
    else if (op->site == OREC_S_CELT_DYNALLOC)
      key ^= (okey) 0x7102 << 32 ^ (okey) pf_dyn[b] << 56 ^
             (okey) fr_dyn[b] << 60;
  }
  if (OM_DYN && op->site == OREC_S_CELT_DYNALLOC) key ^= (okey) dn_ctx() << 8;
  return key;
}

static u16 logp_init(const oprec * op) {
  unsigned logp = op->ftb;
  return (u16) (65535U - (65536U >> MIN(15, logp)));
}

static void logp_note(const oprec * op, int b, int c, int v) {
  if (op->site == OREC_S_CELT_COARSE_BIT && OM_TRACK) en_coarse(b, c, -v);
  if (om_feat & OF_SIDE) {
    if (op->site == OREC_S_CELT_TF) fr_tf[b] = (u8) v;
    else if (op->site == OREC_S_CELT_DYNALLOC && v && fr_dyn[b] < 3)
      fr_dyn[b]++;
  }
  if (OM_DYN && op->site == OREC_S_CELT_DYNALLOC) dn_k += v;
  site_hist[op->site] = (u8) ((site_hist[op->site] << 1 | v) & 3);
}

static int lap_dq(int b, int c) {
  i32 d = MAX(-9 * 256, en_E[c][b]) - en_pred(b, c);
  return d < -384 ? 0 : d >= 384 ? 4 : (int) ((d + 640) / 256);
}

static okey lap_key(int b, int c, int lm0, int * q5) {
  int n5 = q5of(band_qi[c]), x5 = c && c0_qib == b ? 1 + q5of(c0_qi) : 0;
  *q5 = om_feat & OF_COARSE && !lm0 ? lap_dq(b, c) : q5of(prev_qi[b][c]);
  return (okey) b << 16 | (okey) c << 15 | (okey) (orec_intra & 1) << 14 |
         (okey) ((om_feat & OF_COARSE ? orec_LM : orec_bLM) & 3) << 12 |
         (okey) *q5 << 9 | (okey) n5 << 6 | (okey) x5;
}

static void lap_note(int b, int c, int v) {
  PROF(prof_coarse(b, c, orec_intra & 1, orec_LM & 3,
                   (i32) prev_qi[b][c], (i32) band_qi[c], (i32) v));
  prev_qi[b][c] = v;  band_qi[c] = v;  fr_qi[b][c] = v;
  if (OM_TRACK) en_coarse(b, c, v);
  if (!c) { c0_qi = v;  c0_qib = b; }
}

static mctx * uint_ctx(const oprec * op) {
  prior * pr = prior_uniform((int) op->nsym, KP[OP_UINT].scale);
  return ctx_find((okey) 0x60000000UL << 32 | (okey) op->site << 20 |
                  (okey) op->nsym, pr);
}

static mctx * bits_ctx(int b, int c, int nb, int g1) {
  prior * pr = prior_uniform(1 << nb, KP[OP_BITS].scale);
  okey key = (okey) 0x70000000UL << 32 | (okey) b << 16 | (okey) c << 15
           | (okey) nb << 11 | (okey) (prev_fine[b][c] & 0x7FF);
  if (g1 && c && c0_fineb == b && c0_finen == nb)
    key |= (1 + ((okey) ((u32) c0_fine * 5) >> nb)) << 24;
  return ctx_find(key, pr);
}

static void bits_note(const oprec * op, int b, int c, int nb, int g1, int v) {
  if (!g1) return;
  PROF(prof_fine(b, c, nb, prev_fine[b][c], (i32) v));
  prev_fine[b][c] = v;
  if (!c) { c0_fine = v;  c0_fineb = b;  c0_finen = nb; }
  if (OM_TRACK) en_bits(op->site, b, c, nb, v);
}

static u32 ap_cs[1026], ap_ks[PVQ_UK + 3];
static blx ap_bx;

static void ap_fill(opa_pri * ap, const mctx * t) {
  ap->kind = OPA_CUM;  ap->cum = ap_cs;  ap->n = (int) t->n;
  cum_of(ap_cs, ctx_fq(t), (int) t->n);
}

static const u32 * ap_split(int dep, int off, int N, int K, int par) {
  bl_cum(dep, N, K, pvq_off + off, N / 2, par, ap_ks, &ap_bx);
  return ap_ks;
}

static void ap_splitk(int k) { bl_learn(&ap_bx, k); }

static void ap_op(oprec * op) {
  int b = clampb(orec_band), c = orec_ch & 1, v, inc = 0, cap = 0;
  int nb = (int) op->ftb, q5;
  int N = orec_pvqN, K = orec_pvqK;
  int pvq = 0;
  mctx * t = NULL;
  u16 * pp = NULL;
  thx th;
  opa_pri ap;
  memset(&ap, 0, sizeof ap);
  fr_sync();
  switch (op->kind) {
  case OP_ICDF:
  case OP_ICDF16:
    t = icdf_ctx(op);  inc = KP[op->kind].inc;  cap = KP[op->kind].cap;
    break;
  case OP_LOGP:
    pp = bin_slot(logp_key(op, b), logp_init(op));
    ap.kind = OPA_BIT;  ap.p1 = 65536 - (u32) *pp;
    break;
  case OP_LAPLACE: {
    prior * pr = prior_laplace(op->aux, (int) op->ftb);
    t = ctx_find(lap_key(b, c, 0, &q5), pr);
    inc = KP[OP_LAPLACE].inc;  cap = KP[OP_LAPLACE].cap;
    break;
  }
  case OP_THETA:
    th_open(op, b, &th);  t = th_ctx(&th, b);
    inc = th_inc;  cap = KP[OP_THETA].cap;
    break;
  case OP_UINT:
    if (op->site == OREC_S_CELT_PVQ) {
      pvq_open(b, orec_pvqrole & 3, ps_at(N), N, K);
      ap.split = ap_split;  ap.splitk = ap_splitk;  pvq = 1;
    } else if (op->nsym <= 1024) {
      t = uint_ctx(op);  inc = KP[OP_UINT].inc;  cap = KP[OP_UINT].cap;
    }
    break;
  case OP_BITS:
    if (op->site == OREC_S_CELT_FINE && nb > 0 && nb <= 8)
      t = fine_ctx(b, c, nb);
    else if (op->site == OREC_S_CELT_FINALISE && nb == 1) t = final_ctx(b, c);
    else if (nb > 0 && nb <= 8 && orec_ftb == nb) t = bits_ctx(b, c, nb, 0);
    inc = KP[OP_BITS].inc;  cap = KP[OP_BITS].cap;
    break;
  default: break;
  }
  if (t) ap_fill(&ap, t);
  opa_op(om_acm, op, om_mode == OM_DEC, &ap);
  v = op->v;
  if (t) {
    int s = op->kind == OP_LAPLACE
          ? (v + LAPW < 0 || v + LAPW >= LAPN - 1 ? LAPN - 1 : v + LAPW) : v;
    ctx_update(t, CLAMP(s, 0, (int) t->n - 1), inc, cap);
  }
  switch (op->kind) {
  case OP_ICDF:
  case OP_ICDF16: icdf_note(op, b, c, v);  break;
  case OP_LOGP:
    bin_learn(pp, v);  logp_note(op, b, c, v);
    break;
  case OP_LAPLACE: lap_note(b, c, v);  break;
  case OP_THETA: th_close(&th, b, v);  break;
  case OP_UINT:
    if (pvq) pvq_note(b, N, K, (u32) v);
    break;
  case OP_BITS:
    if (op->site == OREC_S_CELT_FINE && nb > 0 && nb <= 8)
      fine_note(b, c, nb, v);
    else if (op->site == OREC_S_CELT_FINALISE && nb == 1) final_note(b, c, v);
    break;
  default: break;
  }
}
