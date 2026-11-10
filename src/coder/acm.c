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

#include "coder/acm.h"
#include "coder/cm.h"
#include "sys/cpu.h"
#include "sys/map.h"

#define TBITS   23
#define MROWS   16384
#define LTBITS  21
#define LMROWS  4096
#define NIN     (3 + 3 * ACM_NCX)
#define NIN16   128
#define AROWS   (1 << 16)
#define LAROWS  (1 << 14)
#define DLIM    1023
#define SMLIM   1023
#define GATEN   12

#if defined(__GNUC__)
#define PREFETCH(p) __builtin_prefetch(p)
#else
#define PREFETCH(p) ((void) (p))
#endif

typedef struct { u16 chk, n, p, q;  u8 st; } slot;

struct acm {
  blr_map tmap, wmap;
  slot * t;
  u16 * sm;
  i32 * w1;  u32 * w1n;
  i16 * h1;
  i32 * w2;
  i32 * w2l;
  i32 * wa;  u32 * wan;
  u16 * a[3];
  acm_coder code;  void * cctx;
  int lrmin, lr2, apw, flim, conf;
  int lite, sel8;
  int w16;
  u8 eng[ACM_NTY], cap[ACM_NTY];
  acm_dec d;
};


static int64_t dot_c(const i32 * w, const int * x, int n) {
  int64_t a = 0;
  int i;
  Fi(n, a += (int64_t) w[i] * x[i]);
  return a;
}

static void train_c(i32 * w, const int * x, int n, int e) {
  int i;
  Fi(n, w[i] += (i32) (((int64_t) x[i] * e) >> 20));
}

static int64_t dot16_c(const i16 * w, const i16 * x, int n) {
  int64_t a = 0;
  int i;
  Fi(n, a += (int) w[i] * x[i]);
  return a;
}

static void train16_c(i16 * w, const i16 * x, int n, int e) {
  int i;
  Fi(n,
    int v = w[i] + ((x[i] * e + 16384) >> 15);
    w[i] = (i16) (v > 32767 ? 32767 : v < -32768 ? -32768 : v));
}

static int64_t (*row_dot)(const i32 * w, const int * x, int n) = dot_c;
static int64_t (*row_dot16)(const i16 * w, const i16 * x, int n) = dot16_c;
static void (*row_train16)(i16 * w, const i16 * x, int n, int e) = train16_c;
static void (*row_train)(i32 * w, const int * x, int n, int e) = train_c;

short acm_str[4096];
static u16 SQ[4095];
static u32 REC[1025];
static int LR[4096];

void acm_prepare(void) {
  int x, pi = 0, i;
  if (REC[0]) return;
  cm_prepare();
#if defined(HAVE_AVX2)
  if (blr_cpu_avx2()) {
    row_dot = acm_dot_avx2;  row_train = acm_train_avx2;
    row_dot16 = acm_dot16_avx2;  row_train16 = acm_train16_avx2;
  }
#endif
  for (x = -2047; x <= 2047; x++) {
    int v = acm_squash(x);
    for (i = pi; i <= v; i++) acm_str[i] = (short) x;
    pi = v + 1;
  }
  for (i = pi; i < 4096; i++) acm_str[i] = 2047;
  Fi(4095, SQ[i] = (u16) acm_squash(i - 2047));
  for (i = 0; i <= 1024; i++) REC[i] = 131072u / (u32) (2 * i + 3);
  Fi(4096, LR[i] = 983 * 200 / (200 + 16 * i));
}

static sz tbytes(void) { return ((sz) 1 << TBITS) * sizeof (slot); }
static sz smn(void) { return (sz) ACM_NTY * ACM_NCX * 256 * 16; }

sz acm_bytes(void) {
  return tbytes() + 2 * smn() * sizeof (u16) +
         (sz) ACM_NSEL * MROWS * (NIN * sizeof (i32) + sizeof (u32)) +
         2 * (sz) MROWS * (ACM_NSEL + 2) * sizeof (i32) +
         3 * (sz) AROWS * 33 * sizeof (u16) +
         (sz) MROWS * (4 * sizeof (i32) + sizeof (u32));
}

static sz mrows(const acm * q) { return q->lite ? LMROWS : MROWS; }

static i32 * w2new(sz mr, sz ns) {
  i32 * w = xmalloc(mr * (ACM_NSEL + 2) * sizeof *w);
  sz i, j;
  Fi(mr,
    Fj(ACM_NSEL + 2,
      w[i * (ACM_NSEL + 2) + j] = j < ns ? (i32) (65536 / ns) : 0));
  return w;
}

acm * acm_new(int prof) {
  acm * q = xmalloc(sizeof *q);
  sz i, j, ns, tb, mr, ar;
  int apms;
  acm_prepare();
  memset(q, 0, sizeof *q);
  q->w16 = (prof & ACM_W16) != 0;  apms = (prof & ACM_APMS) != 0;
  prof &= ~(ACM_W16 | ACM_APMS);
  if (prof == ACM_OPUS || prof == ACM_OPUSG) {
    q->lrmin = 262;  q->lr2 = 131;  q->apw = 20;  q->flim = 40;  q->conf = 0;
    if (prof == ACM_OPUSG) { q->apw = 50;  q->conf = GATEN; }
  } else {
    q->lrmin = 66;  q->lr2 = 52;  q->apw = 45;  q->flim = 20;  q->conf = 3;
    if (prof == ACM_VORBISG) {
      q->lrmin = 132;  q->lr2 = 104;  q->apw = 75;  q->conf = GATEN;
    }
    if (prof == ACM_VLITE) { q->lrmin = 264;  q->lr2 = 208;  q->apw = 100; }
    if (prof == ACM_MODX) { q->lrmin = 132;  q->lr2 = 104;  q->apw = 75; }
  }
  q->lite = prof == ACM_VLITE || prof == ACM_MODX;
  q->sel8 = prof == ACM_MODX;
  tb = (sz) 1 << (q->lite ? LTBITS : TBITS);
  mr = q->lite ? LMROWS : MROWS;  ar = q->lite ? LAROWS : AROWS;
  if (!apms) {
    bm_alloc_huge(&q->tmap, tb * sizeof (slot));
    q->t = (slot *) (void *) q->tmap.p;
    q->sm = xcalloc(2 * smn(), sizeof *q->sm);
    Fi(smn(),
      q->sm[2 * i] = (u16) (acm_squash(((int) (i & 15) - 8) * 256 + 128) << 4));
    if (q->w16) {
      bm_alloc_huge(&q->wmap, (sz) ACM_NSEL * mr * NIN16 * sizeof *q->h1);
      q->h1 = (i16 *) (void *) q->wmap.p;
      Fi((sz) ACM_NSEL * mr, q->h1[i * NIN16] = 16384);
    } else {
      bm_alloc_huge(&q->wmap, ((sz) ACM_NSEL * mr * NIN + 8) * sizeof *q->w1);
      q->w1 = (i32 *) (void *) q->wmap.p;
      Fi((sz) ACM_NSEL * mr, q->w1[i * NIN] = 65536);
    }
    q->w1n = xcalloc((sz) ACM_NSEL * mr, sizeof *q->w1n);
    ns = q->lite && !q->sel8 ? ACM_NLSEL : ACM_NSEL;
    q->w2 = w2new(mr, ns);
  }
  { u16 row[33];
    Fi(33, row[i] = (u16) (acm_squash(((int) i - 16) * 128) * 16));
    Fj(3,
      q->a[j] = xmalloc(ar * 33 * sizeof *q->a[j]);
      Fi(ar, memcpy(q->a[j] + i * 33, row, sizeof row))); }
  return q;
}

void acm_free(acm * q) {
  int j;
  if (!q) return;
  bm_free(&q->tmap);  xfree(q->sm);  bm_free(&q->wmap);  xfree(q->w1n);
  xfree(q->w2);  xfree(q->w2l);  xfree(q->wa);  xfree(q->wan);
  Fj(3, xfree(q->a[j]));
  xfree(q);
}

void acm_class(acm * q, int ty, int e, int ncap) {
  sz i, mr = mrows(q);
  q->eng[ty] = (u8) e;  q->cap[ty] = (u8) ncap;
  if (e == ACM_ELITE && !q->lite && !q->w2l) q->w2l = w2new(mr, ACM_NLSEL);
  if (e == ACM_EAPM && !q->wa) {
    q->wa = xcalloc(mr * 4, sizeof *q->wa);
    q->wan = xcalloc(mr, sizeof *q->wan);
    Fi(mr, q->wa[i * 4] = 65536);
  }
}

void acm_bind(acm * q, acm_coder c, void * ctx) { q->code = c;  q->cctx = ctx; }
acm_dec * acm_d(acm * q) { return &q->d; }

static INLINE slot * bucket(acm * q, acm_h h, int tbits) {
  return q->t + ((sz) (h & (((acm_h) 1 << tbits) - 1)) & ~(sz) 3);
}

static INLINE slot * find(acm * q, acm_h h, u16 p0, int tbits) {
  u16 chk = (u16) (h >> 48 | 1);
  int j, best = 0, bn = 1 << 30;
  slot * s = bucket(q, h, tbits);
  Fj(4,
    if (s[j].chk == chk) return s + j;
    if (s[j].n < bn) { bn = s[j].n;  best = j; });
  s += best;
  s->chk = chk;  s->st = 0;  s->n = 0;  s->p = s->q = p0;
  return s;
}

static INLINE int clamp(int64_t v) {
  return v > 2047 ? 2047 : v < -2047 ? -2047 : (int) v;
}

static INLINE int apm(const u16 * t, int s, int * lo, int * w) {
  int x = s + 2048;
  *lo = x >> 7;  *w = x & 127;
  return (t[*lo] * (128 - *w) + t[*lo + 1] * *w) >> 11;
}

static INLINE void apm_upd(u16 * t, int lo, int w, int bit) {
  int g = bit ? 65535 : 0;
  t[lo] = (u16) (t[lo] + ((g - t[lo]) * (128 - w) >> 12));
  t[lo + 1] = (u16) (t[lo + 1] + ((g - t[lo + 1]) * w >> 12));
}

static INLINE u16 adapt(u16 p, int bit, u32 r) {
  int g = bit ? 65535 : 0,
      v = p + (int) (((int64_t) (g - p) * (int64_t) r) >> 16);
  return (u16) (v < 16 ? 16 : v > 65519 ? 65519 : v);
}

static INLINE int bit_k(acm * q, int bit, u32 p1, acm_h node, const int nsel,
                        const int nin, const int tbits, const sz mrows,
                        const sz arows, const int w16) {
  acm_dec * d = &q->d;
  const int ncx = q->cap[d->ty] && d->ncx > q->cap[d->ty] ? q->cap[d->ty]
                                                          : d->ncx;
  i32 * w2 = nsel < ACM_NSEL && !q->lite ? q->w2l : q->w2;
  i16 x16[NIN16];
  int x[NIN + 8], dot[ACM_NSEL], ps[ACM_NSEL], i, j, nx, pb, nfresh = 0, spr,
      pm, s2, lo[3], wa[3], pa[3], p, err;
  int xp[ACM_NCX], xq[ACM_NCX];
  slot * sl[ACM_NCX];
  acm_h hc[ACM_NCX];
  u16 * smp[ACM_NCX], * at[3];
  sz r1[ACM_NSEL], r2;
  u16 p0;
  int64_t a;
  if (p1 < 1) p1 = 1;
  if (p1 > 65535) p1 = 65535;
  spr = acm_str[p1 >> 4];
  pb = (spr + 2048) >> 8;
  if (pb > 15) pb = 15;
  x[0] = spr;  x[1] = 64;  x[2] = d->xon ? d->xin : 0;  nx = 3;
  p0 = (u16) p1;
  Fi(ncx,
    hc[i] = PH3(d->cx[i], node, i);
    PREFETCH(bucket(q, hc[i], tbits)));
  Fj(3,
    at[j] = q->a[j] + (sz) (PH3(d->apm[j], d->ty, node) & (arows - 1)) * 33;
    PREFETCH(at[j]);  PREFETCH(at[j] + 32));
  r2 = (sz) (PH3(d->sel[0], d->ty, 0x77) & (mrows - 1));
  PREFETCH(w2 + r2 * (ACM_NSEL + 2));
  Fi(ncx,
    slot * s = sl[i] = find(q, hc[i], p0, tbits);
    if (!s->n) nfresh++;
    xp[i] = acm_str[s->p >> 4];
    if (nin > 2) xq[i] = s->n < q->conf ? 0 : acm_str[s->q >> 4];
    smp[i] = q->sm + 2 * ((((sz) d->ty * ACM_NCX + (sz) i) * 256 + s->st) *
                          16 + (sz) pb);
    PREFETCH(smp[i]));
  Fj(nsel,
    r1[j] = (sz) j * mrows +
            (sz) (PH3(d->sel[j], d->ty, j * 7 + (nfresh * 4 > ncx * 3)) &
                  (mrows - 1));
    if (w16)
      for (i = 0; i < 3 + nin * ncx; i += 32)
        PREFETCH(q->h1 + r1[j] * NIN16 + i);
    else {
      for (i = 0; i < 3 + nin * ncx; i += 16)
        PREFETCH(q->w1 + r1[j] * NIN + i);
      PREFETCH(q->w1 + r1[j] * NIN + 2 + nin * ncx);
    });
  Fi(ncx,
    x[nx++] = acm_str[*smp[i] >> 4];
    x[nx++] = xp[i];
    if (nin > 2) x[nx++] = xq[i]);
  if (w16) {
    Fi(nx, x16[i] = (i16) x[i]);
    while (nx & 15) x16[nx++] = 0;
    Fj(nsel,
      dot[j] = clamp(row_dot16(q->h1 + r1[j] * NIN16, x16, nx) >> 14);
      ps[j] = SQ[dot[j] + 2047]);
  } else {
    if (nsel > 2) while (nx & 7) x[nx++] = 0;
    Fj(nsel,
      dot[j] = clamp(row_dot(q->w1 + r1[j] * NIN, x, nx) >> 16);
      ps[j] = SQ[dot[j] + 2047]);
  }
  { const i32 * w = w2 + r2 * (ACM_NSEL + 2);
    a = 0;
    Fj(nsel, a += (int64_t) w[j] * dot[j]);
    a += (int64_t) w[ACM_NSEL] * spr + (int64_t) w[ACM_NSEL + 1] * 64;
    s2 = clamp(a >> 16); }
  pm = SQ[s2 + 2047];
  Fj(3, pa[j] = apm(at[j], s2, lo + j, wa + j));
  p = (pm * (300 - 3 * q->apw) + (pa[0] + pa[1] + pa[2]) * q->apw + 150) / 300;
  { u32 pp = (u32) p * 16 + 8;
    if (pp < 8) pp = 8;
    if (pp > 65528) pp = 65528;
    bit = q->code(q->cctx, bit, 65536 - pp); }
  Fj(3, apm_upd(at[j], lo[j], wa[j], bit));
  err = (bit << 12) - pm;
  { i32 * w = w2 + r2 * (ACM_NSEL + 2);
    Fj(nsel, w[j] += (i32) (((int64_t) dot[j] * err * q->lr2) >> 20));
    w[ACM_NSEL] += (i32) (((int64_t) spr * err * q->lr2) >> 20);
    w[ACM_NSEL + 1] += (i32) (((int64_t) 64 * err * q->lr2) >> 20); }
  Fj(nsel,
    u32 c = q->w1n[r1[j]];
    int e = ((bit << 12) - ps[j]) * (q->lrmin + LR[c >> 4]);
    if (c < 65535) q->w1n[r1[j]] = c + 1;
    if (w16) {
      e >>= 7;
      row_train16(q->h1 + r1[j] * NIN16, x16, nx,
                  e > 32767 ? 32767 : e < -32767 ? -32767 : e);
    } else row_train(q->w1 + r1[j] * NIN, x, nx, e));
  Fi(ncx,
    slot * s = sl[i];
    u16 * nn = smp[i] + 1;
    *smp[i] = adapt(*smp[i], bit, REC[*nn]);
    if (*nn < SMLIM) (*nn)++;
    s->p = adapt(s->p, bit, REC[MIN(s->n, DLIM)]);
    s->q = adapt(s->q, bit, REC[MIN(s->n, q->flim)]);
    s->st = cm_nex[s->st][bit];
    if (s->n < 65535) s->n++);
  return bit;
}

static int apm_bit(acm * q, int bit, u32 p1, acm_h node) {
  acm_dec * d = &q->d;
  sz mr = mrows(q), ar = q->lite ? LAROWS : AROWS, r;
  int x[3], lo[3], wa[3], pa[3], j, pm, p, e;
  u16 * at[3];
  i32 * w;
  u32 c;
  if (p1 < 1) p1 = 1;
  if (p1 > 65535) p1 = 65535;
  x[0] = acm_str[p1 >> 4];  x[1] = 64;  x[2] = d->xon ? d->xin : 0;
  r = (sz) (PH3(d->sel[0], d->ty, 0x55) & (mr - 1));
  w = q->wa + r * 4;
  Fj(3, at[j] = q->a[j] + (sz) (PH3(d->apm[j], d->ty, node) & (ar - 1)) * 33);
  pm = clamp(((int64_t) w[0] * x[0] + (int64_t) w[1] * x[1] +
              (int64_t) w[2] * x[2]) >> 16);
  Fj(3, pa[j] = apm(at[j], pm, lo + j, wa + j));
  pm = SQ[pm + 2047];
  p = (pm * (300 - 3 * q->apw) + (pa[0] + pa[1] + pa[2]) * q->apw + 150) / 300;
  { u32 pp = (u32) p * 16 + 8;
    if (pp < 8) pp = 8;
    if (pp > 65528) pp = 65528;
    bit = q->code(q->cctx, bit, 65536 - pp); }
  Fj(3, apm_upd(at[j], lo[j], wa[j], bit));
  c = q->wan[r];
  e = ((bit << 12) - pm) * (q->lrmin + LR[c >> 4]);
  if (c < 65535) q->wan[r] = c + 1;
  Fj(3, w[j] += (i32) (((int64_t) x[j] * e) >> 20));
  return bit;
}

int acm_bit(acm * q, int bit, u32 p1, acm_h node) {
  switch (q->eng[q->d.ty]) {
  case ACM_EPRI:
    p1 = p1 < 8 ? 8 : p1 > 65528 ? 65528 : p1;
    return q->code(q->cctx, bit, 65536 - p1);
  case ACM_EAPM: return apm_bit(q, bit, p1, node);
  case ACM_ELITE:
    if (!q->lite)
      return q->w16
        ? bit_k(q, bit, p1, node, ACM_NLSEL, 2, TBITS, MROWS, AROWS, 1)
        : bit_k(q, bit, p1, node, ACM_NLSEL, 2, TBITS, MROWS, AROWS, 0);
  }
  if (q->sel8)
    return bit_k(q, bit, p1, node, ACM_NSEL, 2, LTBITS, LMROWS, LAROWS, 0);
  return q->lite
    ? bit_k(q, bit, p1, node, ACM_NLSEL, 2, LTBITS, LMROWS, LAROWS, 0)
    : q->w16 ? bit_k(q, bit, p1, node, ACM_NSEL, 3, TBITS, MROWS, AROWS, 1)
             : bit_k(q, bit, p1, node, ACM_NSEL, 3, TBITS, MROWS, AROWS, 0);
}

u32 acm_qtree(acm * q, u32 v, const uint64_t * pmf, u32 n, acm_h base,
              int dsel, int mid) {
  u32 lo = 0, hi = n - 1, dep = 0, i;
  acm_h path = 1;
  while (lo < hi && !pmf[lo]) lo++;
  while (hi > lo && !pmf[hi]) hi--;
  while (lo < hi) {
    uint64_t tot = 0, acc = 0, left = 0, best = ~(uint64_t) 0;
    u32 s = lo, p1;
    int bit, sh = 0;
    for (i = lo; i <= hi; i++) tot += pmf[i];
    if (mid) {
      s = lo + (hi - lo) / 2;
      for (i = lo; i <= s; i++) left += pmf[i];
    } else
      for (i = lo; i < hi; i++) {
        uint64_t dd;
        acc += pmf[i];
        dd = acc * 2 > tot ? acc * 2 - tot : tot - acc * 2;
        if (dd < best) { best = dd;  s = i;  left = acc; }
      }
    while ((tot >> sh) >= ((uint64_t) 1 << 40)) sh++;
    p1 = (u32) (((tot - left) >> sh) * 65536 / ((tot >> sh) ? (tot >> sh) : 1));
    if (dsel >= 0) q->d.sel[dsel] = PH2(base, MIN(dep, 7));
    q->d.apm[0] = PH2(base, path);
    bit = acm_bit(q, v > s, p1, PH2(base, path));
    if (bit) lo = s + 1; else hi = s;
    while (lo < hi && !pmf[lo]) lo++;
    while (hi > lo && !pmf[hi]) hi--;
    path = PH2(path, bit);  dep++;
  }
  return lo;
}

u32 acm_utree(acm * q, u32 v, u32 n, acm_h base) {
  u32 lo = 0, hi = n;
  acm_h path = 1;
  while (hi - lo > 1) {
    u32 mid = lo + (hi - lo) / 2;
    int bit;
    q->d.apm[0] = PH2(base, path);
    bit = acm_bit(q, v >= mid,
                  (u32) ((uint64_t) (hi - mid) * 65536 / (hi - lo)),
                  PH2(base, path));
    if (bit) lo = mid; else hi = mid;
    path = PH2(path, bit);
  }
  return lo;
}
