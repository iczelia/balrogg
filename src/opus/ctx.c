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

typedef struct {
  okey key;
  u16 * f;
  u32 * cum;
  u32 tot;
  int n;
} prior;

#define PHB 16

#define K64(hi, lo) (((okey) hi##UL << 32) | (okey) lo##UL)
#define K_GOLD K64(0x9E3779B9, 0x7F4A7C15)
#define K_MIX  K64(0xD6E8FEB8, 0x6659FD93)

static INLINE unsigned om_hash(okey key, okey mul, unsigned bits) {
  return (unsigned) ((key * mul) >> (64 - bits));
}

static INLINE unsigned ptr_slot(const void * p, u32 x) {
  return (unsigned) ((((sz) p >> 3) * 2654435761U + x) & 1023);
}

static INLINE unsigned icdf_at(const void * t, int wide, int i) {
  return wide ? ((const u16 *) t)[i] : ((const unsigned char *) t)[i];
}

static prior pri[1UL << PHB];
static sz npri;
static blr_arena pri_ar;
static u32 * pri_ix;
static unsigned pixb;

static prior * th_pri[3][257];

static void pri_clear(int release) {
  sz i;
  memset(th_pri, 0, sizeof th_pri);
  Fi(npri, memset(pri + i, 0, sizeof *pri));
  ar_free(&pri_ar);
  npri = 0;
  if (pri_ix) memset(pri_ix, 0, ((sz) 1 << pixb) * sizeof *pri_ix);
  if (release) { xfree(pri_ix);  pri_ix = NULL;  pixb = 0; }
}

static INLINE unsigned pri_slot(okey key) {
  return om_hash(key, K64(0x106689D4, 0x5497FDB5), pixb);
}

static prior * pri_find(okey key) {
  unsigned i;
  sz k;
  if (!pri_ix) { pixb = 10;  pri_ix = xcalloc((sz) 1 << pixb, sizeof *pri_ix); }
  for (i = pri_slot(key); pri_ix[i]; i = (i + 1) & ((1U << pixb) - 1))
    if (pri[pri_ix[i] - 1].key == key) return &pri[pri_ix[i] - 1];
  if (npri == (sz) 1 << PHB)
    FATAL_CODE(BLR_EXIT_INTERNAL, "internal: the Opus prior table is full");
  if (2 * (npri + 1) > (sz) 1 << pixb) {
    xfree(pri_ix);  pixb++;  pri_ix = xcalloc((sz) 1 << pixb, sizeof *pri_ix);
    Fk(npri,
      for (i = pri_slot(pri[k].key); pri_ix[i];
           i = (i + 1) & ((1U << pixb) - 1));
      pri_ix[i] = (u32) k + 1);
    for (i = pri_slot(key); pri_ix[i]; i = (i + 1) & ((1U << pixb) - 1));
  }
  pri[npri].key = key;  pri_ix[i] = (u32) ++npri;
  return &pri[npri - 1];
}

static void pri_build(prior * p, okey key, const u32 * raw, int n, u32 ft,
                      int scale) {
  int i;
  p->key = key;  p->n = n;  p->tot = 0;
  p->f = ar_take(&pri_ar, sizeof(u16) * (sz) n);
  Fi(n,
    u32 q = (u32) (((okey) raw[i] * (okey) scale + ft / 2) / ft);
    q = CLAMP(q, 1, 60000);
    p->f[i] = (u16) q;  p->tot += q);
}

#define CB_SHIFT 4
#define CB_NBLK(n) (((n) + (1 << CB_SHIFT) - 1) >> CB_SHIFT)

typedef struct {
  u32 ptot;
  u32 tot;
  u32 n;
  u32 pri;
} mctx;

#define CH_WORDS (sizeof (mctx) / sizeof (u32))

#define CB_SLACK (1 << CB_SHIFT)

static u32 * arena;
static sz arn, arcap;

static void arena_reset(void) { arn = CH_WORDS; }

static u32 ctx_alloc(const prior * pr) {
  int n = pr->n, nb = CB_NBLK(n), k;
  sz need = CH_WORDS + (sz) nb + (sz) n;
  mctx * c;
  u32 * t, * f;
  arena = xgrow(arena, &arcap, arn + need + CB_SLACK, sizeof *arena, 1UL << 16);
  FATAL_UNLESS(arcap <= 0xFFFFFFFFUL, "opus: too many model contexts");
  c = (mctx *) (arena + arn);
  c->ptot = pr->tot;  c->tot = 0;  c->n = (u32) n;  c->pri = (u32) (pr - pri);
  t = arena + arn + CH_WORDS;  f = t + nb;
  memset(t, 0, (sz) nb * sizeof *t);
  Fk(n, f[k] = pr->f[k];  t[k >> CB_SHIFT] += f[k]);
  arn += need;
  memset(arena + arn, 0, CB_SLACK * sizeof *arena);
  return (u32) (arn - need);
}

static INLINE mctx * ctx_at(u32 off) { return (mctx *) (arena + off); }

static INLINE const u32 * ctx_fq(const mctx * c) {
  return (const u32 *) (c + 1) + CB_NBLK(c->n);
}

static INLINE void cum_of(u32 * c, const u32 * f, int n) {
  u32 s = 0;
  int j;
  c[0] = 0;
  Fj(n, s += f[j];  c[j + 1] = s);
}

typedef struct {
  okey key;
  u32 off;
  u32 pri;
} cslot;

#define CHB0 10

static cslot * ctxs;
static unsigned chb;
static sz nlive;

static INLINE unsigned ctx_slot(okey key, u32 pi) {
  return om_hash(key ^ (okey) pi * K64(0xC2B2AE3D, 0x27D4EB4F), K_GOLD, chb);
}

static void ctx_new(unsigned bits) {
  nlive = 0;
  if (ctxs) { memset(ctxs, 0, ((sz) 1 << chb) * sizeof *ctxs);  return; }
  chb = bits;  ctxs = xcalloc((sz) 1 << chb, sizeof *ctxs);
}

static void ctx_grow(void) {
  cslot * old = ctxs;
  sz k, n = (sz) 1 << chb;
  chb++;
  ctxs = xcalloc((sz) 1 << chb, sizeof *ctxs);
  Fk(n,
    if (old[k].off) {
      unsigned i = ctx_slot(old[k].key, old[k].pri);
      while (ctxs[i].off) i = (i + 1) & ((1U << chb) - 1);
      ctxs[i] = old[k];
    });
  xfree(old);
}

static mctx * ctx_find(okey key, const prior * pr) {
  u32 pi = (u32) (pr - pri);
  unsigned i = ctx_slot(key, pi);
  for (;;) {
    if (!ctxs[i].off) {
      if (nlive * 2 >= (sz) 1 << chb) {
        ctx_grow();  i = ctx_slot(key, pi);
        while (ctxs[i].off) i = (i + 1) & ((1U << chb) - 1);
      }
      ctxs[i].key = key;  ctxs[i].pri = pi;  ctxs[i].off = ctx_alloc(pr);
      nlive++;
      return ctx_at(ctxs[i].off);
    }
    if (ctxs[i].key == key && ctxs[i].pri == pi) return ctx_at(ctxs[i].off);
    i = (i + 1) & ((1U << chb) - 1);
  }
}

static INLINE void ctx_update(mctx * c, int v, int inc, int cap) {
  int n = (int) c->n, nb = CB_NBLK(n), i;
  u32 * blk = (u32 *) (c + 1), * f = blk + nb;
  f[v] += (u32) inc;  blk[v >> CB_SHIFT] += (u32) inc;
  c->tot += (u32) inc;
  if (UNLIKELY(c->tot > (u32) cap)) {
    const u16 * pf = pri[c->pri].f;
    u32 s = 0;
    Fi(nb, blk[i] = 0);
    Fi(n,
      u32 cn = (f[i] - pf[i]) >> 1;
      f[i] = pf[i] + cn;  blk[i >> CB_SHIFT] += f[i];  s += cn);
    c->tot = s;
  }
}

static HOT int ctx_dec(mctx * c, int inc, int cap) {
  int n = (int) c->n, i;
  u32 * blk = (u32 *) (c + 1), * f = blk + CB_NBLK(n);
  u32 ft = c->ptot + c->tot, acc = 0, e, code;
  rc_dec_norm(D);
  e = D->range / ft;  code = D->code;
  for (i = 0;
       i + (1 << CB_SHIFT) <= n && code >= (acc + blk[i >> CB_SHIFT]) * e;
       i += 1 << CB_SHIFT)
    acc += blk[i >> CB_SHIFT];
  for (; i < n - 1; i++) {
    if (code < (acc + f[i]) * e) break;
    acc += f[i];
  }
  if (i == n) { i = n - 1;  acc -= f[i]; }
  D->code = code - e * acc;
  D->range = i < n - 1 ? e * f[i] : D->range - e * acc;
  PROF(prof_sym(om_comp, acc, acc + f[i], ft));
  ctx_update(c, i, inc, cap);
  return i;
}

static HOT int ctx_enc(mctx * c, int inc, int cap, int v) {
  int n = (int) c->n, i;
  u32 * blk = (u32 *) (c + 1), * f = blk + CB_NBLK(n);
  u32 ft = c->ptot + c->tot, fl = 0;
  v = CLAMP(v, 0, n - 1);
  for (i = 0; i + (1 << CB_SHIFT) <= v; i += 1 << CB_SHIFT)
    fl += blk[i >> CB_SHIFT];
#if OM_SSE2 && CB_SHIFT == 4
  { const __m128i * q = (const __m128i *) (const void *) (f + i);
    __m128i lim = _mm_set1_epi32(v - i), four = _mm_set1_epi32(4);
    __m128i at = _mm_setr_epi32(0, 1, 2, 3), sum = _mm_setzero_si128();
    int j;
    Fj(4,
      sum = _mm_add_epi32(sum, _mm_and_si128(_mm_cmpgt_epi32(lim, at),
                                             _mm_loadu_si128(q + j)));
      at = _mm_add_epi32(at, four));
    sum = _mm_add_epi32(sum, _mm_shuffle_epi32(sum, 0x4E));
    sum = _mm_add_epi32(sum, _mm_shuffle_epi32(sum, 0xB1));
    fl += (u32) _mm_cvtsi128_si32(sum); }
#else
  for (; i < v; i++) fl += f[i];
#endif
  p_cum(fl, fl + f[v], ft);
  PROF(prof_sym(om_comp, fl, fl + f[v], ft));
  ctx_update(c, v, inc, cap);
  return v;
}

static void ctx_code(mctx * c, int inc, int cap, int * v) {
  *v = om_mode == OM_DEC ? ctx_dec(c, inc, cap) : ctx_enc(c, inc, cap, *v);
}

typedef struct { u32 key, off; } pslot;

#define PCHB0 12

static pslot * pctxs;
static unsigned pchb;
static sz npctx;

static INLINE unsigned pctx_slot(u32 key) {
  return (unsigned) ((key * 0x9E3779B1UL & 0xFFFFFFFFUL) >> (32 - pchb));
}

static void pctx_new(void) {
  npctx = 0;
  if (pctxs) { memset(pctxs, 0, ((sz) 1 << pchb) * sizeof *pctxs);  return; }
  pchb = PCHB0;  pctxs = xcalloc((sz) 1 << pchb, sizeof *pctxs);
}

static void pctx_grow(void) {
  pslot * old = pctxs;
  sz k, n = (sz) 1 << pchb;
  pchb++;
  pctxs = xcalloc((sz) 1 << pchb, sizeof *pctxs);
  Fk(n,
    if (old[k].key) {
      unsigned i = pctx_slot(old[k].key);
      while (pctxs[i].key) i = (i + 1) & ((1U << pchb) - 1);
      pctxs[i] = old[k];
    });
  xfree(old);
}

static prior * prior_split(int n1, int n2, int K);
static prior * prior_feat(int N, int K);

static u32 pctx_off(int band, int dep, int N, int K, int aux, int v) {
  u32 key = (u32) aux << 30 | (u32) v << 24 | (u32) band << 19 |
            (u32) MIN(7, dep) << 16 | (u32) N << 8 | (u32) K;
  unsigned i = pctx_slot(key);
  for (;;) {
    if (pctxs[i].key == key) return pctxs[i].off;
    if (!pctxs[i].key) break;
    i = (i + 1) & ((1U << pchb) - 1);
  }
  if (npctx * 2 >= (sz) 1 << pchb) {
    pctx_grow();  i = pctx_slot(key);
    while (pctxs[i].key) i = (i + 1) & ((1U << pchb) - 1);
  }
  pctxs[i].key = key;
  pctxs[i].off = ctx_alloc(aux ? prior_feat(N, K)
                               : prior_split(N / 2, N - N / 2, K));
  npctx++;
  return pctxs[i].off;
}

static mctx * pctx_find(int band, int dep, int N, int K, int aux, int v) {
  return ctx_at(pctx_off(band, dep, N, K, aux, v));
}

typedef struct { int scale, inc, cap; } kparm;

static const kparm KP1[OP_NKINDS] = {
  {  384, 28, 16384 },
  {  384, 28, 16384 },
  {  256, 24,  8192 },
  { 1024, 16, 16384 },
  { 1024, 24,  8192 },
  {  512, 48, 16384 },
  { 1024, 24, 16384 }
};

static const kparm KP2[OP_NKINDS] = {
  {  384, 42, 16384 },
  {  384, 42, 16384 },
  {  256, 24,  8192 },
  {  512, 16, 16384 },
  { 1024, 24,  8192 },
  {  512, 72, 16384 },
  {  768, 24, 16384 }
};

static const kparm * KP = KP1;

#define TH_SC1  192
#define TH_INC1 32
static int th_sc = 1024, th_inc = 24;

#define NBB  15
#define NBIN (1UL << NBB)

typedef struct { u16 p, hist;  u8 cnt, set; } bslot;
static bslot bins[NBIN];
#define SCB    18
static int scb = SCB;
static u32 * bin_live;
static sz nbin, bincap;

#define BIN_CNT(p) (&((bslot *) (void *) (p))->cnt)

static INLINE void bin_learn(u16 * p, int bit) {
  *p = rc_adapt(*p, BIN_CNT(p), RC_ALIM, bit);
}

static void p_bit(u16 * p, int * v) {
  PROF(prof_sym(om_comp, *v ? *p : 0, *v ? 65536 : *p, 65536));
  *v = om_fbit(*p, *v);
  bin_learn(p, *v);
}

static u16 * bin_slot(okey key, u16 init) {
  unsigned i = om_hash(key, K_MIX, NBB), n = 0;
  bslot * q;
  while (bins[i].set && bins[i].hist != (u16) (key & 0xFFFF)) {
    i = (i + 1) & (NBIN - 1);
    if (UNLIKELY(++n == NBIN))
      FATAL_CODE(BLR_EXIT_INTERNAL, "internal: the Opus bit table is full");
  }
  q = bins + i;
  if (!q->set) {
    bin_live = xgrow(bin_live, &bincap, nbin + 1, sizeof *bin_live, 1024);
    bin_live[nbin++] = i;
    q->set = 1;  q->hist = (u16) (key & 0xFFFF);  q->p = init;  q->cnt = 0;
  }
  return &q->p;
}

#define NB    32
#define LAPW  24
#define LAPN  (2 * LAPW + 2)

static int prev_qi[NB][2];
static int band_qi[2];
static int prev_fine[NB][2];
static int prev_theta[NB];
static int c0_qi, c0_qib = -1;
static int c0_fine, c0_fineb = -1, c0_finen = -1;
static u32 last_pvq[NB][8][8], last_pvqV[NB][8][8];
static int have_pvq[NB][8][8];
static u8 site_hist[OREC_S_COUNT];

static int oz_on, oz_pdep, oz_tree;
static int oz_part;
#define ZP_DMAX 6
static int om_tier;
static void oz_init(void);
static void oz_reset(void);
static void oz_free(void);
static void fh_reset(void);
static void en_reset(void);
static void ps_reset(void);
static void th_reset(void);
static void scm_init(void);
static void scm_free(void);
static void oh_reset(void);

static void om_reset(void) {
  memset(prev_qi, 0, sizeof prev_qi);  memset(band_qi, 0, sizeof band_qi);
  memset(prev_fine, 0, sizeof prev_fine);
  memset(prev_theta, 0, sizeof prev_theta);
  c0_qi = 0;  c0_qib = -1;  c0_fine = 0;  c0_fineb = -1;  c0_finen = -1;
  memset(have_pvq, 0, sizeof have_pvq);  memset(last_pvqV, 0, sizeof last_pvqV);
  memset(site_hist, 0, sizeof site_hist);
  fh_reset();  ps_reset();  th_reset();  en_reset();  orec_frame = 0;
  if (oz_on) oz_reset();
}

static u32 * pvq_ut;
static u32 * st_lgt;
static void pm_lginit(void);
static void split_reset(int release);
static void pm_reset(void);
static void bl_reset(void);
static void px_reset(void);
static void fr_reset(void);

static void om_start(void);

static void om_length(sz n) {
  om_tier = !n || n >= 256 * 1024 ? 0 : n >= 64 * 1024 ? 1
          : n >= 16 * 1024 ? 2 : 3;
  scb = SCB - 2 * om_tier;
}

static void om_init(void) {
  sz i;
  if (!pvq_ut) pvq_ut = cw_new();
  pri_clear(0);
  split_reset(0);
  arena_reset();  ctx_new(CHB0);  pctx_new();
  Fi(nbin, bins[bin_live[i]].set = 0);
  nbin = 0;
  rc_adapt_init();  pm_lginit();  oh_reset();  fr_reset();
  om_start();
  oe_reset();
  orec_spread = orec_trans = orec_thst = orec_pvqoff = orec_pvqrole = 0;
  om_reset();
}

static void om_free(void) {
#ifdef BLR_PROFILE
  if (prof_on) { sz used = nbin;
    fprintf(stderr, "[prof] binp %lu/%lu used (%.3f%%), ctxs %lu/%lu live, "
            "PVQ ctxs %lu/%lu live\n",
            (unsigned long) used, (unsigned long) NBIN, 100.0 * used / NBIN,
            (unsigned long) nlive, (unsigned long) ((sz) 1 << chb),
            (unsigned long) npctx, (unsigned long) ((sz) 1 << pchb)); }
#endif
  pri_clear(1);
  xfree(ctxs);  ctxs = NULL;  chb = 0;  nlive = 0;
  xfree(pctxs);  pctxs = NULL;  pchb = 0;  npctx = 0;
  xfree(arena);  arena = NULL;  arn = arcap = 0;
  xfree(pvq_ut);  pvq_ut = NULL;
  { sz i;  Fi(nbin, bins[bin_live[i]].set = 0); }
  xfree(bin_live);  bin_live = NULL;  nbin = bincap = 0;
  xfree(st_lgt);  st_lgt = NULL;
  split_reset(1);  scm_free();  oz_free();
  oe_reset();
}

typedef struct { const void * ptr;  u32 ftb;  okey id; } tabent;
static tabent tabs[1024];

static okey tab_hash(const void * icdf, int wide, u32 ftb) {
  okey h = 0xcbf29ce484222325ULL ^ (okey) wide << 32 ^ ftb;
  unsigned prev;
  int n = 0;
  do {
    unsigned e = icdf_at(icdf, wide, n);
    h = (h ^ e) * 0x100000001b3ULL;
    prev = e;  n++;
  } while (prev > 0 && n < 512);
  return h;
}

static okey tab_id(const void * icdf, int wide, u32 ftb) {
  sz slot = ptr_slot(icdf, ftb), i;
  Fi(8,
    tabent * t = &tabs[slot];
    if (t->ptr == icdf && t->ftb == ftb) return t->id;
    if (!t->ptr) {
      t->ptr = icdf;  t->ftb = ftb;  t->id = tab_hash(icdf, wide, ftb);
      return t->id;
    }
    slot = (slot + 1) & 1023);
  return tab_hash(icdf, wide, ftb);
}

static prior * prior_icdf(okey key, const void * icdf, int wide, u32 ftb) {
  prior * p = pri_find(key);
  if (!p->f) {
    u32 raw[512];
    int n = 0;
    unsigned prev = 1U << ftb;
    do {
      unsigned e = icdf_at(icdf, wide, n);
      raw[n] = prev - e;  prev = e;  n++;
    } while (prev > 0 && n < 512);
    pri_build(p, key, raw, n, 1UL << ftb,
              KP[wide ? OP_ICDF16 : OP_ICDF].scale);
  }
  return p;
}

static unsigned lap_freq(unsigned fs0, int decay, int val) {
  unsigned fl, fs;
  int v, s, i;
  if (val == 0) return fs0;
  s = -(val < 0);
  v = ABS(val);
  fl = fs0;
  fs = (unsigned) ((i32) (32768 - 1 * (2 * 16) - (int) fs0) *
                   (i32) (16384 - decay) >> 15);
  for (i = 1; fs > 0 && i < v; i++) {
    fs *= 2;  fl += fs + 2 * 1;
    fs = (unsigned) ((i32) fs * (i32) decay >> 15);
  }
  if (!fs) {
    int di, ndi_max;
    ndi_max = 32768 - (int) fl + 1 - 1;
    ndi_max = (ndi_max - s) >> 1;
    di = v - i;
    if (di > ndi_max - 1) di = ndi_max - 1;
    if (di < 0) return 0;
    fl += (unsigned) ((2 * di + 1 + s) * 1);
    if (fl >= 32768) return 0;
    fs = 1;
  } else {
    fs += 1;
    fl += fs & ~(unsigned) s;
  }
  if (fl >= 32768) return 0;
  if (fl + fs > 32768) fs = 32768 - fl;
  return fs;
}

static prior * prior_laplace(u32 fs0, int decay) {
  okey key = (okey) fs0 << 20 | (okey) (unsigned) decay |
             (okey) 0x80000000UL << 32;
  prior * p = pri_find(key);
  if (!p->f) {
    u32 raw[LAPN], used = 0;
    sz i;
    Fi(LAPN - 1,
      raw[i] = lap_freq(fs0, decay, (int) i - LAPW);  used += raw[i]);
    raw[LAPN - 1] = used < 32768 ? 32768 - used : 1;
    pri_build(p, key, raw, LAPN, 32768, KP[OP_LAPLACE].scale);
  }
  return p;
}

static void th_raw(int mode, int qn, u32 * raw) {
  int i;
  Fi(qn + 1,
    raw[i] = (u32) (mode == 1 ? 1 : mode == 0 ? (i <= qn / 2 ? 3 : 1)
                  : i <= qn >> 1 ? i + 1 : qn + 1 - i));
}

static prior * prior_theta(int mode, int qn) {
  okey key = (okey) mode << 24 | (okey) qn | (okey) 0xC0000000UL << 32;
  prior * p;
  if (mode >= 0 && mode < 3 && qn >= 0 && qn < 257 && th_pri[mode][qn])
    return th_pri[mode][qn];
  p = pri_find(key);
  if (mode >= 0 && mode < 3 && qn >= 0 && qn < 257) th_pri[mode][qn] = p;
  if (!p->f) {
    u32 raw[258], ft;
    int n = qn + 1, h = (qn >> 1) + 1;
    FATAL_UNLESS(n <= 258, "opus: theta alphabet %d out of range", n);
    ft = (u32) (mode == 1 ? n : mode == 0 ? 3 * (qn / 2 + 1) + qn / 2 : h * h);
    th_raw(mode, qn, raw);
    pri_build(p, key, raw, n, ft, th_sc);
  }
  return p;
}

static prior * prior_uniform(int n, int scale) {
  okey key;
  prior * p;
  FATAL_UNLESS(n > 0, "opus: empty uniform alphabet");
  key = (okey) n << 8 | (okey) (unsigned) (scale & 0xFF)
      | (okey) 0xE0000000UL << 32;
  if (om_feat & OF_RATE) key |= (okey) (unsigned) (scale >> 8 & 0xFF) << 24;
  p = pri_find(key);
  if (!p->f) {
    u32 raw[4097];
    sz i;
    if (n > 4096) n = 4096;
    Fi((sz) n, raw[i] = 1);
    pri_build(p, key, raw, n, (u32) n, scale);
  }
  return p;
}

static void om_bit(okey key, u16 init, int * v) {
  p_bit(bin_slot(key, init), v);
}

static void om_int(okey key, int nsym, int scale, int inc, int cap, int * v) {
  prior * pr = prior_uniform(nsym, scale);
  ctx_code(ctx_find(key, pr), inc, cap, v);
}
