#include "mp3/zrc.h"

void bs_open(bstream * s, FILE * f, int writing) {
  s->f = f;  s->base = ftell(f);  s->err = 0;
  s->p = s->b;  s->e = writing ? s->b + BSTREAM_SIZE : s->b;
}

int bs_fill(bstream * s) {
  sz n;
  s->base += (long) (s->e - s->b);
  n = fread(s->b, 1, BSTREAM_SIZE, s->f);
  s->p = s->b;  s->e = s->b + n;
  return n ? *s->p++ : EOF;
}

int bs_flush(bstream * s) {
  sz n = (sz) (s->p - s->b);
  if (n && fwrite(s->b, 1, n, s->f) != n) s->err = 1;
  s->base += (long) n;  s->p = s->b;
  return s->err;
}

void rc_shift_low(rcoder * rc) {
  u32 low = rc->low;
  if (low < 0xff000000u || rc->carry != 0) {
    bs_putc(rc->bs, (u8) ((i32) rc->carry + rc->cache));
    for (; rc->ffnum; rc->ffnum--) bs_putc(rc->bs, (u8) ((i32) rc->carry - 1));
    rc->carry = 0;  rc->cache = (i32) low >> 24;
  } else {
    rc->ffnum++;
  }
  rc->low = low << 8;
}

void rc_init_encoder(rcoder * rc, bstream * out) {
  rc->bs = out;  rc->decoding = 0;
  rc->range = 0xffffffffu;
  rc->low = rc->ffnum = rc->carry = rc->nbytes = 0;
}

void rc_init_decoder(rcoder * rc, bstream * in) {
  int i;
  rc->bs = in;  rc->decoding = 1;
  rc->range = 0xffffffffu;
  rc->low = rc->ffnum = rc->carry = rc->nbytes = 0;
  Fi(5, rc->code = (rc->code << 8) + (u32) bs_getc(rc->bs));
}

void rc_flush(rcoder * rc) {
  int i;
  Fi(5, rc_shift_low(rc));
}

u32 rc_raw_bits(rcoder * rc, int nbits, u32 val) {
  u32 v = 0;
  for (int i = nbits - 1; i >= 0; i--)
    v |= (u32) rc_bit(rc, 0x4000, (val >> i) & 1) << i;
  return v;
}

void counters_init(ctr * c, sz count) {
  sz i;
  Fi(count, c[i].p = 0x4000;  c[i].n = 1);
}

u32 code_tree(rcoder * rc, ctr * nodes, int nbits, u32 val, i32 lo, i32 hi,
              rate_tab rates, u32 bias) {
  u32 v = 0;
  for (int i = nbits - 1; i >= 0; i--) {
    int bit = tree_forced_bit(v, i, lo, hi);
    if (bit < 0)
      bit = code_bit(rc, &nodes[(i32) (v + (1u << nbits)) >> (i + 1)],
                     ((i32) val >> i) & 1, rates, bias);
    v |= (u32) bit << i;
  }
  return v;
}

void runbit_init(runbit * r) { counters_init(r->c, ARRAY_LEN(r->c)); }

int code_runbit(rcoder * rc, runbit * r, int bit, rate_tab rates, u32 bias) {
  bit = code_bit(rc, &r->c[r->run], bit, rates, bias);
  if (r->run < RUNBIT_MAX) r->run++;
  if (r->run != 1 && bit != r->last) r->run = 0;
  r->last = bit;
  return bit;
}
