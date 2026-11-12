#ifndef MP3_ZLAYER_H
#define MP3_ZLAYER_H

#include "mp3/zhdr.h"

#define BITBUF_HALF   (BITBUF_SIZE / 2)

static inline u8 * br_base(bitreader * in) { return br_buf(in) + in->top; }

static INLINE u32 br_peek(bitreader * in, i32 pos) {
  return rd32le(br_base(in) + (pos >> 3)) >> (pos & 7);
}

static INLINE u32 br_read(bitreader * in, int n) {
  in->bitpos -= n;
  return br_peek(in, in->bitpos) & ((1u << n) - 1);
}

static INLINE void bw_put(bitwriter * out, int n, u32 v) {
  out->bitpos -= n;
  u8 * p = out->buf + (out->bitpos >> 3);
  wr32le(p, rd32le(p) | v << (out->bitpos & 7));
}

static inline i32 mp3_bitpos(model * m) {
  return m->reading ? m->in.bitpos : m->out.bitpos;
}

static inline i32 code_field(model * m, int n, i32 * f) {
  if (m->reading) *f = (i32) br_read(&m->in, n);
  else bw_put(&m->out, n, (u32) *f);
  return *f;
}

static inline void code_scalefac(model * m, int n, u16 * f) {
  if (m->reading) *f = (u16) br_read(&m->in, n);
  else bw_put(&m->out, n, *f);
}

u32 mp3_crc(u32 h, const u8 * side, int side_len);
int mp3_code_side_info(model * m);

u32 mp3_code_main_data(model * m);

#endif
