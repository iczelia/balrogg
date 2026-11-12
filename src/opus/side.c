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

#define SC_NI  8
#define SC_NW  512
#define SC_LIM 90
#define SC_LR  10
#define SC_LR2 32
#define SC_N0  2

static u32 (* sct)[16];
static blr_map sc_map;
static i32 scw[SC_NW][SC_NI];
static int sc_lr = SC_LR;
static int sc_rt[SC_LIM + 1];
static i16 sc_str[4096];
static i16 sc_lg[1025];

static void scm_init(void) {
  int i, j;
  cm_prepare();
  Fi(SC_LIM + 1, sc_rt[i] = (int) (32768 * 2 / (2 * i + 3)));
  Fi(SC_NW, scw[i][0] = 12288;  Fj(SC_NI - 1, scw[i][j + 1] = 2048));
  { int pi = 0, x;
    for (x = -2047; x <= 2047; x++) {
      int q = (int) (cm_squash16[x + 2048] >> 4);
      for (j = pi; j <= q && j < 4096; j++) sc_str[j] = (i16) x;
      pi = q + 1;
    }
    for (j = pi; j < 4096; j++) sc_str[j] = 2047; }
  Fi(1025, sc_lg[i] = (i16) lg_frac(1024 + (u32) i, 10, 12));
}

static void scm_free(void) { bm_free(&sc_map);  sct = NULL; }

static void sc_clear(void) {
  bm_free(&sc_map);  bm_alloc_huge(&sc_map, ((sz) 1 << scb) * sizeof *sct);
  sct = (u32 (*)[16]) (void *) sc_map.p;
}

static void om_level(const ocfg * c) {
  int i;
  om_feat = c->feat;  om_nodyn = c->nodyn;
  pvq_tree = c->lev >= 7;  pvq_lev = c->lev;  pvq_bl = c->blend;
  om_side = c->side;
  pvq_smax = c->smax;  pvq_xok = c->ext;  pvq_ext = 0;  pvq_free = c->act != 0;
  Fi(PVQ_MIXMAX,
    pvq_opt[i] = pvq_dflt[i] = (u8) (i < c->mix ? PO_MIX : PO_BLEND));
  KP = om_feat & OF_RATE ? KP2 : KP1;
  bl_lr = om_feat & OF_MIXR ? BL_LR2 : BL_LR;
  sc_lr = om_feat & OF_MIXR ? SC_LR2 : SC_LR;
  th_sc = om_feat & OF_THPRI ? (om_feat & OF_THKP1 ? KP1 : KP)[OP_THETA].scale
                             : TH_SC1;
  th_inc = om_feat & OF_THPRI ? KP[OP_THETA].inc : TH_INC1;
  orec_th1 = (om_feat & OF_TH1) != 0;
  oz_on = c->oz != 0;  oz_part = c->oz == 2;
  oz_tree = c->oz && c->lev >= 7 && !(c->oz == 3 && om_tier);
  oz_pdep = !c->oz || oz_tree ? 0 : c->lev >= 7 ? ZP_DMAX : c->lev;
}

static void om_start(void) {
  if (pvq_tree || om_side) cm_prepare();
  if (om_side) { scm_init();  sc_clear(); }
  if (pvq_tree) { pm_reset();  bl_reset(); }
  if (oz_on) oz_init();
}

static INLINE i32 sc_log(u32 x) {
  int e = (int) blr_ilog(x) - 1;
  u32 m = e >= 10 ? x >> (e - 10) : x << (10 - e);
  return (i32) e * 4096 + sc_lg[m - 1024];
}

static INLINE u32 * sc_line(okey key, int * fresh) {
  u32 * l = sct[om_hash(key, K_MIX, scb)];
  u32 chk = om_hash(key, K_GOLD, 32) | 1;
  *fresh = l[0] != chk;
  if (*fresh) {
    int i;
    l[0] = chk;
    for (i = 1; i < 16; i++) l[i] = 0x80000000UL;
  }
  return l;
}

static const u32 * pri_cum(prior * p) {
  if (!p->cum) {
    int i;
    p->cum = ar_take(&pri_ar, sizeof *p->cum * (sz) (p->n + 1));
    p->cum[0] = 0;
    Fi(p->n, p->cum[i + 1] = p->cum[i] + p->f[i]);
  }
  return p->cum;
}

static void sc_seed(u32 * l, const u32 * cum, int lo, int hi) {
  int k;
  for (k = 1; k < 16; k++) {
    int a = lo, b = hi, d = k >= 8 ? 3 : k >= 4 ? 2 : k >= 2 ? 1 : 0, j, m;
    for (j = d - 1; j >= 0 && b - a > 1; j--) {
      m = a + (b - a) / 2;
      if (k >> j & 1) a = m; else b = m;
    }
    if (b - a < 2) continue;
    m = a + (b - a) / 2;
    l[k] = (u32) ((opus_uint64) (cum[b] - cum[m]) * 65535 / (cum[b] - cum[a]))
           << 16 | SC_N0;
  }
}

static INLINE void sc_upd(u32 * s, int bit) {
  int n = (int) (*s & 255);
  i32 p = (i32) (*s >> 16);
  p += ((bit ? 65535 : 0) - p) * sc_rt[n] >> 15;
  *s = (u32) p << 16 | (u32) (n < SC_LIM ? n + 1 : n);
}

static INLINE int sc_st(u32 p1) { return sc_str[p1 >> 4]; }

static INLINE int sc_code(u32 p1, int bit) {
  u32 p0 = 65536 - p1;
  p0 = CLAMP(p0, 16, 65520);
  PROF(prof_sym(om_comp, bit ? p0 : 0, bit ? 65536 : p0, 65536));
  return om_fbit(p0, bit);
}

static INLINE int sc_mix(const int * st, int n, u32 * const * s, int ws,
                         int bit) {
  i32 * w = scw[ws], err, dot = 0;
  int i, d;
  u32 p1;
  Fi(n, dot += w[i] * st[i]);
  d = dot >> 14;
  d = CLAMP(d, -2047, 2047);
  p1 = cm_squash16[d + 2048];
  bit = sc_code(p1, bit);
  err = (((i32) bit << 12) - (i32) (p1 >> 4)) * sc_lr;
  Fi(n,
    i32 v = w[i] + ((st[i] * err + 0x20000) >> 18);
    w[i] = CLAMP(v, -131071, 131071));
  for (i = 1; i < n; i++) sc_upd(s[i], bit);
  return bit;
}

typedef struct {
  okey key[SC_NI - 1];
  int pred[SC_NI - 1];
  int n, seed, deep;
} scx;

static INLINE void sc_sym(mctx * c, int inc, int cap, prior * pr, int * v,
                          const scx * x, int ws) {
  int n = (int) c->n, lo = 0, hi = n, val = *v, dep = 0, fresh, i, k;
  u32 path = 1, * line[SC_NI], * blk = (u32 *) (c + 1), * f = blk + CB_NBLK(n);
  u32 tot = c->ptot + c->tot;
  const u32 * cum = x->seed ? pri_cum(pr) : NULL;
  val = CLAMP(val, 0, n - 1);
  while (hi - lo > 1) {
    int mid = lo + (hi - lo) / 2, st[SC_NI], bit = val >= mid, slot;
    u32 * s[SC_NI], na = 0;
    for (i = lo; i < mid && (i & ((1 << CB_SHIFT) - 1)); i++) na += f[i];
    for (; i + (1 << CB_SHIFT) <= mid; i += 1 << CB_SHIFT)
      na += blk[i >> CB_SHIFT];
    for (; i < mid; i++) na += f[i];
    st[0] = (int) ((sc_log(tot - na) - sc_log(na)) * 2839 >> 16);
    st[0] = CLAMP(st[0], -2047, 2047);
    s[0] = NULL;
    if (dep < 4) {
      slot = (int) (path & ((1U << dep) - 1)) | 1 << dep;
      Fk(x->n,
        int q = x->pred[k], ps;
        if (q >= 0) {
          ps = q < lo ? 0 : q >= hi ? 5
             : q < mid ? 1 + (q >= lo + (mid - lo) / 2)
             : 3 + (q >= mid + (hi - mid) / 2);
          if (!(dep & 1))
            line[k] = sc_line(x->key[k] ^ (okey) dep << 32, &fresh);
          s[k + 1] = line[k] + 1 + (dep & 1) * 6 + ps;
        } else {
          if (!dep) {
            line[k] = sc_line(x->key[k] ^ (okey) 1 << 32, &fresh);
            if (fresh && k < x->seed) sc_seed(line[k], cum, lo, hi);
          }
          s[k + 1] = line[k] + slot;
        }
        st[k + 1] = sc_st(*s[k + 1] >> 16));
      bit = sc_mix(st, x->n + 1, s, ws * 16 + dep, bit);
    } else {
      slot = (int) (path & ((1U << (dep & 3)) - 1)) | 1 << (dep & 3);
      Fk(x->deep,
        if (!(dep & 3))
          line[k] = sc_line(x->key[x->seed + k] ^ (okey) path << 32, &fresh);
        s[k + 1] = line[k] + slot;
        st[k + 1] = sc_st(*s[k + 1] >> 16));
      bit = sc_mix(st, x->deep + 1, s, ws * 16 + MIN(dep, 15), bit);
    }
    if (bit) { lo = mid;  tot -= na; } else { hi = mid;  tot = na; }
    path = path << 1 | (u32) bit;  dep++;
  }
  *v = lo;
  ctx_update(c, lo, inc, cap);
}
