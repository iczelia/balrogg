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

#define OM_B clampb(om_feat & OF_SIDE ? orec_band : orec_band1)
#define OM_LM0 ((om_feat & OF_LM0) && !(orec_LM & 3))

static INLINE int om_enter(oprec * op) {
  if (om_acm) {
    if (om_feat & (OF_ACMX | OF_ACMQ)) ap_op(op);
    else opa_op(om_acm, op, om_mode == OM_DEC, NULL);
    return 0;
  }
  if (om_feat & (OF_COARSE | OF_FINE | OF_SIDE)) fr_sync();
#ifdef BLR_PROFILE
  om_comp = op->kind == OP_LAPLACE ? P_COARSE
          : op->kind == OP_THETA   ? P_THETA
          : op->kind == OP_BITS    ? P_FINE
          : op->kind == OP_LOGP    ? P_OLOGP
          : op->kind == OP_UINT    ? P_OUINT : P_OICDF;
  if (op->kind == OP_UINT && orec_pvqK > 0 && op->nsym > 16) om_comp = P_PVQ;
#endif
  return 1;
}

#ifdef BLR_PROFILE
#define OM_LEAVE() (om_comp = P_OOTHER)
#else
#define OM_LEAVE() ((void) 0)
#endif

opus_int32 om_icdf(oprec * op) {
  int b = OM_B, c = orec_ch & 1, v;
  if (!om_enter(op)) return op->v;
  if (oz_on && (!oz_part || op->site == OREC_S_CELT_COARSE_SMALL)) {
    oz_op(op);
    if (oz_part) site_hist[op->site] = (u8) MIN(7, op->v);
    OM_LEAVE();  return op->v;
  }
  v = op->v;
  ctx_code(icdf_ctx(op), KP[op->kind].inc, KP[op->kind].cap, &v);
  op->v = v;
  icdf_note(op, b, c, v);
  OM_LEAVE();
  return op->v;
}

opus_int32 om_logp(oprec * op) {
  int b = OM_B, c = orec_ch & 1, v;
  okey key;
  u16 init;
  if (!om_enter(op)) return op->v;
  if (oz_on && (!oz_part || op->site == OREC_S_CELT_COARSE_BIT)) {
    oz_op(op);
    if (oz_part)
      site_hist[op->site] = (u8) ((site_hist[op->site] << 1 | op->v) & 3);
    OM_LEAVE();  return op->v;
  }
  key = logp_key(op, b);  init = logp_init(op);  v = op->v;
  if (om_side && om_feat & OF_SLOGP) {
    u16 * pp = bin_slot(key, init);
    u32 * sl[3];
    int st[3], fresh;
    sl[0] = NULL;
    sl[1] = sc_line(key ^ (okey) 0x75 << 56 ^ (okey) (orec_LM & 3) << 44 ^
                    (okey) (orec_trans & 1) << 46, &fresh) + 1;
    sl[2] = sc_line(key ^ (okey) 0x76 << 56 ^ (okey) b << 40 ^ (okey) c << 48,
                    &fresh) + 1;
    st[0] = sc_st(65536 - (u32) *pp);
    st[1] = sc_st(*sl[1] >> 16);  st[2] = sc_st(*sl[2] >> 16);
    v = sc_mix(st, 3, sl, 32 + (op->site & 15), om_mode == OM_DEC ? 0 : v);
    bin_learn(pp, v);
  } else om_bit(key, init, &v);
  op->v = v;
  logp_note(op, b, c, v);
  OM_LEAVE();
  return op->v;
}

opus_int32 om_laplace(oprec * op) {
  int b = OM_B, c = orec_ch & 1, lm0 = OM_LM0, q5, v, sym;
  prior * pr;
  okey key;
  if (!om_enter(op)) return op->v;
  if (oz_on) {
    oz_op(op);
    if (oz_part) fr_qi[b][c] = op->v;
    OM_LEAVE();  return op->v;
  }
  pr = prior_laplace(op->aux, (int) op->ftb);
  key = lap_key(b, c, lm0, &q5);
  v = op->v;  sym = v + LAPW;
  if (sym < 0 || sym >= LAPN - 1) sym = LAPN - 1;
  if (om_side && om_feat & OF_SCOARSE) {
    int lm = orec_LM & 3, it = orec_intra & 1;
    int pp = CLAMP(prev_qi[b][c] + LAPW, 0, LAPN - 2);
    int pb = CLAMP(band_qi[c] + LAPW, 0, LAPN - 2);
    int px = c && c0_qib == b ? CLAMP(c0_qi + LAPW, 0, LAPN - 2) : -1;
    okey g = (okey) 0x73 << 56 ^ (okey) it << 48 ^ (okey) lm << 44;
    scx x;
    x.n = 5;  x.seed = 1;  x.deep = 2;
    x.key[0] = key ^ (okey) 0x74 << 56;
    x.key[1] = g ^ (okey) 2 << 52 ^ (okey) q5 << 40 ^ (okey) b << 32;
    x.key[2] = g ^ (okey) 3 << 52 ^ (okey) q5 << 40;
    x.key[3] = g ^ (okey) 4 << 52;
    x.key[4] = g ^ (okey) 5 << 52 ^ (okey) (px < 0);
    x.pred[0] = x.pred[1] = -1;
    x.pred[2] = pp;  x.pred[3] = pb;  x.pred[4] = px;
    sc_sym(ctx_find(key, pr), KP[OP_LAPLACE].inc, KP[OP_LAPLACE].cap, pr, &sym,
           &x, 24 + lm + 4 * it);
  } else
    ctx_code(ctx_find(key, pr), KP[OP_LAPLACE].inc, KP[OP_LAPLACE].cap, &sym);
  if (sym == LAPN - 1) {
    u32 mag = om_mode == OM_DEC ? 0 : (u32) ABS(v);
    int sg = om_mode == OM_DEC ? 0 : v < 0;
    om_bit((okey) 0xFEED0001UL, 0x8000, &sg);
    p_uniform(&mag, 1UL << 16);
    if (om_mode == OM_DEC) v = sg ? -(int) mag : (int) mag;
  } else v = sym - LAPW;
  op->v = v;
  lap_note(b, c, v);
  OM_LEAVE();
  return op->v;
}

opus_int32 om_angle(oprec * op) {
  int b = OM_B;
  if (!om_enter(op)) return op->v;
  if (oz_on && !oz_part) { oz_op(op);  OM_LEAVE();  return op->v; }
  if (!(om_feat & OF_THCTX)) {
    int qn = MAX((int) op->nsym - 1, 1), mode = (int) op->ftb, v = op->v;
    prior * pr = prior_theta(mode, qn);
    okey key;
    key = (okey) b << 24 | (okey) mode << 22 | (okey) bktN((int) op->aux) << 19
        | (okey) (qn & 255) << 11 | (okey) (prev_theta[b] & 7);
    ctx_code(ctx_find(key, pr), th_inc, KP[OP_THETA].cap, &v);
    op->v = v;
    PROF(prof_theta(b, mode, prev_theta[b], (u32) qn, op->aux, (i32) v));
    prev_theta[b] = v * 8 / (qn + 1);
  } else om_theta(op, b);
  OM_LEAVE();
  return op->v;
}

opus_int32 om_uint(oprec * op) {
  int b = OM_B;
  u32 V = op->nsym;
  if (!om_enter(op)) return op->v;
  if (oz_on && !oz_part && !(oz_tree && orec_pvqK > 0 && V > 16)) {
    oz_op(op);  OM_LEAVE();  return op->v;
  }
  if (orec_pvqK > 0 && V > 16) {
    int nb = bktN(orec_pvqN), kb = bktK(orec_pvqK);
    okey key = (okey) 0x50000000UL << 32 | (okey) b << 8
             | (okey) nb << 4 | (okey) kb;
    u16 * p = bin_slot(key, (u16) (65535U - (65536U >> 10)));
    u32 last = last_pvq[b][nb][kb], x;
    int hv = have_pvq[b][nb][kb] && last_pvqV[b][nb][kb] == V;
    int rep = om_mode != OM_DEC && hv && (u32) op->v == last;
    int ok = pvq_fits(orec_pvqN, orec_pvqK, V);
    if (hv) {
      p_bit(p, &rep);
      if (rep) {
        if (pvq_tree && ok) pvq_note(b, orec_pvqN, orec_pvqK, last);
        orec_pvqK = 0;
        op->v = (opus_int32) last;
        PROF(prof_pvq(b, nb, kb, (u32) orec_pvqN, (u32) orec_pvqK, V,
                      (u32) op->v));
        goto done;
      }
    }
    if (!pvq_tree && !pvq_lev && ok) {
      x = (u32) op->v;
      p_uniform(&x, V);
      op->v = (opus_int32) x;
    } else if (ok) {
      static int y[PVQ_UN + 2];
      int N = orec_pvqN, K = orec_pvqK;
      if (om_mode != OM_DEC) cw_dec(pvq_ut, N, K, (u32) op->v, y);
      pvq_code(b, N, K, y);
      if (om_mode == OM_DEC) op->v = (opus_int32) cw_enc(pvq_ut, N, K, y);
    } else {
      x = 0;
      if (om_mode != OM_DEC) { x = (u32) op->v;  if (hv && x > last) x--; }
      p_uniform(&x, hv ? V - 1 : V);
      if (om_mode == OM_DEC) {
        if (hv && x >= last) x++;
        op->v = (opus_int32) x;
      }
    }
    PROF(prof_pvq(b, nb, kb, (u32) orec_pvqN, (u32) orec_pvqK, V,
                  (u32) op->v));
    orec_pvqK = 0;
    last_pvq[b][nb][kb] = (u32) op->v;
    last_pvqV[b][nb][kb] = V;
    have_pvq[b][nb][kb] = 1;
    goto done;
  }
  if (V <= 1024) {
    int v = op->v;
    ctx_code(uint_ctx(op), KP[OP_UINT].inc, KP[OP_UINT].cap, &v);
    op->v = v;
  } else {
    u32 x = (u32) op->v;
    p_uniform(&x, V);
    op->v = (opus_int32) x;
  }
done:
  OM_LEAVE();
  return op->v;
}

opus_int32 om_bits(oprec * op) {
  int b = OM_B, c = orec_ch & 1, lm0 = OM_LM0;
  unsigned nb = op->ftb;
  int g1;
  if (!om_enter(op)) return op->v;
  if (oz_on && !oz_part) { oz_op(op);  OM_LEAVE();  return op->v; }
  g1 = !(om_feat & OF_FINE) || lm0;
  if (!g1 && op->site == OREC_S_CELT_FINE && nb > 0 && nb <= 8)
    om_fine(op, b, c);
  else if (!g1 && op->site == OREC_S_CELT_FINALISE && nb == 1)
    om_final(op, b, c);
  else if (nb > 0 && nb <= 8 && orec_ftb == (int) nb) {
    int v = op->v;
    ctx_code(bits_ctx(b, c, (int) nb, g1), KP[OP_BITS].inc, KP[OP_BITS].cap,
             &v);
    op->v = v;
    bits_note(op, b, c, (int) nb, g1, v);
  } else {
    u32 x = (u32) op->v;
    p_raw(&x, (int) nb);
    op->v = (opus_int32) x;
  }
  OM_LEAVE();
  return op->v;
}
