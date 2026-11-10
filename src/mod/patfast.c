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

#define PIXN 64

typedef struct {
  u32 hit[3][2][4][4];
  u32 alt[3][2][4];
  u32 emp[3][2][4];
  u32 npr[2][2][2];
  u32 pit[NSYM][256];
  u32 pix[MOD_NSEMI][PIXN];
  u32 seq[2][2];
  u32 smt[2][512];
  u32 eft[17][32];
  u32 peq[16][2];
  u32 prt[16][512];
} fz;

#define FLIM 30

static INLINE int fbit(mx * m, u32 * s, int bit) {
  bit = mx_bit(m, bit, rc_packed_prob(*s));
  *s = rc_adapt_packed(*s, FLIM, bit);
  return bit;
}

static u32 ftree(mx * m, u32 * t, u32 tn, u32 v, u32 n) {
  u32 lo = 0, hi = n, node = 1;
  while (hi - lo > 1) {
    u32 mid = lo + (hi - lo) / 2;
    int bit = fbit(m, t + (node & (tn - 1)), v >= mid);
    if (bit) lo = mid; else hi = mid;
    node = node * 2 + (u32) bit;
  }
  return lo;
}

static u32 raw(const u8 * c) { return rd32be(c); }

void pat_fast(mx * m) {
  const mod_lay * L = &m->L;
  const sz rowc = (sz) L->ch, patc = rowc * MOD_ROWS;
  fz * z = xcalloc(1, sizeof *z);
  u8 * ln = xcalloc(rowc, 1), * ls = xcalloc(rowc, 1), * hs = xcalloc(rowc, 1),
     * lp = xcalloc(rowc * 16, 1);
  u32 * cv = xcalloc(L->ncell + 1, sizeof *cv);
  mtrk T;
  sz i;
  rc_adapt_init();
  mt_init(&T, L->ncell, rowc);
  Fi(L->ncell,
    const u8 * src = m->in ? m->in + L->pat + 4 * i : NULL;
    int c = (int) (i % rowc), r = (int) (i / rowc % MOD_ROWS), hit;
    sz pr = mt_pred(&T, c, i), up = i >= patc ? i - patc : NOC,
       l1 = r ? i - rowc : NOC, pk = pr != NOC ? pr : up,
       qk = pr != NOC && up != NOC ? up : l1;
    int sr = pr != NOC ? 0 : up != NOC ? 1 : 2;
    u32 pv = pk != NOC ? cv[pk] : 0, qv = qk != NOC ? cv[qk] : 0,
        v = src ? raw(src) : 0,
        l1v = l1 != NOC ? cv[l1] : 0, per = 0, sym = 0, smp = 0, eff = 0,
        par = 0;
    u8 b[4];
    m->ty = T_NOTE;
    hit = fbit(m, &z->hit[sr][pv == 0][MIN(T.ml[c], 3)][hs[c] & 3], v == pv);
    if (hit) v = pv;
    else if (qv && qv != pv &&
             fbit(m, &z->alt[sr][qk == up][MIN(T.ml[c], 3)], v == qv))
      v = qv;
    else if (pv && fbit(m, &z->emp[sr][l1v == 0][r & 3], v == 0)) v = 0;
    else {
      int np;
      if (src) cell_get(src, &per, &sym, &smp, &eff, &par);
      np = fbit(m, &z->npr[l1 != NOC && m->nt[l1]][pk != NOC && m->nt[pk]]
                         [up != NOC && m->nt[up]], sym != 0);
      if (np) {
        int s;
        u32 a, cnt;
        m->ty = T_PITCH;
        sym = 1 + ftree(m, z->pit[ln[c]], 256, sym - 1, NSYM - 1);
        s = (int) sym - 1;  a = m->aoff[s];  cnt = (u32) m->aoff[s + 1] - a;
        if (cnt > 1) {
          u32 k = 0;
          m->ty = T_PIDX;
          if (!m->dec) while (m->aper[a + k] != per) k++;
          per = m->aper[a + ftree(m, z->pix[s], PIXN, k, cnt)];
        } else per = cnt ? m->aper[a] : mod_semi_lo(s);
      } else sym = per = 0;
      m->ty = T_SMP;
      if (fbit(m, &z->seq[np][l1 != NOC && m->sm[l1] == ls[c]], smp == ls[c]))
        smp = ls[c];
      else smp = ftree(m, z->smt[np], 512, smp, 256);
      m->ty = T_EFF;
      eff = ftree(m, z->eft[l1 != NOC ? m->ef[l1] : 16], 32, eff, 16);
      m->ty = T_PAR;
      if (fbit(m, &z->peq[eff][l1 != NOC && m->ef[l1] == eff],
               par == lp[c * 16 + eff]))
        par = lp[c * 16 + eff];
      else par = ftree(m, z->prt[eff], 512, par, 256);
      b[0] = (u8) ((smp & 0xF0) | per >> 8);  b[1] = (u8) per;
      b[2] = (u8) ((smp & 15) << 4 | eff);  b[3] = (u8) par;
      v = raw(b);
    }
    b[0] = (u8) (v >> 24);  b[1] = (u8) (v >> 16);  b[2] = (u8) (v >> 8);
    b[3] = (u8) v;
    cell_get(b, &per, &sym, &smp, &eff, &par);
    cv[i] = v;
    cell_put(m, i, per, sym, smp, eff, par);
    hs[c] = (u8) (hs[c] << 1 | (u8) hit);
    if (sym) ln[c] = (u8) sym;
    if (smp) ls[c] = (u8) smp;
    lp[c * 16 + eff] = (u8) par;
    mt_next(&T, m, c, i, pr));
  mt_free(&T);
  xfree(z);  xfree(ln);  xfree(ls);  xfree(hs);  xfree(lp);  xfree(cv);
}
