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

typedef struct { u16 p;  u8 n; } zc;

#define ZL_COARSE 60
#define ZL_FINE   120
#define ZL_THETA  120
#define ZL_SIDE   120
#define ZL_SPLIT  255
#define ZS_SIDE   (om_tier >= 2 ? 4 : 0)
#define ZS_THETA  (om_tier >= 2 ? 4 : 0)
#define ZS_FINE   (om_tier >= 2 ? 16 : 0)
#define ZS_SPLIT  16

static INLINE int zfix(u32 p0, int bit) {
  bit = om_fbit(p0, bit);
  PROF(prof_sym(om_comp, bit ? p0 : 0, bit ? 65536 : p0, 65536));
  return bit;
}

static INLINE int zbit(zc * c, int bit, int lim) {
  bit = zfix(c->p, bit);
  c->p = rc_adapt(c->p, &c->n, lim, bit);
  return bit;
}

static INLINE int zbit_at(zc * c, u16 init, int bit, int lim) {
  if (!c->n && !c->p) c->p = init;
  return zbit(c, bit, lim);
}

static blr_file * zr_file;
static opus_uint64 zr_acc;
static int zr_n;
static sz zr_pos, zr_off, zr_len;

static void zr_start(blr_file * f, sz off, sz len) {
  zr_file = f;  zr_acc = 0;  zr_n = 0;
  zr_pos = 0;  zr_off = off;  zr_len = len;
}

static void zr_finish(void) {
  while (zr_n > 0) { bf_put(zr_file, (u8) zr_acc);  zr_acc >>= 8;  zr_n -= 8; }
  bf_put(zr_file, 0);
  zr_file = NULL;  zr_n = 0;  zr_acc = 0;
}

static INLINE u32 zraw(u32 v, int n) {
  if (n <= 0) return 0;
  PROF(prof_sym(om_comp, 0, 1, 1UL << n));
  if (om_mode == OM_DEC) {
    while (zr_n < n) {
      zr_acc |= (opus_uint64) (zr_pos < zr_len ?
                               bf_get(zr_file, zr_off + zr_pos) : 0) << zr_n;
      zr_pos++;  zr_n += 8;
    }
    v = (u32) zr_acc & ((1UL << n) - 1);
    zr_acc >>= n;  zr_n -= n;
    return v;
  }
  zr_acc |= (opus_uint64) (v & ((1UL << n) - 1)) << zr_n;  zr_n += n;
  while (zr_n >= 8) { bf_put(zr_file, (u8) zr_acc);  zr_acc >>= 8;  zr_n -= 8; }
  return v;
}

static u16 zr_rcp[257];
static u32 zr_ucum[257];

static void zr_init(void) {
  int i;
  if (zr_rcp[0]) return;
  Fi(257, zr_ucum[i] = (u32) i);
  Fi(257,
    u32 q = (65536UL * 256 + (u32) (256 + i) / 2) / (u32) (256 + i);
    zr_rcp[i] = (u16) MIN(65535, q));
}

static u32 zuni(u32 v, u32 V) {
  u32 base = 0;
  while (V > 1) {
    int nb = (int) blr_ilog(V - 1);
    u32 h = 1UL << (nb - 1), vn;
    int top;
    if (V == 2 * h) return base + zraw(v, nb);
    vn = nb >= 9 ? V >> (nb - 9) : V << (9 - nb);
    top = zfix(zr_rcp[vn - 256], v >= h);
    if (!top) return base + zraw(v, nb - 1);
    base += h;  v -= h;  V -= h;
  }
  return base;
}

static zc * za;
static u32 zan;
static sz zacap;

static u32 za_alloc(int n) {
  u32 o;
  za = xgrow(za, &zacap, (sz) zan + (u32) n, sizeof *za, 1UL << 12);
  o = zan;  zan += (u32) n;
  return o;
}

static void zt_seed(zc * t, const u32 * cum, int lo, int hi, int n0) {
  while (hi - lo > 1) {
    int mid = (lo + hi) >> 1;
    u32 l = cum[mid] - cum[lo], a = cum[hi] - cum[lo];
    opus_uint64 p = a ? ((opus_uint64) l << 16) / a : 32768;
    t[mid].p = (u16) CLAMP(p, 64, 65472);  t[mid].n = (u8) n0;
    zt_seed(t, cum, lo, mid, n0);
    lo = mid;
  }
}

static INLINE u32 zt_get(u32 * slot, int n, const u32 * cum, int n0) {
  if (!*slot) {
    u32 o = za_alloc(MAX(n, 1));
    int i;
    if (cum) zt_seed(za + o, cum, 0, n, n0);
    else Fi(n, za[o + i].p = 32768;  za[o + i].n = 0);
    *slot = o + 1;
  }
  return *slot - 1;
}

static INLINE int zt_code(u32 o, int n, int v, int lim) {
  int lo = 0, hi = n;
  while (hi - lo > 1) {
    int mid = (lo + hi) >> 1;
    if (zbit(za + o + mid, v >= mid, lim)) lo = mid;
    else hi = mid;
  }
  return lo;
}

static u32 zcum[1026];

static int z_native(const oprec * op, int n) {
  int i;
  zcum[0] = 0;
  if (op->kind == OP_ICDF || op->kind == OP_ICDF16) {
    u32 ft = 1UL << op->ftb, prev = ft;
    Fi(n,
      u32 e = icdf_at(op->pdf, op->kind == OP_ICDF16, i);
      zcum[i + 1] = zcum[i] + (prev - e) + 1;  prev = e);
    return 1;
  }
  return 0;
}

static int z_icdfn(const oprec * op) {
  int n = 0;
  unsigned e;
  do {
    e = icdf_at(op->pdf, op->kind == OP_ICDF16, n);
    n++;
  } while (e > 0 && n < 1024);
  return n;
}

#define ZT_N 512
#define ZT_C 16
typedef struct {
  const void * pdf;
  u32 ftb, sig;
  int n;
  u32 slot[ZT_C];
} ztabe;
static ztabe zt_tab[ZT_N];
static int zt_ntab;
static int zt_map[1024];

static ztabe * zt_find(const oprec * op) {
  const void * pdf = op->pdf;
  u32 sig = 0;
  unsigned h, j;
  if (op->site == OREC_S_SILK_SIGN) {
    sig = 1UL << 8 | ((const unsigned char *) op->pdf)[0];  pdf = NULL;
  }
  h = ptr_slot(pdf, op->ftb + sig * 40503U);
  for (j = 0; j < 1024; j++, h = (h + 1) & 1023) {
    int e = zt_map[h];
    if (!e) break;
    if (zt_tab[e - 1].pdf == pdf && zt_tab[e - 1].ftb == op->ftb &&
        zt_tab[e - 1].sig == sig) return &zt_tab[e - 1];
  }
  if (zt_ntab == ZT_N || j == 1024) return NULL;
  { ztabe * t = &zt_tab[zt_ntab++];
    t->pdf = pdf;  t->ftb = op->ftb;  t->sig = sig;  t->n = z_icdfn(op);
    memset(t->slot, 0, sizeof t->slot);
    zt_map[h] = zt_ntab;
    return t; }
}

typedef struct {
  u32 uint[OREC_S_COUNT][65][ZT_C], bits[OREC_S_COUNT][9][ZT_C];
  zc bit[OREC_S_COUNT][16][16];
  zc zero[2][4][NB][2][5][5][5][6], sign[2][4][NB][2][5][5][5][6];
  zc mag[2][4][12][5][4][5][6], esc[16];
  u32 fine[9][2][5][5][4];
  zc final[2][9][8];
  zc dyn[19][2];
  u32 theta[3][257][9][8];
  zc rep[NB][8][8];
} ozt;
static ozt * zt;
static u32 * zp_row, * zp_k;
static u32 zp_nk;
static sz zp_ck;
static zc * zq;
static u32 zqn, * zq_ix;
static sz zqcap;
#define ZP_IX(d, t, b, n) \
  ((((sz) (d) * 2 + (sz) (t)) * 21 + (sz) (b)) * (PVQ_UN + 1) + (sz) (n))

static u8 zs_hist[OREC_S_COUNT];
static int zs_frame = -1, zs_ltf, zs_tf[NB], zs_ptf[NB];

static void oz_reset(void) {
  zs_frame = -1;  zs_ltf = 0;
  memset(zs_tf, 0, sizeof zs_tf);  memset(zs_ptf, 0, sizeof zs_ptf);
  memset(zs_hist, 0, sizeof zs_hist);
}

static void oz_free(void) {
  xfree(za);  za = NULL;  zan = zacap = 0;
  xfree(zt);  zt = NULL;  xfree(zp_row);  zp_row = NULL;
  xfree(zp_k);  zp_k = NULL;  zp_nk = zp_ck = 0;
  xfree(zq);  zq = NULL;  zqn = zqcap = 0;  xfree(zq_ix);  zq_ix = NULL;
}

static void oz_init(void) {
  oz_free();
  zan = 1;
  zt_ntab = 0;  memset(zt_map, 0, sizeof zt_map);
  zt = xcalloc(1, sizeof *zt);
  if (oz_pdep) {
    zp_row = xcalloc(ZP_IX(ZP_DMAX, 0, 0, 0), sizeof *zp_row);  zp_nk = 1;
    zq_ix = xcalloc((sz) (PVQ_UN + 1) * (PVQ_UK + 1), sizeof *zq_ix);
  }
  zr_init();  oz_reset();
}

static INLINE void oz_sync(void) {
  if (orec_cframe != zs_frame) {
    zs_frame = orec_cframe;  zs_ltf = 0;
    memcpy(zs_ptf, zs_tf, sizeof zs_tf);  memset(zs_tf, 0, sizeof zs_tf);
    memset(fr_fn, 0, sizeof fr_fn);
    band_qi[0] = band_qi[1] = 0;
  }
}

static INLINE int zbgrp(int b) {
  return b < 4 ? 0 : b < 10 ? 1 : b < 16 ? 2 : 3;
}

static int oz_icdf(oprec * op) {
  ztabe * t = zt_find(op);
  int v = op->v, n;
  if (!t) {
    n = z_icdfn(op);  z_native(op, n);
    { u32 o = za_alloc(n);  zt_seed(za + o, zcum, 0, n, ZS_SIDE);
      v = zt_code(o, n, v, ZL_SIDE);  zan = o; }
    return v;
  }
  n = t->n;
  { u32 * s = &t->slot[zs_hist[op->site] & (ZT_C - 1)];
    if (!*s) z_native(op, n);
    v = zt_code(zt_get(s, n, zcum, ZS_SIDE), n, v, ZL_SIDE); }
  return v;
}

static int oz_logp(oprec * op) {
  int s = op->site, b = clampb(orec_band);
  zc * c;
  if (s == OREC_S_CELT_TF) {
    c = &zt->bit[s][zs_ptf[b] << 2 | zs_ltf << 1 | (orec_trans & 1)]
                [(b < 10) << 3 | MIN(7, op->ftb)];
  } else if (s == OREC_S_CELT_DYNALLOC) {
    int e = dn_ctx();
    c = &zt->dyn[MIN(e, 18)][dn_k > 0];
  } else c = &zt->bit[s][op->ftb & 15][zs_hist[s] & 15];
  return zbit_at(c, logp_init(op), op->v, ZL_SIDE);
}

static void oz_logp_note(const oprec * op, int v) {
  int s = op->site, b = clampb(orec_band);
  if (s == OREC_S_CELT_TF) { zs_tf[b] = v;  zs_ltf = v; }
  else if (s == OREC_S_CELT_DYNALLOC) dn_k += v;
  zs_hist[s] = (u8) ((zs_hist[s] << 1 | v) & 15);
}

static int oz_laplace(oprec * op) {
  int b = clampb(orec_band), c = orec_ch & 1, it = orec_intra & 1;
  int lm = orec_LM & 3, v = op->v, a, j, neg;
  int dq = lap_dq(b, c);
  int pq = CLAMP(prev_qi[b][c], -2, 2) + 2, bq = CLAMP(band_qi[c], -2, 2) + 2;
  int xq = c && c0_qib == b ? CLAMP(c0_qi, -2, 2) + 2 : 5, bz = b;
  u32 fs = (u32) op->aux * 2;
  if (om_tier >= 2) { pq = 2;  bz = 0; }
  a = ABS(v);
  if (!zbit_at(&zt->zero[it][lm][bz][c][dq][bq][pq][xq],
               (u16) CLAMP(fs, 64, 65472), a != 0, ZL_COARSE))
    return 0;
  neg = zbit_at(&zt->sign[it][lm][bz][c][dq][bq][pq][xq], 32768, v < 0,
                ZL_COARSE);
  for (j = 1; j < 12; j++) {
    zc * m = om_tier >= 2 ? &zt->mag[it][lm][j][dq][0][0][0]
                          : &zt->mag[it][lm][j][dq][zbgrp(b)][bq][xq];
    if (!zbit_at(m, 40000, a > j, ZL_COARSE)) break;
  }
  if (j == 12) {
    u32 x = om_mode == OM_DEC ? 0 : (u32) (a - 12);
    int nb = om_mode == OM_DEC ? 0 : (int) blr_ilog(x + 1) - 1, k;
    Fk(16, if (!zbit_at(&zt->esc[k], 32768, k < nb, ZL_COARSE)) break);
    nb = k;
    x = (1UL << nb | zraw(x + 1, nb)) - 1;
    a = 12 + (int) x;
  } else a = j;
  return neg ? -a : a;
}

static void oz_coarse_note(int b, int c, int v) {
  prev_qi[b][c] = v;  band_qi[c] = v;
  en_coarse(b, c, v);
  if (!c) { c0_qi = v;  c0_qib = b; }
}

static int oz_fine(oprec * op) {
  int b = clampb(orec_band), c = orec_ch & 1, nb = (int) op->ftb, v = op->v;
  int xf = fine_xf(b, c, nb, 4), fq = fine_fq(b, c, nb);
  u32 o = zt_get(om_tier >= 3 ? &zt->fine[nb][c][fq][0][0]
                              : &zt->fine[nb][c][fq][xf][zbgrp(b)],
                 1 << nb, ZS_FINE ? zr_ucum : NULL, ZS_FINE);
  return zt_code(o, 1 << nb, v, ZL_FINE);
}

static int oz_theta(oprec * op, int mode) {
  int qn = (int) op->nsym - 1, b = clampb(orec_band), n = qn + 1, v = op->v;
  int px, np, q;
  tnode * me;
  u32 * s, o;
  if (n <= 1) return 0;
  if (qn > 256) return (int) zuni((u32) v, (u32) n);
  me = th_near(b, (int) op->aux, qn, &px, &np);
  q = th_q(px, np, n);
  s = &zt->theta[mode][qn][MIN(8, q)][om_tier >= 3 ? 0 : bktN((int) op->aux)];
  if (!*s) {
    u32 raw[258];
    th_raw(mode, qn, raw);  cum_of(zcum, raw, n);
  }
  o = zt_get(s, n, zcum, ZS_THETA);
  v = zt_code(o, n, v, ZL_THETA);
  me->v = (i16) v;  me->qn = (u16) qn;  me->frame = orec_frame;
  return v;
}

static u32 zq_tree(int N, int K) {
  u32 * s = &zq_ix[(sz) N * (PVQ_UK + 1) + (sz) K];
  if (!*s) {
    u32 raw[PVQ_UK + 2], o;
    int j, sc;
    split_raw(raw, N / 2, N - N / 2, K);
    pvq_widen(raw, K + 1, &sc);
    zcum[0] = 0;
    Fj(K + 1, zcum[j + 1] = zcum[j] + (raw[j] >> 4) + 1);
    zq = xgrow(zq, &zqcap, (sz) zqn + (u32) K + 1, sizeof *zq, 1UL << 12);
    o = zqn;  zqn += (u32) K + 1;
    zq[o].p = 32768;  zq[o].n = 0;
    zt_seed(zq + o, zcum, 0, K + 1, ZS_SPLIT);
    *s = o + 1;
  }
  return *s - 1;
}

static u32 oz_split_tree(int dep, int N, int K) {
  u32 * r = &zp_row[ZP_IX(dep, orec_trans & 1, om_tier >= 2 || orec_band < 0
                                               ? 0 : MIN(20, orec_band), N)];
  u32 * s;
  if (!*r) {
    zp_k = xgrow(zp_k, &zp_ck, (sz) zp_nk + PVQ_UK + 1, sizeof *zp_k, 1UL << 14);
    memset(zp_k + zp_nk, 0, (PVQ_UK + 1) * sizeof *zp_k);
    *r = zp_nk;  zp_nk += PVQ_UK + 1;
  }
  s = &zp_k[*r + (u32) K];
  if (!*s) {
    u32 t = zq_tree(N, K), o = za_alloc(K + 1);
    memcpy(za + o, zq + t, (sz) (K + 1) * sizeof *za);
    *s = o + 1;
  }
  return *s - 1;
}

static int * zw_y;
static int zw_pos, zw_n, zw_k;
static u32 zw_i;

static void zw_to(int at) {
  int n = zw_n, k = zw_k;
  u32 i = zw_i;
  while (zw_pos < at) {
    if (n > 2) { zw_y[zw_pos++] = cw_step(pvq_ut, n, &k, &i);  n--; }
    else {
      if (n == 2) { cw_two(k, i, zw_y + zw_pos);  zw_pos += 2; }
      else zw_y[zw_pos++] = i ? -k : k;
      n = 0;  k = 0;  i = 0;
    }
  }
  zw_n = n;  zw_k = k;  zw_i = i;
}

static int zl_a, zl_k;
static u32 zl_i;

static void oz_pvq(int a, int N, int K, int dep, int last) {
  int * y = zw_y;
  for (;;) {
    int N1, k = 0, j;
    if (K == 0) {
      if (om_mode == OM_DEC) {
        Fj(N, y[a + j] = 0);
        if (last) { zl_a = a;  zl_k = 0;  zl_i = 0; }
      }
      return;
    }
    if (N == 1 || K == 1 || dep >= oz_pdep) {
      u32 V = N == 1 ? 2 : pvq_Vin(N, K), x;
      if (om_mode == OM_DEC) {
        x = N == 1 ? zraw(0, 1) : zuni(0, V);
        if (last) { zl_a = a;  zl_k = K;  zl_i = x; }
        else if (N == 1) y[a] = x ? -K : K;
        else if (K == 1) {
          Fj(N, y[a + j] = 0);
          if (x < (u32) N) y[a + x] = 1;
          else y[a + 2 * N - 1 - (int) x] = -1;
        }
        else cw_dec(pvq_ut, N, K, x, y + a);
      } else {
        x = last && zw_pos == a ? zw_i : N == 1 ? (u32) (y[a] < 0)
          : K == 1 ? pvq_enc1(N, y + a) : cw_enc(pvq_ut, N, K, y + a);
        if (N == 1) zraw(x, 1);
        else zuni(x, V);
      }
      return;
    }
    N1 = N / 2;
    if (om_mode != OM_DEC) {
      zw_to(a + N1);
      Fj(N1, k += ABS(y[a + j]));
    }
    k = zt_code(oz_split_tree(dep, N, K), K + 1, k, ZL_SPLIT);
    oz_pvq(a, N1, k, dep + 1, 0);
    a += N1;  N -= N1;  K -= k;  dep++;
  }
}

static int oz_uint(oprec * op) {
  u32 V = op->nsym;
  int v = op->v;
  if (op->site == OREC_S_CELT_PVQ && orec_pvqK > 0) {
    int N = orec_pvqN, K = orec_pvqK, b = clampb(orec_band);
    int nb = bktN(N), kb = bktK(K);
    int hv = have_pvq[b][nb][kb] && last_pvqV[b][nb][kb] == V;
    u32 last = last_pvq[b][nb][kb], x = (u32) v;
    orec_pvqK = 0;
    if (hv && zbit_at(&zt->rep[b][nb][kb], (u16) (65535U - (65536U >> 10)),
                      om_mode != OM_DEC && x == last, ZL_SIDE))
      return (int) last;
    if (oz_pdep && pvq_fits(N, K, V)) {
      static int y[PVQ_UN + 2];
      zw_y = y;  zw_pos = 0;  zw_n = N;  zw_k = K;  zw_i = x;
      oz_pvq(0, N, K, 0, 1);
      if (om_mode == OM_DEC) x = cw_idx(pvq_ut, N, zw_y, zl_a, zl_i, zl_k);
    } else if (hv) {
      u32 z = zuni(x > last ? x - 1 : x, V - 1);
      if (om_mode == OM_DEC) x = z >= last ? z + 1 : z;
    } else x = zuni(x, V);
    last_pvq[b][nb][kb] = x;  last_pvqV[b][nb][kb] = V;
    have_pvq[b][nb][kb] = 1;
    return (int) x;
  }
  if (op->site == OREC_S_CELT_THETA) return oz_theta(op, 1);
  if (V >= 2 && V <= 64) {
    u32 o = zt_get(&zt->uint[op->site][V][zs_hist[op->site] & (ZT_C - 1)],
                   (int) V, NULL, 0);
    return zt_code(o, (int) V, v, ZL_SIDE);
  }
  return (int) zuni((u32) v, V);
}

static int oz_bits(oprec * op) {
  int nb = (int) op->ftb, b = clampb(orec_band), c = orec_ch & 1, v = op->v;
  if (op->site == OREC_S_CELT_FINE && nb > 0 && nb <= 8) {
    v = oz_fine(op);
    fine_upd(b, c, nb, v);
    return v;
  }
  if (op->site == OREC_S_CELT_FINALISE && nb == 1) {
    int fn = fr_fn[b][c];
    v = zbit_at(&zt->final[c][MIN(fn, 8)][fn ? fr_fv[b][c] & 7 : 0], 32768, v,
                ZL_FINE);
    final_note(b, c, v);
    return v;
  }
  if (nb > 0 && nb <= 6) {
    u32 o = zt_get(&zt->bits[op->site][nb][zs_hist[op->site] & (ZT_C - 1)],
                   1 << nb, NULL, 0);
    return zt_code(o, 1 << nb, v, ZL_SIDE);
  }
  return (int) zraw((u32) v, nb);
}

static void oz_op(oprec * op) {
  int v, b = clampb(orec_band), c = orec_ch & 1;
  oz_sync();
  switch (op->kind) {
  case OP_ICDF:
  case OP_ICDF16:
    v = oz_icdf(op);
    if (op->site == OREC_S_CELT_COARSE_SMALL) oz_coarse_note(b, c, unzig(v));
    zs_hist[op->site] = (u8) MIN(15, v);
    break;
  case OP_LOGP:
    v = oz_logp(op);
    if (op->site == OREC_S_CELT_COARSE_BIT) oz_coarse_note(b, c, -v);
    oz_logp_note(op, v);
    break;
  case OP_LAPLACE:
    v = oz_laplace(op);
    oz_coarse_note(b, c, v);
    break;
  case OP_THETA: v = oz_theta(op, (int) op->ftb);  break;
  case OP_UINT:
    v = oz_uint(op);  zs_hist[op->site] = (u8) CLAMP(v, 0, 15);
    break;
  case OP_BITS: v = oz_bits(op);  zs_hist[op->site] = (u8) (v & 15);  break;
  default: v = op->v;  break;
  }
  op->v = v;
}
