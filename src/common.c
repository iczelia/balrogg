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

#include "common.h"
#include "file.h"

#include <errno.h>
#include <stdarg.h>
#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif

int blr_progress_enabled;
static const char * progress_path, * progress_phase;
static sz progress_total;
static int progress_last = -1, progress_live;

static void progress_draw(int percent) {
  char bar[21];
  int i;
  if (!blr_progress_enabled || !progress_live || percent == progress_last) return;
  /*  Line-oriented batch/log output needs only twenty checkpoints.  */
  if (blr_progress_enabled == 2 && percent < 100 && percent / 5 == progress_last / 5
      && progress_last >= 0) return;
  Fi(20, bar[i] = i < percent / 5 ? '#' : '-');
  bar[20] = 0;
  fprintf(stderr, "%sbalrogg: %s: %s [%s] %3d%%%s",
          blr_progress_enabled == 1 ? "\r" : "", progress_path, progress_phase,
          bar, percent, blr_progress_enabled == 2 || percent == 100 ? "\n" : "");
  fflush(stderr);
  progress_last = percent;
}

void blr_progress_cancel(void) {
  if (progress_live && blr_progress_enabled == 1) fputc('\n', stderr);
  progress_live = 0;
}

void blr_progress_begin(const char * path, const char * phase, sz total) {
  blr_progress_cancel();
  if (!blr_progress_enabled) return;
  progress_path = path;  progress_phase = phase;  progress_total = total;
  progress_last = -1;  progress_live = 1;
  progress_draw(0);
}

void blr_progress_update(sz done) {
  int percent;
  if (!blr_progress_enabled || !progress_live) return;
  /*  Floating-point division avoids overflowing size_t on 32-bit hosts.  */
  percent = !progress_total || done >= progress_total ? 99
              : (int) (100.0 * (double) done / (double) progress_total);
  progress_draw(percent);
}

void blr_progress_end(void) {
  progress_draw(100);
  progress_live = 0;
}

/*  Let fuzzers catch normal input rejection without treating it as a crash.  */
#ifdef BLR_FUZZ
jmp_buf blr_fuzz_jmp;
int blr_fuzz_armed;
#define BLR_BAIL  do { if (blr_fuzz_armed) longjmp(blr_fuzz_jmp, 1); } while (0)
#else
#define BLR_BAIL  do { } while (0)
#endif

static void message(const char * fmt, va_list ap) BLR_PRINTF(1, 0);

static void message(const char * fmt, va_list ap) {
  blr_progress_cancel();
  fputs("balrogg: ", stderr);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
}

static NORETURN void die(int code) {
  BLR_BAIL;
  exit(code);
}

NORETURN void blr_fatal(const char * fmt, ...) {
  va_list ap;
  va_start(ap, fmt);  message(fmt, ap);  va_end(ap);
  die(BLR_EXIT_REFUSED);
}

NORETURN void blr_fatal_code(int code, const char * fmt, ...) {
  va_list ap;
  va_start(ap, fmt);  message(fmt, ap);  va_end(ap);
  die(code);
}

void blr_fatal_unless(int cond, const char * fmt, ...) {
  va_list ap;
  if (cond) return;
  va_start(ap, fmt);  message(fmt, ap);  va_end(ap);
  die(BLR_EXIT_REFUSED);
}

/*  Set RLIMIT_AS where supported. Sanitizer builds remain unrestricted.  */

#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(memory_sanitizer) ||    \
    __has_feature(thread_sanitizer)
#define BLR_SANITIZED 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define BLR_SANITIZED 1
#endif

/*  Also detect a linked ASan runtime in mixed-instrumentation builds.  */
#if defined(__GNUC__) && defined(__ELF__) && !defined(BLR_NO_ATTRS)
extern void __asan_init(void) __attribute__((weak));
#define BLR_SAN_LIVE (__asan_init != 0)
#else
#define BLR_SAN_LIVE 0
#endif

static unsigned long memcap_mb;

void blr_memcap(void) {
  const char * e = getenv("BLR_MEMCAP");
  if (e && *e) {
    char * end;
    errno = 0;
    memcap_mb = strtoul(e, &end, 10);
    if (*e < '0' || *e > '9' || errno || end == e || *end)
      FATAL_CODE(BLR_EXIT_USAGE, "invalid BLR_MEMCAP value '%s'", e);
  } else memcap_mb = BLR_MEMCAP_MB;
#if defined(HAVE_SYS_RESOURCE_H) && defined(RLIMIT_AS) && !defined(BLR_SANITIZED)
  if (memcap_mb && !BLR_SAN_LIVE) {
    struct rlimit r;
    rlim_t want = (rlim_t) memcap_mb << 20;
    /*  Ignore values that overflow rlim_t.  */
    if ((unsigned long) (want >> 20) == memcap_mb &&
        !getrlimit(RLIMIT_AS, &r) &&
        (r.rlim_cur == RLIM_INFINITY || r.rlim_cur > want)) {
      r.rlim_cur = want;
      if (setrlimit(RLIMIT_AS, &r)) memcap_mb = 0;
    }
  }
#else
  memcap_mb = 0;
#endif
}

static void oom(sz n) {
  if (memcap_mb)
    FATAL("allocation of %lu bytes exceeds the %lu MiB cap",
          (unsigned long) n, memcap_mb);
  FATAL("out of memory (%lu bytes)", (unsigned long) n);
}

void * xmalloc(sz n) {
  void * p = malloc(n ? n : 1);
  if (UNLIKELY(p == NULL)) oom(n);
  return p;
}

/* Zero-initialized allocation. Callers must bound the requested capacity. */
void * xcalloc(sz n, sz size) {
  if (size && n > SIZE_MAX / size) oom(SIZE_MAX);
  void * p = calloc(n ? n : 1, size ? size : 1);
  if (UNLIKELY(p == NULL)) oom(n * size);
  return p;
}

void * xrealloc(void * p, sz n) {
  void * q = realloc(p, n ? n : 1);
  if (UNLIKELY(q == NULL)) oom(n);
  return q;
}

/*  Read to EOF so pipes work.  */
u8 * slurp(const char * path, sz * len) {
  FILE * f = fopen(path, "rb");
  sz cap = 1 << 16, n = 0, got;
  u8 * b;
  int bad;
  if (!f) FATAL_CODE(BLR_EXIT_IO, "cannot open %s", path);
  b = xmalloc(cap);
  for (;;) {
    if (n == cap) {
      sz add = MIN(cap, BLR_IO_CHUNK);
      if (cap > SIZE_MAX - add)
        FATAL_CODE(BLR_EXIT_IO, "%s is too large", path);
      cap += add;  b = xrealloc(b, cap);
    }
    got = fread(b + n, 1, MIN(cap - n, BLR_IO_CHUNK), f);
    if (!got) break;
    n += got;
  }
  bad = ferror(f);
  if (fclose(f)) bad = 1;
  if (bad) FATAL_CODE(BLR_EXIT_IO, "read error on %s", path);
  if (n < cap) b = xrealloc(b, n ? n : 1);
  *len = n;  return b;
}

void spew(const char * path, const u8 * data, sz len) {
  FILE * f = fopen(path, "wb");
  sz at = 0;
  int bad = 0;
  if (!f) FATAL_CODE(BLR_EXIT_IO, "cannot create %s", path);
  while (at < len) {
    sz n = MIN(len - at, BLR_IO_CHUNK);
    if (fwrite(data + at, 1, n, f) != n) { bad = 1;  break; }
    at += n;
  }
  if (fclose(f)) bad = 1;
  if (bad)
    FATAL_CODE(BLR_EXIT_IO, "write error on %s", path);
}
