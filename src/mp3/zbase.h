#ifndef MP3_ZBASE_H
#define MP3_ZBASE_H

#include "sys/common.h"
#include "mp3/mp3.h"

typedef uint64_t u64;
typedef int64_t i64;

#if (-1 >> 1) != -1
#error "the z engine requires arithmetic signed right shift"
#endif

static inline int write_le32(FILE * f, u32 v) {
  u8 b[4];  wr32le(b, v);
  return fwrite(b, 1, 4, f) == 4;
}

static inline int read_le32(FILE * f, u32 * v) {
  u8 b[4];
  if (fread(b, 1, 4, f) != 4) return 0;
  *v = rd32le(b);  return 1;
}

#define BSTREAM_SIZE 0x10000

typedef struct {
  FILE * f;
  u8 * p, * e;
  long base;
  int err;
  u8 b[BSTREAM_SIZE];
} bstream;

void bs_open(bstream * s, FILE * f, int writing);
int bs_fill(bstream * s);
int bs_flush(bstream * s);

static INLINE int bs_getc(bstream * s) {
  return s->p < s->e ? *s->p++ : bs_fill(s);
}

static INLINE void bs_putc(bstream * s, int c) {
  if (s->p == s->e) bs_flush(s);
  *s->p++ = (u8) c;
}

static inline long bs_tell(const bstream * s) {
  return s->base + (long) (s->p - s->b);
}

#endif
