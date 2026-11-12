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

#define PHASH(st_) cm_hst(phb, phx, (u32) (st_))
#define RS_ENCODING(s_) (RS_ENC < 0 ? (s_)->z->enc : RS_ENC)
#define RS_HISTON(s_) (RS_HIST < 0 ? (s_)->n->mavg != NULL : RS_HIST)
#define RS_GATEON(s_) (RS_GATE < 0 ? (s_)->gate != NULL : RS_GATE)
#define RS_DPON(s_) (RS_DP < 0 ? (s_)->dpb != NULL : RS_DP)
#ifndef RS_H2
#define RS_H2 (RS_DP < 0 ? -1 : 0)
#endif
#define RS_H2ON(c_) (RS_H2 > 0 || (RS_H2 < 0 && (c_)->h2on))

#if RS_CM
static INLINE int RS_NAME(mixc)(cm * c, int st, int sel, u32 h, u32 * p,
                                int bit) {
#if CM_INLINE
  if (((RS_DP && (RS_DP > 0 || c->dq)) || RS_H2ON(c)) && LIKELY(cm_inl))
    return mix_bit(c, st, sel, h, p, bit, 0, RS_ENC < 0 ? -1 : !RS_ENC, 0,
                   RS_H2, RS_APM);
  if (LIKELY(cm_inl))
    return mix_bit(c, st, sel, h, p, bit, 1, RS_ENC < 0 ? -1 : !RS_ENC,
                   RS_HIST <= 0, 0, RS_APM);
#endif
  if ((RS_DP && (RS_DP > 0 || c->dq)) || RS_H2ON(c))
    return cm_bit(c, st, sel, h, p, bit);
  return cm_plain(c, st, sel, h, p, bit);
}

static INLINE int RS_NAME(mix)(const rs_ctx * s, cm * c, int st, int sel,
                               u32 h, u32 * p, int bit) {
#if RS_GATE
  u32 p0 = rc_packed_prob(*p);
  if (RS_GATEON(s) && RS_GATED(p0))
    return gate_bit(s, st, p, p0, bit, RS_ENCODING(s));
#endif
  return RS_NAME(mixc)(c, st, sel, h, p, bit);
}
#endif

#if RS_CM != 31
static INLINE int RS_NAME(bit)(const rs_ctx * s, u32 * p, int bit) {
  if (!RS_ENCODING(s)) return rc_dec_bit_packed(s->d, p, RC_ALIM);
  rc_enc_bit_packed(s->e, p, RC_ALIM, bit);
  return bit;
}
#endif

static HOT NOINLINE i32 RS_NAME(val)(const rs_ctx * s, u32 c, u32 ch, i32 v,
                                     u32 fz) {
  vb_ctx * n = s->n;
  u32 * o;
  u32 ax, il, m, a, mag = 0, k, idx;
  u32 la = n->fh[0], nb = 0;
  u8 * mw;
  int t, sg = 0;
#if RS_CM
  int psel = 0, memf = 0;
#if RS_HIST && (RS_CM & 2)
  int psg = 0;
#endif
  u32 pslot = 0, phb = 0, phx = 0;
#if RS_HIST
  u16 * av = NULL;
  u32 ab = 0;
#endif
#endif
  u32 zb = c / 4 >> s->zs;
  int zm;
#if RS_CM && RS_DP
  u32 * dpq = RS_DPON(s) ? s->dpb + ch * AR_MAXIDX + c : NULL;
#define RS_DQ(nd) \
  if (RS_DPON(s)) n->cm.dq = dpq + (sz) (nd) * (AR_NCH * AR_MAXIDX);
#else
#define RS_DQ(nd)
#endif
#ifdef BLR_PROFILE
  int e_sh = n->sh[ch], e_mh = n->mh[0];
#endif
  FATAL_IF_HOT(c >= AR_MAXIDX)
    ("vorbis: residue index %lu out of range", (unsigned long) c);
#if !RS_CM
  fz = VB_FZALL;
#endif
  mw = s->mw + c * AR_NCH + ch;
  m = s->mr[c * AR_NCH + ch];
  ax = n->psl == s->slot;
  ax = ax * 2 + !(m & 7);
  zm = n->t.flags & VB_TF_ZMAG && s->il >= 2;
  ax = zm ? ax * 4 + MIN(n->mh[0], 3) : ax * 2 + la;
#if RS_CM
  if (n->nstarted && n->nidx[ch] + 1 == c) n->nrun[ch]++;
  else n->nrun[ch] = 0;
#if RS_HIST
  if (RS_HISTON(s)) {
    av = n->mavg + (mw - n->mem);  ab = *av;
    ab = ab < 16 ? 0 : ab < 112 ? 1 : ab < 224 ? 2 : ab < 560 ? 3 : 4;
    zb = c / 16 * 4 + MIN(ab, 3);
  }
#if RS_CM & 2
#define RS_BANK psg = psel;  psel += CM_SEL * (int) ab;
#else
#define RS_BANK psel += CM_SEL * (int) ab;
#endif
#else
#define RS_BANK
#endif
#define RS_CTX \
  { int c1, cx; \
    c1 = n->nrun[ch] >= 1 ? mcls(n->nv0[ch]) : 0; \
    cx = n->nstarted && (u32) n->npch != ch ? mcls(n->nxv) : 0; \
    memf = (int) ((m & 3) + 4 * (m >> 7)); \
    psel = ((c1 * 5 + cx) * 2 + (int) (ch & 1)) * 4 + (int) (m & 3); \
    RS_BANK \
    pslot = s->slot; \
    phb = cm_hpre(pslot, ch, c, (u32) memf); \
    phx = cm_hpx(pslot, ch, c, (u32) memf); \
    if (RS_DP > 0 || RS_HIST > 0 || n->cm.xt) { \
      n->cm.sel2 = pslot * AR_NCH + ch; \
      if (RS_APM > 0 || (RS_APM && n->cm.apm)) { \
        n->cm.actx[0] = ch * VB_APMC + (c < 2048 ? c : 1536 + (c >> 2)); \
        n->cm.actx[1] = ((blr_ilog(c) * AR_MCLS + (m & 15)) * AR_NCH + \
                         ch) * 5 + (u32) cx; } \
      if (RS_H2ON(&n->cm)) { \
        n->cm.h2b = cm_hpre(pslot, ch, 0, (u32) (c1 * 5 + cx)) + 0x5bd1e995u; \
        n->cm.h2x = cm_hpx(pslot, ch, 0, (u32) (c1 * 5 + cx)) ^ 0x2f; } } }
#endif
  if (zm) ax = ax * (AR_NBIN / 2) +
               (RS_HISTON(s) ? c / 32 * 4 + (zb & 3) : zb >> 1);
  else ax = ax * AR_NBIN + zb;
  o = s->tab[A_RZERO - A_RZERO] + (ch >> 1) * 2 * AR_RZCH +
      (ax % AR_NBIN * 8 + ax / AR_NBIN) * 2 + (ch & 1);
  PROF(prof_site = P_RZERO);
  if ((fz & (VB_FZ0 | VB_FZNZ)) != (VB_FZ0 | VB_FZNZ)) {
    t = !(fz & VB_FZ0);
#if RS_CM
    if (t) RS_CTX
#endif
  } else
#if RS_CM & 1
#if RS_GATE
  if (RS_GATEON(s) && RS_GATED(rc_packed_prob(*o))) {
    t = gate_bit(s, 0, o, rc_packed_prob(*o), v != 0, RS_ENCODING(s));
    if (t) RS_CTX
  } else
#endif
  { RS_CTX
    RS_DQ(0)
    t = RS_NAME(mixc)(&n->cm, 0, psel, PHASH(0), o, v != 0); }
#else
  {
#if RS_CM
    RS_CTX
#endif
    t = RS_NAME(bit)(s, o, v != 0);
  }
#endif
  n->psl = s->slot;
  if (LIKELY(!t)) {
    n->fh[ch] = 0;  n->mh[ch] = 0;  *mw = 0;
#if RS_HIST
    if (RS_HISTON(s)) *av = (u16) (*av * 6 / 7);
#endif
    PROF(prof_res((int) s->slot, (int) s->q, (int) s->pass, (int) ch, c,
                  (int) m, (int) la, e_sh, e_mh, 0));
    PROF(prof_site = P_VOTHER);
#if RS_CM
    cm_step(n, ch, c, 0);
#endif
    return 0;
  }
  il = blr_ilog(c) >> s->is;  n->fh[ch] = 1;
  if (RS_ENCODING(s)) mag = (u32) ABS(v);
  ax = m != 0;
  ax = ax * 2 + (m >> 7 & 1);
  ax = ax * 2 + la;
  ax = ax * AR_HIST2 + n->sh[ch];
  ax = ax * AR_ILOG + il;
  o = s->tab[A_RSIGN - A_RZERO] + ch * s->cs[A_RSIGN - A_RZERO] + ax;
  PROF(prof_site = P_RSIGN);
  if ((fz & (VB_FZP | VB_FZN)) != (VB_FZP | VB_FZN)) sg = !(fz & VB_FZP);
  else {
#if RS_CM & 2
  RS_DQ(1)
#if RS_HIST
  sg = RS_NAME(mix)(s, &n->cm, 1, psg, PHASH(1), o, v < 0);
#else
  sg = RS_NAME(mix)(s, &n->cm, 1, psel, PHASH(1), o, v < 0);
#endif
#else
  sg = RS_NAME(bit)(s, o, v < 0);
#endif
  }
  ax = m != 0;
  ax = ax * AR_MCLS + n->mh[0];
  ax = ax * 2 + (u32) sg;
  ax = ax * AR_ILOG + il;
  o = s->tab[A_RONE - A_RZERO] + ch * s->cs[A_RONE - A_RZERO] + ax;
  PROF(prof_site = P_RONE);
  if ((fz & (VB_FZ1 | VB_FZ2)) != (VB_FZ1 | VB_FZ2)) t = !(fz & VB_FZ2);
  else {
#if RS_CM & 4
    RS_DQ(2)
    t = RS_NAME(mix)(s, &n->cm, 2, psel, PHASH(2), o, mag == 1);
#else
    t = RS_NAME(bit)(s, o, mag == 1);
#endif
  }
  n->sh[ch] = (u8) ((sg + n->sh[ch] * 2) & 3);
  if (t) { n->mh[ch] = 1;  *mw = (u8) ((sg << 7) + 1);  mag = 1; }
  else {
#if RS_CM & 8
    u32 h3;
#endif
#if RS_CM & 16
    u32 h4;
#endif
    if (RS_ENCODING(s)) nb = blr_ilog(mag) - 2;
    FATAL_IF_HOT(RS_ENCODING(s) && !(mag >= 2 && nb < AR_MAGB))
      ("vorbis: residue digit %ld exceeds 8 bits", (long) v);
    ax = n->mh[0];
    o = s->tab[A_RLEN - A_RZERO] + ch * s->cs[A_RLEN - A_RZERO] + ax * 8;
    PROF(prof_site = P_RLEN);
    idx = 1;
#if RS_CM & 8
    h3 = PHASH(3);
#endif
    for (k = 3; k > 0; k--) {
#if RS_CM & 8
      RS_DQ(2 + idx)
      t = RS_NAME(mix)(s, &n->cm, 3, psel, h3, o + idx,
                       (int) (nb >> (k - 1) & 1));
#else
      t = RS_NAME(bit)(s, o + idx, (int) (nb >> (k - 1) & 1));
#endif
      idx = idx * 2 + (u32) t;
    }
    nb = idx - 8;
    ax = nb;
    ax = ax * AR_MCLS + n->mh[0];
    o = s->tab[A_RMANT - A_RZERO] + ch * s->cs[A_RMANT - A_RZERO] +
        ax * AR_LOW2;
    PROF(prof_site = P_RMANT);
    a = 1;
#if RS_CM & 16
    h4 = PHASH(4);
#endif
    for (k = nb + 1; k > 0; k--) {
#if RS_CM & 16
      RS_DQ(10 + (a & 3))
      t = RS_NAME(mix)(s, &n->cm, 4, psel, h4, o + (a & 3),
                       (int) (mag >> (k - 1) & 1));
#else
      t = RS_NAME(bit)(s, o + (a & 3), (int) (mag >> (k - 1) & 1));
#endif
      a = a * 2 + (u32) t;
    }
    mag = a;  n->mh[ch] = (u8) (nb + 2);  *mw = (u8) ((sg << 7) + 2 + nb);
  }
  { i32 rv = sg ? -(i32) mag : (i32) mag;
    PROF(prof_res((int) s->slot, (int) s->q, (int) s->pass, (int) ch, c,
                  (int) m, (int) la, e_sh, e_mh, rv));
    PROF(prof_site = P_VOTHER);
#if RS_CM
    cm_step(n, ch, c, rv);
#if RS_HIST
    if (RS_HISTON(s)) *av = (u16) (*av * 6 / 7 + mag * 32);
#endif
#endif
    return rv; }
}

static INLINE void RS_NAME(sym)(const rs_ctx * s, vb_book * b, u32 g, u32 st,
                               u32 il) {
  io * z = s->z;
  u32 k, e = 0, np = 1, t = 0, c, ch, pre = 0, fw = 0, fm;
  const u8 * fz;
  i32 d;
  if (RS_ENCODING(s)) { e = bk_get(z, b);  t = e; }
  if (z->probe) return;
  fz = RS_CM ? b->fz : NULL;
  fm = RS_CM ? b->fzm : VB_FZALL;
  if (fz) { fw = 1;  if (b->nv > 1) Fk(b->dim - 1, fw *= b->nv); }
  rs_pos(g, il, &c, &ch);
  Fk(b->dim,
    u32 chc = MIN(ch, 3);
    if (RS_ENCODING(s)) {
      if (b->dig) d = b->dig[e * b->dim + k];
      else {
        u32 next = (u32) ((uint64_t) t * b->divmul >> b->divshift);
        d = (i32) b->mult[t - next * b->nv] - (i32) b->off;  t = next;
      }
      RS_NAME(val)(s, c, chc, d, fz ? fz[k * fw + pre] : fm);
      if (fz) { pre += b->inv[(u32) (d + (i32) b->off)] * np;  np *= b->nv; }
    } else {
      u32 p;
      d = RS_NAME(val)(s, c, chc, 0, fz ? fz[k * fw + e] : fm);
      FATAL_IF_HOT(!(d + (i32) b->off >= 0 && d + (i32) b->off < (i32) b->base))
        ("vorbis: residue digit %ld outside codebook grid", (long) d);
      p = b->inv[(u32) (d + (i32) b->off)];
      FATAL_IF_HOT(p == (u32) -1)
        ("vorbis: residue digit %ld is not a codebook multiplicand", (long) d);
      e += p * np;  np *= b->nv;
    }
    if (il) { ch += st;  while (ch >= il) { ch -= il;  c++; } }
    else c += st);
  if (!RS_ENCODING(s)) {
    FATAL_IF_HOT(e >= b->ent)("vorbis: residue has no codebook entry");
    bk_put(z, b, e);
  }
}

static HOT FLATTEN void RS_NAME(part)(io * z, vb_res * r, vb_book * b, u32 q,
                                     u32 pass, u32 g, u32 il) {
  rs_ctx s;
  u32 i, st;
  rs_init(&s, z, b->slot, q, pass);
  s.il = il;
  if (r->type == 0) {
    st = r->psz / b->dim;
    FATAL_IF_HOT(st * b->dim != r->psz)
      ("vorbis: residue 0 partition %lu not divisible by %lu",
       (unsigned long) r->psz, (unsigned long) b->dim);
    Fi(st,
      RS_NAME(sym)(&s, b, g + i, st, il);
      if (z->rawpkt) return);
  } else
    for (i = 0; i < r->psz && !z->rawpkt; i += b->dim)
      RS_NAME(sym)(&s, b, g + i, 1, il);
}

#undef RS_CTX
#undef RS_DQ
#undef RS_DPON
#undef RS_H2ON
#undef RS_H2
#undef RS_APM
#undef RS_DP
#undef RS_BANK
#undef RS_HISTON
#undef RS_GATEON
#undef RS_HIST
#undef RS_GATE
#undef RS_ENCODING
#undef RS_ENC
#undef PHASH
#undef RS_NAME
#undef RS_CM
