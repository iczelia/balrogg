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

#include "model.h"

void mdl_adapt(void) { rc_adapt_init(); }

/*  Lay out four context tables consecutively. The zero table begins at 0.  */
void mdl_init(model * m, const mdl_cfg * c) {
  u32 d = (u32) 1 << c->depth;
  m->c = *c;
  m->wlen = d;                       /*  one tree per previous bit-length  */
  m->wmant = 2UL * c->freeze;        /*  capped mantissa index width  */
  m->bsg = 4;
  m->blen = m->bsg + 4;
  m->bmant = m->blen + (d + 1) * m->wlen;
  m->n = m->bmant + (c->bank ? d : 1) * m->wmant;
  m->p = xmalloc(m->n * sizeof *m->p);
  m->cn = xcalloc(m->n, 1);
  rc_probs_init(m->p, m->n);  mdl_reset(m);
}

/*  One slot of the block.  */
static INLINE void mbe(model * m, rc_enc * e, u32 s, int b) {
  rc_enc_bit_ad(e, m->p + s, m->cn + s, RC_ALIM, b);
}

static INLINE int mbd(model * m, rc_dec * d, u32 s) {
  return rc_dec_bit_ad(d, m->p + s, m->cn + s, RC_ALIM);
}

void mdl_free(model * m) { free(m->p);  m->p = NULL;  free(m->cn);  m->cn = NULL; }

void mdl_reset(model * m) { m->h0 = m->h1 = m->hl = 0;  m->m0 = m->m1 = 0; }

/*  Shared shape.
    Encoding and decoding share bank selection and history updates.  */

static u32 nzslot(model * m) { return m->h0; }
static u32 sgslot(model * m) { return m->bsg + m->h1; }

static void nzstep(model * m, int nz) {
  m->h0 = (u8) ((nz + m->h0 * 2) & 3);
  /*  A zero clears the length history.  */
  if (!nz) m->hl = 0;
}

static void sgstep(model * m, int neg) {
  m->h1 = (u8) (m->c.shist == 1 ? neg : ((neg + m->h1 * 2) & 3));
}

static u32 lenbase(model * m) { return m->blen + m->hl * m->wlen; }
static u32 mntbase(model * m, u32 n) {
  return m->bmant + (m->c.bank ? n * m->wmant : 0);
}

static u32 bitlen(u32 v) { return blr_ilog(v) - 1; }

/*  First order codes v - m0. Second order also subtracts m1. Unsigned
    arithmetic preserves required wraparound.  */

static u32 fwd(model * m, u32 v) {
  u32 r, t;
  if (!m->c.order) return v;
  r = v - m->m0;  m->m0 = v;
  if (m->c.order > 1) { t = r - m->m1;  m->m1 = r;  r = t; }
  return r;
}

static u32 inv(model * m, u32 r) {
  if (!m->c.order) return r;
  if (m->c.order > 1) { r += m->m1;  m->m1 = r; }
  m->m0 += r;  return m->m0;
}


void mdl_enc(model * m, rc_enc * e, u32 v) {
  u32 r = fwd(m, v), base, idx, n;
  int i, neg = 0, nz;
  if (m->c.sgn && (r & 0x80000000UL)) { neg = 1;  r = 0 - r; }
  nz = r != 0;
  mbe(m, e, nzslot(m), nz);  nzstep(m, nz);
  if (!nz) return;
  if (m->c.sgn) { mbe(m, e, sgslot(m), neg);  sgstep(m, neg); }
  n = bitlen(r);
  FATAL_IF_HOT(n >= (u32) (1 << m->c.depth))
    ("value %lu exceeds a depth-%d length tree", (unsigned long) r, m->c.depth);
  base = lenbase(m);  idx = 1;
  for (i = m->c.depth - 1; i >= 0; i--) {
    int b = (int) ((((1UL << m->c.depth) | n) >> i) & 1);
    mbe(m, e, base + idx, b);  idx = idx * 2 + (u32) b;
  }
  m->hl = (u8) (n + 1);
  base = mntbase(m, n);  idx = 1;
  for (i = (int) n - 1; i >= 0; i--) {
    int b = (int) ((r >> i) & 1);
    mbe(m, e, base + idx, b);
    if (idx < m->c.freeze) idx = (idx * 2 | (u32) b) & (2 * m->c.freeze - 1);
  }
}


u32 mdl_dec(model * m, rc_dec * d) {
  u32 base, idx, n, r;
  int i, neg = 0, nz;
  nz = mbd(m, d, nzslot(m));  nzstep(m, nz);
  if (!nz) return inv(m, 0);
  if (m->c.sgn) { neg = mbd(m, d, sgslot(m));  sgstep(m, neg); }
  base = lenbase(m);  idx = 1;
  for (i = m->c.depth; i > 0; i--) idx = idx * 2 + (u32) mbd(m, d, base + idx);
  n = idx - (1UL << m->c.depth);
  m->hl = (u8) (n + 1);
  base = mntbase(m, n);  idx = 1;  r = 1;
  for (i = (int) n - 1; i >= 0; i--) {
    int b = mbd(m, d, base + idx);
    r = r * 2 + (u32) b;
    if (idx < m->c.freeze) idx = (idx * 2 | (u32) b) & (2 * m->c.freeze - 1);
  }
  return inv(m, neg ? 0 - r : r);
}
