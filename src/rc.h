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

#ifndef BLR_RC_H
#define BLR_RC_H

#include "common.h"
#include "file.h"

/*  Binary range coder with 16-bit probabilities and a fixed 1/64 rate.  */

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
  int carry;            /*  bit 32 of `low`, pending until the next shift.  */
  u8 cache;             /*  byte held back in case a carry reaches it.  */
  sz pending;           /*  cache + (pending - 1) copies of 0xFF are owed.  */
  u8 * buf;
  sz len, cap;
  blr_file * file;
} rc_enc;

void rc_dec_init(rc_dec * d, const u8 * buf, sz len);
void rc_dec_file(rc_dec * d, blr_file * f, sz off, sz len);
void rc_dec_free(rc_dec * d);
void rc_dec_refill(rc_dec * d);

/*  Detect decoding beyond the zero-filled lookahead and safety margin.  */
#define RC_SPENT_SLACK 64
static INLINE int rc_dec_spent(const rc_dec * d) {
  return d->pos > d->len + RC_SPENT_SLACK;
}

void rc_enc_init(rc_enc * e);
void rc_enc_file(rc_enc * e, blr_file * output);
void rc_enc_free(rc_enc * e);
/*  Finalise the stream; returns its length.  The bytes are rc_enc_data(e).  */
sz rc_enc_finish(rc_enc * e);
u8 * rc_enc_data(rc_enc * e);

void rc_probs_init(u16 * p, sz n);

/*  Adapt by 1/(c + 3) using a capped observation count.  */

#define RC_CNTMAX 255
#define RC_ALIM   120

extern u16 rc_divt[RC_CNTMAX + 1];
void rc_adapt_init(void);

/*  Optional profiling hook for modeled slots. Raw operations do not report.  */
void rc_hook_set(rc_hook h, void * ctx);

/*  Per-bit operations.
    Header definitions allow inlining without LTO.  */
extern rc_hook rc_hook_fn;
extern void * rc_hook_ctx;

#define REPORT(p, b)  if (rc_hook_fn) rc_hook_fn(rc_hook_ctx, (p), (b))

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

static INLINE void rc_shift(rc_enc * e) {
  if (e->low < 0xFF000000UL || e->carry) {
    rc_put(e, (u8) (e->cache + e->carry));
    while (--e->pending) rc_put(e, (u8) (0xFF + e->carry));
    e->cache = (u8) (e->low >> 24);
    e->pending = 0;  e->carry = 0;
  }
  e->pending++;
  e->low <<= 8;
}

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

static INLINE u16 rc_adapt(u16 v, u8 * c, int lim, int bit) {
  u32 count = *c, d = rc_divt[count];
  i32 nv;
  if ((int) count < lim) *c = (u8) (count + 1);
  nv = bit ? (i32) v - (i32) ((u32) v * d >> 16)
           : (i32) v + (i32) ((u32) (0xFFFF - v) * d >> 16);
  return (u16) nv;
}

static INLINE void rc_enc_bit_ad(rc_enc * e, u16 * p, u8 * c, int lim, int bit) {
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

static INLINE int rc_dec_bit_raw(rc_dec * d, u32 prob) {
  u32 split;
  rc_dec_norm(d);
  split = (d->range >> 16) * prob;
  if (d->code < split) { d->range = split;  return 0; }
  d->code -= split;  d->range -= split;
  return 1;
}

#endif
