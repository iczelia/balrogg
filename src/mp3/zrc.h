#ifndef MP3_ZRC_H
#define MP3_ZRC_H

#include "mp3/zbase.h"

#define RC_TOP 0x1000000u

typedef struct {
  bstream * bs;
  u8 decoding;
  u32 low;
  u32 code;
  u32 range;
  u32 ffnum;
  u32 carry;
  i32 cache;
  u32 nbytes;
} rcoder;

void rc_init_encoder(rcoder * rc, bstream * out);
void rc_init_decoder(rcoder * rc, bstream * in);
void rc_flush(rcoder * rc);
void rc_shift_low(rcoder * rc);

static INLINE int rc_bit(rcoder * rc, u32 p0, int bit) {
  u32 range = rc->range;
  u32 r0 = range < 0x10000000 ? p0 * (range >> 11) >> 4 : p0 * (range >> 15);
  if (!rc->decoding) {
    bit = bit != 0;
    if (bit) {
      u32 low = rc->low + r0;
      rc->carry += low < rc->low;  rc->low = low;
      range -= r0;
    } else {
      range = r0;
    }
    while (range < RC_TOP) {
      rc->nbytes++;  rc->range = range << 8;
      rc_shift_low(rc);
      range = rc->range;
    }
  } else {
    if (rc->code < r0) {
      bit = 0;  range = r0;
    } else {
      bit = 1;  rc->code -= r0;  range -= r0;
    }
    while (range < RC_TOP) {
      rc->nbytes++;  range <<= 8;
      rc->code = (rc->code << 8) + (u32) bs_getc(rc->bs);
    }
  }
  rc->range = range;
  return bit;
}

u32 rc_raw_bits(rcoder * rc, int nbits, u32 val);

typedef struct {
  u16 p, n;
} ctr;

void counters_init(ctr * c, sz count);
#define COUNTERS_INIT(a) counters_init((ctr *) &(a), sizeof (a) / sizeof (ctr))

typedef const u16 * rate_tab;
extern u16 rates_w20[100], rates_w28[128], rates_w40[200], rates_w45[200],
    rates_w64[300], rates_w100[460], rates_w120[600], rates_w150[800],
    rates_w175[950], rates_w200[1050], rates_w250[1400], rates_w350[2050],
    rates_w400[2400], rates_w512[3200], rates_w999[5000];

static INLINE void counter_update(ctr * c, int bit, rate_tab rates, u32 bias) {
  u32 n = (u32) c->n + 1, r;
  i32 t, p;
  if ((i32) rates[0] < (i32) n) n = rates[0];
  c->n = (u16) n;
  r = rates[(u16) n];
  t = (i32) (bias * r) >> 15;
  if (!bit) t = (i32) r - t;
  p = ((i32) ((0x8000u - r) * c->p + 0x7fffu) >> 15) + t;
  c->p = (u16) CLAMP(p, 1, 0x7fff);
}

static INLINE int code_bit(rcoder * rc, ctr * c, int bit, rate_tab rates,
                           u32 bias) {
  bit = rc_bit(rc, c->p, bit);
  counter_update(c, bit, rates, bias);
  return bit;
}

u32 code_tree(rcoder * rc, ctr * nodes, int nbits, u32 val, i32 lo, i32 hi,
              rate_tab rates, u32 bias);

static INLINE int tree_forced_bit(u32 prefix, int i, i32 lo, i32 hi) {
  u32 half = 1u << i;
  if ((i32) (prefix + half - 1) <= lo) return 1;
  if ((i32) (prefix + half) >= hi) return 0;
  return -1;
}

static INLINE u32 code_tree_lt(rcoder * rc, ctr * nodes, int nbits, u32 val,
                               i32 hi, rate_tab rates, u32 bias) {
  return code_tree(rc, nodes, nbits, val, -1, hi, rates, bias);
}

#define RUNBIT_MAX 31

typedef struct {
  ctr c[RUNBIT_MAX + 1];
  i32 run;
  i32 last;
} runbit;

void runbit_init(runbit * r);
int code_runbit(rcoder * rc, runbit * r, int bit, rate_tab rates, u32 bias);

#endif
