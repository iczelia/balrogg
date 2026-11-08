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

#define PVQ_UN   CW_N
#define PVQ_UK   CW_N
#define PVQ_UBAD CW_BAD
#define PVQ_MIXMAX 4

static int pvq_lev, pvq_tree;
#define PVQ_SC   1024
#define PVQ_FSC  2048
#define PVQ_INC  128
#define PVQ_FINC 32
#define PVQ_CAP  65536

#define PU(n, k) CW_U(pvq_ut, n, k)

static u32 pvq_enc1(int n, const int * y) {
  int p = 0;
  while (!y[p]) p++;
  return y[p] > 0 ? (u32) p : (u32) (2 * n - 1 - p);
}

#define PVQ_SPA  205
#define PVQ_SPWF 64
#define PVQ_SPWT 384
#define PVQ_SPMAX 32768

static INLINE int pm_lg(u32 x);
static u32 pvq_x2[256];

static u32 pvq_widen(u32 * raw, int n, int * scale) {
  int j, lm = 0, w = pvq_tree ? PVQ_SPWT : PVQ_SPWF;
  u32 s = 0;
  opus_uint64 t;
  if (!pvq_x2[0]) cw_x2(pvq_x2);
  Fj(n, if (!raw[j]) raw[j] = 1;  lm = MAX(lm, pm_lg(raw[j])));
  Fj(n,
    u32 e = (u32) (lm - pm_lg(raw[j])) * PVQ_SPA >> 8;
    raw[j] = e >> 8 >= 16 ? 1 : (u32) ((opus_uint64) (65536U >> (e >> 8)) *
                                       pvq_x2[e & 255] >> 30);
    if (!raw[j]) raw[j] = 1;
    s += raw[j]);
  t = (opus_uint64) s * (u32) w >> 16;
  *scale = (int) CLAMP(t, PVQ_SC, PVQ_SPMAX);
  return s;
}

static void split_raw(u32 * raw, int n1, int n2, int K) {
  int j;
  Fj(K + 1,
    opus_uint64 t = (opus_uint64) cw_V(pvq_ut, n1, j) *
                    (opus_uint64) cw_V(pvq_ut, n2, K - j);
    raw[j] = t >= (opus_uint64) PVQ_UBAD ? PVQ_UBAD - 1 : (u32) t);
}

static prior * prior_split_find(int n1, int n2, int K) {
  okey key = (okey) 0xA0000000UL << 32 | (okey) n1 << 18
           | (okey) n2 << 9 | (okey) K;
  prior * p = pri_find(key);
  if (!p->f) {
    u32 raw[PVQ_UK + 2], ft;
    int sc = PVQ_SC;
    split_raw(raw, n1, n2, K);
    ft = om_feat & OF_SPRI ? pvq_widen(raw, K + 1, &sc)
                           : cw_V(pvq_ut, n1 + n2, K);
    pri_build(p, key, raw, K + 1, ft, sc);
  }
  return p;
}

static prior * prior_feat(int N, int K) {
  okey key = (okey) 0xA2000000UL << 32 | (okey) N << 9 | (okey) K;
  prior * p = pri_find(key);
  if (!p->f) {
    const prior * b0 = prior_split(N / 2, N - N / 2, K);
    u32 raw[PVQ_UK + 2], ft = 0;
    int j;
    Fj(K + 1, raw[j] = b0->f[j];  ft += raw[j]);
    pri_build(p, key, raw, K + 1, ft, PVQ_FSC);
  }
  return p;
}

static prior ** split_pr;

static prior * prior_split(int n1, int n2, int K) {
  prior ** c;
  if (!split_pr)
    split_pr = xcalloc((sz) (PVQ_UN + 1) * (PVQ_UK + 1), sizeof *split_pr);
  c = split_pr + (sz) (n1 + n2) * (PVQ_UK + 1) + (sz) K;
  if (!*c) *c = prior_split_find(n1, n2, K);
  return *c;
}

static void split_reset(int release) {
  if (split_pr && !release)
    memset(split_pr, 0, (sz) (PVQ_UN + 1) * (PVQ_UK + 1) * sizeof *split_pr);
  if (release) { xfree(split_pr);  split_pr = NULL; }
}

static INLINE u32 pvq_Vin(int n, int k) { return PU(n, k) + PU(n, k + 1); }

static int pvq_fits(int N, int K, u32 V) {
  return N >= 1 && N <= PVQ_UN && K >= 1 && K <= PVQ_UK &&
         V == cw_V(pvq_ut, N, K);
}

static int pvq_cum[PVQ_UN + 1];

static int pvq_ext;

#define PS_W 256
static short ps_cur[NB][4][PS_W], ps_prev[NB][4][PS_W], ps_prev2[NB][4][PS_W];
static int ps_cf[NB][4], ps_pf[NB][4], ps_pf2[NB][4];
static int ps_ag[NB][4], ps_dg[NB][4];
static u16 ps_rate[NB][4];

static void ps_reset(void) {
  int i, j;
  Fi(NB,
    Fj(4,
      ps_cf[i][j] = ps_pf[i][j] = ps_pf2[i][j] = -1;
      ps_ag[i][j] = ps_dg[i][j] = 0));
  memset(ps_rate, 0, sizeof ps_rate);
  if (!pvq_tree) return;
  memset(ps_cur, 0, sizeof ps_cur);  memset(ps_prev, 0, sizeof ps_prev);
  memset(ps_prev2, 0, sizeof ps_prev2);
}

static void ps_open(int b, int r) {
  if (ps_cf[b][r] == orec_frame) return;
  if (pvq_ext) {
    memcpy(ps_prev2[b][r], ps_prev[b][r], sizeof ps_cur[b][r]);
    ps_pf2[b][r] = ps_pf[b][r];
  }
  memcpy(ps_prev[b][r], ps_cur[b][r], sizeof ps_cur[b][r]);
  ps_pf[b][r] = ps_cf[b][r];
  memset(ps_cur[b][r], 0, sizeof ps_cur[b][r]);
  ps_cf[b][r] = orec_frame;  ps_ag[b][r] = ps_dg[b][r] = 0;
}

static int ps_at(int N) {
  int off = orec_pvqoff;
  return off < 0 || off + N > PS_W ? 0 : off;
}

static void ps_store(int b, int r, int off, int N, const int * y) {
  int j;
  Fj(N, if (off + j < PS_W) ps_cur[b][r][off + j] = (short) y[j]);
  if (ps_ag[b][r] + ps_dg[b][r]) {
    i32 q = (i32) ((u32) ps_ag[b][r] * 65535U /
                   (u32) (ps_ag[b][r] + ps_dg[b][r]));
    ps_rate[b][r] = (u16) (ps_rate[b][r] + ((q - (i32) ps_rate[b][r]) >> 3));
  }
}

#define PM_NI   8
#define PM_LR   2
#define PM_SETS (PVQ_MIXMAX * 4 * 9 * 4)
#define PM_CB   0
#define PM_CX   (PM_CB + NB * PVQ_MIXMAX * 4 * 10 * 10 * 4)
#define PM_CP   (PM_CX + PVQ_MIXMAX * 4 * 11 * 9 * 2)
#define PM_NC   (PM_CP + PVQ_MIXMAX * 4 * 11 * 9 * 2)
#define SG_SETS (2 * 16 + 3 * 4)
#define SG_NC   (SG_C5 + 2 * NB * 8)
#define SG_C1   0
#define SG_C2   (SG_C1 + 2 * 64)
#define SG_C3   (SG_C2 + 2 * NB * 64)
#define SG_C4   (SG_C3 + 2 * 8 * 16)
#define SG_C5   (SG_C4 + 2 * 6 * 6 * 16)
#define SN_C1   0
#define SN_C2   (SN_C1 + 3 * 4 * 6 * 6)
#define SN_C3   (SN_C2 + NB * 3 * 3 * 6)
#define SN_NC   (SN_C3 + 3 * 3 * 9 * 2)
#define PM_CLIM 255
#define PVQ_SNDENS 4

static short pm_w[PM_SETS][PM_NI], sg_w[SG_SETS][PM_NI];
static u16 pm_p[PM_NC], sg_p[SG_NC], sn_p[SN_NC];
static u8 pm_n[PM_NC], sg_n[SG_NC], sn_n[SN_NC];
static int pvq_bl, om_side;

enum { PO_BLEND, PO_MIX, PO_MIXX, PO_N };
static u8 pvq_opt[PVQ_MIXMAX], pvq_dflt[PVQ_MIXMAX];
static int pvq_smax, pvq_xok;
static int pvq_free;
static int om_stat, st_virt;
static opus_uint64 st_cost[PVQ_MIXMAX][PO_N], st_acc;
static u32 st_n[PVQ_MIXMAX];

static u16 pm_lgt[257];

static void pm_lginit(void) {
  int i, j;
  if (pm_lgt[256]) return;
  Fi(257,
    u32 x = 256 + (u32) i, r = 0;
    Fj(8,
      x = x * x;
      r <<= 1;
      if (x >= 1UL << 17) { x >>= 9;  r |= 1; } else x >>= 8);
    pm_lgt[i] = (u16) r);
}

static void pm_reset(void) {
  int i, j;
  pm_lginit();
  Fi(PM_SETS, Fj(PM_NI, pm_w[i][j] = (short) (j < 3 ? 2731 : 0)));
  Fi(SG_SETS, Fj(PM_NI, sg_w[i][j] = 0));
  Fi(PM_NC, pm_p[i] = 0x8000);  memset(pm_n, 0, sizeof pm_n);
  Fi(SG_NC, sg_p[i] = 0x8000);  memset(sg_n, 0, sizeof sg_n);
  Fi(SN_NC, sn_p[i] = 0x8000);  memset(sn_n, 0, sizeof sn_n);
}

static u32 lg_frac(u32 m, int k, int n) {
  opus_uint64 y = (opus_uint64) m << (30 - k);
  u32 r = 0;
  int j;
  Fj(n,
    y = (y * y) >> 30;  r <<= 1;
    if (y >= (opus_uint64) 2 << 30) { y >>= 1;  r |= 1; });
  return r;
}

static void st_lginit(void) {
  u32 i;
  if (st_lgt) return;
  st_lgt = xmalloc(32769 * sizeof *st_lgt);
  for (i = 0; i <= 32768; i++) st_lgt[i] = lg_frac(32768 + i, 15, 16);
}

static opus_uint64 st_lg(u32 x) {
  int e = (int) blr_ilog(x) - 1;
  u32 m;
  if (e <= 15) m = x << (15 - e);
  else {
    m = (u32) (((opus_uint64) x + ((opus_uint64) 1 << (e - 16))) >> (e - 15));
    if (m == 65536) { m = 32768;  e++; }
  }
  return (opus_uint64) e * 65536 + st_lgt[m - 32768];
}

static INLINE int pm_lg(u32 x) {
  int e = (int) blr_ilog(x) - 1;
  u32 m = e >= 8 ? x >> (e - 8) : x << (8 - e);
  return e * 256 + pm_lgt[m - 256];
}

static INLINE short pm_st2(u32 l, u32 r) {
  int d = (pm_lg(r) - pm_lg(l)) * 177 >> 8;
  return (short) CLAMP(d, -2047, 2047);
}

static INLINE short pm_stc(u16 p0) { return cm_str16[65536 - (u32) p0]; }

static INLINE int pm_mix(short * w, const short * st, int bit, int n) {
  i32 dot = 0;
  int i, d, e;
  u32 p1, p0;
#if OM_SSE2
  __m128i x0 = _mm_loadu_si128((const __m128i *) (const void *) st);
  __m128i w0 = _mm_loadu_si128((const __m128i *) (const void *) w);
  __m128i x1 = _mm_setzero_si128(), w1 = x1, s = _mm_madd_epi16(x0, w0), t, ev;
  if (n > 8) {
    x1 = _mm_loadu_si128((const __m128i *) (const void *) (st + 8));
    w1 = _mm_loadu_si128((const __m128i *) (const void *) (w + 8));
    s = _mm_add_epi32(s, _mm_madd_epi16(x1, w1));
  }
  s = _mm_add_epi32(s, _mm_srli_si128(s, 8));
  s = _mm_add_epi32(s, _mm_srli_si128(s, 4));
  dot = (i32) _mm_cvtsi128_si32(s);
#else
  Fi(n, dot += (i32) w[i] * st[i]);
#endif
  d = dot >> 13;  d = CLAMP(d, -2047, 2047);
  p1 = cm_squash16[d + 2048];  p0 = 65536 - p1;
  if (st_virt) st_acc += 16 * 65536 - st_lg(bit ? p1 : p0);
  else bit = om_fbit(p0, bit);
  PROF(prof_sym(om_comp, bit ? p0 : 0, bit ? 65536 : p0, 65536));
  e = ((bit << 12) - (int) (p1 >> 4)) * PM_LR;
#if OM_SSE2
  ev = _mm_set1_epi16((short) e);
  t = _mm_mulhi_epi16(_mm_add_epi16(x0, x0), ev);
  t = _mm_srai_epi16(_mm_add_epi16(t, _mm_set1_epi16(1)), 1);
  _mm_storeu_si128((__m128i *) (void *) w, _mm_adds_epi16(w0, t));
  if (n > 8) {
    t = _mm_mulhi_epi16(_mm_add_epi16(x1, x1), ev);
    t = _mm_srai_epi16(_mm_add_epi16(t, _mm_set1_epi16(1)), 1);
    _mm_storeu_si128((__m128i *) (void *) (w + 8), _mm_adds_epi16(w1, t));
  }
  (void) i;
#else
  Fi(n,
    i32 v = w[i] + (((((i32) st[i] * 2 * e) >> 16) + 1) >> 1);
    w[i] = (short) CLAMP(v, -32768, 32767));
#endif
  return bit;
}

static int pm_bit(short * w, const short * st, int bit) {
  return pm_mix(w, st, bit, PM_NI);
}

static int pm_bit16(short * w, const short * st, int bit) {
  return pm_mix(w, st, bit, 16);
}

static INLINE void pm_learn(u16 * p, u8 * n, int bit) {
  *p = rc_adapt(*p, n, PM_CLIM, bit);
}

static int bkt9(int x) {
  return x <= 2 ? MAX(x, 0) : x <= 4 ? 3 : x <= 8 ? 4 : x <= 16 ? 5
       : x <= 32 ? 6 : x <= 64 ? 7 : 8;
}

static int pvq_b, pvq_r, pvq_off;
static const short * pvq_xs;
static u32 pvq_pp[PS_W + 1], pvq_px[PS_W + 1];
static int pvq_hp, pvq_hx;
static u32 pvq_pp2[PS_W + 1];
static int pvq_hp2, pvq_sh[10], pvq_right, pvq_cc, pvq_nc;
static int pvq_ls, pvq_la, pvq_dens;

static int pvq_share(const u32 * pre, int a, int N1, int N) {
  u32 L = pre[a + N1] - pre[a], R = pre[a + N] - pre[a + N1];
  int s;
  if (!(L + R)) return 1;
  s = (int) (L * 7 / (L + R));
  return 2 + MIN(6, s);
}

static i32 pvq_khat(int K, u32 L, u32 T) {
  return T ? (i32) (((opus_uint64) K * L << 8) / T) : -1;
}

static INLINE int pvq_rel(int mid, i32 kq) {
  i32 d, m;
  if (kq < 0) return 0;
  d = (i32) mid * 256 - kq;  m = ABS(d);
  return 1 + (m <= 128 ? 0 : m <= 384 ? 1 : m <= 768 ? 2 : m <= 1536 ? 3 : 4) +
         (d > 0) * 5;
}

static INLINE int ps_cls(void) {
  return pvq_tree ? (orec_spread & 3) | (orec_trans & 1) << 2 : 0;
}

static INLINE void pvq_tabs(int dep, int N, int K, int sp, int sx, int par,
                            u32 * o) {
  o[0] = pctx_off(pvq_b, dep, N, K, 0, ps_cls());
  o[1] = sx ? pctx_off(pvq_b, dep, N, K, 2, sx)
       : sp ? pctx_off(pvq_b, dep, N, K, 1, sp) : 0;
  o[2] = par ? pctx_off(pvq_b, dep, N, K, 3, par) : 0;
}

static INLINE void pvq_count(const u32 * o, int k) {
  int j;
  if (st_virt) return;
  Fj(3, if (o[j]) ctx_update(ctx_at(o[j]), k, PVQ_INC, PVQ_CAP));
}

#define PX_NC (PVQ_MIXMAX * 4 * 16 * 11 * 2)
static short px_w[PM_SETS][16];
static u16 px_p[PM_NC], px_e[4][PX_NC];
static u8 px_n[PM_NC], px_en[4][PX_NC];

static void px_reset(void) {
  int i, j;
  Fi(PM_SETS, Fj(16, px_w[i][j] = (short) (j < 3 ? 2731 : 0)));
  Fi(PM_NC, px_p[i] = 0x8000);  memset(px_n, 0, sizeof px_n);
  Fi(4, Fj(PX_NC, px_e[i][j] = 0x8000));  memset(px_en, 0, sizeof px_en);
}

static INLINE i32 pvq_kpre(const u32 * pre, int K, int a, int N1, int N) {
  return pvq_khat(K, pre[a + N1] - pre[a], pre[a + N] - pre[a]);
}

static int pvq_split(int dep, int N, int K, int a, int N1, int par, int k) {
  static u32 cum[3][PVQ_UK + 2];
  u32 o[3];
  i32 kx = -1, kp = -1;
  int sp = 0, sx = 0, lo = 0, hi = K + 1, step = 0, bk = bkt9(K), cb, i;
  const mctx * t1, * t2;
  if (pvq_hp) {
    sp = pvq_share(pvq_pp, a, N1, N);
    kp = pvq_kpre(pvq_pp, K, a, N1, N);
  }
  if (pvq_hx) {
    sx = pvq_share(pvq_px, a, N1, N);
    kx = pvq_kpre(pvq_px, K, a, N1, N);
  }
  pvq_tabs(dep, N, K, sp, sx, par, o);
  t1 = o[1] ? ctx_at(o[1]) : NULL;  t2 = o[2] ? ctx_at(o[2]) : NULL;
  cum_of(cum[0], ctx_fq(ctx_at(o[0])), K + 1);
  if (t1) cum_of(cum[1], ctx_fq(t1), K + 1);
  if (t2) cum_of(cum[2], ctx_fq(t2), K + 1);
  cb = PM_CB + (((pvq_b * PVQ_MIXMAX + dep) * 4 * 10 + sp) * 10 + sx) * 4 + par;
  while (hi - lo > 1) {
    short st[PM_NI];
    int sb = MIN(3, step), mid, bit, ci[3];
    u32 half = (cum[0][hi] - cum[0][lo]) / 2;
    u32 ma = (u32) lo + 1, mb = (u32) hi - 1;
    while (ma < mb) {
      u32 m = (ma + mb) / 2;
      if (cum[0][m + 1] - cum[0][lo] > half) mb = m;  else ma = m + 1;
    }
    mid = (int) ma;
    st[0] = pm_st2(cum[0][mid] - cum[0][lo], cum[0][hi] - cum[0][mid]);
    st[1] = t1 ? pm_st2(cum[1][mid] - cum[1][lo], cum[1][hi] - cum[1][mid]) : 0;
    st[2] = t2 ? pm_st2(cum[2][mid] - cum[2][lo], cum[2][hi] - cum[2][mid]) : 0;
    ci[0] = cb + sb * 400;
    ci[1] = PM_CX + (((dep * 4 + sb) * 11 + pvq_rel(mid, kx)) * 9 + bk) * 2 +
            (mid * 2 > K);
    ci[2] = PM_CP + (((dep * 4 + sb) * 11 + pvq_rel(mid, kp)) * 9 + bk) * 2 +
            (mid * 2 > K);
    Fi(3, st[3 + i] = pm_stc(pm_p[ci[i]]));
    st[6] = 256;  st[7] = 0;
    bit = pm_bit(pm_w[(((dep * 4 + sb) * 9 + bk) * 4 + (sx > 0) * 2 +
                       (sp > 0))], st, k >= mid);
    Fi(3, pm_learn(pm_p + ci[i], pm_n + ci[i], bit));
    if (bit) lo = mid;  else hi = mid;
    step++;
  }
  pvq_count(o, lo);
  return lo;
}

static int pvq_splitx(int dep, int N, int K, int a, int N1, int par, int k) {
  static u32 cum[3][PVQ_UK + 2];
  u32 o[3];
  i32 kx = -1, kp = -1, k2 = -1;
  i32 ku = (i32) (((opus_uint64) K * (u32) N1 << 8) / (u32) N);
  int sp = 0, sx = 0, lo = 0, hi = K + 1, step = 0, bk = bkt9(K), cb, i, f[4];
  const mctx * t1, * t2;
  if (pvq_hp) {
    sp = pvq_share(pvq_pp, a, N1, N);
    kp = pvq_kpre(pvq_pp, K, a, N1, N);
  }
  if (pvq_hx) {
    sx = pvq_share(pvq_px, a, N1, N);
    kx = pvq_kpre(pvq_px, K, a, N1, N);
  }
  if (pvq_hp2) k2 = pvq_kpre(pvq_pp2, K, a, N1, N);
  { int d = pvq_hp ? K - (int) (pvq_pp[a + N] - pvq_pp[a]) : 0;
    f[0] = !pvq_hp ? 0 : d < -4 ? 1 : d < -1 ? 2 : d == -1 ? 3 : d == 0 ? 4
         : d == 1 ? 5 : d <= 4 ? 6 : 7;
    f[1] = pvq_hp2 ? pvq_share(pvq_pp2, a, N1, N) : 0;
    f[2] = (dep >= 2 ? pvq_sh[dep - 2] : 0) * 4 + (pvq_right ? pvq_sh[dep] : 0);
    f[3] = pvq_nc ? 1 + MIN(7, pvq_cc / (pvq_nc * 16)) : 0; }
  pvq_tabs(dep, N, K, sp, sx, par, o);
  t1 = o[1] ? ctx_at(o[1]) : NULL;  t2 = o[2] ? ctx_at(o[2]) : NULL;
  cum_of(cum[0], ctx_fq(ctx_at(o[0])), K + 1);
  if (t1) cum_of(cum[1], ctx_fq(t1), K + 1);
  if (t2) cum_of(cum[2], ctx_fq(t2), K + 1);
  cb = PM_CB + (((pvq_b * PVQ_MIXMAX + dep) * 4 * 10 + sp) * 10 + sx) * 4 + par;
  while (hi - lo > 1) {
    short st[16];
    int sb = MIN(3, step), mid, bit, ci[3], ce[4], up;
    u32 half = (cum[0][hi] - cum[0][lo]) / 2;
    u32 ma = (u32) lo + 1, mb = (u32) hi - 1;
    while (ma < mb) {
      u32 m = (ma + mb) / 2;
      if (cum[0][m + 1] - cum[0][lo] > half) mb = m;  else ma = m + 1;
    }
    mid = (int) ma;  up = mid * 2 > K;
    st[0] = pm_st2(cum[0][mid] - cum[0][lo], cum[0][hi] - cum[0][mid]);
    st[1] = t1 ? pm_st2(cum[1][mid] - cum[1][lo], cum[1][hi] - cum[1][mid]) : 0;
    st[2] = t2 ? pm_st2(cum[2][mid] - cum[2][lo], cum[2][hi] - cum[2][mid]) : 0;
    ci[0] = cb + sb * 400;
    ci[1] = PM_CX + (((dep * 4 + sb) * 11 + pvq_rel(mid, kx)) * 9 + bk) * 2 +
            up;
    ci[2] = PM_CP + (((dep * 4 + sb) * 11 + pvq_rel(mid, kp)) * 9 + bk) * 2 +
            up;
    Fi(3, st[3 + i] = pm_stc(px_p[ci[i]]));
    st[6] = 256;
    ce[0] = (((dep * 4 + sb) * 16 + f[0]) * 11 + pvq_rel(mid, kp)) * 2 + up;
    ce[1] = (((dep * 4 + sb) * 16 + f[1]) * 11 + pvq_rel(mid, k2)) * 2 + up;
    ce[2] = (((dep * 4 + sb) * 16 + f[2]) * 11 + pvq_rel(mid, ku)) * 2 + up;
    ce[3] = (((dep * 4 + sb) * 16 + f[3]) * 11 + pvq_rel(mid, ku)) * 2 + up;
    Fi(4, st[7 + i] = pm_stc(px_e[i][ce[i]]));
    Fi(5, st[11 + i] = 0);
    bit = pm_bit16(px_w[(((dep * 4 + sb) * 9 + bk) * 4 + (sx > 0) * 2 +
                         (sp > 0))], st, k >= mid);
    Fi(3, pm_learn(px_p + ci[i], px_n + ci[i], bit));
    Fi(4, pm_learn(px_e[i] + ce[i], px_en[i] + ce[i], bit));
    if (bit) lo = mid;  else hi = mid;
    step++;
  }
  pvq_count(o, lo);
  return lo;
}

#define BL_LR    6
#define BL_LR2   4
static int bl_lr = BL_LR;
#define BL_FLOOR 512

static i32 bl_w[8 * 9 * 4][3];

static void bl_reset(void) {
  int i;
  Fi(8 * 9 * 4, bl_w[i][0] = 16384;  bl_w[i][1] = 8192;  bl_w[i][2] = 8192);
}

typedef struct { u32 o[3], r[3];  i32 * w, ws;  int nt, ix[3]; } blx;

static u32 bl_cum(int dep, int N, int K, int a, int N1, int par, u32 * cum,
                  blx * x) {
  const u32 * f[3];
  int sp = 0, sx = 0, n = K + 1, j, i;
  if (pvq_hp) sp = pvq_share(pvq_pp, a, N1, N);
  if (pvq_hx) sx = pvq_share(pvq_px, a, N1, N);
  pvq_tabs(dep, N, K, sp, sx, par, x->o);
  x->w = bl_w[(MIN(7, dep) * 9 + bkt9(K)) * 4 + (x->o[1] != 0) * 2 +
              (x->o[2] != 0)];
  x->nt = 0;  x->ws = 0;
  Fj(3, if (x->o[j]) { x->ix[x->nt++] = j;  x->ws += x->w[j]; });
  Fi(x->nt,
    const mctx * c = ctx_at(x->o[x->ix[i]]);
    u32 wv = (u32) ((opus_uint64) x->w[x->ix[i]] * 32768 / (u32) x->ws);
    f[i] = ctx_fq(c);
    x->r[i] = (wv << 16) / (c->ptot + c->tot));
  cum[0] = 0;
  if (x->nt == 3) {
    Fj(n,
      cum[j + 1] = cum[j] + 1 + ((f[0][j] * x->r[0] + f[1][j] * x->r[1] +
                                  f[2][j] * x->r[2]) >> 16));
  } else {
    Fj(n,
      u32 t = 0;
      Fi(x->nt, t += f[i][j] * x->r[i]);
      cum[j + 1] = cum[j] + 1 + (t >> 16));
  }
  return cum[n];
}

static void bl_learn(const blx * x, int k) {
  int i, j;
  if (x->nt > 1) {
    u32 tot = 0, c[3];
    Fi(x->nt,
      c[i] = ctx_fq(ctx_at(x->o[x->ix[i]]))[k] * x->r[i] >> 16;
      tot += c[i]);
    if (tot) Fi(x->nt,
      i32 tgt = (i32) ((opus_uint64) c[i] * (u32) x->ws / tot), v;
      j = x->ix[i];
      v = x->w[j] + ((tgt - x->w[j]) >> bl_lr);
      x->w[j] = MAX(v, BL_FLOOR));
  }
  pvq_count(x->o, k);
}

static int pvq_blend(int dep, int N, int K, int a, int N1, int par, int k) {
  static u32 cum[PVQ_UK + 3];
  blx x;
  u32 ft = bl_cum(dep, N, K, a, N1, par, cum, &x), s, e;
  int n = K + 1;
  if (st_virt) st_acc += st_lg(ft) - st_lg(cum[k + 1] - cum[k]);
  else if (om_mode == OM_DEC) {
    rc_dec_norm(D);
    e = D->range / ft;
    s = MIN(D->code / e, ft - 1);
    for (k = 0; k + 1 < n && cum[k + 1] <= s; k++) ;
    orc_dec_cum_upd(D, e, cum[k], cum[k + 1], ft);
  } else p_cum(cum[k], cum[k + 1], ft);
  PROF(prof_sym(om_comp, cum[k], cum[k + 1], ft));
  bl_learn(&x, k);
  return k;
}

static void pvq_cost(int dep, int N, int K, int a, int N1, int par, int k) {
  u32 o[3];
  int sp = pvq_hp ? pvq_share(pvq_pp, a, N1, N) : 0;
  int sx = pvq_hx ? pvq_share(pvq_px, a, N1, N) : 0;
  st_virt = 1;
  st_acc = 0;  pvq_blend(dep, N, K, a, N1, par, k);
  st_cost[dep][PO_BLEND] += st_acc;
  st_acc = 0;  pvq_split(dep, N, K, a, N1, par, k);
  st_cost[dep][PO_MIX] += st_acc;
  if (pvq_ext) {
    st_acc = 0;  pvq_splitx(dep, N, K, a, N1, par, k);
    st_cost[dep][PO_MIXX] += st_acc;
  }
  st_virt = 0;  st_n[dep]++;
  pvq_tabs(dep, N, K, sp, sx, par, o);
  pvq_count(o, k);
}

static int pvq_sign(int a, int mag, int neg) {
  int o = pvq_xs && a < PS_W ? pvq_xs[a] : 0, b = pvq_b, r = pvq_r & 1;
  int i, ci[5], dis;
  short st[PM_NI];
  if (!o) {
    int ls = pvq_ls, db = MIN(bkt9(a - pvq_la), 5), mb = MIN(bkt9(mag), 5), w;
    if (pvq_dens < PVQ_SNDENS) return p_sign(neg);
    ci[0] = SN_C1 + ((ls * 4 + MIN(db, 3)) * 6 + mb) * 6 + MIN(pvq_dens, 5);
    ci[1] = SN_C2 + ((b * 3 + ls) * 3 + MIN(db, 2)) * 6 + MIN(pvq_dens, 5);
    ci[2] = SN_C3 + ((ls * 3 + MIN(db, 2)) * 9 + bkt9(mag)) * 2 + (pvq_r & 1);
    Fi(3, st[i] = pm_stc(sn_p[ci[i]]));
    st[3] = 256;  st[4] = st[5] = st[6] = st[7] = 0;
    w = 32 + ls * 4 + MIN(db, 3);
    neg = pm_bit(sg_w[w], st, neg);
    Fi(3, pm_learn(sn_p + ci[i], sn_n + ci[i], neg));
    return neg;
  }
  { int a7 = MIN(ps_ag[b][pvq_r], 7), d7 = MIN(ps_dg[b][pvq_r], 7);
    int a3 = MIN(a7, 3), d3 = MIN(d7, 3), ba = ps_rate[b][pvq_r] >> 13;
    int ma = MIN(bkt9(mag), 5), mo = MIN(bkt9(ABS(o)), 5);
    ci[0] = SG_C1 + r * 64 + a7 * 8 + d7;
    ci[1] = SG_C2 + (r * NB + b) * 64 + a7 * 8 + d7;
    ci[2] = SG_C3 + (r * 8 + ba) * 16 + a3 * 4 + d3;
    ci[3] = SG_C4 + ((r * 6 + ma) * 6 + mo) * 16 + a3 * 4 + d3;
    ci[4] = SG_C5 + (r * NB + b) * 8 + ba;
    Fi(5, st[i] = pm_stc(sg_p[ci[i]]));
    st[5] = 256;  st[6] = st[7] = 0;
    dis = pm_bit(sg_w[r * 16 + a3 * 4 + d3], st, neg != (o < 0));
    Fi(5, pm_learn(sg_p + ci[i], sg_n + ci[i], dis));
    if (dis) ps_dg[b][pvq_r]++;  else ps_ag[b][pvq_r]++;
    return dis ? !(o < 0) : o < 0;
  }
}

static const int * pvq_y0;

static INLINE mctx * pctx_flat(int band, int dep, int N, int K) {
  int v = om_feat & OF_FTRANS ? orec_trans & 1 : 0;
  u32 key = (u32) v << 24 | (u32) band << 19 | (u32) dep << 16 |
            (u32) N << 8 | (u32) K;
  unsigned i = pctx_slot(key);
  for (;;) {
    if (pctxs[i].key == key) return ctx_at(pctxs[i].off);
    if (!pctxs[i].key) break;
    i = (i + 1) & ((1U << pchb) - 1);
  }
  return ctx_at(pctx_off(band, dep, N, K, 0, v));
}

static void flat_dec(int band, int N, int K, int dep, int * y) {
  int j;
  for (;;) {
    int N1, k;
    if (K == 0) { Fj(N, y[j] = 0);  return; }
    if (N == 1) { y[0] = p_sign(0) ? -K : K;  return; }
    if (dep >= pvq_lev) {
      cw_dec(pvq_ut, N, K, uni_dec(pvq_Vin(N, K)), y);
      return;
    }
    N1 = N / 2;
    k = ctx_dec(pctx_flat(band, dep, N, K), PVQ_FINC, PVQ_CAP);
    flat_dec(band, N1, k, dep + 1, y);
    y += N1;  N -= N1;  K -= k;  dep++;
  }
}

static void flat_enc(int band, int N, int K, int dep, const int * y) {
  for (;;) {
    int N1, k, at;
    if (K == 0) return;
    if (N == 1) { p_sign(y[0] < 0);  return; }
    if (dep >= pvq_lev) {
      uni_enc(cw_enc(pvq_ut, N, K, y), pvq_Vin(N, K));
      return;
    }
    N1 = N / 2;  at = (int) (y - pvq_y0);
    k = pvq_cum[at + N1] - pvq_cum[at];
    ctx_enc(pctx_flat(band, dep, N, K), PVQ_FINC, PVQ_CAP, k);
    flat_enc(band, N1, k, dep + 1, y);
    y += N1;  N -= N1;  K -= k;  dep++;
  }
}

static void pvq_node(int a, int N, int K, int dep, int par, int * y) {
  for (;;) {
    int N1, k = 0, pr, o;
    if (K == 0) return;
    if (N == 1) {
      int at = a - pvq_off, neg = om_mode == OM_DEC ? 0 : y[at] < 0;
      neg = pvq_sign(a, K, neg);
      y[at] = neg ? -K : K;
      pvq_ls = 1 + neg;  pvq_la = a;
      return;
    }
    N1 = N / 2;
    if (om_mode != OM_DEC) k = pvq_cum[a - pvq_off + N1] - pvq_cum[a - pvq_off];
    o = dep < PVQ_MIXMAX ? pvq_opt[dep] : PO_BLEND;
    if (om_stat && dep < pvq_smax) pvq_cost(dep, N, K, a, N1, par, k);
    else if (o == PO_MIX) k = pvq_split(dep, N, K, a, N1, par, k);
    else if (o == PO_MIXX) k = pvq_splitx(dep, N, K, a, N1, par, k);
    else if (pvq_bl) k = pvq_blend(dep, N, K, a, N1, par, k);
    else
      ctx_code(pctx_find(pvq_b, dep, N, K, 0, ps_cls()), PVQ_INC, PVQ_CAP, &k);
    pr = k * 4 * N > K * N1 * 5 ? 2 : k * 5 * N < K * N1 * 4 ? 0 : 1;
    if (pvq_ext) {
      if (K >= 2) {
        int dv = k * 256 / K - N1 * 256 / N;
        pvq_cc += ABS(dv);  pvq_nc++;
      }
      if (dep < 9) { pvq_sh[dep] = 1 + pr;  pvq_sh[dep + 1] = 0; }
      pvq_right = 0;
    }
    pvq_node(a, N1, k, dep + 1, 1 + pr, y);
    if (pvq_ext) pvq_right = 1;
    par = 3 - pr;
    a += N1;  N -= N1;  K -= k;  dep++;
  }
}

static const int PO_T[PO_N] = { 0, 2, 3 };

static void pvq_pick(int lam) {
  opus_uint64 base = 0, best = 0;
  int i, j, n = 1, no = pvq_xok ? PO_N : PO_MIXX, have = 0;
  Fi(pvq_smax, base += st_cost[i][pvq_dflt[i]];  n *= no);
  Fj(n,
    opus_uint64 tot = 0, v = 0;
    int r = j;
    Fi(pvq_smax,
      int o = r % no;
      r /= no;  tot += st_cost[i][o];
      v += st_cost[i][o] + ((opus_uint64) lam * (u32) PO_T[o] * st_n[i] << 8));
    if (tot > base || (have && v >= best)) continue;
    best = v;  have = 1;  r = j;
    Fi(pvq_smax, pvq_opt[i] = (u8) (r % no);  r /= no));
}

static void pvq_note(int b, int N, int K, u32 v) {
  static int y[PVQ_UN + 2];
  int r = orec_pvqrole & 3;
  cw_dec(pvq_ut, N, K, v, y);
  ps_open(b, r);  ps_store(b, r, ps_at(N), N, y);
}

static void pvq_pre(u32 * pre, const short * P, int off, int N) {
  int j;
  pre[off] = 0;
  for (j = off; j < off + N; j++) pre[j + 1] = pre[j] + (u32) ABS(P[j]);
}

static void pvq_open(int b, int r, int off, int N, int K) {
  int pr = r == 1 ? 0 : r == 3 ? 2 : -1;
  ps_open(b, r);
  pvq_b = b;  pvq_r = r;  pvq_off = off;
  pvq_hp = ps_pf[b][r] == orec_frame - 1;
  pvq_hx = pr >= 0 && ps_cf[b][pr] == orec_frame;
  pvq_xs = pvq_hx ? ps_cur[b][pr] : NULL;
  pvq_ls = 0;  pvq_la = off;  pvq_dens = bkt9(K * 4 / N);
  if (pvq_hp) pvq_pre(pvq_pp, ps_prev[b][r], off, N);
  if (pvq_ext) {
    pvq_hp2 = ps_pf2[b][r] == orec_frame - 2;  pvq_cc = pvq_nc = pvq_right = 0;
    if (pvq_hp2) pvq_pre(pvq_pp2, ps_prev2[b][r], off, N);
  }
  if (pvq_hx) pvq_pre(pvq_px, ps_cur[b][pr], off, N);
}

static void pvq_code(int b, int N, int K, int * y) {
  int r = orec_pvqrole & 3, off = ps_at(N), j;
  pvq_b = b;  pvq_off = off;
  if (om_mode != OM_DEC) {
    pvq_cum[0] = 0;
    Fj(N, pvq_cum[j + 1] = pvq_cum[j] + ABS(y[j]));
  }
  if (!pvq_tree) {
    if (om_mode == OM_DEC) flat_dec(b, N, K, 0, y);
    else { pvq_y0 = y;  flat_enc(b, N, K, 0, y); }
    return;
  }
  pvq_open(b, r, off, N, K);
  if (om_mode == OM_DEC) Fj(N, y[j] = 0);
  pvq_node(off, N, K, 0, 0, y);
  ps_store(b, r, off, N, y);
}

static int clampb(int b) { return CLAMP(b, 0, NB - 1); }

static int q5of(int x) {
  return x < -1 ? 0 : x == -1 ? 1 : x == 0 ? 2 : x == 1 ? 3 : 4;
}

static int bktN(int n) {
  return n <= 2 ? 0 : n <= 4 ? 1 : n <= 8 ? 2 : n <= 16 ? 3
       : n <= 32 ? 4 : n <= 64 ? 5 : n <= 128 ? 6 : 7;
}

static int bktK(int k) {
  return k <= 0 ? 0 : k < 3 ? 1 : k < 6 ? 2 : k < 12 ? 3
       : k < 24 ? 4 : k < 48 ? 5 : k < 96 ? 6 : 7;
}
