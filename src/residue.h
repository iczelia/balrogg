/*  Copyright (C) 2026 Kamila Szewczyk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, version 3.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program. If not, see <http://www.gnu.org/licenses/>.  */

/*  Included once per supported stage mask. RS_CM selects mixed stages and
    RS_NAME names the digit, symbol and partition functions. The dispatch is
    outside the partition loop, so each bit has a fixed model path.  */

#define PHASH(st_) cm_hst(phb, phx, (u32) (st_))

/*  Keep digit temporaries out of the surrounding symbol loop. Each stage
    has a fixed model path, and plain digits need no mixer state.  */
static HOT NOINLINE i32 RS_NAME(val)(io * z, vb_book * b, u32 q, u32 pass, u32 c,
                              u32 ch, u32 mb, i32 v) {
  vb_ctx * n = z->v;
#if RS_CM != 31
  int lim = n->t.alim;
#endif
  u32 o, ax, il, m, a, mag = 0, k, idx;
  u32 la = n->fh[0], nb = 0;
  u8 * mr, * mw;
  int t, sg = 0;
#if RS_CM
  int psel = 0, memf = 0;
  u32 pslot = 0, phb = 0, phx = 0;
  i32 pv = 0;                 /*  the match model's digit, while it holds  */
  u32 pm = 0;
  int mok = 0, mex = -1;
#endif
#ifdef BLR_PROFILE
  int e_sh = n->sh[ch], e_mh = n->mh[0];
#endif
  FATAL_IF_HOT(c >= AR_MAXIDX)
    ("vorbis: residue index %lu out of range", (unsigned long) c);
  mr = n->mem + ((q * AR_NPASS + (pass ? pass - 1 : 0)) * AR_MAXIDX + c)
               * AR_NCH + ch;
  mw = n->mem + ((q * AR_NPASS + pass) * AR_MAXIDX + c) * AR_NCH + ch;
  m = *mr;
  ax = q;                                       /*  A_RZERO  */
  ax = ax * 2 + (n->psl == b->slot);
  ax = ax * 2 + !(m & 7);
  ax = ax * 2 + la;
  ax = ax * AR_NBIN + c / 4;
  o = mb + atab(n, A_RZERO, ax * AR_NCH + ch);
#if RS_CM
  /*  Mixer neighborhood uses adjacent and cross-channel digits plus class.  */
  { int c1, cx;
    if (n->nstarted && n->nidx[ch] + 1 == c) n->nrun[ch]++;
    else n->nrun[ch] = 0;
    c1 = n->nrun[ch] >= 1 ? mcls(n->nv0[ch]) : 0;

    /*  For interleaved residue, cx is the other channel at this bin.  */
    cx = n->nstarted && (u32) n->npch != ch ? mcls(n->nxv) : 0;
    memf = (int) ((m & 3) + 4 * (m >> 7));
    psel = ((c1 * 5 + cx) * 2 + (int) (ch & 1)) * 4 + (int) (m & 3);
    pslot = b->slot;
    phb = cm_hpre(pslot, ch, c, (u32) memf);
    phx = cm_hpx(pslot, ch, c, (u32) memf);
    if (n->t.flags & VB_TF_MATCH) mok = cm_match(&n->cm, &pv);
    if (mok) pm = (u32) (pv < 0 ? -pv : pv);
  }
#endif
  PROF(prof_site = P_RZERO);
  /*  Each stage expects the predicted digit's bit until one disagrees.  */
#if RS_CM & 1
  mex = mok ? pv != 0 : -1;
  t = pbit(z, o, 0, psel, PHASH(0), mex, v != 0);
#else
  t = abit(z, o, lim, v != 0);
#endif
#if RS_CM
  if (t != (pv != 0)) mok = 0;
#endif
  n->psl = b->slot;
  if (!t) {
    n->fh[ch] = 0;  n->mh[ch] = 0;  *mw = 0;
#if RS_CM
    if (n->t.flags & VB_TF_MATCH) cm_match_push(&n->cm, 0);
#endif
    PROF(prof_res((int) b->slot, (int) q, (int) pass, (int) ch, c, (int) m,
                  (int) la, e_sh, e_mh, 0));
    PROF(prof_site = P_VOTHER);
#if RS_CM
    cm_step(n, ch, c, 0);
#endif
    return 0;
  }
  il = blr_ilog(c);  n->fh[ch] = 1;
  if (z->enc) mag = (u32) (v < 0 ? -v : v);
  ax = q;                                       /*  A_RSIGN  */
  ax = ax * 2 + (m != 0);
  ax = ax * 2 + (m >> 7 & 1);
  ax = ax * 2 + la;
  ax = ax * AR_HIST2 + n->sh[ch];
  ax = ax * AR_ILOG + il;
  o = mb + atab(n, A_RSIGN, ax * AR_NCH + ch);
  PROF(prof_site = P_RSIGN);
#if RS_CM & 2
  mex = mok ? pv < 0 : -1;
  sg = pbit(z, o, 1, psel, PHASH(1), mex, v < 0);
#else
  sg = abit(z, o, lim, v < 0);
#endif
#if RS_CM
  if (sg != (pv < 0)) mok = 0;
#endif
  ax = q;                                       /*  A_RONE  */
  ax = ax * 2 + (m != 0);
  ax = ax * AR_MCLS + n->mh[0];
  ax = ax * 2 + (u32) sg;
  ax = ax * AR_ILOG + il;
  o = mb + atab(n, A_RONE, ax * AR_NCH + ch);
  PROF(prof_site = P_RONE);
#if RS_CM & 4
  mex = mok ? pm == 1 : -1;
  t = pbit(z, o, 2, psel, PHASH(2), mex, mag == 1);
#else
  t = abit(z, o, lim, mag == 1);
#endif
#if RS_CM
  if (t != (pm == 1)) mok = 0;
#endif
  n->sh[ch] = (u8) ((sg + n->sh[ch] * 2) & 3);
  if (t) { n->mh[ch] = 1;  *mw = (u8) ((sg << 7) + 1);  mag = 1; }
  else {
#if RS_CM & 24
    u32 pnb = mok ? blr_ilog(pm) - 2 : 0;
#endif
#if RS_CM & 8
    u32 h3;
#endif
#if RS_CM & 16
    u32 h4;
#endif
    if (z->enc) nb = blr_ilog(mag) - 2;
    FATAL_IF_HOT(z->enc && !(mag >= 2 && nb < AR_MAGB))
      ("vorbis: residue digit %ld exceeds 8 bits", (long) v);
    ax = q * AR_MCLS + n->mh[0];                /*  A_RLEN  */
    o = mb + atab(n, A_RLEN, (ax * AR_NCH + ch) * 8);
    PROF(prof_site = P_RLEN);
    idx = 1;
#if RS_CM & 8
    h3 = PHASH(3);
#endif
    for (k = 3; k > 0; k--) {
#if RS_CM & 24
      mex = mok ? (int) (pnb >> (k - 1) & 1) : -1;
#endif
#if RS_CM & 8
      t = pbit(z, o + idx, 3, psel, h3, mex, (int) (nb >> (k - 1) & 1));
#else
      t = abit(z, o + idx, lim, (int) (nb >> (k - 1) & 1));
#endif
#if RS_CM & 24
      if (t != mex) mok = 0;
#endif
      idx = idx * 2 + (u32) t;
    }
    nb = idx - 8;
    ax = q * AR_MAGB + nb;                      /*  A_RMANT  */
    ax = ax * AR_MCLS + n->mh[0];
    o = mb + atab(n, A_RMANT, (ax * AR_NCH + ch) * AR_LOW2);
    PROF(prof_site = P_RMANT);
    a = 1;
#if RS_CM & 16
    h4 = PHASH(4);
#endif
    for (k = nb + 1; k > 0; k--) {
#if RS_CM & 16
      mex = mok ? (int) (pm >> (k - 1) & 1) : -1;
      t = pbit(z, o + (a & 3), 4, psel, h4, mex, (int) (mag >> (k - 1) & 1));
      if (t != mex) mok = 0;
#else
      t = abit(z, o + (a & 3), lim, (int) (mag >> (k - 1) & 1));
#endif
      a = a * 2 + (u32) t;
    }
    mag = a;  n->mh[ch] = (u8) (nb + 2);  *mw = (u8) ((sg << 7) + 2 + nb);
  }
  { i32 rv = sg ? -(i32) mag : (i32) mag;
    PROF(prof_res((int) b->slot, (int) q, (int) pass, (int) ch, c, (int) m,
                  (int) la, e_sh, e_mh, rv));
    PROF(prof_site = P_VOTHER);
#if RS_CM
    cm_step(n, ch, c, rv);
    if (n->t.flags & VB_TF_MATCH) cm_match_push(&n->cm, rv);
#endif
    return rv; }
}

/*  Process one codebook symbol as base-`nv` digits.  */
static INLINE void RS_NAME(sym)(io * z, vb_book * b, u32 q, u32 pass, u32 g, u32 st,
                               u32 il, u32 mb) {
  u32 k, e = 0, np = 1, t = 0, c, ch;
  i32 d;
  if (z->enc) { e = bk_get(z, b);  t = e; }
  if (z->probe) return;
  if (il == 2) { c = g >> 1;  ch = g & 1; }
  else if (il > 2) { c = g / il;  ch = g % il; }
  else { c = g;  ch = 0; }
  Fk(b->dim,
    u32 chc = ch > 3 ? 3 : ch;
    if (z->enc) {
      u32 next = (u32) ((uint64_t) t * b->divmul >> b->divshift);
      d = (i32) b->mult[t - next * b->nv] - (i32) b->off;  t = next;
      RS_NAME(val)(z, b, q, pass, c, chc, mb, d);
    } else {
      u32 p;
      d = RS_NAME(val)(z, b, q, pass, c, chc, mb, 0);
      FATAL_IF_HOT(!(d + (i32) b->off >= 0 && d + (i32) b->off < (i32) b->base))
        ("vorbis: residue digit %ld outside codebook grid", (long) d);
      p = b->inv[(u32) (d + (i32) b->off)];
      FATAL_IF_HOT(p == (u32) -1)
        ("vorbis: residue digit %ld is not a codebook multiplicand", (long) d);
      /*  Digit k weighs nv^k, the same decomposition the encoder took the
          entry apart with.  */
      e += p * np;  np *= b->nv;
    }
    if (il) { ch += st;  while (ch >= il) { ch -= il;  c++; } }
    else c += st);
  if (!z->enc) {
    FATAL_IF_HOT(e >= b->ent)("vorbis: residue has no codebook entry");
    bk_put(z, b, e);
  }
}

/*  Process one partition with inlined symbol operations.  */
static HOT FLATTEN void RS_NAME(part)(io * z, vb_res * r, vb_book * b, u32 q,
                                     u32 pass, u32 g, u32 il) {
  u32 i, st, mb = z->probe ? 0 : ar_slot(z->v, b->slot);
  if (r->type == 0) {
    st = r->psz / b->dim;
    FATAL_IF_HOT(st * b->dim != r->psz)
      ("vorbis: residue 0 partition %lu not divisible by %lu",
       (unsigned long) r->psz, (unsigned long) b->dim);
    Fi(st, RS_NAME(sym)(z, b, q, pass, g + i, st, il, mb);
           if (z->rawpkt) return);
  } else
    for (i = 0; i < r->psz && !z->rawpkt; i += b->dim)
      RS_NAME(sym)(z, b, q, pass, g + i, 1, il, mb);
}

#undef PHASH
#undef RS_NAME
#undef RS_CM
