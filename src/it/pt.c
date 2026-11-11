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

#define PCAP ((sz) 1 << 23)
#define PMB 18
#define PRB 16

typedef uint64_t pkc;

typedef struct {
  pkc * g;
  sz gcap, base, mp[64], rp, pb, * qb;
  u32 * mt, * rt, ml[64], rml, prow, S, prv, pnc, pr[64], * qr, qn, qcap, pd;
  u32 uh, ut;
  int rok;
  u8 ln[64], ln2[64], li[64], lv[64], lc[64], lh[64], hh[64];
  u8 lpc[64][32], lpc2[64][32], nti[256], iv[256], ic[256], ip[256];
  u8 phd[IT_PATH];
} pst;

#define KP(k) ((u32) ((k) >> 48 & 1))
#define KH(k) ((u32) ((k) >> 40 & 15))
#define KF(k, j) ((u32) ((k) >> (32 - 8 * (j)) & 255))

static INLINE u32 khash(pkc k) { return zh((u32) k, (u32) (k >> 32)); }

static INLINE pkc pkey(u32 has, const u8 * f) {
  return (pkc) 1 << 48 | (pkc) has << 40 | (pkc) f[0] << 32 |
         (pkc) f[1] << 24 | (pkc) f[2] << 16 | (pkc) f[3] << 8 | f[4];
}

static void pst_clear(pst * p) {
  int i;
  p->base = 0;  p->rp = NOC;  p->rml = 0;  p->rok = 1;  p->pb = NOC;
  p->qn = 0;
  Fi(64, p->mp[i] = NOC;  p->ml[i] = 0);
}

static void pst_reset(pst * p) {
  memset(p->mt, 0, ((sz) 1 << PMB) * sizeof *p->mt);
  memset(p->rt, 0, ((sz) 1 << PRB) * sizeof *p->rt);
  pst_clear(p);
}

static pst * pst_new(void) {
  pst * p = xcalloc(1, sizeof *p);
  int i;
  p->mt = xcalloc((sz) 1 << PMB, sizeof *p->mt);
  p->rt = xcalloc((sz) 1 << PRB, sizeof *p->rt);
  pst_clear(p);
  p->prv = 64;
  Fi(64, p->lh[i] = 16);
  return p;
}

static void pst_free(pst * p) {
  if (!p) return;
  xfree(p->g);  xfree(p->mt);  xfree(p->rt);  xfree(p->qb);  xfree(p->qr);
  xfree(p);
}

static void prow(pst * p, sz i0) {
  u32 hk = 0, k;
  if (i0 < p->S) return;
  Fk(p->S, hk = zh(hk, khash(p->g[i0 - p->S + k])));
  hk >>= 32 - PRB;
  if (p->rp != NOC && p->rok) { p->rp += p->S;  p->rml++; }
  else { p->rp = p->rt[hk] ? p->rt[hk] - 1 : NOC;  p->rml = 0; }
  p->rt[hk] = (u32) i0 + 1;
  p->rok = 1;
}

static void ptrack(pst * p, u32 c, sz i, sz pr) {
  const sz S = p->S;
  if (pr != NOC && p->g[pr] == p->g[i]) p->ml[c]++;
  else p->ml[c] = 0;
  if (pr != NOC && p->ml[c]) p->mp[c] = pr + S;
  else if (i >= S) {
    u32 j = p->mt[zh(khash(p->g[i - S]), khash(p->g[i])) >> (32 - PMB)];
    p->mp[c] = j ? j - 1 : NOC;
  } else p->mp[c] = NOC;
  if (i >= S)
    p->mt[zh(khash(p->g[i - S]), khash(p->g[i])) >> (32 - PMB)] =
      (u32) (i + S + 1);
}

static void addp(zc * z, u32 v, u32 h) {
  z->pv[z->np] = v;  z->ph[z->np++] = h;
}


static u32 cnote(ix * x, pst * p, u32 c, u32 has, pkc kp, pkc up, pkc lf,
                 u32 v) {
  zc * z = &x->z->z;
  u32 kn = KP(kp) && KH(kp) & 1 ? KF(kp, 0) : 256,
      un = KP(up) && KH(up) & 1 ? KF(up, 0) : 256,
      fn = KP(lf) && KH(lf) & 1 ? KF(lf, 0) : 256;
  z->n = 6;
  z->h[0] = ZH2(0x641, p->ln[c]);
  z->h[1] = ZH3(0x642, p->ln[c], p->ln2[c]);
  z->h[2] = ZH3(0x643, kn, p->ln[c]);
  z->h[3] = ZH3(0x644, un, c);
  z->h[4] = ZH3(0x645, fn, p->ln[c]);
  z->h[5] = ZH3(0x646, has, p->li[c]);
  addp(z, p->ln[c], ZH2(0x64A, 0));
  if (kn < 256) addp(z, kn, ZH2(0x64B, 0));
  if (un < 256) addp(z, un, ZH2(0x64C, 0));
  if (fn < 256) addp(z, fn, ZH2(0x64D, 0));
  return zsym(&x->b, z, v, 8, ZH3(0x6F8, has, 0), ZH2(0x6F9, p->ln[c]));
}

static u32 cins(ix * x, pst * p, u32 c, u32 has, pkc kp, u32 nn, u32 v) {
  zc * z = &x->z->z;
  u32 ki = KP(kp) && KH(kp) & 2 ? KF(kp, 1) : 256;
  z->n = 5;
  z->h[0] = ZH2(0x651, p->li[c]);
  z->h[1] = ZH3(0x652, nn, p->li[c]);
  z->h[2] = ZH2(0x653, nn < 256 ? p->nti[nn] : 256);
  z->h[3] = ZH2(0x654, ki);
  z->h[4] = ZH3(0x655, c, has);
  addp(z, p->li[c], ZH2(0x65A, 0));
  if (nn < 256) addp(z, p->nti[nn], ZH2(0x65B, 0));
  if (ki < 256) addp(z, ki, ZH2(0x65C, 0));
  return zsym(&x->b, z, v, 8, ZH2(0x6FA, has & 1), ZH2(0x6FB, nn < 256));
}

static pkc pcell(ix * x, pst * p, sz i, u32 r, u32 c, u32 R, pkc cur,
                u32 np) {
  zc * z = &x->z->z;
  mx * m = &x->b;
  const sz S = p->S;
  sz kpi = p->mp[c] < i ? p->mp[c] : NOC,
     qi = p->rp != NOC && p->rp + c < i ? p->rp + c : NOC,
     ui = p->pb != NOC && r < p->prow ? p->pb + (sz) r * S + c : NOC;
  pkc kp = kpi != NOC ? p->g[kpi] : 0, kq = qi != NOC ? p->g[qi] : 0,
      ab = i >= S ? p->g[i - S] : 0, up = ui != NOC ? p->g[ui] : 0,
      lf = c ? p->g[i - 1] : 0;
  u32 ml = MIN(p->ml[c], 15), hh = p->hh[c], pres, has, f[5], j, ie;
  {
    u32 ph = p->pr[c];
    z->n = 9;
    z->h[0] = ZH3(0x621, c, ph & 0xFF);
    z->h[1] = ZH3(0x622, ph & 0xFFFF, r & 3);
    z->h[2] = ZH4(0x623, r & 15, KP(ab), ui != NOC ? KP(up) : 2);
    z->h[3] = ZH4(0x624, c ? KP(lf) : 2, MIN(np, 3), MIN(c, 4));
    z->h[4] = ZH4(0x625, kpi != NOC ? KP(kp) : 2, qi != NOC ? KP(kq) : 2,
                  ph >> 15 & 1);
    z->h[5] = ZH4(0x626, ph >> 3 & 1, ph >> 7 & 1, r & 7);
    z->h[6] = ZH3(0x627, c, r);
    z->h[7] = ZH5(0x628, kpi != NOC ? KP(kp) : 2, ml, hh & 7, ph & 1);
    z->h[8] = ZH4(0x629, qi != NOC ? KP(kq) : 2, MIN(p->rml, 7), p->rok);
    pres = (u32) zflag(m, z, KP(cur), ZH4(0x6F2, KP(ab), ph & 1,
                                          kpi != NOC ? KP(kp) * 4 + MIN(ml, 3)
                                                     : 8),
                       ZH3(0x6F3, c, ph & 3));
    if (!pres) return 0;
    if (KP(kp)) {
      z->n = 5;
      z->h[0] = ZH3(0x601, ml, hh & 3);
      z->h[1] = ZH3(0x602, ui != NOC ? kp == up : 2, kp == ab);
      z->h[2] = ZH2(0x603, hh);
      z->h[3] = ZH3(0x604, r & 15, ml > 0);
      z->h[4] = ZH4(0x605, qi != NOC ? kq == kp : 2, MIN(p->rml, 3), np);
      if (zflag(m, z, cur == kp, ZH2(0x6F0, MIN(ml, 7)), ZH2(0x6F1, hh & 15)))
        return kp;
    }
    if (KP(kq) && kq != kp) {
      z->n = 4;
      z->h[0] = ZH2(0x611, MIN(p->rml, 15));
      z->h[1] = ZH4(0x612, ui != NOC ? kq == up : 2, kq == ab, KP(kp));
      z->h[2] = ZH2(0x613, p->rok);
      z->h[3] = ZH2(0x614, hh & 7);
      if (zflag(m, z, cur == kq, ZH2(0x6F4, MIN(p->rml, 7)),
                ZH2(0x6F5, KP(kp))))
        return kq;
    }
    if (KP(up) && up != kp && up != kq) {
      u32 ur = p->ut ? MIN(8 * p->uh / p->ut, 7) : 8;
      int h;
      z->n = 4;
      z->h[0] = ZH3(0x615, ur, MIN(p->ut, 3));
      z->h[1] = ZH4(0x616, up == ab, KP(kp), KP(kq));
      z->h[2] = ZH3(0x617, ml, hh & 3);
      z->h[3] = ZH3(0x618, MIN(p->pd, 3), ur);
      h = zflag(m, z, cur == up, ZH2(0x6F6, ur), ZH2(0x6F7, MIN(p->pd, 3)));
      p->ut++;  p->uh += (u32) h;
      if (h) return up;
    }
  }
  if (!pres) return 0;
  {
    u32 abh = KP(ab) ? KH(ab) : 16, kph = KP(kp) ? KH(kp) : 16,
        uph = KP(up) ? KH(up) : 16, lfh = KP(lf) ? KH(lf) : 16;
    z->n = 5;
    z->h[0] = ZH3(0x631, c, p->lh[c]);
    z->h[1] = ZH3(0x632, abh, kpi != NOC ? kph : 17);
    z->h[2] = ZH2(0x633, uph);
    z->h[3] = ZH3(0x634, p->lh[c], r & 3);
    z->h[4] = ZH2(0x635, lfh);
    if (p->lh[c] < 16) addp(z, p->lh[c], ZH2(0x63A, 0));
    if (abh < 16) addp(z, abh, ZH2(0x63B, 0));
    if (kpi != NOC && kph < 16) addp(z, kph, ZH2(0x63C, 0));
    has = zsym(m, z, KH(cur), 4, ZH2(0x6F6, p->lh[c]), ZH2(0x6F7, c));
  }
  Fj(5, f[j] = m->dec ? 0 : KF(cur, j));
  if (has & 1) f[0] = cnote(x, p, c, has, kp, up, lf, f[0]);
  if (has & 2)
    f[1] = cins(x, p, c, has, kp, has & 1 && f[0] < 120 ? f[0] : 256, f[1]);
  ie = has & 2 ? f[1] : p->li[c];
  if (has & 4) {
    u32 kv = KP(kp) && KH(kp) & 4 ? KF(kp, 2) : 256,
        av = KP(ab) && KH(ab) & 4 ? KF(ab, 2) : 256;
    z->n = 5;
    z->h[0] = ZH3(0x661, p->lv[c], has & 1);
    z->h[1] = ZH3(0x662, ie, has & 1);
    z->h[2] = ZH3(0x663, p->iv[ie], has & 1);
    z->h[3] = ZH2(0x664, kv);
    z->h[4] = ZH3(0x665, p->lv[c], av);
    addp(z, p->lv[c], ZH2(0x66A, 0));
    addp(z, p->iv[ie], ZH2(0x66B, 0));
    if (kv < 256) addp(z, kv, ZH2(0x66C, 0));
    f[2] = zsym(m, z, f[2], 8, ZH2(0x6FC, has & 3), ZH2(0x6FD, 0));
  }
  if (has & 8) {
    u32 kc = KP(kp) && KH(kp) & 8 ? KF(kp, 3) : 256,
        ac = KP(ab) && KH(ab) & 8 ? KF(ab, 3) : 256, e, kpp, abp;
    z->n = 6;
    z->h[0] = ZH2(0x671, p->lc[c]);
    z->h[1] = ZH3(0x672, has & 7, p->lc[c]);
    z->h[2] = ZH2(0x673, kc);
    z->h[3] = ZH2(0x674, p->ic[ie]);
    z->h[4] = ZH3(0x675, r + 1 == R, r & 15);
    z->h[5] = ZH2(0x676, ac);
    addp(z, p->lc[c], ZH2(0x67A, 0));
    if (kc < 256) addp(z, kc, ZH2(0x67B, 0));
    addp(z, p->ic[ie], ZH2(0x67C, 0));
    f[3] = zsym(m, z, f[3], 8, ZH2(0x6FE, has & 7), ZH2(0x6FF, 0));
    e = f[3] & 31;
    kpp = kc == f[3] ? KF(kp, 4) : 256;
    abp = ac == f[3] ? KF(ab, 4) : 256;
    z->n = 6;
    z->h[0] = ZH3(0x681, f[3], p->lpc[c][e]);
    z->h[1] = ZH3(0x682, f[3], kpp);
    z->h[2] = ZH2(0x683, f[3]);
    z->h[3] = ZH3(0x684, f[3], abp);
    z->h[4] = ZH3(0x685, f[3], ie);
    z->h[5] = ZH4(0x686, f[3], p->lpc[c][e], p->lpc2[c][e]);
    addp(z, p->lpc[c][e], ZH2(0x68A, f[3]));
    if (kpp < 256) addp(z, kpp, ZH2(0x68B, f[3]));
    if (p->ic[ie] == f[3]) addp(z, p->ip[ie], ZH2(0x68C, f[3]));
    f[4] = zsym(m, z, f[4], 8, ZH2(0x6E0, f[3]), ZH2(0x6E1, f[3]));
  } else f[3] = f[4] = 0;
  if (!(has & 1)) f[0] = 0;
  if (!(has & 2)) f[1] = 0;
  if (!(has & 4)) f[2] = 0;
  { u8 b[5];
    Fj(5, b[j] = (u8) f[j]);
    return pkey(has, b); }
}

static void pupd(pst * p, u32 c, pkc v, int hit) {
  u32 has = KH(v), ie;
  p->pr[c] = p->pr[c] << 1 | KP(v);
  p->hh[c] = (u8) (p->hh[c] << 1 | (u32) hit);
  if (!KP(v)) return;
  p->lh[c] = (u8) has;
  if (has & 2) p->li[c] = (u8) KF(v, 1);
  ie = p->li[c];
  if (has & 1) {
    p->ln2[c] = p->ln[c];  p->ln[c] = (u8) KF(v, 0);
    if (KF(v, 0) < 120) p->nti[KF(v, 0)] = (u8) ie;
  }
  if (has & 4) { p->lv[c] = (u8) KF(v, 2);  p->iv[ie] = (u8) KF(v, 2); }
  if (has & 8) {
    u32 e = KF(v, 3) & 31;
    p->lc[c] = (u8) KF(v, 3);
    p->lpc2[c][e] = p->lpc[c][e];  p->lpc[c][e] = (u8) KF(v, 4);
    p->ic[ie] = (u8) KF(v, 3);  p->ip[ie] = (u8) KF(v, 4);
  }
}

static void pgrid(pst * p, u32 rows) {
  sz need = (sz) rows * p->S;
  if (p->base + need > PCAP) pst_reset(p);
  if (p->base + need > p->gcap) {
    sz nc = MAX(p->gcap * 2, p->base + need);
    p->g = xrealloc(p->g, nc * sizeof *p->g);
    p->gcap = nc;
  }
}

typedef struct { u8 lm[64], last[64][5], set[64], seen[64]; } pkst;

static u8 pcan(const pkst * s, const pent * y, u8 * cmp) {
  u32 c = y->ch, j;
  u8 cm = 0;
  Fj(3, if (y->has >> j & 1)
          cm |= (u8) (s->set[c] >> j & 1 && s->last[c][j] == y->f[j]
                      ? 16 << j : 1 << j));
  if (y->has & 8)
    cm |= (u8) ((s->set[c] & 8) && s->last[c][3] == y->f[3] &&
                s->last[c][4] == y->f[4] ? 128 : 8);
  *cmp = (u8) (cm != s->lm[c] || !s->seen[c]);
  return cm;
}

static void ppk(pkst * s, const pent * y) {
  u32 c = y->ch, j;
  if (y->mp) s->lm[c] = y->m;
  s->set[c] |= y->m & 15;  s->seen[c] = 1;
  Fj(3, if (y->m >> j & 1) s->last[c][j] = y->f[j]);
  if (y->m & 8) { s->last[c][3] = y->f[3];  s->last[c][4] = y->f[4]; }
}

static void ppack(ix * x, pkst * s, pent * y) {
  zc * z = &x->z->z;
  mx * m = &x->b;
  u8 cmp, cm = pcan(s, y, &cmp);
  z->n = 2;
  z->h[0] = ZH2(0x691, y->has);
  z->h[1] = ZH2(0x692, cmp);
  if (zflag(m, z, !m->dec && y->m == cm && y->mp == cmp, ZH2(0x6E2, 0),
            ZH2(0x6E3, cmp))) { y->m = cm;  y->mp = cmp; }
  else {
    z->h[0] = ZH2(0x693, cmp);
    z->h[1] = ZH2(0x694, y->has);
    y->mp = (u8) zflag(m, z, y->mp, ZH2(0x6E4, 0), ZH2(0x6E5, 0));
    if (y->mp) {
      z->h[0] = ZH2(0x695, cm);
      z->h[1] = ZH2(0x696, y->has);
      y->m = (u8) zsym(m, z, y->m, 8, ZH2(0x6E6, 0), ZH2(0x6E7, 0));
    } else y->m = s->lm[y->ch];
  }
  ppk(s, y);
}

static void pcells(ix * x, pst * p, pent * E, u32 * rs, u32 rows, u32 nc) {
  mx * m = &x->b;
  const sz S = p->S;
  pkst ks;
  u32 r, c, ne = 0;
  memset(&ks, 0, sizeof ks);
  pgrid(p, rows);
  for (r = 0; r < rows; r++) {
    u32 k = m->dec ? 0 : rs[r], np = 0;
    sz i0 = p->base + (sz) r * S;
    prow(p, i0);
    rs[r] = ne;
    for (c = 0; c < S; c++) {
      sz i = i0 + c, pr = p->mp[c] < i ? p->mp[c] : NOC,
         q = p->rp != NOC && p->rp + c < i ? p->rp + c : NOC;
      pent * y = E + ne;
      pkc cur = 0, v;
      int j;
      if (c >= nc) { p->g[i] = 0;  continue; }
      if (!m->dec && k < rs[r + 1] && E[k].ch == c) {
        y = E + k++;
        cur = pkey(y->has, y->f);
      }
      v = pcell(x, p, i, r, c, rows, cur, np);
      p->g[i] = v;
      if (q == NOC || p->g[q] != v) p->rok = 0;
      ptrack(p, c, i, pr);
      pupd(p, c, v, pr != NOC && p->g[pr] == v);
      if (!KP(v)) continue;
      if (m->dec) {
        memset(y, 0, sizeof *y);
        y->has = (u8) KH(v);
        Fj(5, y->f[j] = (u8) KF(v, j));
      }
      y->ch = (u8) c;
      ppack(x, &ks, y);
      ne++;  np++;
    }
  }
  rs[rows] = ne;
  if (p->qn == p->qcap) {
    p->qcap = p->qcap * 2 + 16;
    p->qb = xrealloc(p->qb, p->qcap * sizeof *p->qb);
    p->qr = xrealloc(p->qr, p->qcap * sizeof *p->qr);
  }
  p->qb[p->qn] = p->base;  p->qr[p->qn++] = rows;
  p->base += (sz) rows * S;
}

static void zbytes(ix * x, u8 * t, sz n, u32 id) {
  zc * z = &x->z->z;
  mx * m = &x->b;
  sz j;
  Fj(n,
    u32 p1 = j ? t[j - 1] : 256, p2 = j > 1 ? t[j - 2] : 256,
        p3 = j > 2 ? t[j - 3] : 256;
    z->n = 4;
    z->h[0] = ZH2(id, p1);
    z->h[1] = ZH3(id, p1, p2);
    z->h[2] = ZH4(id, p1, p2, p3);
    z->h[3] = ZH2(id, 0x1000);
    t[j] = (u8) zsym(m, z, m->dec ? 0 : t[j], 8, ZH2(id, 1), ZH2(id, 2)));
}

static u32 znum(ix * x, u32 v, u32 prev, int nb, u32 id) {
  zc * z = &x->z->z;
  mx * m = &x->b;
  z->n = 2;
  z->h[0] = ZH2(id, 0);
  z->h[1] = ZH2(id, prev);
  if (zflag(m, z, v == prev, ZH2(id, 2), ZH2(id, 3))) return prev;
  z->n = 2;
  z->h[0] = ZH2(id, 4);
  z->h[1] = ZH2(id, prev);
  if (nb > 8) {
    u32 hi = zsym(m, z, v >> 8, nb - 8, ZH2(id, 5), ZH2(id, 6));
    z->n = 2;
    z->h[0] = ZH3(id, 7, hi);
    z->h[1] = ZH3(id, 8, prev);
    return hi << 8 | zsym(m, z, v & 255, 8, ZH2(id, 9), ZH2(id, 10));
  }
  return zsym(m, z, v, nb, ZH2(id, 5), ZH2(id, 6));
}

static void psrc(ix * x, pst * p, const pent * E, const u32 * rs, u32 rows) {
  zc * z = &x->z->z;
  mx * m = &x->b;
  const sz S = p->S;
  u32 d = 0, n = MIN(p->qn, 63), k, r, best = 0;
  if (!m->dec && n) {
    pkc * ky = xmalloc((rs[rows] + 1) * sizeof *ky);
    Fk(rs[rows], ky[k] = pkey(E[k].has, E[k].f));
    for (k = 0; k < n; k++) {
      sz b = p->qb[p->qn - 1 - k];
      u32 rr = MIN(rows, p->qr[p->qn - 1 - k]), sim = 0, q;
      for (r = 0; r < rr; r++)
        for (q = rs[r]; q < rs[r + 1]; q++)
          sim += p->g[b + (sz) r * S + E[q].ch] == ky[q];
      if (sim > best) { best = sim;  d = k; }
    }
    xfree(ky);
  }
  z->n = 2;
  z->h[0] = ZH2(0x6C1, MIN(p->pd, 8));
  z->h[1] = ZH2(0x6C2, 0);
  d = zsym(m, z, d, 6, ZH2(0x6C3, 0), ZH2(0x6C4, 0));
  p->pd = d;
  p->pb = d < p->qn ? p->qb[p->qn - 1 - d] : NOC;
  p->prow = d < p->qn ? p->qr[p->qn - 1 - d] : 0;
  p->uh = p->ut = 0;
}

static int ptn(ix * x, pst * p, it_ent * e) {
  mx * m = &x->b;
  zc * z = &x->z->z;
  const u8 * src = x->in ? x->in + e->at : NULL;
  u8 hd[IT_PATH];
  u32 rows, len = 0, mode = 1, nc = 0, i;
  pent * E = NULL;
  u32 * rs = NULL;
  int ne = -1;
  memset(hd, 0, sizeof hd);
  if (src) memcpy(hd, src, IT_PATH);
  rows = znum(x, rd16le(hd + 2), p->prv, 16, 0x6A0);
  wr16le(hd + 2, rows);
  Fi(4,
    z->n = 2;
    z->h[0] = ZH3(0x6A1, i, p->phd[4 + i]);
    z->h[1] = ZH2(0x6A2, i);
    hd[4 + i] = (u8) zsym(m, z, hd[4 + i], 8, ZH2(0x6A3, i), ZH2(0x6A4, i)));
  if (src) {
    len = rd16le(src);
    if (rows <= IT_MAXROW) {
      u8 * o;
      E = xmalloc((len + 1) * sizeof *E);
      rs = xmalloc((rows + 1) * sizeof *rs);
      ne = unpack(src + IT_PATH, len, rows, E, rs);
      o = xmalloc((sz) rows * (64 * 7 + 1) + 1);
      if (ne >= 0 && repack(E, rs, rows, o) == len &&
          !memcmp(o, src + IT_PATH, len))
        mode = 0;
      xfree(o);
      if (!mode) Fi((u32) ne, nc = MAX(nc, E[i].ch + 1u));
      if (nc > p->S) mode = 1;
    }
  }
  z->n = 1;
  z->h[0] = ZH2(0x6A5, 0);
  if (zflag(m, z, (int) mode, ZH2(0x6A6, 0), ZH2(0x6A7, 0))) {
    len = znum(x, len, 0, 16, 0x6A8);
    own(x, e, IT_PATH + len);
    zbytes(x, e->d + IT_PATH, len, 0x6B0);
  } else {
    if (rows > IT_MAXROW) return -1;
    if (m->dec) {
      E = xmalloc(((sz) rows * 64 + 1) * sizeof *E);
      rs = xmalloc((rows + 1) * sizeof *rs);
    }
    nc = znum(x, nc, p->pnc, 7, 0x6A9);
    if (nc > p->S) { xfree(E);  xfree(rs);  return -1; }
    p->pnc = nc;
    psrc(x, p, E, rs, rows);
    pcells(x, p, E, rs, rows, nc);
    e->d = xmalloc(IT_PATH + (sz) rows * (64 * 7 + 1) + 1);
    len = (u32) repack(E, rs, rows, e->d + IT_PATH);
    if (len > 65535) { xfree(E);  xfree(rs);  return -1; }
  }
  xfree(E);  xfree(rs);
  wr16le(hd, len);
  memcpy(e->d, hd, IT_PATH);
  memcpy(p->phd, hd, IT_PATH);
  p->prv = rows;
  e->len = IT_PATH + len;
  return 0;
}

static u32 pscan(ix * x) {
  it_lay * L = &x->L;
  u32 i, S = 0;
  Fi(L->pn,
    const it_ent * e = L->e + it_epat(L, i);
    const u8 * src = x->in + e->at;
    u32 rows, len, j;
    pent * E;
    u32 * rs;
    u8 * o;
    int ne;
    if (e->st != IS_PRI) continue;
    rows = rd16le(src + 2);  len = rd16le(src);
    if (rows > IT_MAXROW) continue;
    E = xmalloc((len + 1) * sizeof *E);
    rs = xmalloc((rows + 1) * sizeof *rs);
    ne = unpack(src + IT_PATH, len, rows, E, rs);
    o = xmalloc((sz) rows * (64 * 7 + 1) + 1);
    if (ne >= 0 && repack(E, rs, rows, o) == len &&
        !memcmp(o, src + IT_PATH, len))
      Fj((u32) ne, S = MAX(S, E[j].ch + 1u));
    xfree(o);  xfree(E);  xfree(rs));
  return S;
}

static int pats(ix * x) {
  mx * m = &x->b;
  zc * z = &x->z->z;
  it_lay * L = &x->L;
  pst * p = pst_new();
  u32 i;
  int bad = 0;
  z->n = 1;
  z->h[0] = ZH2(0x6AA, 0);
  p->S = zsym(m, z, m->dec ? 0 : pscan(x), 7, ZH2(0x6AB, 0), ZH2(0x6AC, 0));
  if (p->S > 64) bad = 1;
  p->pnc = p->S;
  for (i = 0; i < L->pn && !bad; i++) {
    it_ent * e = L->e + it_epat(L, i);
    if (e->st == IS_PRI && ptn(x, p, e)) bad = 1;
  }
  pst_free(p);
  return bad ? -1 : 0;
}
