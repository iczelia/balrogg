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
#include "mod/lstm.h"

enum { F_NP0, F_NPS, F_NP, F_STD, F_NX0, F_NX, F_AV };

static const u16 EXT[60] = {
  1712, 1616, 1524, 1440, 1356, 1280, 1208, 1140, 1076, 1016, 960, 906,
  856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453,
  428, 404, 381, 360, 340, 320, 302, 285, 269, 254, 240, 226,
  214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113,
  107, 101, 95, 90, 85, 80, 76, 71, 67, 64, 60, 57
};

static const mcfg CFG[] = {
  [1] = { PE_FAST, 0 },
  [20] = { PE_CM, 384 },  [21] = { PE_CM, 387 },  [23] = { PE_CM, 15 },
  [24] = { PE_CM, 127 },
  [40] = { PE_X, 0x400 },  [41] = { PE_X, 0x2a },  [42] = { PE_X, 0x4a },
  [43] = { PE_X, 0x0a },  [44] = { PE_X, 0x08 },  [47] = { PE_X, 0x108 },
  [48] = { PE_X, 0x105 },  [49] = { PE_X, 0x205 },
};
#define NCFG ((int) ARRAY_LEN(CFG))

int mod_config(int cfg) {
  cfg -= MOD_CFG0;
  return cfg >= 0 && cfg < NCFG && CFG[cfg].pe != PE_NONE;
}

static int rc_cb(void * ctx, int bit, u32 p0) { return mx_bit(ctx, bit, p0); }

static u32 num(mx * m, u32 v, u32 n, int f, acm_h c0, acm_h c1, acm_h c2) {
  acm_dec * d = m->d;
  d->cx[0] = PH2(f, 0);  d->cx[1] = PH3(f, 1, c0);  d->cx[2] = PH3(f, 2, c1);
  d->cx[3] = PH3(f, 3, c2);  d->cx[4] = PH4(f, 4, c0, c1);
  use(m, T_SNUM, 5, PH2(f, c0), f, PH2(f, c1), PH2(f, c2));
  return val(m, v, n, PH2(T_SNUM, f));
}


static void header(mx * m, int ns, u8 * t) {
  acm_dec * d = m->d;
  sz hl = ns == 31 ? MOD_HDR31 : MOD_HDR15, i;
  mlstm s;
  i32 x[ML_NI];
  int pf = -1, seen = 0, j;
  ml_reset(&s);
  Fi(hl,
    int fp, si, f = mod_field(ns, t, i, &fp, &si), sf, tok = 0;
    u32 prev = i ? t[i - 1] : 0, ab, node = 1;
    if (!i || f != pf || (f == MF_NAME && !fp)) seen = 0;
    sf = f >= MF_NAME && f <= MF_LLL && si > 0;
    ab = sf ? t[i - 30] : 0;
    Fj(8,
      x[j] = (i32) ((prev >> (7 - j)) & 1) * 256;
      x[8 + j] = (i32) ((ab >> (7 - j)) & 1) * 256);
    Fj(MF_N, x[16 + j] = j == f ? 256 : 0);
    x[31] = f == MF_NAME || f == MF_TITLE ? (fp * 512 + 21) / 42 : 0;
    x[32] = seen ? 256 : 0;
    x[33] = si >= 0 ? (si * 512 + 30) / 60 : 0;
    ml_step(&s, x);
    use(m, T_HDR, 0, f, 0, f, PH2(f, fp));
    if (!m->dec) tok = t[i];
    Fj(8,
      int bit = (tok >> (7 - j)) & 1;
      d->apm[0] = PH2(f, node);
      bit = acm_bit(m->q, bit, ml_p1(&s, node) * 16 + 8, PH2(T_HDR, node));
      node = node * 2 + (u32) bit);
    t[i] = (u8) node;
    if ((f == MF_NAME || f == MF_TITLE) && !t[i]) seen = 1;
    pf = f);
}

static int frame(mx * m, sz n) {
  u8 * h = m->h;
  int ns;
  u32 np = m->L.np, p0;
  acm_dec * d = m->d;
  d->cx[0] = 0;
  use(m, T_KIND, 1, 0, 0, 0, 0);
  d->apm[0] = 0;
  ns = acm_bit(m->q, m->L.ns == 31, 32768, 1) ? 31 : 15;
  { u8 t[MOD_HDR31];
    sz hl = ns == 31 ? MOD_HDR31 : MOD_HDR15;
    memcpy(t, h, hl);
    if (!m->dec) mod_xform(t, ns, 0);
    m->tm[1] = now();
    header(m, ns, t);
    m->tm[2] = now();
    mod_xform(t, ns, 1);
    memcpy(h, t, hl);
    if (ns == 31 && !mod_tag_ch(h + 1080)) return -1; }
  MARK(m, 4);
  if (n < (ns == 31 ? MOD_HDR31 : MOD_HDR15)) return -1;
  p0 = mod_p0(h, ns);
  if (!num(m, np == p0, 2, F_NP0, 0, 0, 0)) {
    int neg = (int) num(m, np < p0, 2, F_NPS, 0, 0, 0);
    u32 mag = num(m, (neg ? p0 - np : np - p0) - 1, MOD_MAXPAT, F_NP, neg,
                  0, 0) + 1;
    if (neg ? mag > p0 : p0 + mag > MOD_MAXPAT) return -1;
    np = neg ? p0 - mag : p0 + mag;
  } else np = p0;
  mod_layout(&m->L, h, ns, np, n);
  return 0;
}

static int alphabet(mx * m) {
  u8 seen[4096];
  int s, ps = 0, px = 0;
  sz i, na = 0;
  memset(seen, 0, sizeof seen);
  if (!m->dec)
    Fi(m->L.ncell, seen[cell_per(m->in + m->L.pat + 4 * i)] = 1);
  for (s = 0; s < MOD_NSEMI; s++) {
    u32 lo = mod_semi_lo(s), hi = mod_semi_hi(s), sd = m->std[s], v, c = 0,
        at = lo, j;
    int hs = 0;
    if (sd) hs = (int) num(m, seen[sd], 2, F_STD, (acm_h) s, ps, px);
    if (!m->dec) Fi(hi - lo + 1, c += seen[lo + i] && lo + i != sd);
    if (num(m, c > 0, 2, F_NX0, (acm_h) s, hs, px)) {
      c = 1 + num(m, c - 1, hi - lo + 1, F_NX, (acm_h) s, hs, 0);
      for (j = 0; j < c; j++) {
        if (at > hi) return -1;
        v = at;
        if (!m->dec) while (!seen[v] || v == sd) v++;
        v = at + num(m, v - at, hi - at + 1, F_AV, (acm_h) s, j, 0);
        if (v > hi || v == sd) return -1;
        seen[v] = 1;
        at = v + 1;
      }
    } else c = 0;
    if (hs) seen[sd] = 1;
    m->aoff[s] = (u16) na;
    for (v = lo; v <= hi; v++) if (seen[v]) m->aper[na++] = (u16) v;
    ps = hs;  px = c > 0;
  }
  m->aoff[MOD_NSEMI] = (u16) na;
  return 0;
}

static void bytes(mx * m, u8 * b, const u8 * src, sz len, int id) {
  acm_dec * d = m->d;
  sz j;
  Fj(len,
    u32 p1 = j ? b[j - 1] : 256, p2 = j > 1 ? b[j - 2] : 256,
        p3 = j > 2 ? b[j - 3] : 256;
    d->cx[0] = PH2(1, id);
    d->cx[1] = PH3(2, id, p1);
    d->cx[2] = PH3(3, p1, p2);
    d->cx[3] = PH4(4, p1, p2, p3);
    d->cx[4] = PH3(5, id, MIN(j, 32));
    use(m, T_TAIL, 5, PH2(id, j < 16), p1 >> 4, p1, PH2(id, MIN(j, 32)));
    b[j] = (u8) val(m, m->dec ? 0 : src[j], 256, T_TAIL));
}

void mt_init(mtrk * t, sz ncell, sz rowc) {
  sz i;
  t->rowc = rowc;
  t->mb = CLAMP((int) blr_ilog((u32) ncell) + 1, 8, 22);
  t->mp = xmalloc(rowc * sizeof *t->mp);
  t->mt = xcalloc((sz) 1 << t->mb, sizeof *t->mt);
  t->ml = xcalloc(rowc, sizeof *t->ml);
  Fi(rowc, t->mp[i] = NOC);
}

void mt_free(mtrk * t) { xfree(t->mp);  xfree(t->mt);  xfree(t->ml); }

void mt_next(mtrk * t, const mx * m, int c, sz i, sz pr) {
  const sz rowc = t->rowc;
  if (pr != NOC && ckey(m, pr) == ckey(m, i)) t->ml[c]++;
  else t->ml[c] = 0;
  if (pr != NOC && t->ml[c]) t->mp[c] = pr + rowc;
  else if (i >= rowc) {
    u32 j = t->mt[chash(ckey(m, i - rowc), ckey(m, i), t->mb)];
    t->mp[c] = j ? j - 1 : NOC;
  } else t->mp[c] = NOC;
  if (i >= rowc)
    t->mt[chash(ckey(m, i - rowc), ckey(m, i), t->mb)] = (u32) (i + rowc + 1);
}

static void init(mx * m, int dec, u8 * h, int cfg) {
  int i, full;
  memset(m, 0, sizeof *m);
  m->tm[0] = now();
  m->dec = dec;  m->h = h;  m->cf = &CFG[cfg - MOD_CFG0];
  full = m->cf->pe == PE_X;
  m->q = acm_new(full ? ACM_VLITE : ACM_VLITE | ACM_APMS);
  m->d = acm_d(m->q);
  acm_bind(m->q, rc_cb, m);
  acm_class(m->q, T_HDR, ACM_EAPM, 0);
  if (!full) {
    acm_class(m->q, T_KIND, ACM_EAPM, 0);
    acm_class(m->q, T_SNUM, ACM_EAPM, 0);
    acm_class(m->q, T_TAIL, ACM_EAPM, 0);
  }
  Fi(60, { int s = mod_semi(EXT[i]);  if (!m->std[s]) m->std[s] = EXT[i]; });
}

static void finish(mx * m) {
  acm_free(m->q);
  xfree(m->nt);  xfree(m->sm);  xfree(m->ef);  xfree(m->pa);
}

static void rest(mx * m) {
  const mod_lay * L = &m->L;
  sz pb = L->ncell * 4, pr = L->plen - pb;
  u8 tmp[4];
  if (pr) {
    bytes(m, m->out ? m->out + L->pat + pb : tmp,
          m->in ? m->in + L->pat + pb : NULL, pr, 1);
  }
  MARK(m, 1);
  if (L->tlen) {
    u8 * t = m->out ? m->out + L->tail : xmalloc(L->tlen);
    bytes(m, t, m->in ? m->in + L->tail : NULL, L->tlen, 2);
    if (!m->out) xfree(t);
  }
}

static void body(mx * m) {
  sz nc = m->L.ncell + 1;
  m->nt = xmalloc(nc);  m->sm = xmalloc(nc);
  m->ef = xmalloc(nc);  m->pa = xmalloc(nc);
  MARK(m, 0);
  switch (m->cf->pe) {
  case PE_FAST: pat_fast(m);  break;
  case PE_CM: pat_cm(m);  break;
  default: pat_x(m);
  }
  rest(m);
  MARK(m, 3);
}

sz mod_code(const u8 * b, sz n, int cfg, u8 ** out) {
  mx * m = xmalloc(sizeof *m);
  u8 h[MOD_HDR31];
  int ns = mod_kind(b, n, n);
  sz len, raw;
  FATAL_UNLESS(ns && mod_config(cfg), "internal: not a MOD");
  memset(h, 0, sizeof h);
  memcpy(h, b, ns == 31 ? MOD_HDR31 : MOD_HDR15);
  init(m, 0, h, cfg);
  rc_enc_init(&m->e);
  m->in = b;
  mod_layout(&m->L, b, ns, 0, n);
  m->L.np = mod_pick_np(b, ns, m->L.ch, n);
  FATAL_UNLESS(!frame(m, n), "internal: MOD framing");
  FATAL_UNLESS(!memcmp(h, b, m->L.hdr), "internal: MOD header transform");
  FATAL_UNLESS(!alphabet(m), "internal: MOD period alphabet");
  body(m);
  if (getenv("BLR_MOD_STATS"))
    fprintf(stderr, "modstats %lu %lu %lu %lu %lu %lu\n", (unsigned long) n,
            (unsigned long) m->mark[0], (unsigned long) (m->mark[1] - m->mark[0]),
            (unsigned long) (m->L.tail - m->L.smp),
            (unsigned long) (m->mark[3] - m->mark[1]),
            (unsigned long) m->mark[4]);
  if (getenv("BLR_MOD_STATS")) {
    int k;
    fprintf(stderr, "modtime %.6f %.6f %.6f\n", m->tm[1] - m->tm[0],
            m->tm[2] - m->tm[1], now() - m->tm[2]);
    fprintf(stderr, "modcost");
    for (k = 0; k < 16; k++) fprintf(stderr, " %.0f", m->cost[k] / 8);
    for (k = 0; k < 4; k++) fprintf(stderr, " %.0f", m->ccost[k] / 8);
    fprintf(stderr, "\n");
  }
  len = rc_enc_finish(&m->e);
  raw = m->L.tail - m->L.smp;
  *out = xmalloc(4 + len + raw);
  wr32le(*out, (u32) len);
  memcpy(*out + 4, rc_enc_data(&m->e), len);
  memcpy(*out + 4 + len, b + m->L.smp, raw);
  len += 4 + raw;
  rc_enc_free(&m->e);
  finish(m);
  xfree(m);
  return len;
}

int mod_decode(const u8 * a, sz alen, sz n, int cfg, u8 * out) {
  mx * m;
  u8 h[MOD_HDR31];
  int bad = 0;
  sz cl = alen >= 4 ? rd32le(a) : 0;
  if (alen < 4 || cl > alen - 4 || !mod_config(cfg)) return -1;
  m = xmalloc(sizeof *m);
  memset(h, 0, sizeof h);
  init(m, 1, h, cfg);
  rc_dec_init(&m->r, a + 4, cl);
  m->out = out;
  m->L.ns = 31;
  if (frame(m, n) || alphabet(m)) bad = 1;
  else {
    memcpy(out, h, m->L.hdr);
    body(m);
    if (rc_dec_spent(&m->r) || alen - 4 - cl != m->L.tail - m->L.smp) bad = 1;
    else memcpy(out + m->L.smp, a + 4 + cl, m->L.tail - m->L.smp);
  }
  finish(m);
  xfree(m);
  return bad ? -1 : 0;
}
