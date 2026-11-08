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

#define BLR_ALLOW_FREE 1
#include "sys/common.h"
#include "sys/file.h"

#include <errno.h>
#include <stdarg.h>
#if !defined(BLR_WIN32) && !defined(BLR_DOS)
#include <sys/types.h>
#endif
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
  if (!blr_progress_enabled || !progress_live || percent == progress_last)
    return;
  if (blr_progress_enabled == 2 && percent < 100 &&
      percent / 5 == progress_last / 5 && progress_last >= 0) return;
  Fi(20, bar[i] = i < percent / 5 ? '#' : '-');
  bar[20] = 0;
  fprintf(stderr, "%sbalrogg: %s: %s [%s] %3d%%%s",
          blr_progress_enabled == 1 ? "\r" : "", progress_path, progress_phase,
          bar, percent,
          blr_progress_enabled == 2 || percent == 100 ? "\n" : "");
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
  percent = !progress_total || done >= progress_total ? 99
          : (int) (progress_total <= (sz) -1 / 100
                     ? done * 100 / progress_total
                     : done / (progress_total / 100));
  if (percent > 99) percent = 99;
  progress_draw(percent);
}

void blr_progress_end(void) {
  progress_draw(100);
  progress_live = 0;
}

static void message(const char * fmt, va_list ap) BLR_PRINTF(1, 0);

static void message(const char * fmt, va_list ap) {
  blr_progress_cancel();
  fputs("balrogg: ", stderr);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
}

static NORETURN void die(int code) {
  bf_doomed_remove();
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

#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(memory_sanitizer) ||    \
    __has_feature(thread_sanitizer)
#define BLR_SANITIZED 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define BLR_SANITIZED 1
#endif

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
#if defined(HAVE_SYS_RESOURCE_H) && defined(RLIMIT_AS) && \
    !defined(BLR_SANITIZED)
  if (memcap_mb && !BLR_SAN_LIVE) {
    struct rlimit r;
    rlim_t want = (rlim_t) memcap_mb << 20;
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

#define X_RGMIN ((sz) 1 << 10)
#define X_RGGROW ((sz) 256 << 10)
#define X_RG(n) ((n) >= X_RGMIN && (n) <= RG_MAX && !blr_no_mmap)

void * xmalloc(sz n) {
  void * p = X_RG(n) ? rg_take(n, 0) : NULL;
  if (!p) p = malloc(n ? n : 1);
  if (UNLIKELY(p == NULL)) oom(n);
  return p;
}

void * xcalloc(sz n, sz size) {
  void * p;
  if (size && n > SIZE_MAX / size) oom(SIZE_MAX);
  p = X_RG(n * size) ? rg_take(n * size, 1) : NULL;
  if (!p) p = calloc(n ? n : 1, size ? size : 1);
  if (UNLIKELY(p == NULL)) oom(n * size);
  return p;
}

void * xrealloc(void * p, sz n) {
  void * q;
  if (!p) return xmalloc(n);
  if (rg_owns(p)) {
    sz have = rg_size(p);
    if (rg_grow(p, n)) return p;
    q = n > X_RGGROW ? malloc(n) : xmalloc(n);
    if (UNLIKELY(q == NULL)) oom(n);
    memcpy(q, p, MIN(have, n));  rg_give(p);
    return q;
  }
  q = realloc(p, n ? n : 1);
  if (UNLIKELY(q == NULL)) oom(n);
  return q;
}

void * xgrow(void * p, sz * cap, sz need, sz size, sz init) {
  sz c = *cap ? *cap : init;
  if (need <= *cap) return p;
  while (c < need) {
    if (c > SIZE_MAX / 2 / size) oom(SIZE_MAX);
    c *= 2;
  }
  *cap = c;
  return xrealloc(p, c * size);
}

void xfree(void * p) {
  if (p && rg_owns(p)) rg_give(p);
  else free(p);
}

struct ar_chunk { ar_chunk * next; };

#define AR_ALIGN ((sz) 16)
#define AR_CHUNK0 ((sz) 64 << 10)
#define AR_CHUNK1 ((sz) 1 << 20)

void * ar_take(blr_arena * a, sz n) {
  ar_chunk * c;
  u8 * p;
  if (n > SIZE_MAX - 2 * AR_ALIGN) oom(n);
  n = n ? (n + AR_ALIGN - 1) & ~(AR_ALIGN - 1) : AR_ALIGN;
  if (n <= a->left) { p = a->at;  a->at += n;  a->left -= n;  return p; }
  if (!a->cap) a->cap = AR_CHUNK0;
  if (n > a->cap / 4) {
    c = xmalloc(AR_ALIGN + n);
    if (a->head) { c->next = a->head->next;  a->head->next = c; }
    else { c->next = NULL;  a->head = c; }
    return (u8 *) c + AR_ALIGN;
  }
  c = xmalloc(a->cap);
  c->next = a->head;  a->head = c;
  p = (u8 *) c + AR_ALIGN;
  a->at = p + n;  a->left = a->cap - AR_ALIGN - n;
  if (a->cap < AR_CHUNK1) a->cap *= 2;
  return p;
}

void ar_free(blr_arena * a) {
  while (a->head) { ar_chunk * c = a->head;  a->head = c->next;  xfree(c); }
  a->at = NULL;  a->left = a->cap = 0;
}

int blr_same(const void * a, const void * b, sz n) {
  const u8 * p = a, * q = b;
  uint64_t x[4], y[4], d = 0;
  for (; n >= sizeof x; n -= sizeof x, p += sizeof x, q += sizeof x) {
    memcpy(x, p, sizeof x);  memcpy(y, q, sizeof y);
    if ((x[0] ^ y[0]) | (x[1] ^ y[1]) | (x[2] ^ y[2]) | (x[3] ^ y[3])) return 0;
  }
  for (; n; n--) d |= (uint64_t) (*p++ ^ *q++);
  return !d;
}

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

#if defined(BLR_WIN32)
int blr_fseek(FILE * f, int64_t at, int whence) {
  return _fseeki64(f, at, whence);
}
int64_t blr_ftell(FILE * f) { return _ftelli64(f); }
#elif defined(BLR_DOS)
int blr_fseek(FILE * f, int64_t at, int whence) {
  if (at != (long) at) { errno = EINVAL;  return -1; }
  return fseek(f, (long) at, whence);
}
int64_t blr_ftell(FILE * f) { return ftell(f); }
#else
int blr_fseek(FILE * f, int64_t at, int whence) {
  if (at != (off_t) at) { errno = EINVAL;  return -1; }
  return fseeko(f, (off_t) at, whence);
}
int64_t blr_ftell(FILE * f) { return ftello(f); }
#endif
