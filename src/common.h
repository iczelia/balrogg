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

#ifndef BLR_COMMON_H
#define BLR_COMMON_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t  u8;   typedef int8_t  i8;
typedef uint16_t u16;  typedef int16_t i16;
typedef uint32_t u32;  typedef int32_t i32;
typedef size_t   sz;

/*  Inlining hints for hot paths and builds without link-time optimization.  */
#if defined(__GNUC__) && !defined(BLR_NO_ATTRS)
#define INLINE      __inline__ __attribute__((__always_inline__))
#define HOT         __attribute__((__hot__))
#define FLATTEN     __attribute__((__flatten__))
#define NORETURN    __attribute__((__noreturn__))
#define LIKELY(x)   __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#define BLR_PRINTF(f, a) __attribute__((__format__(__printf__, f, a)))
#else
#define INLINE
#define HOT
#define FLATTEN
#define NORETURN
#define LIKELY(x)   (x)
#define UNLIKELY(x) (x)
#define BLR_PRINTF(f, a)
#endif

/*  The number of bits in v: ilog(0) = 0, ilog(1) = 1, ilog(7) = 3.  */
static INLINE u32 blr_ilog(u32 v) {
#if defined(__GNUC__) && !defined(BLR_NO_ATTRS)
  return v ? 32 - (u32) __builtin_clz(v) : 0;
#else
  u32 n = 0;
  while (v) { n++;  v >>= 1; }
  return n;
#endif
}

/*  Exit statuses distinguish refused input from access failures.  */
#define BLR_EXIT_OK        0  /*  Success.  */
#define BLR_EXIT_REFUSED   1  /*  Malformed, unsupported, or foreign input.  */
#define BLR_EXIT_USAGE     2  /*  Usage error.  */
#define BLR_EXIT_IO        3  /*  A file could not be opened, read or written.  */
#define BLR_EXIT_INTERNAL  4  /*  Internal error.  */

/*  These aliases keep call sites short without variadic macros.  */
#define FATAL         blr_fatal
#define FATAL_CODE    blr_fatal_code
#define FATAL_UNLESS  blr_fatal_unless

/*  Messages include the program name. FATAL uses BLR_EXIT_REFUSED.  */
NORETURN void blr_fatal(const char * fmt, ...) BLR_PRINTF(1, 2);
NORETURN void blr_fatal_code(int code, const char * fmt, ...) BLR_PRINTF(2, 3);
void blr_fatal_unless(int cond, const char * fmt, ...) BLR_PRINTF(2, 3);

#ifdef BLR_FUZZ
#include <setjmp.h>
extern jmp_buf blr_fuzz_jmp;
extern int blr_fuzz_armed;
#endif

/*  Avoid FATAL_UNLESS call overhead on per-bit hot paths.  */
#define FATAL_IF_HOT(cond)  if (cond) FATAL

/*  Loop macros require the caller to declare each induction variable.  */
#define Fi(n, ...)                                                            \
  for (i = 0; i < (n); i++) {                                                 \
    __VA_ARGS__;                                                              \
  }
#define Fj(n, ...)                                                            \
  for (j = 0; j < (n); j++) {                                                 \
    __VA_ARGS__;                                                              \
  }
#define Fk(n, ...)                                                            \
  for (k = 0; k < (n); k++) {                                                 \
    __VA_ARGS__;                                                              \
  }

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))


/*  Per-process memory cap. BLR_MEMCAP overrides it in MiB and 0 disables it.  */
#define BLR_MEMCAP_MB 2048
void blr_memcap(void);

/*  Allocation and whole-file I/O helpers.  */
void * xmalloc(sz n);
/*  Use lazy zero pages for large, partly used model tables.  */
void * xcalloc(sz n, sz size);
void * xrealloc(void * p, sz n);
u8 * slurp(const char * path, sz * len);
void spew(const char * path, const u8 * data, sz len);

#endif
