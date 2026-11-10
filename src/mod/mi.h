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


#ifndef BLR_MODI_H
#define BLR_MODI_H

#define _POSIX_C_SOURCE 199309L
#include "mod/mod.h"
#include "coder/acm.h"
#include "coder/rc.h"
#include <time.h>

enum {
  T_KIND, T_HDR, T_SNUM, T_NOTE, T_PITCH, T_PIDX, T_SMP, T_EFF, T_PAR, T_TAIL,
  T_PCM
};


#define NSYM (MOD_NSEMI + 1)
#define NOC  ((sz) -1)
enum { PE_NONE, PE_FAST, PE_CM };

typedef struct { u8 pe;  u32 opt; } mcfg;

enum { SE_NONE, SE_FAST, SE_LS };

typedef struct { u8 se;  u32 opt; } scfgt;

typedef struct mx {
  acm * q;
  acm_dec * d;
  rc_enc e;
  rc_dec r;
  int dec;
  const mcfg * cf;
  const scfgt * sc;
  int ty;
  u8 * h;
  const u8 * in;
  u8 * out;
  mod_lay L;
  u16 std[MOD_NSEMI];
  u16 aper[4096];
  u16 aoff[MOD_NSEMI + 1];
  u8 * nt, * sm, * ef, * pa, * tdup;
  sz mark[5];
  double cost[16], ccost[4];
  int cls;
  double tm[4];
} mx;

static INLINE double now(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec + t.tv_nsec * 1e-9;
}

static INLINE double nlog2(double v) {
  double y, t, sum = 0;
  int e = 0, k;
  while (v < 1) { v *= 2;  e--; }
  while (v >= 2) { v /= 2;  e++; }
  y = (v - 1) / (v + 1);  t = y;
  for (k = 1; k < 40; k += 2) { sum += t / k;  t *= y * y; }
  return e + 2 * sum / 0.69314718055994531;
}

#define MARK(m, k) ((m)->mark[k] = (m)->dec ? 0 : (m)->e.len + (m)->e.pending)
static INLINE int mx_bit(mx * m, int bit, u32 p0) {
#ifdef MOD_STATS
  if (!m->dec && m->ty >= T_NOTE && m->ty <= T_PAR)
    m->ccost[m->cls] -= nlog2((bit ? 65536.0 - p0 : (double) p0) / 65536);
  if (!m->dec) m->cost[m->ty] -= nlog2((bit ? 65536.0 - p0 : (double) p0) / 65536);
#endif
  if (m->dec) return rc_dec_bit_raw(&m->r, p0);
  rc_enc_bit_raw(&m->e, p0, bit);
  return bit;
}

static INLINE void use(mx * m, int ty, int ncx, acm_h s0, acm_h s1, acm_h a1,
                       acm_h a2) {
  acm_dec * d = m->d;
  m->ty = ty;
  d->ty = ty;  d->ncx = ncx;  d->xon = 0;
  d->sel[0] = s0;  d->sel[1] = s1;  d->apm[1] = a1;  d->apm[2] = a2;
}

static INLINE u32 val(mx * m, u32 v, u32 n, acm_h base) {
  return acm_utree(m->q, m->dec ? 0 : v, n, base);
}

static INLINE acm_h fld(const u8 * a, sz i) { return i == NOC ? 300 : a[i]; }

static INLINE u32 cell_per(const u8 * c) { return (u32) (c[0] & 15) << 8 | c[1]; }
static INLINE u32 ckey(const mx * m, sz i) {
  return (u32) m->nt[i] << 24 | (u32) m->sm[i] << 16 | (u32) m->ef[i] << 8 |
         m->pa[i];
}

static INLINE u32 chash(u32 a, u32 b, int bits) {
  return ((a * 0x9E3779B1u) ^ b) * 0x85EBCA6Bu >> (32 - bits);
}

static INLINE void cell_get(const u8 * c, u32 * per, u32 * sym, u32 * smp,
                            u32 * eff, u32 * par) {
  *per = cell_per(c);
  *sym = *per ? 1 + (u32) mod_semi(*per) : 0;
  *smp = (c[0] & 0xF0u) | c[2] >> 4;
  *eff = c[2] & 15u;  *par = c[3];
}

static INLINE void cell_put(mx * m, sz i, u32 per, u32 sym, u32 smp, u32 eff,
                            u32 par) {
  u8 * d = m->out ? m->out + m->L.pat + 4 * i : NULL;
  m->nt[i] = (u8) sym;  m->sm[i] = (u8) smp;
  m->ef[i] = (u8) eff;  m->pa[i] = (u8) par;
  if (!d) return;
  d[0] = (u8) ((smp & 0xF0) | per >> 8);  d[1] = (u8) per;
  d[2] = (u8) ((smp & 15) << 4 | eff);  d[3] = (u8) par;
}

static INLINE int pbit(mx * m, u32 * s, int lim, int bit) {
  bit = mx_bit(m, bit, rc_packed_prob(*s));
  *s = rc_adapt_packed(*s, lim, bit);
  return bit;
}

static INLINE u32 ptree(mx * m, u32 * t, u32 tn, int lim, u32 v, u32 n) {
  u32 lo = 0, hi = n, node = 1;
  while (hi - lo > 1) {
    u32 mid = lo + (hi - lo) / 2;
    int bit = pbit(m, t + (node & (tn - 1)), lim, v >= mid);
    if (bit) lo = mid; else hi = mid;
    node = node * 2 + (u32) bit;
  }
  return lo;
}

typedef struct { sz * mp, rowc;  u32 * mt, * ml;  int mb; } mtrk;
void mt_init(mtrk * t, sz ncell, sz rowc);
void mt_free(mtrk * t);
void mt_next(mtrk * t, const mx * m, int c, sz i, sz pr);
static INLINE sz mt_pred(const mtrk * t, int c, sz i) {
  return t->mp[c] < i ? t->mp[c] : NOC;
}

typedef struct { u32 * rt, rml;  sz rp, rowc;  int mb, rok; } rtrk;
void rt_init(rtrk * t, sz ncell, sz rowc);
void rt_row(rtrk * t, const mx * m, sz i);
static INLINE sz rt_pred(const rtrk * t, int c, sz i) {
  return t->rp != NOC && t->rp + (sz) c < i ? t->rp + (sz) c : NOC;
}
static INLINE void rt_next(rtrk * t, const mx * m, sz q, sz i) {
  if (q == NOC || ckey(m, q) != ckey(m, i)) t->rok = 0;
}

void pat_fast(mx * m);
void pat_cm(mx * m);

void * smp_fast_new(mx * m);
void smp_fast_one(mx * m, void * st, int si, const u8 * src, u8 * dst, sz n);
void smp_fast_free(void * st);
void * smp_ls_new(mx * m);
void smp_ls_one(mx * m, void * st, int si, const u8 * src, u8 * dst, sz n);
void smp_ls_free(void * st);


#endif
