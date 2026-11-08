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

#ifndef BLR_RC_H
#define BLR_RC_H

#include "sys/common.h"
#include "sys/file.h"

#define RC_TOP   0x1000000UL
#define RC_PINIT 0x8000

typedef void (*rc_hook)(void * ctx, u32 prob, int bit);

typedef struct {
  u32 range, code;
  const u8 * in;
  sz pos, len;
  blr_file * file;
  u8 * window;
  sz off, base, avail;
} rc_dec;

typedef struct {
  u32 low, range;
  int carry;
  u8 cache;
  sz pending;
  u8 * buf;
  sz len, cap;
  blr_file * file;
} rc_enc;

void rc_dec_init(rc_dec * d, const u8 * buf, sz len);
void rc_dec_file(rc_dec * d, blr_file * f, sz off, sz len);
void rc_dec_free(rc_dec * d);
void rc_dec_refill(rc_dec * d);

#define RC_SPENT_SLACK 64
static INLINE int rc_dec_spent(const rc_dec * d) {
  return d->pos > d->len + RC_SPENT_SLACK;
}

void rc_enc_init(rc_enc * e);
void rc_enc_file(rc_enc * e, blr_file * output);
void rc_enc_free(rc_enc * e);
sz rc_enc_finish(rc_enc * e);
u8 * rc_enc_data(rc_enc * e);

void rc_probs_init(u16 * p, sz n);

#define RC_CNTMAX 255
#define RC_ALIM   120

extern u16 rc_divt[RC_CNTMAX + 1];
void rc_adapt_init(void);

#ifdef BLR_PROFILE
void rc_hook_set(rc_hook h, void * ctx);
#endif

#ifdef BLR_PROFILE
extern rc_hook rc_hook_fn;
extern void * rc_hook_ctx;

#define REPORT(p, b)  if (rc_hook_fn) rc_hook_fn(rc_hook_ctx, (p), (b))
#else
#define REPORT(p, b)  ((void) 0)
#endif

static INLINE void rc_put(rc_enc * e, u8 b) {
  if (e->file) {
    if (e->len) bf_put(e->file, b);
    else FATAL_IF_HOT(b != 0)("range coder leading carry byte");
    e->len++;  return;
  }
  if (e->len == e->cap) {
    FATAL_IF_HOT(e->cap > SIZE_MAX / 2)("range coder output is too large");
    e->cap *= 2;  e->buf = xrealloc(e->buf, e->cap);
  }
  e->buf[e->len++] = b;
}

NOINLINE void rc_shift(rc_enc * e);

static INLINE void rc_norm(rc_enc * e) {
  while (e->range < RC_TOP) { rc_shift(e);  e->range <<= 8; }
}

static INLINE u8 rc_get(rc_dec * d) {
  if (d->pos >= d->len) { d->pos++;  return 0; }
  if (d->pos - d->base >= d->avail) rc_dec_refill(d);
  return d->in[d->pos++ - d->base];
}

static INLINE void rc_dec_norm(rc_dec * d) {
  while (d->range < RC_TOP) {
    d->code = d->code << 8 | rc_get(d);
    d->range <<= 8;
  }
}

static INLINE void rc_addlow(rc_enc * e, u32 split) {
  u32 lo = e->low + split;
  if (lo < e->low) {
    FATAL_IF_HOT(e->carry)("range coder: two carries pending");
    e->carry = 1;
  }
  e->low = lo;
}

static INLINE void rc_enc_bit_raw(rc_enc * e, u32 prob, int bit) {
  u32 split = (e->range >> 16) * prob;
  if (bit) { rc_addlow(e, split);  e->range -= split; }
  else e->range = split;
  rc_norm(e);
}

static INLINE u16 rc_adapt_fixed(u16 v, int bit) {
  return (u16) (bit ? (u32) v - (v >> 6)
                    : (u32) v + ((0xFFFFU - v) >> 6));
}

static INLINE void rc_enc_bit(rc_enc * e, u16 * p, int bit) {
  u32 v = *p, split = (e->range >> 16) * v;
  REPORT(v, bit);
  if (bit) {
    rc_addlow(e, split);  e->range -= split;
  } else {
    e->range = split;
  }
  *p = rc_adapt_fixed((u16) v, bit);
  rc_norm(e);
}

static INLINE int rc_dec_bit(rc_dec * d, u16 * p) {
  u32 split, v = *p;
  int bit;
  rc_dec_norm(d);
  split = (d->range >> 16) * v;
  if (d->code < split) {
    d->range = split;  bit = 0;
  } else {
    d->code -= split;  d->range -= split;  bit = 1;
  }
  *p = rc_adapt_fixed((u16) v, bit);
  REPORT(v, bit);
  return bit;
}

static INLINE u16 rc_adapt_prob(u32 v, u32 rate, int bit) {
  return (u16) (bit ? v - (v * rate >> 16)
                    : v + ((0xFFFF - v) * rate >> 16));
}

static INLINE u16 rc_adapt(u16 v, u8 * c, int lim, int bit) {
  u32 count = *c, rate = rc_divt[count];
  if ((int) count < lim) *c = (u8) (count + 1);
  return rc_adapt_prob(v, rate, bit);
}

static INLINE u32 rc_packed_prob(u32 state) {
  return (state & 0xFFFF) ^ RC_PINIT;
}

static INLINE u32 rc_adapt_packed(u32 state, int lim, int bit) {
  u32 count = state >> 16, rate = rc_divt[count];
  u32 p = rc_adapt_prob(rc_packed_prob(state), rate, bit);
  return (p ^ RC_PINIT) | (count + ((int) count < lim)) << 16;
}

static INLINE void rc_enc_bit_packed(rc_enc * restrict e, u32 * restrict p,
                                    int lim, int bit) {
  u32 state = *p, v = rc_packed_prob(state), split = (e->range >> 16) * v;
  REPORT(v, bit);
  if (bit) { rc_addlow(e, split);  e->range -= split; }
  else e->range = split;
  *p = rc_adapt_packed(state, lim, bit);
  rc_norm(e);
}

static INLINE int rc_dec_bit_packed(rc_dec * restrict d, u32 * restrict p,
                                   int lim) {
  u32 state = *p, v = rc_packed_prob(state), split;
  int bit;
  rc_dec_norm(d);
  split = (d->range >> 16) * v;
  if (d->code < split) { d->range = split;  bit = 0; }
  else { d->code -= split;  d->range -= split;  bit = 1; }
  *p = rc_adapt_packed(state, lim, bit);
  REPORT(v, bit);
  return bit;
}

static INLINE void rc_enc_bit_ad(rc_enc * e, u16 * p, u8 * c, int lim,
                                 int bit) {
  u32 v = *p, split = (e->range >> 16) * v;
  REPORT(v, bit);
  if (bit) { rc_addlow(e, split);  e->range -= split; }
  else e->range = split;
  *p = rc_adapt((u16) v, c, lim, bit);
  rc_norm(e);
}

static INLINE int rc_dec_bit_ad(rc_dec * d, u16 * p, u8 * c, int lim) {
  u32 split, v = *p;
  int bit;
  rc_dec_norm(d);
  split = (d->range >> 16) * v;
  if (d->code < split) { d->range = split;  bit = 0; }
  else { d->code -= split;  d->range -= split;  bit = 1; }
  *p = rc_adapt((u16) v, c, lim, bit);
  REPORT(v, bit);
  return bit;
}

static INLINE int rc_dec_bit_ready(rc_dec * d, u32 prob) {
  u32 split;
  split = (d->range >> 16) * prob;
  if (d->code < split) { d->range = split;  return 0; }
  d->code -= split;  d->range -= split;
  return 1;
}

static INLINE int rc_dec_bit_raw(rc_dec * d, u32 prob) {
  rc_dec_norm(d);
  return rc_dec_bit_ready(d, prob);
}

static INLINE void rc_enc_bits(rc_enc * e, uint64_t v, int n) {
  while (n-- > 0) rc_enc_bit_raw(e, RC_PINIT, (int) (v >> n & 1));
}

static INLINE uint64_t rc_dec_bits(rc_dec * d, int n) {
  uint64_t v = 0;
  while (n-- > 0) v = v << 1 | (uint64_t) rc_dec_bit_raw(d, RC_PINIT);
  return v;
}

#endif
