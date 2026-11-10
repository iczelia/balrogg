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

#ifndef BLR_COMMON_H
#define BLR_COMMON_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t  u8;  typedef int8_t  i8;
typedef uint16_t u16;  typedef int16_t i16;
typedef uint32_t u32;  typedef int32_t i32;
typedef size_t   sz;

#if CHAR_BIT != 8 || SHRT_MAX != 32767 || INT_MAX != 2147483647
#error "balrogg requires 8-bit bytes, 16-bit short, and 32-bit int"
#endif
#if (-1 >> 1) != -1
#error "balrogg requires arithmetic signed right shift"
#endif
typedef char blr_signed_conversion_check[
  (i32) UINT32_MAX == -1 && (i16) UINT16_MAX == -1 ? 1 : -1];

#if defined(__GNUC__) && !defined(BLR_NO_ATTRS)
#define INLINE      __inline__ __attribute__((__always_inline__))
#define NOINLINE    __attribute__((__noinline__))
#define HOT         __attribute__((__hot__))
#define FLATTEN     __attribute__((__flatten__))
#define NORETURN    __attribute__((__noreturn__))
#define LIKELY(x)   __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#define PREFETCH(p) __builtin_prefetch(p)
#define BLR_PRINTF(f, a) __attribute__((__format__(__printf__, f, a)))
#else
#define INLINE
#define NOINLINE
#define HOT
#define FLATTEN
#define NORETURN
#define LIKELY(x)   (x)
#define UNLIKELY(x) (x)
#define PREFETCH(p) ((void) 0)
#define BLR_PRINTF(f, a)
#endif

static INLINE u32 blr_ilog(u32 v) {
#if defined(__GNUC__) && !defined(BLR_NO_ATTRS)
  return v ? 32 - (u32) __builtin_clz(v) : 0;
#else
  u32 n = 0;
  while (v) { n++;  v >>= 1; }
  return n;
#endif
}

static INLINE int64_t blr_fdiv(int64_t a, int64_t b) {
  return a >= 0 ? a / b : -((-a + b - 1) / b);
}

static INLINE u32 blr_ilog64(uint64_t v) {
  return v >> 32 ? 32 + blr_ilog((u32) (v >> 32)) : blr_ilog((u32) v);
}

static INLINE uint64_t blr_isqrt64(uint64_t v) {
  uint64_t r = 0, b = (uint64_t) 1 << 62;
  while (b > v) b >>= 2;
  for (; b; b >>= 2) {
    uint64_t t = r + b, k = (uint64_t) 0 - (uint64_t) (v >= t);
    v -= t & k;  r = (r >> 1) + (b & k);
  }
  return r;
}

static INLINE u32 rd16le(const u8 * p) { return p[0] | (u32) p[1] << 8; }
static INLINE u32 rd32le(const u8 * p) {
  return p[0] | (u32) p[1] << 8 | (u32) p[2] << 16 | (u32) p[3] << 24;
}
static INLINE uint64_t rd64le(const u8 * p) {
  return rd32le(p) | (uint64_t) rd32le(p + 4) << 32;
}
static INLINE u32 rd16be(const u8 * p) { return (u32) p[0] << 8 | p[1]; }
static INLINE u32 rd32be(const u8 * p) {
  return (u32) p[0] << 24 | (u32) p[1] << 16 | (u32) p[2] << 8 | p[3];
}
static INLINE void wr16le(u8 * p, u32 v) { p[0] = (u8) v;  p[1] = (u8) (v >> 8); }
static INLINE void wr16be(u8 * p, u32 v) { p[0] = (u8) (v >> 8);  p[1] = (u8) v; }
static INLINE void wr32le(u8 * p, u32 v) {
  p[0] = (u8) v;  p[1] = (u8) (v >> 8);  p[2] = (u8) (v >> 16);  p[3] = (u8) (v >> 24);
}
static INLINE void wr64le(u8 * p, uint64_t v) {
  wr32le(p, (u32) v);  wr32le(p + 4, (u32) (v >> 32));
}

#define BLR_NLEV 20

#define BLR_EXIT_OK        0
#define BLR_EXIT_REFUSED   1
#define BLR_EXIT_USAGE     2
#define BLR_EXIT_IO        3
#define BLR_EXIT_INTERNAL  4

#define FATAL         blr_fatal
#define FATAL_CODE    blr_fatal_code
#define FATAL_UNLESS  blr_fatal_unless

NORETURN void blr_fatal(const char * fmt, ...) BLR_PRINTF(1, 2);
NORETURN void blr_fatal_code(int code, const char * fmt, ...) BLR_PRINTF(2, 3);
void blr_fatal_unless(int cond, const char * fmt, ...) BLR_PRINTF(2, 3);

#define FATAL_IF_HOT(cond)  if (cond) FATAL

#define Fi(n, ...) for (i = 0; i < (n); i++) {  __VA_ARGS__; }
#define Fj(n, ...) for (j = 0; j < (n); j++) {  __VA_ARGS__; }
#define Fk(n, ...) for (k = 0; k < (n); k++) {  __VA_ARGS__; }

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))
#define ABS(x) ((x) < 0 ? -(x) : (x))
#define ARRAY_LEN(a) (sizeof (a) / sizeof *(a))

#define BLR_MEMCAP_MB 2048
void blr_memcap(void);

void * xmalloc(sz n);
void * xcalloc(sz n, sz size);
void * xrealloc(void * p, sz n);
void * xgrow(void * p, sz * cap, sz need, sz size, sz init);
void xfree(void * p);
#if !defined(BLR_ALLOW_FREE) && !defined(_WIN32)
void blr_free_is_xfree(void * p);
void * blr_realloc_is_xrealloc(void * p, sz n);
#define free(p) blr_free_is_xfree(p)
#define realloc(p, n) blr_realloc_is_xrealloc(p, n)
#endif
typedef struct ar_chunk ar_chunk;
typedef struct { ar_chunk * head;  u8 * at;  sz left, cap; } blr_arena;
void * ar_take(blr_arena * a, sz n);
void ar_free(blr_arena * a);
int blr_same(const void * a, const void * b, sz n);
u8 * slurp(const char * path, sz * len);
void spew(const char * path, const u8 * data, sz len);
int blr_fseek(FILE * f, int64_t at, int whence);
int64_t blr_ftell(FILE * f);

extern int blr_progress_enabled;
void blr_progress_begin(const char * path, const char * phase, sz total);
void blr_progress_update(sz done);
void blr_progress_end(void);
void blr_progress_cancel(void);

#endif
