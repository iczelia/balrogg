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

enum { XE_HDR, XE_PAT, XE_INS, XE_SHD, XE_SMP, XE_END };

typedef struct {
  u32 ver, ch, np, ni, pi, ii, shp, sd, nsh;
  int hd;
  sz * sh, scap;
} xw;

typedef struct {
  pst * p;
  u32 hl, pt, is, pm, ph;
  u8 pl[32];
} xst;

static int xnext(const xw * w) {
  const int o = w->ver < 0x104;
  if (!w->hd) return XE_HDR;
  if (!o && w->pi < w->np) return XE_PAT;
  if (w->shp) return XE_SHD;
  if (!o && w->sd < w->nsh) return XE_SMP;
  if (w->ii < w->ni) return XE_INS;
  if (w->pi < w->np) return XE_PAT;
  return w->sd < w->nsh ? XE_SMP : XE_END;
}

static void xstep(xw * w, int k, const u8 * t, sz pos, sz len) {
  u32 i;
  switch (k) {
  case XE_HDR:
    w->hd = 1;  w->ver = rd16le(t + 58);  w->ch = rd16le(t + 68);
    w->np = rd16le(t + 70);  w->ni = rd16le(t + 72);
    break;
  case XE_PAT: w->pi++;  break;
  case XE_INS:
    w->ii++;
    w->shp = len >= 29 ? rd16le(t + 27) : 0;
    break;
  case XE_SHD:
    w->sh = xgrow(w->sh, &w->scap, w->nsh + w->shp + 1, sizeof *w->sh, 64);
    Fi(w->shp, w->sh[w->nsh++] = pos + (sz) XM_SHD * i);
    w->shp = 0;
    break;
  case XE_SMP: w->sd++;  break;
  }
}

static uint64_t xsl(const u8 * s) {
  uint64_t l = rd32le(s);
  return s[17] == 0xAD && !(s[14] & 16) ? 16 + (l + 1) / 2 : l;
}

static int xlen(const xw * w, int k, const u8 * b, sz n, sz pos,
                uint64_t * len) {
  const sz r = n - pos, ph = w->ver == 0x102 ? 8 : 9;
  const u8 * t = b + pos;
  uint64_t v;
  switch (k) {
  case XE_HDR:
    if (r < XM_HDR || rd32le(t + 60) < 20) return -1;
    *len = 60 + (uint64_t) rd32le(t + 60);
    return 0;
  case XE_PAT:
    if (r < ph || rd32le(t) < ph) return -1;
    *len = (uint64_t) rd32le(t) + rd16le(t + ph - 2);
    return 0;
  case XE_INS:
    if (r < 4) return -1;
    v = rd32le(t);
    *len = v < 4 ? XM_INS : v;
    return 0;
  case XE_SHD:
    *len = (uint64_t) w->shp * XM_SHD;
    return 0;
  }
  *len = xsl(b + w->sh[w->sd]);
  return 0;
}

static sz xscan(const u8 * b, sz n) {
  xw w;
  sz pos = 0, ns = 0;
  uint64_t len;
  int k;
  memset(&w, 0, sizeof w);
  while ((k = xnext(&w)) != XE_END && !xlen(&w, k, b, n, pos, &len)) {
    if (len > n - pos) {
      if (k == XE_SMP) ns += n - pos;
      break;
    }
    if (k == XE_SMP) ns += (sz) len;
    xstep(&w, k, b + pos, pos, (sz) len);
    pos += (sz) len;
  }
  xfree(w.sh);
  return n - ns;
}

static void xzr(ix * x, u8 * t, sz n, u32 id) {
  zc * z = &x->z->z;
  mx * m = &x->b;
  sz j;
  int nz = 0;
  if (!n) return;
  if (!m->dec) Fj(n, nz |= t[j]);
  z->n = 1;
  z->h[0] = ZH3(id, 0x2000, 0);
  if (zflag(m, z, !nz, ZH3(id, 0x2001, 0), ZH3(id, 0x2002, 0))) {
    memset(t, 0, n);
    return;
  }
  zbytes(x, t, n, id);
}

static int xhdr(ix * x, u8 * t, sz r, uint64_t * len) {
  u32 h, nb, no, so;
  if (r < XM_HDR) return -1;
  rec(x, RK_XH, t, XM_HDR, 0);
  h = rd32le(t + 60);
  if (h < 20 || h > r - 60) return -1;
  nb = h - 20;  no = MIN(nb, 256);
  so = MIN(rd16le(t + 64), no);
  ords(x, t + XM_HDR, so);
  xzr(x, t + XM_HDR + so, no - so, 0x910);
  xzr(x, t + XM_HDR + no, nb - no, 0x920);
  *len = 60 + (uint64_t) h;
  return 0;
}

static int xunp(const u8 * d, sz len, u32 R, u32 S, pent * E, u32 * rs,
                u8 * pk) {
  sz q = 0;
  u32 ne = 0, r, c;
  for (r = 0; r < R; r++) {
    rs[r] = ne;
    for (c = 0; c < S; c++) {
      u8 f[5], b;
      int j;
      if (q >= len) return -1;
      b = d[q++];
      if (b & 0x80) {
        Fj(5, f[j] = 0;
              if (b >> j & 1) { if (q >= len) return -1;
                                f[j] = d[q++]; });
      } else {
        if (len - q < 4) return -1;
        f[0] = b;  memcpy(f + 1, d + q, 4);  q += 4;
        b = 0;
      }
      pk[(sz) r * S + c] = b;
      if (f[0] | f[1] | f[2] | f[3] | f[4]) {
        pent * y = E + ne++;
        memset(y, 0, sizeof *y);
        y->ch = (u8) c;
        y->has = (u8) ((f[0] != 0) | (f[1] != 0) << 1 | (f[2] != 0) << 2 |
                       (f[3] != 0 || f[4] != 0) << 3);
        memcpy(y->f, f, 5);
      }
    }
  }
  rs[R] = ne;
  return q == len ? (int) ne : -1;
}

static sz xrep(const pent * E, const u32 * rs, u32 R, u32 S, const u8 * pk,
               u8 * o) {
  sz q = 0;
  u32 r, c, k;
  for (r = 0; r < R; r++)
    for (c = 0, k = rs[r]; c < S; c++) {
      u8 f[5] = { 0, 0, 0, 0, 0 }, v = pk[(sz) r * S + c];
      int j;
      if (k < rs[r + 1] && E[k].ch == c) memcpy(f, E[k++].f, 5);
      if (!v) { memcpy(o + q, f, 5);  q += 5;  continue; }
      o[q++] = v;
      Fj(5, if (v >> j & 1) o[q++] = f[j]);
    }
  return q;
}

static void xpks(ix * x, xst * s, const pent * E, const u32 * rs, u32 R,
                 u8 * pk) {
  zc * z = &x->z->z;
  mx * m = &x->b;
  const u32 S = s->p->S;
  u32 r, c, k;
  for (r = 0; r < R; r++)
    for (c = 0, k = rs[r]; c < S; c++) {
      u8 f[5] = { 0, 0, 0, 0, 0 };
      u32 m5 = 0, c1, a, v = m->dec ? 0 : pk[(sz) r * S + c];
      int j, eq, ae = 0;
      if (k < rs[r + 1] && E[k].ch == c) memcpy(f, E[k++].f, 5);
      Fj(5, m5 |= (u32) (f[j] != 0) << j);
      c1 = m5 == 31 ? 0 : 0x80 | m5;
      a = s->pl[m5];
      z->n = 3;
      z->h[0] = ZH3(0x931, m5, s->ph & 3);
      z->h[1] = ZH2(0x932, s->ph & 255);
      z->h[2] = ZH3(0x933, a, c1);
      eq = zflag(m, z, v == c1, ZH2(0x934, s->ph & 1), ZH2(0x935, m5 == 31));
      if (eq) v = c1;
      else {
        if (a != c1 && (a || f[0] < 0x80)) {
          z->n = 2;
          z->h[0] = ZH2(0x936, m5);
          z->h[1] = ZH2(0x937, a);
          ae = zflag(m, z, v == a, ZH2(0x938, 0), ZH2(0x939, 0));
        }
        if (ae) v = a;
        else {
          z->n = 2;
          z->h[0] = ZH2(0x93A, m5);
          z->h[1] = ZH2(0x93B, a);
          v = zsym(m, z, v, 8, ZH2(0x93C, 0), ZH2(0x93D, 0));
        }
        s->pl[m5] = (u8) v;
      }
      s->ph = s->ph << 1 | (u32) !eq;
      pk[(sz) r * S + c] = (u8) v;
    }
}

static int xcells(ix * x, xst * s, u8 * d, sz avail, u32 R, u8 * pk,
                  pent * E, u32 * rs, u32 * ps) {
  pst * p = s->p;
  const u32 S = p->S;
  u32 nc = 0, i;
  u8 * o;
  sz len;
  if (!x->b.dec) Fi(rs[R], nc = MAX(nc, E[i].ch + 1u));
  nc = znum(x, nc, p->pnc, 7, 0x950);
  if (nc > S) return -1;
  p->pnc = nc;
  psrc(x, p, E, rs, R);
  pcells(x, p, E, rs, R, nc);
  xpks(x, s, E, rs, R, pk);
  o = xmalloc((sz) R * S * 6 + 1);
  len = xrep(E, rs, R, S, pk, o);
  if (len > 65535 || len > avail) { xfree(o);  return -1; }
  memcpy(d, o, len);
  xfree(o);
  *ps = (u32) len;
  return 0;
}

static int xpat(ix * x, xst * s, const xw * w, u8 * t, sz r,
                uint64_t * len) {
  zc * z = &x->z->z;
  mx * m = &x->b;
  pst * p = s->p;
  const u32 ph = w->ver == 0x102 ? 8 : 9, S = p->S;
  u32 hl, R, ps = 0, mode = 1;
  pent * E = NULL;
  u32 * rs = NULL;
  u8 * pk = NULL;
  int bad = 0;
  if (r < ph) return -1;
  hl = znum(x, rd32le(t), s->hl, 32, 0x940);
  s->hl = hl;
  wr32le(t, hl);
  if (hl < ph || hl > r) return -1;
  z->n = 1;
  z->h[0] = ZH2(0x941, s->pt);
  t[4] = (u8) zsym(m, z, t[4], 8, ZH2(0x942, 0), ZH2(0x943, 0));
  s->pt = t[4];
  if (ph == 8) {
    t[5] = (u8) znum(x, t[5], p->prv, 8, 0x944);
    p->prv = t[5];  R = t[5] + 1u;
  } else {
    R = znum(x, rd16le(t + 5), p->prv, 16, 0x944);
    wr16le(t + 5, R);  p->prv = R;
  }
  zbytes(x, t + ph, hl - ph, 0x948);
  if (!m->dec) {
    ps = rd16le(t + ph - 2);
    if (S && R <= IT_MAXROW && ps) {
      E = xmalloc(((sz) R * S + 1) * sizeof *E);
      rs = xmalloc((R + 1) * sizeof *rs);
      pk = xmalloc((sz) R * S + 1);
      mode = xunp(t + hl, ps, R, S, E, rs, pk) < 0;
    }
  }
  z->n = 2;
  z->h[0] = ZH2(0x945, s->pm);
  z->h[1] = ZH2(0x946, S && R <= IT_MAXROW);
  mode = (u32) zflag(m, z, (int) mode, ZH2(0x947, 0), ZH2(0x949, 0));
  s->pm = mode;
  if (mode) {
    ps = znum(x, ps, 0, 16, 0x94A);
    if (ps > r - hl) bad = 1;
    else zbytes(x, t + hl, ps, 0x960);
  } else if (!S || R > IT_MAXROW) bad = 1;
  else {
    if (m->dec) {
      E = xmalloc(((sz) R * S + 1) * sizeof *E);
      rs = xmalloc((R + 1) * sizeof *rs);
      pk = xmalloc((sz) R * S + 1);
    }
    bad = xcells(x, s, t + hl, r - hl, R, pk, E, rs, &ps);
  }
  xfree(E);  xfree(rs);  xfree(pk);
  if (bad) return -1;
  wr16le(t + ph - 2, ps);
  *len = (uint64_t) hl + ps;
  return 0;
}

static int xins(ix * x, xst * s, u8 * t, sz r, u32 idx, uint64_t * len) {
  u32 v;
  sz l;
  if (r < 4) return -1;
  v = znum(x, rd32le(t), s->is, 32, 0x970);
  s->is = v;
  wr32le(t, v);
  l = v < 4 ? XM_INS : v;
  if (l > r) return -1;
  rec(x, RK_XI, t, MIN(l, XM_INS), idx);
  if (l > XM_INS) zbytes(x, t + XM_INS, l - XM_INS, 0x978);
  *len = l;
  return 0;
}

static int xsmp(ix * x, const u8 * s, u8 * t, sz r, uint64_t * len,
                int * cl) {
  mx * m = &x->b;
  const uint64_t full = xsl(s);
  const sz l = full > r ? r : (sz) full;
  const int b16 = s[14] >> 4 & 1, bp = b16 + 1;
  sz nv = l / (sz) bp, fr, i;
  uint64_t fv = full / (uint64_t) bp;
  i16 * p;
  *len = l;  *cl = full > r;
  if (s[17] == 0xAD && !b16) {
    sec(x, S_OPQ);
    x->sraw[S_OPQ] += l;
    gbytes(m, t, l, G_OPQ);
    return 0;
  }
  fr = s[14] & 32 && fv >= 2 ? (sz) MIN(fv / 2, (uint64_t) nv) : nv;
  p = xmalloc(nv * 2 + 2);
  if (!m->dec) {
    u32 a = 0;
    Fi(nv,
      if (!(i % fr)) a = 0;
      a += b16 ? rd16le(t + 2 * i) : t[i];
      p[i] = b16 ? (i16) (u16) a : (i16) (i8) (u8) a);
  }
  chans(x, m->dec ? NULL : p, m->dec ? p : NULL, nv, fr, b16);
  if (m->dec) {
    u32 a = 0;
    Fi(nv,
      u32 v = b16 ? (u16) p[i] : (u8) p[i];
      if (!(i % fr)) a = 0;
      if (b16) wr16le(t + 2 * i, v - a);
      else t[i] = (u8) (v - a);
      a = v);
  }
  xfree(p);
  if (nv * (sz) bp < l) gbytes(m, t + nv * bp, l - nv * bp, G_ODD);
  return 0;
}

static int xm_code(ix * x, u8 * o) {
  zc * z;
  mx * m = &x->b;
  const sz n = x->L.n;
  static const u8 SK[XE_END] = { S_HDR, S_PAT, S_INS, S_SHD, S_S8 };
  xw w;
  xst s;
  sz pos = 0;
  int k, pk = IK_N, bad = 0, cl = 0;
  ist_tbn(x, m->dec ? 0 : xscan(o, n));
  z = &x->z->z;
  memset(&w, 0, sizeof w);
  memset(&s, 0, sizeof s);
  s.p = pst_new();
  s.pl[31] = 0x9F;
  while (!bad && !cl && (k = xnext(&w)) != XE_END) {
    const sz r = n - pos;
    uint64_t len = 0;
    int ok = 1;
    u8 * t = o + pos;
    u32 i;
    if (!m->dec)
      ok = !xlen(&w, k, o, n, pos, &len) && (k == XE_SMP || len <= r);
    sec(x, S_LAY);
    z->n = 2;
    z->h[0] = ZH2(0x901, k);
    z->h[1] = ZH3(0x902, k, !r);
    if (!zflag(m, z, ok, ZH2(0x903, k), ZH2(0x904, 0))) break;
    if (k != XE_SMP) {
      sec(x, SK[k]);
      x->sraw[SK[k]] += (sz) MIN(len, r);
    }
    switch (k) {
    case XE_HDR:
      bad = xhdr(x, t, r, &len);
      if (!bad) {
        s.p->S = rd16le(t + 68) <= 64 ? rd16le(t + 68) : 0;
        s.p->pnc = s.p->S;
      }
      break;
    case XE_PAT: bad = xpat(x, &s, &w, t, r, &len);  break;
    case XE_INS: bad = xins(x, &s, t, r, w.ii, &len);  break;
    case XE_SHD:
      if ((uint64_t) w.shp * XM_SHD > r) bad = 1;
      else Fi(w.shp, rec(x, RK_XS, t + XM_SHD * (sz) i, XM_SHD, i));
      len = (uint64_t) w.shp * XM_SHD;
      break;
    default: bad = xsmp(x, o + w.sh[w.sd], t, r, &len, &cl);
    }
    if (bad) break;
    xstep(&w, k, t, pos, (sz) len);
    pos += (sz) len;
    pk = k;
  }
  if (!bad) {
    sec(x, S_LAY);
    x->sraw[S_LAY] += n - pos;
    zgap(x, o + pos, n - pos, 0, (u32) pk, IK_N);
  }
  sec(x, S_HDR);
  pst_free(s.p);
  xfree(w.sh);
  return bad ? -1 : 0;
}
