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

#include "it/itz.h"

#define MAXEV 0x80000u

typedef struct {
  int sb, mw, fa, rb, lim, ad;
  u32 per, mask;
} prm;

static prm getprm(int b16) {
  prm r;
  r.sb = b16 ? 16 : 8;  r.mw = r.sb + 1;  r.fa = b16 ? 4 : 3;
  r.rb = b16 ? 8 : 4;  r.lim = b16 ? 33 : 17;  r.ad = b16 ? 4 : 3;
  r.per = b16 ? 0x4000 : 0x8000;  r.mask = b16 ? 0xFFFF : 0xFF;
  return r;
}

static i32 sext(u32 v, int w) {
  u32 m = 1u << (w - 1);
  v &= (m << 1) - 1;
  return (i32) (v ^ m) - (i32) m;
}

static INLINE int minw(i32 d, const prm * p) {
  u32 a = (u32) (d < 0 ? -d : d), k = (u32) p->rb - (d < 0);
  int w = (int) blr_ilog(a) + 1, v = w + (int) ((a + k) >> (w - 1));
  w = w > 6 ? v : w;
  return w > p->sb ? p->mw : w;
}

static int fits(i32 x, int w, const prm * p) {
  i32 h;
  if (w >= p->mw) return 1;
  h = (i32) 1 << (w - 1);
  if (w <= 6) return x > -h && x < h;
  return x >= -h + p->rb && x < h - p->rb;
}

static void canon_it(const u8 * m, u32 n, const prm * p, u8 * t) {
  u8 * b = t + 1;
  u32 i, run, st, c;
  int dl, lo, hi, mb, ot;
  t[0] = (u8) p->mw;
  memcpy(b, m, n);
  for (dl = 1; dl < p->mw; dl++)
    for (i = 0; i < n; i++) {
      const u8 * q = memchr(b + i, dl, n - i);
      if (!q) break;
      st = (u32) (q - b);
      for (i = st + 1; i < n && b[i] == dl; i++);
      if (i == n) break;
      run = i - st;
      if (run > (u32) p->lim) continue;
      lo = MIN(b[i], t[st]);  hi = MAX(b[i], t[st]);
      if (lo > dl) mb = lo, ot = hi;
      else if (hi < dl) mb = 0, ot = 0;
      else mb = hi, ot = lo;
      c = (u32) (mb + (mb <= 5 ? p->ad : 0) + (ot <= 5 ? p->ad : 0));
      if (mb && (run == 1 || (run <= c && run * (u32) (mb - dl) <= c)))
        memset(b + st, mb, i - st);
    }
}

static int64_t ccost(int w, const prm * p) {
  return w + (w <= 6 ? p->fa : 0);
}

static void canon_grd(const u8 * m, u32 n, const prm * p, u8 * t) {
  u8 * b = t + 1;
  u32 i, run, st;
  int dl, l, r, lo, hi, mb;
  int64_t sv;
  t[0] = (u8) p->mw;
  memcpy(b, m, n);
  for (dl = 1; dl < p->mw; dl++)
    for (i = 0; i < n; i++) {
      const u8 * q = memchr(b + i, dl, n - i);
      if (!q) break;
      st = (u32) (q - b);
      for (i = st + 1; i < n && b[i] == dl; i++);
      run = i - st;
      l = t[st];  r = i < n ? b[i] : 0;
      lo = r ? MIN(l, r) : l;  hi = r ? MAX(l, r) : l;
      mb = lo > dl ? lo : hi > dl ? hi : 0;
      if (!mb) continue;
      sv = (r ? ccost(dl, p) : 0) + (l == mb ? ccost(l, p) : 0)
         - (r && r != mb ? ccost(mb, p) : 0);
      if ((int64_t) run * (mb - dl) < sv) memset(b + st, mb, i - st);
    }
}

typedef struct {
  const u8 * m;
  u8 * w;
  u32 n;
  const prm * p;
} sq;

static int wcs(int w, const prm * p) {
  static const i8 t[17] =
    { 4, 5, 6, 7, 8, 9, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17 };
  return t[w - 1] + (w <= 6 && p->sb == 16);
}

static void canon_mpt(const sq * s, int sw, int lw, int rw, int wd,
                      u32 off, u32 len) {
  u32 i = off, end = off + len, st, bl;
  uint64_t kd, lv;
  int xl, xr, l, ss, ww, c;
  if (wd + 1 < 1) { memset(s->w + off, sw, len); return; }
  while (i < end) {
    if (s->m[i] > wd + 1) { s->w[i++] = (u8) sw; continue; }
    st = i;
    while (i < end && s->m[i] <= wd + 1) i++;
    bl = i - st;
    xl = st == off ? lw : sw;  xr = i == end ? rw : sw;
    l = wcs(xl, s->p);  ss = wcs(sw, s->p);  ww = wcs(wd + 1, s->p);
    if (i == s->n) {
      kd = (uint64_t) l + (uint64_t) (wd + 1) * bl;
      lv = (uint64_t) l + (uint64_t) sw * bl;
      if (xl == sw) lv -= (uint64_t) l;
    } else {
      kd = (uint64_t) l + (uint64_t) (wd + 1) * bl + (uint64_t) ww;
      lv = (uint64_t) l + (uint64_t) sw * bl + (uint64_t) ss;
      if (xl == sw) lv -= (uint64_t) l;
      if (xr == sw) lv -= (uint64_t) ss;
    }
    c = kd <= lv;
    canon_mpt(s, c ? wd + 1 : sw, xl, xr, wd - 1, st, bl);
  }
}

static void canon(int alg, const u8 * m, u32 n, const prm * p, u8 * t) {
  if (alg == ITZ_A_IT) canon_it(m, n, p, t);
  else if (alg == ITZ_A_GRD) canon_grd(m, n, p, t);
  else {
    sq s;
    s.m = m;  s.w = t + 1;  s.n = n;  s.p = p;  t[0] = (u8) p->mw;
    canon_mpt(&s, p->mw, p->mw, p->mw, p->mw - 2, 0, n);
  }
}

static void deltas(const i16 * x, u32 n, const prm * p, int it215,
                   i32 * d, u8 * m) {
  u32 k, px = 0, pd = 0, cx, d1;
  for (k = 0; k < n; k++) {
    cx = (u32) x[k] & p->mask;  d1 = (cx - px) & p->mask;
    d[k] = sext(it215 ? d1 - pd : d1, p->sb);
    m[k] = (u8) minw(d[k], p);
    pd = d1;  px = cx;
  }
}

static int evsame(const u8 * t, u32 n, const itz_ev * e, u32 ne, int mw) {
  u32 i, j = 0;
  int w = mw;
  for (i = 0; i < n; i++)
    if (t[i + 1] != w) {
      w = t[i + 1];
      if (j >= ne || e[j].pos != i || e[j].w != w) return 0;
      j++;
    }
  return j == ne;
}

static u32 evpre(const u8 * t, u32 n, const itz_ev * e, u32 ne, int mw) {
  u32 i, j = 0;
  int w = mw;
  for (i = 0; i < n && j < ne; i++)
    if (t[i + 1] != w) {
      w = t[i + 1];
      if (e[j].pos != i || e[j].w != w) break;
      j++;
    }
  return j;
}

static void * grow(void * p, u32 * cap, u32 need, sz es) {
  u32 c = *cap ? *cap : 16;
  void * q;
  if (need <= *cap) return p;
  while (c < need) c *= 2;
  q = xrealloc(p, (sz) c * es);
  *cap = c;
  return q;
}

void itz_init(itz_side * si) { memset(si, 0, sizeof (*si)); }

void itz_free(itz_side * si) {
  xfree(si->blk);  xfree(si->ev);  xfree(si->tb);
  itz_init(si);
}

static int setup(itz_side * si, u32 frames, int ch, int b16,
                 int it215, u32 * nexp) {
  prm p = getprm(b16);
  uint64_t e = ((uint64_t) frames + p.per - 1) / p.per * (uint64_t) ch;
  itz_free(si);
  if (ch < 1 || ch > 2 || e > 0x10000000u) return -1;
  si->frames = frames;  si->ch = ch;  si->b16 = !!b16;
  si->it215 = !!it215;  *nexp = (u32) e;
  si->ev = xmalloc(16 * sizeof (itz_ev));
  si->cev = 16;
  if (e) {
    si->blk = xcalloc((sz) e, sizeof (itz_blk));
    si->cblk = (u32) e;
  }
  return 0;
}

static INLINE u32 rdb(const u8 * s, u32 avail, uint64_t pos, int w) {
  const u8 * q = s + (pos >> 3);
  int sh = (int) (pos & 7), nb = (sh + w + 7) >> 3, i;
  u32 x = 0;
  if ((pos >> 3) + 4 <= avail) x = rd32le(q);
  else Fi(nb, x |= (u32) q[i] << (8 * i));
  return (x >> sh) & ((1u << w) - 1);
}

static int addev(itz_side * si, u32 pos, int w) {
  itz_ev * e;
  if (si->nev >= MAXEV * 64u) return -1;
  e = grow(si->ev, &si->cev, si->nev + 1, sizeof (itz_ev));
  si->ev = e;
  e[si->nev].pos = pos;  e[si->nev].w = (u8) w;
  si->nev++;
  return 0;
}

static int64_t decblk(const u8 * s, u32 avail, u32 bn,
                      const prm * p, int it215, i16 * out,
                      u8 * m, itz_side * si, itz_blk * b) {
  uint64_t lim = (uint64_t) avail * 8, pos = 0;
  u32 k = 0, v, top, d1 = 0, d2 = 0, e, c;
  int w = p->mw, nw;
  i32 x;
  while (k < bn) {
    if (pos + (uint64_t) w > lim) break;
    v = rdb(s, avail, pos, w);  top = 1u << (w - 1);
    if (w <= 6) {
      if (v == top) {
        if (pos + (uint64_t) (w + p->fa) > lim) break;
        nw = (int) rdb(s, avail, pos + (uint64_t) w, p->fa) + 1;
        if (nw >= w) nw++;
        if (addev(si, k, nw)) return -1;
        pos += (uint64_t) (w + p->fa);  w = nw;
        continue;
      }
      x = sext(v, w);
    } else if (w < p->mw) {
      if (v >= top - (u32) p->rb && v < top + (u32) p->rb) {
        nw = (int) (v - (top - (u32) p->rb)) + 1;
        if (nw >= w) nw++;
        if (addev(si, k, nw)) return -1;
        pos += (uint64_t) w;  w = nw;
        continue;
      }
      x = sext(v, w);
    } else {
      if (v & top) {
        c = v & (top - 1);
        if (c + 1 > (u32) p->mw) break;
        if (addev(si, k, (int) c + 1)) return -1;
        pos += (uint64_t) w;  w = (int) c + 1;
        continue;
      }
      x = (i32) v;
    }
    pos += (uint64_t) w;
    m[k] = (u8) minw(sext((u32) x, p->sb), p);
    d1 += (u32) x;  d2 += d1;
    out[k] = (i16) sext(it215 ? d2 : d1, p->sb);
    k++;
  }
  b->nval = k;
  e = (u32) ((pos + 7) >> 3);
  b->xb = avail - e;
  b->pad = e > (pos >> 3) ? (u32) s[pos >> 3] >> (pos & 7) : 0;
  return (int64_t) pos;
}

typedef struct {
  i32 * d;
  u8 * m, * t, * mt;
  u32 bpc;
  const i16 * pcm;
  prm p;
} scr;

static int tryalg(int a, const itz_blk * b, const itz_side * si, scr * c) {
  canon(a, c->m, b->nval, &c->p, c->t);
  return evsame(c->t, b->nval, si->ev + b->ev0, b->nev, c->p.mw);
}

static int blkok(const itz_side * si, scr * c, u32 j, int a) {
  if (!(c->mt[j] >> a & 1)) {
    const itz_blk * b = si->blk + j;
    u32 off = j % c->bpc * c->p.per;
    deltas(c->pcm + (sz) (j / c->bpc) * si->frames + off, b->nval,
           &c->p, si->it215, c->d, c->m);
    c->mt[j] |= (u8) (1u << a);
    if (tryalg(a, b, si, c)) c->mt[j] |= (u8) (16u << a);
  }
  return c->mt[j] >> (4 + a) & 1;
}

static void pickalg(itz_side * si, scr * c, const u32 * cnt) {
  u32 j, tot[ITZ_A_NONE], best = 0;
  int a, na = 0;
  for (a = 0; a < ITZ_A_NONE; a++) {
    si->mcnt[a] = cnt[a];
    tot[a] = cnt[a];
    na += cnt[a] > 0;
  }
  if (na > 1)
    for (a = 0; a < ITZ_A_NONE; a++) if (cnt[a])
      for (j = 0, tot[a] = 0; j < si->nblk; j++)
        tot[a] += (u32) blkok(si, c, j, a);
  for (a = 1; a < ITZ_A_NONE; a++) if (tot[a] > tot[best]) best = (u32) a;
  si->alg = tot[best] || !si->nblk ? (int) best : ITZ_A_NONE;
}

int itz_decode(const u8 * s, sz n, u32 frames, int ch, int b16, int it215,
               i16 * pcm, itz_side * si) {
  scr c;
  u32 nexp, j, cnt[ITZ_A_NONE], ne, k;
  sz q = 0;
  int a, i, pref = ITZ_A_IT, r = -1;
  if (setup(si, frames, ch, b16, it215, &nexp)) return -1;
  memset(cnt, 0, sizeof (cnt));
  c.p = getprm(b16);  c.bpc = nexp / (u32) ch;  c.pcm = pcm;
  c.d = xmalloc(c.p.per * sizeof (i32));
  c.m = xmalloc(c.p.per);
  c.t = xmalloc(c.p.per + 1);
  c.mt = xcalloc((sz) nexp + 1, 1);
  for (j = 0; j < nexp; j++) {
    u32 off = j % c.bpc * c.p.per;
    u32 bn = MIN(c.p.per, frames - off), len, avail, ev0 = si->nev;
    i16 * o = pcm + (sz) (j / c.bpc) * frames + off, h;
    itz_blk * b = si->blk + j;
    int64_t pos;
    if (q + 2 > n) break;
    len = (u32) s[q] | (u32) s[q + 1] << 8;
    q += 2;
    avail = (u32) MIN((sz) len, n - q);
    pos = decblk(s + q, avail, bn, &c.p, it215, o, c.m, si, b);
    if (pos < 0) goto out;
    b->len = len;  b->ev0 = ev0;  b->nev = si->nev - ev0;
    b->flag = (u8) ((b->nval < bn ? ITZ_F_PART : 0)
            | (avail < len ? ITZ_F_TRUNC : 0)
            | (b->xb || b->pad ? ITZ_F_TAIL : 0));
    if (b->xb) {
      u8 * nt = grow(si->tb, &si->ctb, si->ntb + b->xb, 1);
      si->tb = nt;  b->tb0 = si->ntb;  b->ntb = b->xb;
      memcpy(nt + si->ntb, s + q + avail - b->xb, b->xb);
      si->ntb += b->xb;
    }
    for (i = 0; i < ITZ_A_NONE; i++) {
      a = i ? (i <= pref ? i - 1 : i) : pref;
      c.mt[j] |= (u8) (1u << a);
      if (tryalg(a, b, si, &c)) {
        c.mt[j] |= (u8) (16u << a);  cnt[a]++;  pref = a;
        break;
      }
    }
    h = b->nval ? o[b->nval - 1] : off ? o[-1] : 0;
    for (k = b->nval; k < bn; k++) o[k] = h;
    q += avail;
  }
  si->nblk = j;  si->miss = nexp - j;  si->used = q;
  for (; j < nexp; j++) {
    u32 off = j % c.bpc * c.p.per;
    u32 bn = MIN(c.p.per, frames - off);
    i16 * o = pcm + (sz) (j / c.bpc) * frames + off;
    i16 h = off ? o[-1] : 0;
    Fk(bn, o[k] = h);
  }
  pickalg(si, &c, cnt);
  ne = 0;
  for (j = 0; j < si->nblk; j++) {
    itz_blk * b = si->blk + j;
    if (si->alg != ITZ_A_NONE && blkok(si, &c, j, si->alg)) {
      b->nev = 0;  b->ev0 = ne;
      continue;
    }
    b->flag |= ITZ_F_EV;
    if (si->alg != ITZ_A_NONE) {
      deltas(pcm + (sz) (j / c.bpc) * frames + j % c.bpc * c.p.per,
             b->nval, &c.p, it215, c.d, c.m);
      canon(si->alg, c.m, b->nval, &c.p, c.t);
      b->npre = evpre(c.t, b->nval, si->ev + b->ev0, b->nev, c.p.mw);
      b->ev0 += b->npre;  b->nev -= b->npre;
    }
    if (b->nev)
      memmove(si->ev + ne, si->ev + b->ev0, b->nev * sizeof (itz_ev));
    b->ev0 = ne;  ne += b->nev;
  }
  si->nev = ne;
  for (j = 0; j < si->nblk; j++) if (!(si->blk[j].flag & ITZ_F_TRUNC))
    si->blk[j].len = 0;
  r = 0;
out:
  xfree(c.d);  xfree(c.m);  xfree(c.t);  xfree(c.mt);
  if (r) itz_free(si);
  return r;
}

typedef struct {
  u8 * b;
  sz n, cap;
  uint64_t acc;
  int na;
} bw;

static void bres(bw * o, sz k) {
  sz c = o->cap ? o->cap : 4096;
  if (o->n + k <= o->cap) return;
  while (c < o->n + k) c *= 2;
  o->b = xrealloc(o->b, c);  o->cap = c;
}

static void bbyte(bw * o, u8 v) {
  bres(o, 1);
  o->b[o->n++] = v;
}

static INLINE void bput(bw * o, u32 v, int w) {
  o->acc |= (uint64_t) (v & ((1u << w) - 1)) << o->na;
  o->na += w;
  if (o->na >= 32) {
    wr32le(o->b + o->n, (u32) o->acc);
    o->n += 4;  o->acc >>= 32;  o->na -= 32;
  }
}

static void bflush(bw * o) {
  while (o->na >= 8) {
    o->b[o->n++] = (u8) o->acc;
    o->acc >>= 8;  o->na -= 8;
  }
}

static int chg(bw * o, int w, int nw, const prm * p) {
  u32 top = 1u << (w - 1), cd = (u32) (nw - 1 - (nw > w));
  if (nw < 1 || nw > p->mw) return -1;
  if (w <= 6) {
    if (nw == w) return -1;
    bput(o, top, w);  bput(o, cd, p->fa);
  } else if (w < p->mw) {
    if (nw == w) return -1;
    bput(o, top - (u32) p->rb + cd, w);
  } else bput(o, top | (u32) (nw - 1), w);
  return 0;
}

static int encblk(bw * o, const i16 * x, u32 bn, const prm * p,
                  const itz_side * si, const itz_blk * b, i32 * d,
                  u8 * m, u8 * t, itz_ev ** te, u32 * tc) {
  u32 nv = b->flag & ITZ_F_PART ? b->nval : bn, k, ne, ei = 0;
  u32 h = (u32) o->n, av;
  const itz_ev * ev;
  int w = p->mw;
  if (nv > bn || ((b->flag & ITZ_F_PART) && nv == bn)) return -1;
  deltas(x, nv, p, si->it215, d, m);
  if ((b->flag & ITZ_F_EV) && !b->npre) {
    ev = si->ev + b->ev0;  ne = b->nev;
  } else {
    itz_ev * e;
    u32 nx = b->flag & ITZ_F_EV ? b->nev : 0;
    if (si->alg == ITZ_A_NONE) return -1;
    canon(si->alg, m, nv, p, t);
    for (k = 0, ne = 0; k < nv; k++) ne += t[k + 1] != t[k];
    e = grow(*te, tc, ne + nx + 1, sizeof (itz_ev));
    *te = e;
    for (k = 0, ne = 0; k < nv; k++) if (t[k + 1] != t[k])
      e[ne].pos = k, e[ne++].w = t[k + 1];
    if (b->flag & ITZ_F_EV) {
      if (b->npre > ne) return -1;
      ne = b->npre;
      for (k = 0; k < nx; k++) {
        e[ne] = si->ev[b->ev0 + k];
        if (ne && e[ne].pos < e[ne - 1].pos) return -1;
        ne++;
      }
    }
    ev = e;
  }
  bbyte(o, 0);  bbyte(o, 0);
  bres(o, (sz) (nv + ne) * 17 / 8 + 8);
  o->acc = 0;  o->na = 0;
  for (k = 0; k <= nv; k++) {
    while (ei < ne && ev[ei].pos == k) {
      if (chg(o, w, ev[ei].w, p)) return -1;
      w = ev[ei++].w;
    }
    if (k == nv) break;
    if (!fits(d[k], w, p)) return -1;
    bput(o, (u32) d[k] & (w >= p->mw ? p->mask : ~0u), w);
  }
  if (ei < ne) return -1;
  bflush(o);
  if (o->na) {
    if (b->pad >> (8 - o->na)) return -1;
    bput(o, b->pad, 8 - o->na);
    bflush(o);
  } else if (b->pad) return -1;
  if (b->xb != b->ntb) return -1;
  Fk(b->xb, bbyte(o, si->tb[b->tb0 + k]));
  av = (u32) (o->n - h - 2);
  if (b->flag & ITZ_F_TRUNC) {
    if (b->len <= av || b->len > 0xFFFF) return -1;
    av = b->len;
  } else if (av > 0xFFFF) return -1;
  o->b[h] = (u8) av;  o->b[h + 1] = (u8) (av >> 8);
  return 0;
}

u8 * itz_encode(const i16 * pcm, const itz_side * si, sz * outn) {
  prm p = getprm(si->b16);
  u32 bpc, j, tc = 0;
  i32 * d = xmalloc(p.per * sizeof (i32));
  u8 * t = xmalloc(p.per + 1), * m = xmalloc(p.per);
  itz_ev * te = NULL;
  bw o;
  int r = -1;
  memset(&o, 0, sizeof (o));
  bpc = (u32) ((si->frames + (uint64_t) p.per - 1) / p.per);
  if (si->nblk + si->miss != bpc * (u32) si->ch) goto out;
  for (j = 0; j < si->nblk; j++) {
    u32 c = j / bpc, off = j % bpc * p.per;
    u32 bn = MIN(p.per, si->frames - off);
    const i16 * x = pcm + (sz) c * si->frames + off;
    const itz_blk * b = si->blk + j;
    if ((b->flag & ITZ_F_TRUNC) && j + 1 != si->nblk) goto out;
    if (encblk(&o, x, bn, &p, si, b, d, m, t, &te, &tc)) goto out;
  }
  r = 0;
out:
  xfree(d);  xfree(t);  xfree(m);  xfree(te);
  if (r) { xfree(o.b); return NULL; }
  *outn = o.n;
  if (!o.b) o.b = xmalloc(1);
  return o.b;
}

int itz_side_put(const itz_side * si, const itz_io * io) {
  u32 j, k, pv;
  io->put(io->u, ITZ_K_ALG, (u32) si->alg);
  io->put(io->u, ITZ_K_MISS, si->miss);
  for (j = 0; j < si->nblk; j++) {
    const itz_blk * b = si->blk + j;
    io->put(io->u, ITZ_K_FLAG, b->flag);
    if (b->flag & ITZ_F_PART) io->put(io->u, ITZ_K_NVAL, b->nval);
    if (b->flag & ITZ_F_EV) {
      if (si->alg != ITZ_A_NONE) io->put(io->u, ITZ_K_NPRE, b->npre);
      io->put(io->u, ITZ_K_NEV, b->nev);
      for (k = 0, pv = 0; k < b->nev; k++) {
        const itz_ev * e = si->ev + b->ev0 + k;
        io->put(io->u, ITZ_K_GAP, e->pos - pv);
        io->put(io->u, ITZ_K_WID, e->w);
        pv = e->pos;
      }
    }
    if (b->flag & ITZ_F_TRUNC) io->put(io->u, ITZ_K_LEN, b->len);
    if (b->flag & ITZ_F_TAIL) {
      io->put(io->u, ITZ_K_XB, b->xb);
      io->put(io->u, ITZ_K_PAD, b->pad);
      for (k = 0; k < b->xb; k++)
        io->put(io->u, ITZ_K_TBYTE, si->tb[b->tb0 + k]);
    }
  }
  return 0;
}

int itz_side_get(itz_side * si, u32 frames, int ch, int b16,
                 int it215, const itz_io * io) {
  prm p = getprm(b16);
  u32 nexp, j, k, v, pv, bpc;
  if (setup(si, frames, ch, b16, it215, &nexp)) return -1;
  bpc = nexp / (u32) ch;
  if (io->get(io->u, ITZ_K_ALG, &v) || v >= ITZ_A_N) goto bad;
  si->alg = (int) v;
  if (io->get(io->u, ITZ_K_MISS, &v) || v > nexp) goto bad;
  si->miss = v;  si->nblk = nexp - v;
  for (j = 0; j < si->nblk; j++) {
    itz_blk * b = si->blk + j;
    u32 bn = MIN(p.per, frames - j % bpc * p.per);
    if (io->get(io->u, ITZ_K_FLAG, &v) || v > 15) goto bad;
    b->flag = (u8) v;
    if (b->flag & ITZ_F_PART) {
      if (io->get(io->u, ITZ_K_NVAL, &b->nval) || b->nval >= bn) goto bad;
    } else b->nval = bn;
    b->ev0 = si->nev;
    if (b->flag & ITZ_F_EV) {
      if (si->alg != ITZ_A_NONE && (io->get(io->u, ITZ_K_NPRE, &b->npre)
          || b->npre > MAXEV)) goto bad;
      if (io->get(io->u, ITZ_K_NEV, &b->nev) || b->nev > MAXEV) goto bad;
      for (k = 0, pv = 0; k < b->nev; k++) {
        u32 g, w;
        if (io->get(io->u, ITZ_K_GAP, &g) || g > b->nval - pv) goto bad;
        if (io->get(io->u, ITZ_K_WID, &w) || w < 1 || w > (u32) p.mw)
          goto bad;
        pv += g;
        if (addev(si, pv, (int) w)) goto bad;
      }
    }
    if (b->flag & ITZ_F_TRUNC) {
      if (j + 1 != si->nblk || io->get(io->u, ITZ_K_LEN, &b->len)
          || b->len > 0xFFFF) goto bad;
    }
    if (b->flag & ITZ_F_TAIL) {
      u8 * nt;
      if (io->get(io->u, ITZ_K_XB, &b->xb) || b->xb > 0xFFFF) goto bad;
      if (io->get(io->u, ITZ_K_PAD, &b->pad) || b->pad > 0xFF) goto bad;
      nt = grow(si->tb, &si->ctb, si->ntb + b->xb + 1, 1);
      si->tb = nt;  b->tb0 = si->ntb;  b->ntb = b->xb;
      for (k = 0; k < b->xb; k++) {
        if (io->get(io->u, ITZ_K_TBYTE, &v) || v > 0xFF) goto bad;
        nt[si->ntb++] = (u8) v;
      }
    }
  }
  return 0;
bad:
  itz_free(si);
  return -1;
}
