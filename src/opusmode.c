/*  Copyright (C) 2026 Kamila Szewczyk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, version 3.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program. If not, see <http://www.gnu.org/licenses/>.  */

#include "opusmode.h"
#include "archive.h"
#include "ogg.h"
#include "opusent.h"
#include "rc.h"
#include "prof.h"

#include "opusdec.h"

#include <limits.h>

const char * opus_mode_version(void) { return OPUS_PARSER_VERSION; }

/*  Adapt Opus probability tables as priors while preserving exact n-ary
    coding for uniform PVQ values.  */

/*  Opus range coder and n-ary model.  */
typedef rc_enc orc_enc;

typedef struct {
  blr_file * file;
  sz off, len, pos;
  u32 code, range, ext;
} orc_dec;

static void orc_enc_init(orc_enc * e, blr_file * output) { rc_enc_file(e, output); }
static void orc_enc_free(orc_enc * e) { rc_enc_free(e); }
static INLINE void orc_addlow(orc_enc * e, u32 v) { rc_addlow(e, v); }
static INLINE void orc_norm(orc_enc * e) { rc_norm(e); }

/*  Use the count-capped adaptation rate from rc.h.  */
static INLINE u16 orc_adapt(u16 v, u8 * c, int bit) {
  return c ? rc_adapt(v, c, RC_ALIM, bit) : rc_adapt_fixed(v, bit);
}

static INLINE void orc_enc_bit(orc_enc * e, u16 * p, u8 * c, int bit) {
  u32 split = (e->range >> 16) * (u32) *p;
  if (!bit) e->range = split;
  else {
    orc_addlow(e, split);
    e->range -= split;
  }
  *p = orc_adapt((u16) *p, c, bit);
  orc_norm(e);
}

/*  The n-ary step.  `code` is measured from the bottom of the range, as in
    orc_enc_bit, so the division remainder falls to the last symbol.  */
static void orc_enc_cum(orc_enc * e, u32 fl, u32 fh, u32 ft) {
  u32 r;
  /*  Avoid varargs overhead on this hot path.  */
  FATAL_IF_HOT(!(fl < fh && fh <= ft && ft <= (1UL << 17)))
    ("opus: bad cumulative step %lu..%lu of %lu",
     (unsigned long) fl, (unsigned long) fh, (unsigned long) ft);
  r = e->range / ft;
  if (fl > 0) orc_addlow(e, r * fl);
  e->range = (fh < ft) ? r * (fh - fl) : e->range - r * fl;
  orc_norm(e);
}

static void orc_enc_raw(orc_enc * e, u32 v, int nbits) {
  if (nbits > 0) orc_enc_cum(e, v, v + 1, 1UL << nbits);
}

static void orc_enc_done(orc_enc * e) { rc_enc_finish(e); }

static u32 orc_get(orc_dec * d) { return d->pos < d->len ? bf_get(d->file, d->off + d->pos++) : 0; }

static void orc_dec_init(orc_dec * d, blr_file * file, sz off, sz len) {
  int i;
  d->file = file;  d->off = off;  d->len = len;  d->pos = 0;  d->code = 0;
  Fi(4, d->code = (d->code << 8) | orc_get(d));
  d->range = 0xFFFFFFFFUL;
}

static INLINE void orc_dnorm(orc_dec * d) {
  while (d->range < RC_TOP) { d->code = (d->code << 8) | orc_get(d);  d->range <<= 8; }
}

static INLINE int orc_dec_bit(orc_dec * d, u16 * p, u8 * c) {
  u32 split;
  int bit;
  orc_dnorm(d);
  split = (d->range >> 16) * (u32) *p;
  if (d->code < split) { d->range = split;  bit = 0; }
  else { d->code -= split;  d->range -= split;  bit = 1; }
  *p = orc_adapt((u16) *p, c, bit);
  return bit;
}

static INLINE u32 orc_dec_cum_get(orc_dec * d, u32 ft) {
  u32 s;
  orc_dnorm(d);
  d->ext = d->range / ft;
  s = d->code / d->ext;
  return s < ft ? s : ft - 1;
}

static INLINE void orc_dec_cum_upd(orc_dec * d, u32 fl, u32 fh, u32 ft) {
  d->code -= d->ext * fl;
  d->range = (fh < ft) ? d->ext * (fh - fl) : d->range - d->ext * fl;
}

static u32 orc_dec_raw(orc_dec * d, int nbits) {
  u32 s;
  if (nbits <= 0) return 0;
  s = orc_dec_cum_get(d, 1UL << nbits);
  orc_dec_cum_upd(d, s, s + 1, 1UL << nbits);
  return s;
}

#define OM_ENC 1
#define OM_DEC 2

static int om_mode;
static orc_enc * E;
static orc_dec * D;
#ifdef BLR_PROFILE
static int om_comp = P_OOTHER;
#endif

static void p_bit(u16 * p, u8 * c, int * v) {
  PROF(prof_sym(om_comp, *v ? *p : 0, *v ? 65536 : *p, 65536));
  if (om_mode == OM_DEC) *v = orc_dec_bit(D, p, c);
  else orc_enc_bit(E, p, c, *v);
}

static void p_cum(u32 fl, u32 fh, u32 ft) { orc_enc_cum(E, fl, fh, ft); }

static void p_raw(u32 * v, int nbits) {
  while (nbits > 0) {
    int k = nbits > 16 ? 16 : nbits;
    nbits -= k;
    if (om_mode == OM_DEC) *v |= orc_dec_raw(D, k) << nbits;
    else orc_enc_raw(E, (*v >> nbits) & ((1UL << k) - 1UL), k);
  }
}

/*  Uniform value using Opus's 8-bit range-coded head and raw tail split.  */
static void p_uniform(u32 * v, u32 V) {
  u32 t, tt;
  int ftb = 0;
  if (V <= 1) { *v = 0;  return; }
  PROF(prof_sym(om_comp, 0, 1, V));
  FATAL_IF_HOT(om_mode != OM_DEC && *v >= V)
    ("opus: uniform %lu out of range %lu", (unsigned long) *v, (unsigned long) V);
  t = V - 1;
  for (tt = t; tt; tt >>= 1) ftb++;          /*  ftb = ILOG(V - 1)  */
  if (ftb > 8) {
    u32 ft, hi, lo;
    ftb -= 8;
    ft = (t >> ftb) + 1;
    if (om_mode == OM_DEC) {
      hi = orc_dec_cum_get(D, ft);  orc_dec_cum_upd(D, hi, hi + 1, ft);
      lo = 0;  p_raw(&lo, ftb);
      *v = (hi << ftb) | lo;
      if (*v > t) *v = t;
    } else {
      hi = *v >> ftb;  lo = *v & ((1UL << ftb) - 1UL);
      p_cum(hi, hi + 1, ft);  p_raw(&lo, ftb);
    }
  } else if (om_mode == OM_DEC) {
    u32 s = orc_dec_cum_get(D, V);
    orc_dec_cum_upd(D, s, s + 1, V);
    *v = s;
  } else p_cum(*v, *v + 1, V);
}

/*  Scale Opus priors to `scale`, with a minimum of 1.  */
typedef struct {
  okey key;
  u16 * f;
  u32 tot;
  int n;
} prior;

#define PHB 16

/*  64-bit odd multipliers.  */
#define K64(hi, lo) (((okey) hi##UL << 32) | (okey) lo##UL)

static prior pri[1UL << PHB];

static prior * pri_find(okey key) {
  okey h = key * K64(0x106689D4, 0x5497FDB5);
  unsigned i = (unsigned) (h >> (64 - PHB)), n;
  /*  The table never grows: a full one would otherwise probe forever.  */
  for (n = 0; n < (1U << PHB); n++) {
    if (!pri[i].f || pri[i].key == key) return &pri[i];
    i = (i + 1) & ((1U << PHB) - 1);
  }
  FATAL_CODE(BLR_EXIT_INTERNAL, "internal: the Opus prior table is full");
  return NULL;
}

static void pri_build(prior * p, okey key, const u32 * raw, int n, u32 ft, int scale) {
  int i;
  p->key = key;  p->n = n;  p->tot = 0;
  p->f = xmalloc(sizeof(u16) * (sz) n);
  Fi(n,
    u32 q = (u32) (((okey) raw[i] * (okey) scale + ft / 2) / ft);
    if (q < 1) q = 1;
    if (q > 60000) q = 60000;
    p->f[i] = (u16) q;  p->tot += q);
}

/*  Contexts.
    Open addressing uses a full 64-bit context key.  */

/*  Store prior and adapted counts with 16-symbol sums.  */
#define CB_SHIFT 4
#define CB_NBLK(n) (((n) + (1 << CB_SHIFT) - 1) >> CB_SHIFT)

typedef struct {
  okey key;
  u32 * t;                  /*  CB_NBLK(n) block sums, then n frequencies  */
  const prior * pr;
  u32 tot;
  int n;
} mctx;

/*  Start at 2^14 slots and double at half capacity. Full key comparison makes
    table growth output-neutral.  */
#define CHB0 14

static mctx * ctxs;
static unsigned chb;

/*  Track occupied slots.  */
static u32 * live;
static sz nlive, clive;

/*  Include the prior index in the hash because PVQ keys omit geometry. Probes
    still compare the complete key and prior pair.  */
static INLINE unsigned ctx_slot(okey key, const prior * pr) {
  okey h = (key ^ ((okey) (pr - pri) * K64(0xC2B2AE3D, 0x27D4EB4F)))
         * K64(0x9E3779B9, 0x7F4A7C15);
  return (unsigned) (h >> (64 - chb));
}

static void ctx_new(unsigned bits) {
  free(ctxs);  chb = bits;
  ctxs = xcalloc((sz) 1 << chb, sizeof *ctxs);
}

/*  Double the table and reinsert complete entries.  */
static void ctx_grow(void) {
  mctx * old = ctxs;
  sz k;
  chb++;
  ctxs = xcalloc((sz) 1 << chb, sizeof *ctxs);
  Fk(nlive,
    mctx e = old[live[k]];
    unsigned i = ctx_slot(e.key, e.pr);
    while (ctxs[i].t) i = (i + 1) & ((1U << chb) - 1);
    ctxs[i] = e;  live[k] = (u32) i);
  free(old);
}

static mctx * ctx_find(okey key, const prior * pr) {
  unsigned i;
  if (nlive * 2 >= ((sz) 1 << chb)) ctx_grow();
  i = ctx_slot(key, pr);
  for (;;) {
    if (!ctxs[i].t) {
      int n = pr->n, k, nb = CB_NBLK(n);
      u32 * t = xcalloc((sz) nb + (sz) n, sizeof *t), * f = t + nb;
      Fk(n, f[k] = pr->f[k];  t[k >> CB_SHIFT] += f[k]);
      ctxs[i].key = key;  ctxs[i].pr = pr;  ctxs[i].n = n;
      ctxs[i].t = t;  ctxs[i].tot = 0;
      if (nlive == clive) {
        clive = clive ? clive * 2 : 4096;
        live = xrealloc(live, clive * sizeof *live);
      }
      live[nlive++] = (u32) i;
      return &ctxs[i];
    }
    if (ctxs[i].key == key && ctxs[i].pr == pr) return &ctxs[i];
    i = (i + 1) & ((1U << chb) - 1);
  }
}

/*  Code one symbol against prior + counts.  `inc` and `cap` are the learning rate
    and the halving point, chosen per symbol kind in KP below.  */
static HOT void ctx_code(mctx * c, int inc, int cap, int * v) {
  int n = c->n, i, nb = CB_NBLK(n);
  u32 * blk = c->t, * f = blk + nb;
  u32 ft = c->pr->tot + c->tot, fl = 0, fh = 0;
  if (om_mode == OM_DEC) {
    u32 s = orc_dec_cum_get(D, ft), acc = 0;
    /*  Skip whole blocks below the target, then walk the one it is in.  */
    for (i = 0; i + (1 << CB_SHIFT) <= n && s >= acc + blk[i >> CB_SHIFT];
         i += 1 << CB_SHIFT)
      acc += blk[i >> CB_SHIFT];
    for (; i < n; i++) {
      if (s < acc + f[i]) break;
      acc += f[i];
    }
    /*  A target past the last symbol, which a spent stream can produce,
        decodes as the last symbol.  */
    if (i == n) { i = n - 1;  acc -= f[i]; }
    *v = i;  fl = acc;  fh = acc + f[i];
    orc_dec_cum_upd(D, fl, fh, ft);
  } else {
    int t = *v;
    if (t < 0) t = 0;
    if (t >= n) t = n - 1;
    for (i = 0; i + (1 << CB_SHIFT) <= t; i += 1 << CB_SHIFT) fl += blk[i >> CB_SHIFT];
    for (; i < t; i++) fl += f[i];
    fh = fl + f[t];
    p_cum(fl, fh, ft);
    *v = t;
  }
  PROF(prof_sym(om_comp, fl, fh, ft));
  f[*v] += (u32) inc;  blk[*v >> CB_SHIFT] += (u32) inc;
  c->tot += (u32) inc;
  if (c->tot > (u32) cap) {
    /*  Halve adapted counts but retain the prior.  */
    const u16 * pf = c->pr->f;
    u32 s = 0;
    Fi(nb, blk[i] = 0);
    Fi(n,
      u32 cn = (f[i] - pf[i]) >> 1;
      f[i] = pf[i] + cn;  blk[i >> CB_SHIFT] += f[i];  s += cn);
    c->tot = s;
  }
}

/*  Per-kind parameters: the prior scale, learning rate, halving point,
    and context bits of each symbol kind.  */

typedef struct { int scale, inc, cap; } kparm;

static const kparm KP[OP_NKINDS] = {
  {  384, 28, 16384 },  /*  OP_ICDF     */
  {  384, 28, 16384 },  /*  OP_ICDF16   */
  {  256, 24,  8192 },  /*  OP_LOGP is binary; only `scale` is used  */
  { 1024, 16, 16384 },  /*  OP_BITS     */
  { 1024, 24,  8192 },  /*  OP_UINT     */
  {  512, 48, 16384 },  /*  OP_LAPLACE  */
  {  192, 32, 16384 }   /*  OP_THETA    */
};

/*  Binary probability slots.
    Binary decisions use compact slots tagged by the key's low 16 bits.  */

#define NBIN (1UL << 18)

static u16 binp[NBIN];
static u8 bincnt[NBIN];
static u8 binset[NBIN];
static u16 bin_hist[NBIN];

static u16 * bin_slot(okey key, u16 init) {
  okey h = key * K64(0xD6E8FEB8, 0x6659FD93);
  unsigned i = (unsigned) (h >> (64 - 18)), n = 0;
  while (binset[i] && bin_hist[i] != (u16) (key & 0xFFFF)) {
    i = (i + 1) & (NBIN - 1);
    if (UNLIKELY(++n == NBIN))
      FATAL_CODE(BLR_EXIT_INTERNAL, "internal: the Opus bit table is full");
  }
  if (!binset[i]) {
    binset[i] = 1;  bin_hist[i] = (u16) (key & 0xFFFF);
    binp[i] = init;  bincnt[i] = 0;
  }
  return &binp[i];
}


#define NB    32                 /*  CELT has 21 bands; round up and clamp.  */
#define LAPW  24                 /*  Laplace window, plus one escape symbol.  */
#define LAPN  (2 * LAPW + 2)

static int prev_qi[NB][2];       /*  previous frame's coarse qi, per band  */
static int band_qi[2];           /*  previous band's qi, this frame  */
static int prev_theta[NB];       /*  the previous frame's band angle, rescaled to 0..7
                                     by the angle alphabet size qn  */
static int prev_fine[NB][2];
/*  Channel 1 uses channel 0 of the same band and frame as context.  */
static int c0_qi, c0_qib = -1;
static int c0_fine, c0_fineb = -1, c0_finen = -1;
static u32 last_pvq[NB][8][8], last_pvqV[NB][8][8];
static int have_pvq[NB][8][8];
static u8 site_hist[OREC_S_COUNT];

static void om_reset(void) {
  memset(prev_qi, 0, sizeof prev_qi);      memset(band_qi, 0, sizeof band_qi);
  memset(prev_theta, 0, sizeof prev_theta);memset(prev_fine, 0, sizeof prev_fine);
  c0_qi = 0;  c0_qib = -1;  c0_fine = 0;  c0_fineb = -1;  c0_finen = -1;
  memset(have_pvq, 0, sizeof have_pvq);    memset(last_pvqV, 0, sizeof last_pvqV);
  memset(site_hist, 0, sizeof site_hist);
}

static u32 * pvq_ut;            /*  the codeword-count table U(n, k), from pvq_ubuild  */
static void pvq_ubuild(void);

static void om_init(void) {
  sz i;
  Fi(1UL << PHB, free(pri[i].f);  pri[i].f = NULL);
  memset(pri, 0, sizeof pri);
  /*  Reset the context table after freeing its count vectors.  */
  Fi(nlive, free(ctxs[live[i]].t));
  nlive = 0;
  ctx_new(CHB0);
  memset(binset, 0, sizeof binset);
  rc_adapt_init();  pvq_ubuild();
  oe_reset();
  om_reset();
}

static void om_free(void) {
  sz i;
#ifdef BLR_PROFILE
  /*  Report table load.  */
  if (prof_on) { sz used = 0;  Fi(NBIN, used += binset[i] != 0);
    fprintf(stderr, "[prof] binp %lu/%lu used (%.3f%%), ctxs %lu/%lu live\n",
            (unsigned long) used, (unsigned long) NBIN, 100.0 * used / NBIN,
            (unsigned long) nlive, (unsigned long) ((sz) 1 << chb)); }
#endif
  Fi(1UL << PHB, free(pri[i].f);  pri[i].f = NULL);
  if (ctxs) { Fi(nlive, free(ctxs[live[i]].t));  free(ctxs);  ctxs = NULL; }
  chb = 0;
  free(live);  live = NULL;  nlive = clive = 0;
  free(pvq_ut);  pvq_ut = NULL;
  oe_reset();   /*  including opusent.c's mirror scratch  */
}

/*  Identify inverse-CDF tables by content.  Hash the supplied tail through
    its terminating zero and cache by pointer.  */
typedef struct { const void * ptr; u32 ftb; okey id; } tabent;
static tabent tabs[1024];

static okey tab_hash(const void * icdf, int wide, u32 ftb) {
  okey h = 0xcbf29ce484222325ULL ^ ((okey) wide << 32) ^ ftb;
  unsigned prev;
  int n = 0;
  do {
    unsigned e = wide ? ((const u16 *) icdf)[n] : ((const unsigned char *) icdf)[n];
    h = (h ^ e) * 0x100000001b3ULL;
    prev = e;  n++;
  } while (prev > 0 && n < 512);
  return h;
}

static okey tab_id(const void * icdf, int wide, u32 ftb) {
  sz slot = (((sz) icdf >> 3) * 2654435761U + ftb) & 1023, i;
  for (i = 0; i < 8; i++, slot = (slot + 1) & 1023) {
    tabent * t = &tabs[slot];
    if (t->ptr == icdf && t->ftb == ftb) return t->id;
    if (!t->ptr) {
      t->ptr = icdf;  t->ftb = ftb;  t->id = tab_hash(icdf, wide, ftb);
      return t->id;
    }
  }
  return tab_hash(icdf, wide, ftb);
}

/*  Prior constructors.  */

/*  `key` is the table's identity from tab_id or tab_hash; `wide` selects
    the 16-bit table layout.  */
static prior * prior_icdf(okey key, const void * icdf, int wide, u32 ftb) {
  prior * p = pri_find(key);
  if (!p->f) {
    u32 raw[512];
    int n = 0;
    unsigned prev = 1U << ftb;
    do {
      unsigned e = wide ? ((const u16 *) icdf)[n] : ((const unsigned char *) icdf)[n];
      raw[n] = prev - e;  prev = e;  n++;
    } while (prev > 0 && n < 512);
    pri_build(p, key, raw, n, 1UL << ftb, KP[wide ? OP_ICDF16 : OP_ICDF].scale);
  }
  return p;
}

/*  Build the frequency vector for ec_laplace_encode.  */
static unsigned lap_freq(unsigned fs0, int decay, int val) {
  unsigned fl, fs;
  int v, s, i;
  if (val == 0) return fs0;
  s = -(val < 0);
  v = (val + s) ^ s;
  fl = fs0;
  fs = (unsigned) (((i32) (32768 - 1 * (2 * 16) - (int) fs0) * (i32) (16384 - decay)) >> 15);
  for (i = 1; fs > 0 && i < v; i++) {
    fs *= 2;  fl += fs + 2 * 1;
    fs = (unsigned) (((i32) fs * (i32) decay) >> 15);
  }
  if (!fs) {
    int di, ndi_max;
    ndi_max = (32768 - (int) fl + 1 - 1);
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
  okey key = ((okey) fs0 << 20) | (okey) (unsigned) decay | ((okey) 0x80000000UL << 32);
  prior * p = pri_find(key);
  if (!p->f) {
    u32 raw[LAPN], used = 0;
    sz i;
    Fi(LAPN - 1, raw[i] = lap_freq(fs0, decay, (int) i - LAPW);  used += raw[i]);
    raw[LAPN - 1] = (used < 32768) ? 32768 - used : 1;   /*  escape  */
    pri_build(p, key, raw, LAPN, 32768, KP[OP_LAPLACE].scale);
  }
  return p;
}

/*  Mode 0 uses the step PDF and mode 2 the triangular PDF.  */
static prior * prior_theta(int mode, int qn) {
  okey key = ((okey) mode << 24) | (okey) qn | ((okey) 0xC0000000UL << 32);
  prior * p = pri_find(key);
  if (!p->f) {
    u32 raw[258], ft;
    sz i;
    int n = qn + 1;
    FATAL_UNLESS(n <= 258, "opus: theta alphabet %d out of range", n);
    if (mode == 0) {
      int p0 = 3, x0 = qn / 2;
      ft = (u32) (p0 * (x0 + 1) + x0);
      Fi((sz) n, raw[i] = (u32) ((int) i <= x0 ? p0 : 1));
    } else {
      ft = (u32) (((qn >> 1) + 1) * ((qn >> 1) + 1));
      Fi((sz) n, raw[i] = (u32) ((int) i <= (qn >> 1) ? (int) i + 1 : qn + 1 - (int) i));
    }
    pri_build(p, key, raw, n, ft, KP[OP_THETA].scale);
  }
  return p;
}

static prior * prior_uniform(int n, int scale) {
  okey key;
  prior * p;
  FATAL_UNLESS(n > 0, "opus: empty uniform alphabet");
  key = ((okey) n << 8) | (okey) (unsigned) (scale & 0xFF)
      | ((okey) 0xE0000000UL << 32);
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
  u16 * p = bin_slot(key, init);
  p_bit(p, bincnt + (p - binp), v);
}

static void om_int(okey key, int nsym, int scale, int inc, int cap, int * v) {
  prior * pr = prior_uniform(nsym, scale);
  ctx_code(ctx_find(key, pr), inc, cap, v);
}

static void om_uni(u32 * v, u32 V) { p_uniform(v, V); }


/*  Split PVQ indices with the CELT pulse-count prior.  */
#define PVQ_UN   208
#define PVQ_UK   208
#define PVQ_UBAD 0xFFFFFFFFUL
#define PVQ_LEVMAX 7             /*  three bits in the flags byte  */
#define PVQ_LEVDEF 6             /*  deeper splits cost time without shrinking output  */

static int pvq_lev = PVQ_LEVDEF;
#define PVQ_SC   1024            /*  the prior scale  */
#define PVQ_INC  16
#define PVQ_CAP  32768

#define PU(n, k) pvq_ut[(sz) (n) * (PVQ_UK + 3) + (k)]

static void pvq_ubuild(void) {
  int n, k;
  if (pvq_ut) return;
  pvq_ut = xmalloc((sz) (PVQ_UN + 1) * (PVQ_UK + 3) * sizeof *pvq_ut);
  for (k = 0; k <= PVQ_UK + 2; k++) PU(0, k) = (k == 0);
  for (n = 1; n <= PVQ_UN; n++) {
    PU(n, 0) = 0;
    for (k = 1; k <= PVQ_UK + 2; k++) {
      opus_uint64 v = (opus_uint64) PU(n - 1, k) + PU(n, k - 1) + PU(n - 1, k - 1);
      PU(n, k) = v >= (opus_uint64) PVQ_UBAD ? PVQ_UBAD : (u32) v;
    }
  }
}

static u32 pvq_V(int n, int k) {
  opus_uint64 v;
  if (n < 0 || n > PVQ_UN || k < 0 || k + 1 > PVQ_UK + 2) return PVQ_UBAD;
  if (PU(n, k) == PVQ_UBAD || PU(n, k + 1) == PVQ_UBAD) return PVQ_UBAD;
  v = (opus_uint64) PU(n, k) + PU(n, k + 1);
  return v >= (opus_uint64) PVQ_UBAD ? PVQ_UBAD : (u32) v;
}

static void pvq_dec(int n, int k, u32 i, int * y) {
  u32 p, q;
  int s, k0, val, j;
  if (k == 0) { Fj(n, y[j] = 0);  return; }
  if (n == 1) { y[0] = i ? -k : k;  return; }
  while (n > 2) {
    if (k >= n) {
      p = PU(n, k + 1);
      s = (i >= p) ? -1 : 0;
      if (s) i -= p;
      k0 = k;
      q = PU(n, n);
      if (q > i) { k = n;  do p = PU(--k, n); while (p > i); }
      else       { for (p = PU(n, k); p > i; p = PU(n, k)) k--; }
      i -= p;
      val = (k0 - k + s) ^ s;
    } else {
      p = PU(k, n);
      q = PU(k + 1, n);
      if (p <= i && i < q) { i -= p;  val = 0; }
      else {
        s = (i >= q) ? -1 : 0;
        if (s) i -= q;
        k0 = k;
        do p = PU(--k, n); while (p > i);
        i -= p;
        val = (k0 - k + s) ^ s;
      }
    }
    *y++ = val;  n--;
  }
  p = 2 * (u32) k + 1;
  s = (i >= p) ? -1 : 0;
  if (s) i -= p;
  k0 = k;
  k = (int) ((i + 1) >> 1);
  if (k) i -= 2 * (u32) k - 1;
  val = (k0 - k + s) ^ s;
  *y++ = val;
  s = -(int) i;
  *y = (k + s) ^ s;
}

static u32 pvq_enc(int n, int k, const int * y) {
  u32 i;
  int j, kk;
  if (k == 0) return 0;
  if (n == 1) return y[0] < 0 ? 1UL : 0UL;
  j = n - 1;
  i = (u32) (y[j] < 0);
  kk = y[j] < 0 ? -y[j] : y[j];
  do {
    j--;
    i += PU(n - j, kk);
    kk += y[j] < 0 ? -y[j] : y[j];
    if (y[j] < 0) i += PU(n - j, kk + 1);
  } while (j > 0);
  return i;
}

/*  Build and cache the split prior for this geometry.  */
static prior * prior_split(int n1, int n2, int K) {
  okey key = ((okey) 0xA0000000UL << 32) | ((okey) n1 << 18)
           | ((okey) n2 << 9) | (okey) K;
  prior * p = pri_find(key);
  if (!p->f) {
    u32 raw[PVQ_UK + 2], ft;
    int j;
    for (j = 0; j <= K; j++) {
      opus_uint64 t = (opus_uint64) pvq_V(n1, j) * (opus_uint64) pvq_V(n2, K - j);
      raw[j] = t >= (opus_uint64) PVQ_UBAD ? PVQ_UBAD - 1 : (u32) t;
    }
    ft = pvq_V(n1 + n2, K);
    pri_build(p, key, raw, K + 1, ft, PVQ_SC);
  }
  return p;
}

/*  Code one split node, or a flat sub-index at a leaf.  */
static void pvq_tree(int band, int N, int K, int dep, int * y) {
  int N1 = N / 2, N2 = N - N / 2, k = 0, j;
  u32 x;
  if (N < 2 || K == 0 || dep >= pvq_lev) {
    x = (om_mode == OM_DEC) ? 0 : pvq_enc(N, K, y);
    p_uniform(&x, pvq_V(N, K));
    if (om_mode == OM_DEC) pvq_dec(N, K, x, y);
    return;
  }
  if (om_mode != OM_DEC) Fj(N1, k += y[j] < 0 ? -y[j] : y[j]);
  {
    prior * pr = prior_split(N1, N2, K);
    okey key = ((okey) 0x51000000UL << 32) | ((okey) dep << 12) | (okey) band;
    ctx_code(ctx_find(key, pr), PVQ_INC, PVQ_CAP, &k);
  }
  pvq_tree(band, N1, k, dep + 1, y);
  pvq_tree(band, N2, K - k, dep + 1, y + N1);
}

static int clampb(int b) { return b < 0 ? 0 : (b >= NB ? NB - 1 : b); }

/*  Bucket small signed quantizer residuals around zero.  */
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

opus_int32 om_op(oprec * op) {
  int b = clampb(orec_band), c = orec_ch & 1;
#ifdef BLR_PROFILE
  om_comp = op->kind == OP_LAPLACE ? P_COARSE
          : op->kind == OP_THETA   ? P_THETA
          : op->kind == OP_BITS    ? P_FINE
          : op->kind == OP_LOGP    ? P_OLOGP
          : op->kind == OP_UINT    ? P_OUINT : P_OICDF;
  if (op->kind == OP_UINT && orec_pvqK > 0 && op->nsym > 16) om_comp = P_PVQ;
#endif

  switch (op->kind) {
  case OP_ICDF:
  case OP_ICDF16: {
    /*  SILK changes its stack-allocated sign table between calls.  */
    int stable = op->site != OREC_S_SILK_SIGN, wide = op->kind == OP_ICDF16;
    okey id = stable ? tab_id(op->pdf, wide, op->ftb)
                     : tab_hash(op->pdf, wide, op->ftb);
    prior * pr = prior_icdf(id, op->pdf, wide, op->ftb);
    okey key = id ^ ((okey) op->site << 40) ^ ((okey) site_hist[op->site] << 3);
    int v = op->v;
    ctx_code(ctx_find(key, pr), KP[op->kind].inc, KP[op->kind].cap, &v);
    op->v = v;
    site_hist[op->site] = (u8) (v > 7 ? 7 : v);
    break;
  }
  case OP_LOGP: {
    okey key = ((okey) op->site << 20) ^ ((okey) op->ftb << 4)
             ^ (okey) site_hist[op->site];
    unsigned logp = op->ftb;
    u16 init = (u16) (65535U - (65536U >> (logp > 15 ? 15 : logp)));
    int v = op->v;
    om_bit(key, init, &v);
    op->v = v;
    site_hist[op->site] = (u8) (((site_hist[op->site] << 1) | v) & 3);
    break;
  }
  case OP_LAPLACE: {
    /*  Coarse-energy context adds band, channel, frame shape, and neighbors.  */
    prior * pr = prior_laplace(op->aux, (int) op->ftb);
    int q5 = q5of(prev_qi[b][c]), n5 = q5of(band_qi[c]);
    int x5 = (c && c0_qib == b) ? 1 + q5of(c0_qi) : 0;
    okey key = ((okey) b << 16) | ((okey) c << 15)
             | ((okey) (orec_intra & 1) << 14) | ((okey) (orec_LM & 3) << 12)
             | ((okey) q5 << 9) | ((okey) n5 << 6) | (okey) x5;
    int v = op->v, sym = v + LAPW;
    if (sym < 0 || sym >= LAPN - 1) sym = LAPN - 1;
    ctx_code(ctx_find(key, pr), KP[OP_LAPLACE].inc, KP[OP_LAPLACE].cap, &sym);
    if (sym == LAPN - 1) {
      /*  Escape values outside the modeled window.  */
      u32 mag = (om_mode == OM_DEC) ? 0 : (u32) (v < 0 ? -v : v);
      int sg = (om_mode == OM_DEC) ? 0 : (v < 0);
      om_bit((okey) 0xFEED0001UL, 0x8000, &sg);
      p_uniform(&mag, 1UL << 16);
      if (om_mode == OM_DEC) v = sg ? -(int) mag : (int) mag;
    } else v = sym - LAPW;
    op->v = v;
    PROF(prof_coarse(b, c, orec_intra & 1, orec_LM & 3,
                     (i32) prev_qi[b][c], (i32) band_qi[c], (i32) v));
    prev_qi[b][c] = v;  band_qi[c] = v;
    if (!c) { c0_qi = v;  c0_qib = b; }
    break;
  }
  case OP_THETA: {
    /*  Predict the band angle from the previous frame.  */
    int qn = (int) op->nsym - 1, mode = (int) op->ftb, v = op->v;
    prior * pr;
    okey key;
    if (qn < 1) qn = 1;
    pr = prior_theta(mode, qn);
    key = ((okey) b << 24) | ((okey) mode << 22) | ((okey) bktN((int) op->aux) << 19)
        | ((okey) (qn & 255) << 11) | (okey) (prev_theta[b] & 7);
    ctx_code(ctx_find(key, pr), KP[OP_THETA].inc, KP[OP_THETA].cap, &v);
    op->v = v;
    PROF(prof_theta(b, mode, prev_theta[b], (u32) qn, op->aux, (i32) v));
    prev_theta[b] = (v * 8) / (qn + 1);
    break;
  }
  case OP_UINT: {
    u32 V = op->nsym;
    if (orec_pvqK > 0 && V > 16) {
      /*  Consume PVQ geometry now and reuse only a matching alphabet.  */
      int nb = bktN(orec_pvqN), kb = bktK(orec_pvqK);
      okey key = ((okey) 0x50000000UL << 32) | ((okey) b << 8)
               | ((okey) nb << 4) | (okey) kb;
      u16 * p = bin_slot(key, (u16) (65535U - (65536U >> 10)));
      u32 last = last_pvq[b][nb][kb], x;
      int hv = have_pvq[b][nb][kb] && last_pvqV[b][nb][kb] == V;
      int rep = (om_mode != OM_DEC) && hv && ((u32) op->v == last);
      if (hv) {
        p_bit(p, bincnt + (p - binp), &rep);
        if (rep) {
          orec_pvqK = 0;
          op->v = (opus_int32) last;
          PROF(prof_pvq(b, nb, kb, (u32) orec_pvqN, (u32) orec_pvqK, V,
                        (u32) op->v));
          break;
        }
      }
      /*  Split only valid codeword geometry. Otherwise code the flat index.  */
      if (orec_pvqN >= 1 && orec_pvqN <= PVQ_UN && orec_pvqK >= 1
          && orec_pvqK <= PVQ_UK && V == pvq_V(orec_pvqN, orec_pvqK)) {
        static int y[PVQ_UN + 2];
        int N = orec_pvqN, K = orec_pvqK;
        if (om_mode != OM_DEC) pvq_dec(N, K, (u32) op->v, y);
        pvq_tree(b, N, K, 0, y);
        if (om_mode == OM_DEC) op->v = (opus_int32) pvq_enc(N, K, y);
      } else {
        x = 0;
        if (om_mode != OM_DEC) { x = (u32) op->v;  if (hv && x > last) x--; }
        p_uniform(&x, hv ? V - 1 : V);
        if (om_mode == OM_DEC) {
          if (hv && x >= last) x++;
          op->v = (opus_int32) x;
        }
      }
      PROF(prof_pvq(b, nb, kb, (u32) orec_pvqN, (u32) orec_pvqK, V,
                    (u32) op->v));
      orec_pvqK = 0;
      last_pvq[b][nb][kb] = (u32) op->v;
      last_pvqV[b][nb][kb] = V;
      have_pvq[b][nb][kb] = 1;
      break;
    }
    if (V <= 1024) {
      prior * pr = prior_uniform((int) V, KP[OP_UINT].scale);
      okey key = ((okey) 0x60000000UL << 32) | ((okey) op->site << 20) | (okey) V;
      int v = op->v;
      ctx_code(ctx_find(key, pr), KP[OP_UINT].inc, KP[OP_UINT].cap, &v);
      op->v = v;
    } else {
      u32 x = (u32) op->v;
      p_uniform(&x, V);
      op->v = (opus_int32) x;
    }
    break;
  }
  case OP_BITS: {
    /*  Adapt fine-energy bits identified by orec_ftb. Pass other raw bits
        through.  */
    unsigned nb = op->ftb;
    if (nb > 0 && nb <= 8 && orec_ftb == (int) nb) {
      prior * pr = prior_uniform(1 << nb, KP[OP_BITS].scale);
      /*  Divide the unsigned alphabet into five equal buckets.  */
      okey xf = (c && c0_fineb == b && c0_finen == (int) nb)
              ? 1 + ((okey) ((u32) c0_fine * 5) >> nb) : 0;
      okey key = ((okey) 0x70000000UL << 32) | ((okey) b << 16) | ((okey) c << 15)
               | ((okey) nb << 11) | (okey) (prev_fine[b][c] & 0x7FF) | (xf << 24);
      int v = op->v;
      ctx_code(ctx_find(key, pr), KP[OP_BITS].inc, KP[OP_BITS].cap, &v);
      op->v = v;
      PROF(prof_fine(b, c, (int) nb, prev_fine[b][c], (i32) v));
      prev_fine[b][c] = v;
      if (!c) { c0_fine = v;  c0_fineb = b;  c0_finen = (int) nb; }
    } else {
      u32 x = (u32) op->v;
      p_raw(&x, (int) nb);
      op->v = (opus_int32) x;
    }
    break;
  }
  default: break;
  }
#ifdef BLR_PROFILE
  om_comp = P_OOTHER;
#endif
  return op->v;
}

/*  Rebuild Opus lacing and model other framing fields as residuals.  */
#define K_TOC     ((okey) 0x10000000UL << 32)
#define K_HDR     ((okey) 0x11000000UL << 32)
#define K_NHDR    ((okey) 0x12000000UL << 32)
#define K_LEN     ((okey) 0x13000000UL << 32)
#define K_TRL     ((okey) 0x15000000UL << 32)
#define K_HTYPE   ((okey) 0x17000000UL << 32)
#define K_NSEGS   ((okey) 0x18000000UL << 32)
#define K_SEQOK   ((okey) 0x19000000UL << 32)
#define K_GDOK    ((okey) 0x1A000000UL << 32)
#define K_GDELTA  ((okey) 0x1B000000UL << 32)
#define K_RAW     ((okey) 0x1D000000UL << 32)
#define K_FSLACK  ((okey) 0x1E000000UL << 32)
#define K_FBYTE   ((okey) 0x1F000000UL << 32)

/*  Refuse header packets that would overflow the 4096-wide model.  */
#define OC_BLOBMAX  4096
/*  Likewise the packet-header length model is 16-wide.  */
#define OC_HDRMAX   16

typedef struct {
  u8 prev_hdr[8];
  int prev_nhdr, prev_len, prev_htype, prev_nsegs;
  u32 prev_seq;
  opus_uint64 prev_gran;
  opus_int64 prev_gdelta;
} oc_state;

typedef struct {
  int htype, nsegs;
  u32 seqno;
  opus_uint64 granule;
} oc_pagehdr;

static void byte_ctx(okey base, int slot, int prev, int * v) {
  om_int(base | ((okey) slot << 16) | (okey) (prev & 0xFF), 256, 192, 28, 16384, v);
}

/*  Store bytes changed by reconstruction.  */
void om_frame(u8 * buf, const u8 * mir, u32 n) {
  int diff = 0, prev = 0;
  u32 lo = 0, hi = 0, i, span = 0;
  if (om_mode != OM_DEC) {
    while (lo < n && buf[lo] == mir[lo]) lo++;
    if (lo < n) {
      diff = 1;
      hi = n - 1;
      while (buf[hi] == mir[hi]) hi--;
      span = hi - lo;
    }
  }
  om_bit(K_FSLACK, (u16) (65535U - (65536U >> 12)), &diff);
  if (!diff) return;
  om_uni(&lo, n);
  FATAL_IF_HOT(om_mode == OM_DEC && lo >= n)
    ("opus: frame patch starts at %lu of %lu", (unsigned long) lo, (unsigned long) n);
  om_uni(&span, n - lo);
  hi = lo + span;
  for (i = lo; i <= hi; i++) {
    int v = (om_mode == OM_DEC) ? 0 : buf[i];
    byte_ctx(K_FBYTE, 0, prev, &v);
    buf[i] = (u8) v;
    prev = v;
  }
}

/*  Use unsigned magnitudes to avoid signed overflow.  */
static void resid(okey base, opus_int64 * v) {
  int zero, sg, nbits = 0;
  opus_uint64 mag, m2;
  zero = (om_mode != OM_DEC) && (*v == 0);
  sg = (om_mode != OM_DEC) && (*v < 0);
  mag = (om_mode != OM_DEC) ? (sg ? 0 - (opus_uint64) *v : (opus_uint64) *v) : 0;
  om_bit(base | 1, 0x8000, &zero);
  if (zero) { if (om_mode == OM_DEC) *v = 0;  return; }
  om_bit(base | 2, 0x8000, &sg);
  if (om_mode != OM_DEC) for (m2 = mag; m2 > 1; m2 >>= 1) nbits++;
  om_int(base | 3, 64, 192, 28, 16384, &nbits);
  if (nbits > 0) {
    /*  The low `nbits` bits, in 16-bit chunks; the top bit is implied.  */
    opus_uint64 m = 0;
    int sh = nbits;
    while (sh > 0) {
      int k = sh > 16 ? 16 : sh;
      u32 part;
      sh -= k;
      part = (om_mode == OM_DEC) ? 0 : (u32) ((mag >> sh) & ((1UL << k) - 1));
      om_uni(&part, (u32) 1 << k);
      m |= (opus_uint64) part << sh;
    }
    if (om_mode == OM_DEC) mag = ((opus_uint64) 1 << nbits) | m;
  } else if (om_mode == OM_DEC) mag = 1;
  if (om_mode == OM_DEC) *v = (opus_int64) (sg ? 0 - mag : mag);
}

static void oc_packet_hdr(oc_state * s, int * nhdr, u8 * hdr, int * len) {
  int i, v = *nhdr;
  opus_int64 d;
  om_int(K_NHDR | (okey) (s->prev_nhdr & 15), 16, 192, 28, 16384, &v);
  *nhdr = v;  s->prev_nhdr = v;
  Fi(*nhdr,
    int b = (om_mode == OM_DEC) ? 0 : hdr[i];
    byte_ctx(i ? K_HDR : K_TOC, i < 8 ? i : 8, i < 8 ? s->prev_hdr[i] : 0, &b);
    hdr[i] = (u8) b;
    if (i < 8) s->prev_hdr[i] = (u8) b);
  d = (om_mode == OM_DEC) ? 0 : (opus_int64) *len - (opus_int64) s->prev_len;
  resid(K_LEN, &d);
  if (om_mode == OM_DEC) {
    opus_int64 next = (opus_int64) s->prev_len + d;
    FATAL_UNLESS(next >= INT_MIN && next <= INT_MAX,
                 "opus: packet length is out of range");
    *len = (int) next;
  }
  s->prev_len = *len;
}

static void oc_packet_trailer(int n, u8 * t) {
  int i;
  Fi(n,
    int b = (om_mode == OM_DEC) ? 0 : t[i];
    byte_ctx(K_TRL, i < 8 ? i : 8, i ? t[i - 1] : 0, &b);
    t[i] = (u8) b);
}

/*  Code OpusHead and OpusTags whole.  */
static void oc_blob(u8 * b, int * n) {
  int i, v = *n;
  om_int(K_RAW, 4096, 192, 28, 16384, &v);
  *n = v;
  Fi(*n,
    int x = (om_mode == OM_DEC) ? 0 : b[i];
    byte_ctx(K_RAW | 0x10000, 0, i ? b[i - 1] : 0, &x);
    b[i] = (u8) x);
}

static void oc_u32(u32 * v) {
  u32 hi = (om_mode == OM_DEC) ? 0 : (*v >> 16), lo = (om_mode == OM_DEC) ? 0 : (*v & 0xFFFF);
  om_uni(&hi, 65536);  om_uni(&lo, 65536);
  if (om_mode == OM_DEC) *v = (hi << 16) | lo;
}

static void oc_page(oc_state * s, oc_pagehdr * p) {
  int v;
  opus_int64 dd;
  v = (om_mode == OM_DEC) ? 0 : p->htype;
  om_int(K_HTYPE | (okey) s->prev_htype, 256, 192, 28, 16384, &v);
  p->htype = v;  s->prev_htype = v;

  v = (om_mode == OM_DEC) ? 0 : p->nsegs;
  om_int(K_NSEGS | (okey) s->prev_nsegs, 256, 192, 28, 16384, &v);
  p->nsegs = v;  s->prev_nsegs = v;

  v = (om_mode == OM_DEC) ? 0 : (p->seqno == s->prev_seq + 1);
  om_bit(K_SEQOK, 0xF000, &v);
  if (v) { if (om_mode == OM_DEC) p->seqno = s->prev_seq + 1; }
  else { u32 x = p->seqno;  oc_u32(&x);  p->seqno = x; }
  s->prev_seq = p->seqno;

  v = (om_mode == OM_DEC) ? 0 : (p->granule == ~(opus_uint64) 0);
  om_bit(K_GDOK | 1, 0x0400, &v);
  if (v) { if (om_mode == OM_DEC) p->granule = ~(opus_uint64) 0;  return; }
  /*  Form arbitrary 64-bit granule residuals modulo 2^64.  */
  dd = (om_mode == OM_DEC) ? 0
     : (opus_int64) (p->granule - s->prev_gran - (opus_uint64) s->prev_gdelta);
  resid(K_GDELTA, &dd);
  if (om_mode == OM_DEC)
    p->granule = s->prev_gran + (opus_uint64) s->prev_gdelta + (opus_uint64) dd;
  s->prev_gdelta = (opus_int64) (p->granule - s->prev_gran);
  s->prev_gran = p->granule;
}

/*  Ogg framing over lacing values and packet boundaries.  */
#define OPUS_MAXPKT (48 * 1275 + 64)  /*  48 frames of the largest legal size  */

typedef struct { sz off, end;  int len; } opkt;
typedef struct { int htype, nsegs; u32 seqno; opus_uint64 granule; } opage;

typedef struct {
  opkt * pk;  int npk, pkcap;
  opage * pg;  int npg, pgcap;
  u32 serial;
  int channels;
} ostream;

static void st_free(ostream * s) {
  free(s->pk);  free(s->pg);
  memset(s, 0, sizeof *s);
}

/* Fetch a packet from its original pages; continued packets skip framing. */
static void packet_read(blr_file * input, const opkt * pk, u8 * packet) {
  sz at = pk->off, end = pk->end, left = (sz) pk->len;
  while (left) {
    sz take = MIN(left, end - at);
    bf_read(input, at, packet, take);
    packet += take;  at += take;  left -= take;
    if (left) {
      u8 h[OGG_HDRMIN + OGG_MAXSEG];
      sz n, i;
      bf_read(input, end, h, OGG_HDRMIN);
      FATAL_UNLESS(!memcmp(h, "OggS", 4), "opus: missing continuation page");
      n = h[26];  bf_read(input, end + OGG_HDRMIN, h + OGG_HDRMIN, n);
      at = end + OGG_HDRMIN + n;  end = at;
      Fi(n, end += h[OGG_HDRMIN + i]);
    }
  }
}

/*  Split Ogg Opus input into packets and page headers.  */
static int parse_stream(blr_file * input, ostream * s) {
  sz off = 0, len = input->len, pstart = 0, pend = 0;
  u8 * page = xmalloc(OGG_HDRMIN + OGG_MAXSEG + 65025);
  u8 head[19];
  int acclen = 0;
  memset(s, 0, sizeof *s);

  while (off + OGG_HDRMIN <= len) {
    int i, boff = 0;
    sz got;
    ogg_page p;
    const u8 * h = page;
    bf_read(input, off, page, OGG_HDRMIN);
    if (memcmp(h, "OggS", 4)) { fprintf(stderr, "balrogg: not an Ogg file\n");  goto bad; }
    if (h[4]) { fprintf(stderr, "balrogg: unsupported Ogg version %u\n", h[4]);  goto bad; }
    got = ogg_read(input, off, &p, page);
    if (!got) { fprintf(stderr, "balrogg: truncated Ogg page\n");  goto bad; }
    /*  CRCs must be valid because rebuild() recomputes them.  */
    if (!ogg_crc_ok(h, got)) {
      fprintf(stderr, "balrogg: Ogg page %lu has a bad CRC\n", (unsigned long) off);
      goto bad;
    }
    if (s->npg == s->pgcap) {
      if (s->pgcap > INT_MAX / 2 ||
          (sz) s->pgcap > SIZE_MAX / 2 / sizeof *s->pg) {
        fprintf(stderr, "balrogg: too many Ogg pages\n");  goto bad;
      }
      s->pgcap = s->pgcap ? s->pgcap * 2 : 1024;
      s->pg = xrealloc(s->pg, (sz) s->pgcap * sizeof *s->pg);
    }
    s->pg[s->npg].htype = p.type;
    s->pg[s->npg].nsegs = p.nseg;
    s->pg[s->npg].seqno = p.seq;
    s->pg[s->npg].granule = (opus_uint64) p.glo | ((opus_uint64) p.ghi << 32);
    /*  Chained Opus streams are unsupported.  */
    if (s->npg && p.serial != s->serial) {
      fprintf(stderr, "balrogg: chained Ogg Opus streams are unsupported\n");
      goto bad;
    }
    s->serial = p.serial;
    s->npg++;
    Fi(p.nseg,
      int sl = p.lace[i];
      if (acclen + sl > OPUS_MAXPKT) { fprintf(stderr, "balrogg: over-long Opus packet\n");  goto bad; }
      if (!acclen) { pstart = off + OGG_HDRMIN + p.nseg + (sz) boff; pend = off + got; }
      acclen += sl;  boff += sl;
      if (sl < 255) {
        if (s->npk == s->pkcap) {
          if (s->pkcap > INT_MAX / 2 ||
              (sz) s->pkcap > SIZE_MAX / 2 / sizeof *s->pk) {
            fprintf(stderr, "balrogg: too many Opus packets\n");  goto bad;
          }
          s->pkcap = s->pkcap ? s->pkcap * 2 : 4096;
          s->pk = xrealloc(s->pk, (sz) s->pkcap * sizeof *s->pk);
        }
        s->pk[s->npk].off = pstart;
        s->pk[s->npk].end = pend;
        s->pk[s->npk].len = acclen;
        s->npk++;  acclen = 0;
      });
    off += got;
  }
  free(page);
  memset(head, 0, sizeof head);
  if (s->npk) { opkt first = s->pk[0];
    first.len = (int) MIN((sz) first.len, sizeof head);
    packet_read(input, &first, head); }
  if (off != len) { fprintf(stderr, "balrogg: data follows the last Ogg page\n");  return 1; }
  if (acclen) { fprintf(stderr, "balrogg: incomplete final Opus packet\n");  return 1; }
  if (s->npk < 2 || s->pk[0].len < 8 || memcmp(head, "OpusHead", 8)) {
    fprintf(stderr, "balrogg: not an Ogg Opus stream\n");  return 1;
  }
  if (s->pk[0].len < 19) { fprintf(stderr, "balrogg: truncated OpusHead\n");  return 1; }
  if (head[18]) {
    fprintf(stderr, "balrogg: unsupported channel mapping family %d\n", head[18]);
    return 1;
  }
  s->channels = head[9];
  if (s->channels < 1 || s->channels > 2) {
    fprintf(stderr, "balrogg: unsupported channel count %d\n", s->channels);
    return 1;
  }
  s->pk = xrealloc(s->pk, (sz) s->npk * sizeof *s->pk);
  s->pg = xrealloc(s->pg, (sz) s->npg * sizeof *s->pg);
  s->pkcap = s->npk;  s->pgcap = s->npg;
  return 0;
bad:
  free(page);
  return 1;
}

/* Page metadata precedes packets, so decoding emits each completed page once. */
typedef struct {
  const ostream * stream;
  blr_file * output;
  u8 * page;
  int pg, seg;
  sz body;
} page_writer;

static void page_flush(page_writer * w) {
  const opage * p = w->stream->pg + w->pg;
  u8 * o = w->page;
  sz size = OGG_HDRMIN + (sz) p->nsegs + w->body;
  int k;
  memcpy(o, "OggS", 4); o[4] = 0; o[5] = (u8) p->htype;
  Fk(8, o[6 + k] = (u8) (p->granule >> (8 * k)));
  Fk(4, o[14 + k] = (u8) (w->stream->serial >> (8 * k));
        o[18 + k] = (u8) (p->seqno >> (8 * k)));
  memset(o + 22, 0, 4); o[26] = (u8) p->nsegs;
  ogg_crc_set(o, size); bf_write(w->output, w->output->len, o, size);
  w->pg++; w->seg = 0; w->body = 0;
}

static void page_empty(page_writer * w) {
  while (w->pg < w->stream->npg && !w->stream->pg[w->pg].nsegs) page_flush(w);
}

static void page_packet(page_writer * w, const u8 * packet, sz len) {
  sz take;
  do {
    int ns;
    page_empty(w);
    FATAL_UNLESS(w->pg < w->stream->npg, "opus: packets exceed page layout");
    ns = w->stream->pg[w->pg].nsegs;
    take = MIN(len, (sz) 255);
    w->page[OGG_HDRMIN + w->seg++] = (u8) take;
    memcpy(w->page + OGG_HDRMIN + ns + w->body, packet, take);
    w->body += take; packet += take; len -= take;
    if (w->seg == ns) page_flush(w);
  } while (take == 255);
}

int opus_pack(const char * in, const char * out, int lev) {
  ostream s;
  orc_enc e;
  oc_state cs;
  OpusDecoder * dec = NULL;
  u8 * packet = xmalloc(OPUS_MAXPKT);
  blr_file * input = bf_open(in, 0);
  archive a;
  blr_file * output = NULL;
  int i, err = 0, rc = BLR_EXIT_REFUSED;
  u32 u;

  FATAL_UNLESS(lev >= 0 && lev <= PVQ_LEVMAX, "opus: level %d is out of range", lev);
  pvq_lev = lev;
  memset(&e, 0, sizeof e);
  arc_init(&a, (u8) (0x09 | ARC_OPUS | (lev << 5)));
  if (parse_stream(input, &s)) goto done;
  blr_progress_begin(in, "encoding", (sz) s.npk);
  /*  Match the decoder's count limits.  */
  if (s.npk > (1L << 24) || s.npg > (1L << 24)) {
    fprintf(stderr, "balrogg: %s has %d packets on %d pages, limit %ld\n",
            in, s.npk, s.npg, 1L << 24);
    goto done;
  }
  Fi(2,
    if (s.pk[i].len >= OC_BLOBMAX) {
      fprintf(stderr, "balrogg: %s header packet %d is %d bytes, limit %d\n",
              in, i, s.pk[i].len, OC_BLOBMAX - 1);
      goto done;
    });

  output = bf_open(out, 1); arc_begin(&a, output);
  orc_enc_init(&e, arc_newstream(&a));
  om_init();  om_mode = OM_ENC;  E = &e;  D = NULL;
  memset(&cs, 0, sizeof cs);

  u = (u32) s.npk;  oc_u32(&u);
  u = (u32) s.npg;  oc_u32(&u);
  u = s.serial;     oc_u32(&u);
  Fi(s.npg,
    oc_pagehdr p;
    p.htype = s.pg[i].htype;  p.nsegs = s.pg[i].nsegs;
    p.seqno = s.pg[i].seqno;  p.granule = s.pg[i].granule;
    oc_page(&cs, &p));

  Fi(2, int n = s.pk[i].len;
        packet_read(input, s.pk + i, packet);  oc_blob(packet, &n));

  dec = opus_decoder_create(s.channels);
  if (!dec) { fprintf(stderr, "balrogg: %s cannot create Opus decoder\n", in);  goto done; }

  for (i = 2; i < s.npk; i++) {
    unsigned char toc;
    const unsigned char * frames[48];
    opus_int16 sizes[48];
    int poff = 0, nf, j, sum = 0, L = s.pk[i].len;
    packet_read(input, s.pk + i, packet);
    nf = opus_packet_parse(packet, L, &toc, frames, sizes, &poff);
    if (nf < 0) { fprintf(stderr, "balrogg: %s packet %d parse failed (%d)\n", in, i, nf);  goto done; }
    if (poff < 1 || poff >= OC_HDRMAX) {
      fprintf(stderr, "balrogg: %s packet %d header is %d bytes, limit %d\n",
              in, i, poff, OC_HDRMAX - 1);
      goto done;
    }
    Fj(nf, sum += sizes[j]);
    oc_packet_hdr(&cs, &poff, packet, &L);
    oc_packet_trailer(L - poff - sum, packet + poff + sum);
    oe_begin();  orec_mode = OREC_ANALYZE;
    err = opus_decode(dec, packet, L);
    oe_end();  orec_mode = OREC_OFF;
    if (err < 0) { fprintf(stderr, "balrogg: %s packet %d decode failed (%d)\n", in, i, err);  goto done; }
    blr_progress_update((sz) i + 1);
  }

  orc_enc_done(&e); arc_finish(&a);
  rc = BLR_EXIT_OK;
done:
  if (rc) blr_progress_cancel();
  if (dec) opus_decoder_destroy(dec);
  orc_enc_free(&e); arc_free(&a); bf_close(output);
  bf_close(input); free(packet); st_free(&s); om_free();
  if (!rc) blr_progress_end();
  return rc;
}

int opus_unpack(const char * in, const char * out) {
  ostream s;
  orc_dec d;
  oc_state cs;
  archive a;
  OpusDecoder * dec = NULL;
  u8 * packet = xmalloc(OPUS_MAXPKT);
  blr_file * input = bf_open(in, 0), * output = NULL;
  page_writer w;
  memset(&w, 0, sizeof w);
  int i, err = 0, rc = BLR_EXIT_REFUSED;
  u32 u = 0;

  memset(&s, 0, sizeof s);
  arc_read(&a, input);
  if (!(a.flags & ARC_OPUS) || a.n != 1) {
    fprintf(stderr, "balrogg: %s invalid Opus archive (%02x, %lu streams)\n",
            in, a.flags, (unsigned long) a.n);
    goto done;
  }
  pvq_lev = (int) ARC_LEVEL(a.flags);
  orc_dec_init(&d, a.s[0].file, a.s[0].off, a.s[0].len);
  om_init();  om_mode = OM_DEC;  E = NULL;  D = &d;
  memset(&cs, 0, sizeof cs);

  oc_u32(&u);
  if (u < 2 || u > (1UL << 24)) {
    fprintf(stderr, "balrogg: %s invalid packet count %lu\n", in,
            (unsigned long) u);
    goto done;
  }
  s.npk = (int) u;
  blr_progress_begin(in, "decoding", (sz) s.npk);
  oc_u32(&u);
  if (u < 1 || u > (1UL << 24)) {
    fprintf(stderr, "balrogg: %s invalid page count %lu\n", in,
            (unsigned long) u);
    goto done;
  }
  s.npg = (int) u;
  oc_u32(&u);  s.serial = u;
  s.pg = xmalloc((sz) s.npg * sizeof *s.pg);
  memset(s.pg, 0, (sz) s.npg * sizeof *s.pg);

  Fi(s.npg,
    oc_pagehdr p;
    memset(&p, 0, sizeof p);
    oc_page(&cs, &p);
    s.pg[i].htype = p.htype;  s.pg[i].nsegs = p.nsegs;
    s.pg[i].seqno = p.seqno;  s.pg[i].granule = p.granule);

  output = bf_open(out, 1); w.output = output; w.stream = &s;
  w.page = xmalloc(OGG_HDRMIN + OGG_MAXSEG + 65025);

  Fi(2,
    int n = 0;
    oc_blob(packet, &n);
    if (!i) {
      if (n < 19 || memcmp(packet, "OpusHead", 8)) {
        fprintf(stderr, "balrogg: %s has no OpusHead\n", in); goto done;
      }
      s.channels = packet[9];
      if (s.channels < 1 || s.channels > 2) {
        fprintf(stderr, "balrogg: %s invalid channel count %d\n", in, s.channels); goto done;
      }
    }
    page_packet(&w, packet, (sz) n));

  dec = opus_decoder_create(s.channels);
  if (!dec) { fprintf(stderr, "balrogg: %s cannot create Opus decoder\n", in);  goto done; }

  for (i = 2; i < s.npk; i++) {
    u8 hdr[OC_HDRMAX];
    unsigned char toc;
    const unsigned char * frames[48];
    opus_int16 sizes[48];
    int nhdr = 0, L = 0, nf, j, sum = 0, poff = 0;
    oc_packet_hdr(&cs, &nhdr, hdr, &L);
    if (L < 1 || L > OPUS_MAXPKT || nhdr < 1 || nhdr >= OC_HDRMAX) {
      fprintf(stderr, "balrogg: %s packet %d has header %d and length %d\n",
              in, i, nhdr, L);
      goto done;
    }
    memset(packet, 0, (sz) L);
    memcpy(packet, hdr, (sz) (nhdr < L ? nhdr : L));
    nf = opus_packet_parse(packet, L, &toc, frames, sizes, &poff);
    if (nf < 0 || poff != nhdr) {
      fprintf(stderr, "balrogg: %s packet %d header mismatch "
                      "(parse %d, size %d, expected %d)\n",
              in, i, nf, poff, nhdr);
      goto done;
    }
    Fj(nf, sum += sizes[j]);
    oc_packet_trailer(L - nhdr - sum, packet + nhdr + sum);
    oe_begin();  orec_mode = OREC_SYNTH;
    err = opus_decode(dec, packet, L);
    oe_end();  orec_mode = OREC_OFF;
    if (err < 0) {
      fprintf(stderr, "balrogg: %s cannot rebuild packet %d (%d)\n", in, i, err);  goto done;
    }
    page_packet(&w, packet, (sz) L);
    blr_progress_update((sz) i + 1);
  }


  page_empty(&w);
  FATAL_UNLESS(w.pg == s.npg && !w.seg, "opus: page layout exceeds packets");
  rc = BLR_EXIT_OK;
done:
  if (rc) blr_progress_cancel();
  if (dec) opus_decoder_destroy(dec);
  bf_close(output); free(w.page);
  bf_close(input); free(packet); arc_free(&a); st_free(&s); om_free();
  if (!rc) blr_progress_end();
  return rc;
}
