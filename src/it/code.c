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

#include "it/ii.h"

enum { TI_NUM = 1, TI_STR, TI_GAP, TI_PAT };
enum {
  F_ST = 1, F_AL, F_RAW, F_OK, F_OF, F_OR, F_GAP, F_CLIP, F_CLN, F_ROWS,
  F_PMODE, F_PLEN, F_NC, F_PRES, F_HAS, F_NOTE, F_INS, F_VOL, F_CMD, F_PAR,
  F_CAN, F_MP, F_MASK, F_ZMODE, F_OLEN, F_ITZ
};
enum { G_GAP, G_ORD, G_MSG, G_PAT, G_OPQ, G_ODD };

typedef struct { u8 ch, mp, m, has, f[5]; } pent;
typedef struct {
  mx b;
  it_lay L;
  u8 * h;
  const u8 * in;
  void * s8, * s16;
  u8 * gap;
  sz * gl;
  u32 zp[ITZ_K_N];
  int s16c, stc, f;
  struct ist * z;
} ix;

static int rc_cb(void * ctx, int bit, u32 p0) { return mx_bit(ctx, bit, p0); }

static void aq(mx * m) {
  if (m->q) return;
  m->q = acm_new(ACM_VLITE | ACM_APMS);
  m->d = acm_d(m->q);
  acm_bind(m->q, rc_cb, m);
  acm_class(m->q, TI_NUM, ACM_EAPM, 0);
  acm_class(m->q, TI_STR, ACM_EAPM, 0);
  acm_class(m->q, TI_GAP, ACM_EAPM, 0);
  acm_class(m->q, TI_PAT, ACM_EAPM, 0);
}

static u32 sym(mx * m, u32 v, u32 n, int f, acm_h c0, acm_h c1) {
  aq(m);
  use(m, TI_NUM, 0, f, 0, PH3(f, 1, c0), PH3(f, 2, c1));
  return val(m, v, n, PH2(TI_NUM, f));
}

static u32 gnum(mx * m, u32 v, int f, acm_h c0, acm_h c1) {
  u32 b = m->dec ? 0 : blr_ilog(v), r = 1;
  int j;
  aq(m);
  use(m, TI_NUM, 0, f, 0, PH3(f, 1, c0), PH3(f, 2, c1));
  b = val(m, b, 33, PH3(TI_NUM, f, 0));
  if (b < 2) return b;
  for (j = (int) b - 2; j >= 0; j--) {
    int d = (int) b - 2 - j;
    acm_h k = d < 3 ? PH4(f, b, d, r) : PH3(f, b, 64 + MIN(d, 20));
    use(m, TI_NUM, 0, PH2(f, b), 0, PH2(k, c0), PH2(k, c1));
    r = r * 2 + val(m, v >> j & 1, 2, k);
  }
  return r;
}

#include "it/st.c"

static void gbytes(mx * m, u8 * t, sz len, int id) {
  sz j;
  aq(m);
  Fj(len,
    u32 p1 = j ? t[j - 1] : 256, p2 = j > 1 ? t[j - 2] : 256,
        p3 = j > 2 ? t[j - 3] : 256;
    use(m, TI_GAP, 0, PH2(id, p1 >> 4), 0, PH3(id, p1, p2),
        PH4(id, p1, p2, p3));
    t[j] = (u8) val(m, t[j], 256, PH3(TI_GAP, id, p1)));
}

static void zput(void * u, int kind, u32 v) {
  ix * x = u;
  gnum(&x->b, v, F_ITZ + kind, MIN(x->zp[kind], 64), 0);
  x->zp[kind] = v;
}

static int zget(void * u, int kind, u32 * v) {
  ix * x = u;
  *v = gnum(&x->b, 0, F_ITZ + kind, MIN(x->zp[kind], 64), 0);
  x->zp[kind] = *v;
  return 0;
}

static void est(ix * x, u32 i, u32 * ps) {
  it_ent * e = x->L.e + i;
  e->st = (u8) (IS_PRI + xs(x, e->st - IS_PRI, 4, F_ST, e->k, *ps));
  *ps = e->st;
  if (e->st == IS_ALIAS) e->v = xn(x, e->v, F_AL, e->k, 0);
  else if (e->st == IS_RAW) e->v = xn(x, e->v, F_RAW, e->k, 0);
}

static u8 * own(ix * x, it_ent * e, sz len) {
  e->d = xcalloc(len + 1, 1);
  if (x->in) memcpy(e->d, x->in + e->at, len);
  return e->d;
}

static void structs(ix * x, int k, u32 lo, u32 hi) {
  mx * m = &x->b;
  u32 i;
  for (i = lo; i < hi; i++) {
    it_ent * e = x->L.e + i;
    u8 * t;
    if (e->st != IS_PRI) continue;
    e->len = k == IK_INS ? IT_INS : x->f == IT_FS3M ? S3_INS : IT_SHD;
    t = own(x, e, e->len);
    if (k == IK_SHD && !m->dec && it_smp_has(x->f, t)) it_smp_set(x->f, t, 0);
    rec(x, k == IK_INS ? RK_INS : x->f == IT_FS3M ? RK_S3I : RK_SHD, t,
        e->len, e->i);
  }
}

static int order(ix * x) {
  mx * m = &x->b;
  it_lay * L = &x->L;
  u8 * pl = xcalloc(L->ne, 1);
  u32 lo[IK_N + 1], t, i, pk = IK_N;
  int bad = 0;
  lo[IK_INS] = 0;  lo[IK_SHD] = L->inn;  lo[IK_PAT] = L->inn + L->sn;
  lo[IK_SMP] = lo[IK_PAT] + L->pn;  lo[IK_MSG] = L->ne - 1;  lo[IK_N] = L->ne;
  for (t = 0; t < L->np && !bad; t++) {
    u32 j = m->dec ? 0 : L->ord[t], k = m->dec ? 0 : L->e[j].k, f, r = 0;
    k = xs(x, k, IK_N, F_OK, pk, 0);
    for (f = lo[k]; f < lo[k + 1]; f++)
      if (L->e[f].st == IS_PRI && !pl[f]) break;
    if (f == lo[k + 1]) { bad = 1;  break; }
    if (!xs(x, j == f, 2, F_OF, k, 0)) {
      if (!m->dec)
        for (i = f + 1; i < j; i++) r += L->e[i].st == IS_PRI && !pl[i];
      r = xn(x, r, F_OR, k, 0);
      for (i = f + 1; i < lo[k + 1]; i++)
        if (L->e[i].st == IS_PRI && !pl[i] && !r--) break;
      if (i == lo[k + 1]) { bad = 1;  break; }
      f = i;
    }
    pl[f] = 1;  L->ord[t] = f;  pk = k;
  }
  xfree(pl);
  return bad ? -1 : 0;
}

static int gaps(ix * x) {
  mx * m = &x->b;
  it_lay * L = &x->L;
  sz pos = L->tab, tot = 0, cap = 0;
  u32 t;
  x->gl = xcalloc(L->np + 1, sizeof *x->gl);
  for (t = 0; t <= L->np; t++) {
    const it_ent * e = t < L->np ? L->e + L->ord[t] : NULL;
    sz g = m->dec ? 0 : (e ? e->at : L->n) - pos;
    g = xn(x, (u32) g, F_GAP, e ? e->k : IK_N, !t);
    if (g > L->n - tot) return -1;
    x->gl[t] = g;
    x->gap = xgrow(x->gap, &cap, tot + g + 1, 1, 256);
    if (!m->dec) memcpy(x->gap + tot, x->in + pos, g);
    zgap(x, x->gap + tot, g, t, t ? L->e[L->ord[t - 1]].k : IK_N,
         e ? e->k : IK_N);
    tot += g;
    if (e) pos = e->at + e->len;
  }
  return 0;
}

static int clip(ix * x) {
  it_lay * L = &x->L;
  it_ent * e;
  if (!L->np) return 0;
  e = L->e + L->ord[L->np - 1];
  if (e->k != IK_SMP || it_smp_cmp(x->f, L->e[it_eshd(L, e->i)].d))
    return 0;
  if (xs(x, L->clip != 0, 2, F_CLIP, 0, 0)) {
    L->clip = xn(x, (u32) L->clip, F_CLN, 0, 0);
    if (!L->clip) return -1;
  }
  return 0;
}

static int unpack(const u8 * p, sz len, u32 rows, pent * E, u32 * rs) {
  u8 lm[64], last[64][5];
  sz q = 0;
  u32 ne = 0, r;
  memset(lm, 0, sizeof lm);  memset(last, 0, sizeof last);
  for (r = 0; r < rows; r++) {
    int pc = -1;
    rs[r] = ne;
    for (;;) {
      u8 cv, mk;
      int c, j;
      pent * y;
      if (q >= len) return -1;
      cv = p[q++];
      if (!cv) break;
      c = (cv - 1) & 63;
      if (c <= pc) return -1;
      pc = c;
      if (cv & 0x80) {
        if (q >= len) return -1;
        mk = lm[c] = p[q++];
      } else mk = lm[c];
      y = E + ne++;
      memset(y, 0, sizeof *y);
      y->ch = (u8) c;  y->mp = cv >> 7;  y->m = mk;
      Fj(3, if (mk >> j & 1) { if (q >= len) return -1;
                                last[c][j] = p[q++]; });
      if (mk & 8) {
        if (q + 2 > len) return -1;
        last[c][3] = p[q++];  last[c][4] = p[q++];
      }
      Fj(4, if (mk >> j & 17) y->has |= (u8) (1 << j));
      memcpy(y->f, last[c], 5);
      Fj(5, if (!(y->has >> MIN(j, 3) & 1)) y->f[j] = 0);
    }
  }
  rs[rows] = ne;
  return q == len ? (int) ne : -1;
}

static sz repack(const pent * E, const u32 * rs, u32 rows, u8 * o) {
  u8 lm[64];
  sz q = 0;
  u32 r, k;
  memset(lm, 0, sizeof lm);
  for (r = 0; r < rows; r++) {
    for (k = rs[r]; k < rs[r + 1]; k++) {
      const pent * y = E + k;
      u8 mk = y->mp ? y->m : lm[y->ch];
      int j;
      o[q++] = (u8) ((y->ch + 1) | (y->mp ? 0x80 : 0));
      if (y->mp) o[q++] = lm[y->ch] = mk;
      Fj(3, if (mk >> j & 1) o[q++] = y->f[j]);
      if (mk & 8) { o[q++] = y->f[3];  o[q++] = y->f[4]; }
    }
    o[q++] = 0;
  }
  return q;
}

#include "it/pt.c"

static void pcm8(ix * x, const i16 * src, i16 * dst, sz n) {
  u8 * a = xmalloc(n + 1), * b = x->b.dec ? xmalloc(n + 1) : NULL,
     * c = src ? a : NULL;
  sz i;
  if (!x->s8) x->s8 = x->b.sc->se == SE_LS ? smp_ls_new(&x->b)
                                            : smp_fast_new(&x->b);
  if (src) Fi(n, a[i] = (u8) src[i]);
  if (x->b.sc->se == SE_LS) smp_ls_one(&x->b, x->s8, 0, c, b, n);
  else smp_fast_one(&x->b, x->s8, 0, c, b, n);
  if (dst) Fi(n, dst[i] = (i8) (b ? b[i] : a[i]));
  xfree(a);  xfree(b);
}

static void pcm(ix * x, const i16 * src, i16 * dst, sz n) {
  if (!n) return;
  pcm8(x, src, dst, n);
}

static void chans(ix * x, const i16 * src, i16 * dst, sz nv, sz fr, int b16) {
  sz o = 0;
  if (b16) {
    if (!nv) return;
    if (!x->s16) x->s16 = s16_new(x->s16c);
    s16_smp(&x->b, x->s16, src, dst, nv, fr);
    return;
  }
  while (o < nv) {
    sz k = MIN(fr, nv - o);
    pcm(x, src ? src + o : NULL, dst ? dst + o : NULL, k);
    o += k;
  }
}

static int opaque(ix * x, it_ent * e) {
  mx * m = &x->b;
  u32 len;
  len = gnum(m, (u32) e->len, F_OLEN, 0, 0);
  if (len > x->L.n) return -1;
  own(x, e, len);
  gbytes(m, e->d, len, G_OPQ);
  e->len = len;
  return 0;
}

static int compressed(ix * x, it_ent * e, const u8 * s) {
  mx * m = &x->b;
  u32 fr = rd32le(s + 48);
  int ch = (s[18] >> 2 & 1) + 1, b16 = s[18] >> 1 & 1, z = s[46] >> 2 & 1,
      mode = 1, bad = 0;
  uint64_t nv = (uint64_t) fr * ch;
  itz_side si;
  itz_io io;
  i16 * p = NULL;
  itz_init(&si);
  io.put = zput;  io.get = zget;  io.u = x;
  if (!m->dec && nv <= 8 * (uint64_t) e->len + 65536) {
    const u8 * src = x->in + e->at;
    p = xmalloc((sz) nv * 2 + 2);
    if (!itz_decode(src, e->len, fr, ch, b16, z, p, &si) &&
        si.used == e->len && (uint64_t) si.ntb * 4 <= e->len) {
      sz on;
      u8 * o = itz_encode(p, &si, &on);
      mode = !o || on != e->len || memcmp(o, src, on);
      xfree(o);
    }
  }
  if (sym(m, (u32) mode, 2, F_ZMODE, b16, 0)) {
    xfree(p);  itz_free(&si);
    return opaque(x, e);
  }
  if (m->dec) {
    if (nv > 8 * (uint64_t) x->L.n + 65536) return -1;
    p = xmalloc((sz) nv * 2 + 2);
    if (itz_side_get(&si, fr, ch, b16, z, &io)) bad = 1;
  } else itz_side_put(&si, &io);
  if (!bad) chans(x, m->dec ? NULL : p, m->dec ? p : NULL, (sz) nv, fr, b16);
  if (!bad && m->dec) {
    sz on;
    e->d = itz_encode(p, &si, &on);
    if (!e->d) bad = 1;
    else e->len = on;
  }
  xfree(p);  itz_free(&si);
  return bad ? -1 : 0;
}

static int raw(ix * x, it_ent * e, const u8 * s) {
  mx * m = &x->b;
  const int f = x->f;
  u32 fr = it_smp_fr(f, s);
  int b16 = it_smp_b16(f, s), bp = b16 + 1,
      sg = f == IT_FS3M ? rd16le(x->h + 0x2A) == 1 : s[46] & 1,
      be = f == IT_FIT && b16 && (s[46] & 2);
  uint64_t len = x->L.clip && e == x->L.e + x->L.ord[x->L.np - 1]
                 ? x->L.clip : it_smp_raw(f, s);
  sz nv, i;
  i16 * p;
  u8 * d, tl[2];
  const u8 * q;
  if (len > x->L.n) return -1;
  nv = (sz) len / (sz) bp;
  d = m->dec ? own(x, e, (sz) len) : NULL;
  q = d ? d : x->in + e->at;
  p = xmalloc(nv * 2 + 2);
  if (!m->dec)
    Fi(nv,
      u32 v = b16 ? (be ? rd16be(q + 2 * i) : rd16le(q + 2 * i)) : q[i];
      v ^= sg ? 0 : b16 ? 0x8000 : 0x80;
      p[i] = b16 ? (i16) (u16) v : (i16) (i8) (u8) v);
  chans(x, m->dec ? NULL : p, m->dec ? p : NULL, nv, fr, b16);
  if (m->dec)
    Fi(nv,
      u32 v = b16 ? (u16) p[i] : (u8) p[i];
      v ^= sg ? 0 : b16 ? 0x8000 : 0x80;
      if (!b16) d[i] = (u8) v;
      else if (be) wr16be(d + 2 * i, v);
      else wr16le(d + 2 * i, v));
  if (nv * bp < len) {
    if (!d) memcpy(tl, q + nv * bp, (sz) len - nv * bp);
    gbytes(m, d ? d + nv * bp : tl, (sz) len - nv * bp, G_ODD);
  }
  xfree(p);
  e->len = (sz) len;
  return 0;
}

#include "it/xm.c"

static int body(ix * x) {
  mx * m = &x->b;
  it_lay * L = &x->L;
  u8 ph[IT_PATH];
  u32 i, ps = 0;
  memset(ph, 0, sizeof ph);
  Fi(L->inn + L->sn + L->pn, est(x, i, &ps));
  structs(x, IK_INS, 0, L->inn);
  structs(x, IK_SHD, L->inn, L->inn + L->sn);
  ps = 0;
  Fi(L->sn,
    it_ent * e = L->e + it_esmp(L, i);
    const it_ent * h = L->e + it_eshd(L, i);
    if (m->dec)
      e->st = h->st == IS_PRI && it_smp_has(x->f, h->d) ? IS_ZERO : IS_NONE;
    if (e->st != IS_NONE) est(x, it_esmp(L, i), &ps));
  if (m->dec)
    L->e[L->ne - 1].st = x->f == IT_FIT && rd16le(x->h + 0x36) ? IS_ZERO
                                                                : IS_NONE;
  if (L->e[L->ne - 1].st != IS_NONE) est(x, L->ne - 1, &ps);
  if (m->dec) {
    L->np = 0;
    Fi(L->ne, L->np += L->e[i].st == IS_PRI);
  }
  if (order(x) || gaps(x) || clip(x)) return -1;
  if (pats(x)) return -1;
  if (L->e[L->ne - 1].st == IS_PRI) {
    it_ent * e = L->e + L->ne - 1;
    e->len = rd16le(x->h + 0x36);
    own(x, e, e->len);
    text(x, e->d, e->len);
  }
  Fi(L->sn,
    it_ent * e = L->e + it_esmp(L, i);
    const u8 * s = L->e[it_eshd(L, i)].d;
    if (e->st != IS_PRI) continue;
    if (!it_smp_cmp(x->f, s) ? raw(x, e, s) : x->f == IT_FS3M ? opaque(x, e)
                                            : compressed(x, e, s))
      return -1);
  return 0;
}

static void header(ix * x) {
  mx * m = &x->b;
  u8 * h = x->h;
  if (x->f == IT_FS3M) {
    ist_tb(x);
    rec(x, RK_S3H, h, S3_HDR, 0);
    return;
  }
  if (!m->dec && rd16le(h + 0x36)) wr32le(h + 0x38, 0);
  ist_tb(x);
  rec(x, RK_HDR, h, IT_HDR, 0);
}

static void tables(ix * x) {
  if (x->f == IT_FIT) { ords(x, x->h + IT_HDR, x->L.on);  return; }
  ords(x, x->h + S3_HDR, x->L.on);
  if (x->h[0x35] == 0xFC) rec(x, RK_S3P, x->h + x->L.tab - 32, 32, 0);
}

int it_cfg_ok(const it_cfg * c) {
  return mod_sconfig(c->s8) && s16_config(c->s16) && st_ok(c->st);
}

static void init(ix * x, int dec, const it_cfg * c, int f) {
  mx * m = &x->b;
  memset(x, 0, sizeof *x);
  m->dec = dec;  x->f = f;
  m->sc = mod_scfg_get(c->s8);
  x->s16c = c->s16;  x->stc = c->st;
  acm_prepare();
  x->z = ist_new(c->st);
}

static void finish(ix * x) {
  acm_free(x->b.q);
  ist_free(x->z);
  if (x->s8) {
    if (x->b.sc->se == SE_LS) smp_ls_free(x->s8);
    else smp_fast_free(x->s8);
  }
  if (x->s16) s16_free(x->s16);
  it_lay_free(&x->L);
  xfree(x->h);  xfree(x->gap);  xfree(x->gl);
}

sz it_code(const u8 * b, sz n, const it_cfg * c, u8 ** out) {
  ix * x = xmalloc(sizeof *x);
  sz len;
  int f = it_kind(b, n);
  FATAL_UNLESS(f && it_cfg_ok(c), "internal: not an IT");
  init(x, 0, c, f);
  rc_enc_init(&x->b.e);
  x->in = b;
  if (f == IT_FXM) {
    u8 * o = xmalloc(n + 1);
    memcpy(o, b, n);
    x->L.n = n;
    FATAL_UNLESS(!xm_code(x, o), "internal: XM layout");
    xfree(o);
  } else {
    it_lay_init(&x->L, b, n, f);
    it_parse(&x->L, b);
    x->h = xmalloc(x->L.tab);
    memcpy(x->h, b, x->L.tab);
    header(x);
    tables(x);
    FATAL_UNLESS(!body(x), "internal: IT layout");
  }
  len = rc_enc_finish(&x->b.e);
  *out = xmalloc(len + 2);
  (*out)[0] = (u8) (c->s16 - IT_S16C0);  (*out)[1] = (u8) (c->st - IT_STC0);
  memcpy(*out + 2, rc_enc_data(&x->b.e), len);
  len += 2;
  rc_enc_free(&x->b.e);
  finish(x);
  xfree(x);
  return len;
}

static int place(ix * x, u8 * out) {
  it_lay * L = &x->L;
  sz pos = L->tab, g = 0;
  u32 t, i;
  Fi(L->ne,
    const it_ent * e = L->e + i;
    if (e->st == IS_ALIAS && (e->v >= L->ne || L->e[e->v].st != IS_PRI))
      return -1);
  for (t = 0; t <= L->np; t++) {
    it_ent * e = t < L->np ? L->e + L->ord[t] : NULL;
    if (x->gl[t] > L->n - pos) return -1;
    pos += x->gl[t];
    if (!e) break;
    if (e->len > L->n - pos) return -1;
    e->at = pos;  pos += e->len;
  }
  if (pos != L->n) return -1;
  it_offsets(L, x->h);
  memcpy(out, x->h, L->tab);
  pos = L->tab;
  for (t = 0; t <= L->np; t++) {
    it_ent * e = t < L->np ? L->e + L->ord[t] : NULL;
    memcpy(out + pos, x->gap + g, x->gl[t]);
    pos += x->gl[t];  g += x->gl[t];
    if (!e) break;
    memcpy(out + pos, e->d, e->len);
    pos += e->len;
  }
  return 0;
}

int it_decode(const u8 * a, sz alen, sz n, int s8, int f, u8 * out) {
  ix * x;
  u8 hd[IT_HDR];
  it_cfg c;
  int bad = 0;
  if (alen < 2 || f < IT_FIT || f > IT_FXM ||
      n < (f == IT_FIT ? IT_HDR : f == IT_FS3M ? S3_HDR : XM_SIG))
    return -1;
  c.s8 = s8;  c.s16 = IT_S16C0 + a[0];  c.st = IT_STC0 + a[1];
  if (!it_cfg_ok(&c)) return -1;
  x = xmalloc(sizeof *x);
  init(x, 1, &c, f);
  rc_dec_init(&x->b.r, a + 2, alen - 2);
  if (f == IT_FXM) {
    x->L.n = n;
    bad = xm_code(x, out) || rc_dec_spent(&x->b.r);
    finish(x);
    xfree(x);
    return bad ? -1 : 0;
  }
  memset(hd, 0, sizeof hd);
  x->h = hd;
  header(x);
  x->h = NULL;
  if ((f == IT_FIT ? it_tab(hd) : it_tab3(hd)) > n) bad = 1;
  else {
    it_lay_init(&x->L, hd, n, f);
    x->h = xcalloc(x->L.tab, 1);
    memcpy(x->h, hd, f == IT_FIT ? IT_HDR : S3_HDR);
    tables(x);
    if (body(x) || place(x, out) || rc_dec_spent(&x->b.r)) bad = 1;
  }
  finish(x);
  xfree(x);
  return bad ? -1 : 0;
}
