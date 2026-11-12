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

#include "sys/map.h"
#if !defined(BLR_NO_MMAP)
#ifdef BLR_WIN32
#include "sys/win32.h"
#elif defined(HAVE_SYS_MMAN_H) && defined(HAVE_MMAP) && \
      defined(HAVE_MUNMAP) && defined(HAVE_MSYNC)
#include <sys/mman.h>
#include <unistd.h>
#define BLR_POSIX_MAP 1
#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#define MAP_ANONYMOUS MAP_ANON
#endif
#endif
#endif

int blr_no_mmap;

int bm_file(blr_map * m, void * handle, sz at, sz n, int write) {
  m->p = NULL;  m->len = n;  m->heap = 0;
  if (!n || blr_no_mmap) return 0;
#if defined(BLR_WIN32) && !defined(BLR_NO_MMAP)
  { uint64_t size = handle ? 0 : n, off = at;
    HANDLE h = CreateFileMappingA(handle ? handle : INVALID_HANDLE_VALUE, NULL,
                                  write ? PAGE_READWRITE : PAGE_READONLY,
                                  (DWORD) (size >> 32), (DWORD) size, NULL);
    if (!h) return 0;
    m->p = MapViewOfFile(h, write ? FILE_MAP_WRITE : FILE_MAP_READ,
                        (DWORD) (off >> 32), (DWORD) off, n);
    CloseHandle(h);
  }
#elif defined(BLR_POSIX_MAP)
  { int flags = write ? MAP_SHARED : MAP_PRIVATE, fd = -1;
    if (handle) fd = fileno((FILE *) handle);
#ifdef MAP_ANONYMOUS
    else flags = MAP_PRIVATE | MAP_ANONYMOUS;
#else
    else return 0;
#endif
    off_t o = (off_t) at;
    if (o < 0 || (uintmax_t) o != at) return 0;
    m->p = mmap(NULL, n, PROT_READ | (write ? PROT_WRITE : 0), flags, fd, o);
    if (m->p == MAP_FAILED) m->p = NULL;
  }
#endif
  return m->p != NULL;
}

void bm_alloc(blr_map * m, sz n) {
  if (bm_file(m, NULL, 0, n, 1)) return;
  m->p = xcalloc(n, 1);  m->heap = 1;
}

#define BM_HUGE ((sz) 2 << 20)

void bm_alloc_huge(blr_map * m, sz n) {
#if defined(BLR_POSIX_MAP) && defined(MAP_ANONYMOUS)
  sz full = (n + BM_HUGE - 1) & ~(BM_HUGE - 1);
  u8 * p, * q;
  if (blr_no_mmap || full < n || full > SIZE_MAX - BM_HUGE) {
    bm_alloc(m, n);  return;
  }
  p = mmap(NULL, full + BM_HUGE, PROT_READ | PROT_WRITE,
           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (p == MAP_FAILED) { bm_alloc(m, n);  return; }
  q = (u8 *) (((uintptr_t) p + BM_HUGE - 1) & ~(uintptr_t) (BM_HUGE - 1));
  if (q > p) munmap(p, (sz) (q - p));
  if (q + full < p + full + BM_HUGE)
    munmap(q + full, (sz) (p + full + BM_HUGE - (q + full)));
#if defined(HAVE_MADVISE) && defined(MADV_HUGEPAGE)
  madvise(q, full, MADV_HUGEPAGE);
#endif
  m->p = q;  m->len = full;  m->heap = 0;
#else
  bm_alloc(m, n);
#endif
}

int bm_flush(blr_map * m) {
  if (!m->p || m->heap) return 1;
#if defined(BLR_WIN32) && !defined(BLR_NO_MMAP)
  return FlushViewOfFile(m->p, m->len) != 0;
#elif defined(BLR_POSIX_MAP)
  return msync(m->p, m->len, MS_ASYNC) == 0;
#else
  return 1;
#endif
}

void bm_free(blr_map * m) {
  if (!m->p) return;
  if (m->heap) xfree(m->p);
#if defined(BLR_WIN32) && !defined(BLR_NO_MMAP)
  else if (!UnmapViewOfFile(m->p))
    FATAL_CODE(BLR_EXIT_IO, "cannot unmap memory");
#elif defined(BLR_POSIX_MAP)
  else if (munmap(m->p, m->len)) FATAL_CODE(BLR_EXIT_IO, "cannot unmap memory");
#endif
  m->p = NULL;  m->len = 0;  m->heap = 0;
}

#if defined(__SANITIZE_ADDRESS__)
#define RG_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define RG_ASAN 1
#endif
#endif
#if defined(__SANITIZE_THREAD__)
#define RG_NONE 1
#elif defined(__has_feature)
#if __has_feature(memory_sanitizer) || __has_feature(thread_sanitizer)
#define RG_NONE 1
#endif
#endif

#if defined(BLR_POSIX_MAP) && defined(MAP_ANONYMOUS) && defined(__GNUC__) && \
    !defined(RG_NONE)

#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0
#endif

#ifdef RG_ASAN
#include <sanitizer/asan_interface.h>
#include <sanitizer/common_interface_defs.h>
extern void __lsan_register_root_region(const void * p, size_t n)
  __attribute__((weak));
#define RG_POISON(p, n) ASAN_POISON_MEMORY_REGION(p, n)
#define RG_UNPOISON(p, n) ASAN_UNPOISON_MEMORY_REGION(p, n)
#define RG_RAW __attribute__((no_sanitize_address))
#define RG_ASANV 1
#define RG_PRE ((sz) 32)
#define RG_RZ ((sz) 32)
#else
#define RG_POISON(p, n) ((void) 0)
#define RG_UNPOISON(p, n) ((void) 0)
#define RG_RAW
#define RG_ASANV 0
#define RG_PRE ((sz) 16)
#define RG_RZ ((sz) 0)
#endif

#define RG_HDR ((sz) 16)
#define RG_MIN ((sz) 1 << 10)
#define RG_NCLS 49
#define RG_SEG ((sz) 64 << 20)
#define RG_NSEG 32
#define RG_TRIM ((sz) 256 << 10)
#ifdef BLR_REGION_CHECK
#define RG_CHK 1
#else
#define RG_CHK 0
#endif
#define RG_FOOT ((sz) 8)
#define RG_JUNK 0xdb
#define RG_WALK 1024
#define RG_QN 512
#define RG_QB ((sz) 64 << 20)

typedef struct rg_head {
  u16 c;  u8 st, fl;  u32 tag;
  union { sz n;  struct rg_head * next; } u;
} rg_head;
typedef char rg_head_fits[sizeof(rg_head) <= RG_HDR && RG_HDR <= RG_PRE
                          ? 1 : -1];
enum { RG_TAKEN = 0x5a, RG_FREE = 0xa5, RG_HELD = 0x3c };
enum { RG_CLEAN = 1, RG_FOOTED = 2, RG_JUNKED = 4 };

static rg_head * rg_list[RG_NCLS], * rg_q[RG_QN];
static u8 * rg_base[RG_NSEG], * rg_used[RG_NSEG];
static u8 * rg_at, * rg_end, * rg_lo, * rg_hi;
static int rg_nseg, rg_dead, rg_state;
static unsigned rg_qh, rg_qn;
static sz rg_page = 4096, rg_live, rg_qb;
void (*rg_watch)(sz live);

static sz rg_csize(unsigned c) { return (RG_MIN << c / 4) / 4 * (4 + c % 4); }

static u32 rg_tag(const u8 * b, u32 v) {
  return ((u32) (uintptr_t) b ^ 0x6a09e667u) * 0x9e3779b1u ^ v * 0x85ebca6bu;
}

static uint64_t rg_footer(const u8 * b) {
  return (uint64_t) rg_tag(b, 0xf00f) << 32 | rg_tag(b, 0x0ff0);
}

static RG_RAW u32 rg_mark(const rg_head * h) {
  return rg_tag((const u8 *) h, h->c | (u32) h->st << 16 | (u32) h->fl << 24);
}

static RG_RAW void rg_set(rg_head * h, unsigned st) {
  h->st = (u8) st;  h->tag = rg_mark(h);
}

static RG_RAW int rg_foot_ok(const u8 * b, sz n) {
  uint64_t f = rg_footer(b), g;
  if (n > rg_csize(((const rg_head *) b)->c) - RG_PRE - RG_FOOT) return 0;
  memcpy(&g, b + RG_PRE + n, RG_FOOT);
  return f == g;
}

static RG_RAW void rg_put_foot(u8 * b, sz n) {
  uint64_t f = rg_footer(b);  memcpy(b + RG_PRE + n, &f, RG_FOOT);
}

static RG_RAW int rg_junk_ok(const u8 * p, sz n) {
  sz i;
  Fi(n, if (p[i] != RG_JUNK) return 0);
  return 1;
}

static NORETURN void rg_fail(const char * what, const void * p) {
  fprintf(stderr, "balrogg: region: %s at %p\n", what, p);
#ifdef RG_ASAN
  __sanitizer_print_stack_trace();
#endif
  abort();
}

static RG_RAW const char * rg_verify(const void ** at) {
  int i;
  Fi(rg_nseg,
    u8 * b = rg_base[i], * e = i + 1 < rg_nseg ? rg_used[i] : rg_at;
    while (b < e) {
      const rg_head * h = (const rg_head *) b;
      *at = b + RG_PRE;
      if (h->c >= RG_NCLS || (sz) (e - b) < rg_csize(h->c) ||
          h->tag != rg_mark(h))
        return "a block header is overwritten";
      if (h->st != RG_TAKEN && h->st != RG_FREE && h->st != RG_HELD)
        return "a block header is overwritten";
      if (RG_CHK && h->st == RG_TAKEN && (h->fl & RG_FOOTED) &&
          !rg_foot_ok(b, h->u.n))
        return "a write past a block's end";
      b += rg_csize(h->c);
    });
  return NULL;
}

const char * rg_walk(void) {
  const void * at;
  const char * w;
  w = rg_verify(&at);
  return w;
}

#if RG_CHK
static unsigned rg_ops;

static void rg_tick(void) {
  const void * at;
  const char * w;
  if (++rg_ops % RG_WALK) return;
  if ((w = rg_verify(&at))) rg_fail(w, at);
}

static void rg_exit_walk(void) {
  const void * at = NULL;
  const char * w;
  w = rg_verify(&at);
  if (w) rg_fail(w, at);
}
#else
#define rg_tick() ((void) 0)
#endif

static void rg_init(void) {
  const char * e = getenv("BLR_REGION");
#if RG_CHK
  atexit(rg_exit_walk);
#endif
  __atomic_store_n(&rg_state, e && !strcmp(e, "0") ? 1 : 2, __ATOMIC_RELEASE);
}

static int rg_ready(void) {
  int s = __atomic_load_n(&rg_state, __ATOMIC_ACQUIRE);
  if (LIKELY(s)) return s == 2;
  if (!rg_state) rg_init();
  return __atomic_load_n(&rg_state, __ATOMIC_ACQUIRE) == 2;
}

sz rg_inuse(void) { return __atomic_load_n(&rg_live, __ATOMIC_RELAXED); }

static void rg_pages(const u8 * b, unsigned c, u8 ** lo, u8 ** hi) {
  *lo = (u8 *) (((uintptr_t) b + RG_PRE + rg_page - 1) &
                ~(uintptr_t) (rg_page - 1));
  *hi = (u8 *) (((uintptr_t) b + rg_csize(c)) & ~(uintptr_t) (rg_page - 1));
}

static RG_RAW void rg_evict(void) {
  rg_head * h = rg_q[rg_qh];
  sz size = rg_csize(h->c);
  rg_qh = (rg_qh + 1) % RG_QN;  rg_qn--;  rg_qb -= size;
  if (RG_CHK && (h->fl & RG_JUNKED) &&
      !rg_junk_ok((u8 *) h + RG_PRE, size - RG_PRE))
    rg_fail("a write to a freed block", (u8 *) h + RG_PRE);
  rg_set(h, RG_FREE);
  h->u.next = rg_list[h->c];  rg_list[h->c] = h;
}

RG_RAW void * rg_take(sz n, int zero) {
  sz need, size, live;
  unsigned c = 0, k, fl = 0;
  const int foot = RG_CHK && !RG_ASANV;
  int reused = 0;
  u8 * b, * p;
  rg_head * h;
  if (!rg_ready()) return NULL;
  need = RG_PRE + n + RG_RZ + (foot ? RG_FOOT : 0);
  if (need < n || need > RG_MAX) return NULL;
  while (rg_csize(c) < need) c++;
  for (k = c; k < c + 4 && k < RG_NCLS && !rg_list[k]; k++);
  if (k < c + 4 && k < RG_NCLS) {
    h = rg_list[k];  b = (u8 *) h;  c = k;  reused = 1;
    if (h->st != RG_FREE || h->tag != rg_mark(h))
      rg_fail("a free block is overwritten", b + RG_PRE);
    rg_list[k] = h->u.next;  fl = h->fl;
    if (RG_CHK && (fl & RG_JUNKED) &&
        !rg_junk_ok(b + RG_PRE, rg_csize(c) - RG_PRE))
      rg_fail("a write to a freed block", b + RG_PRE);
  } else {
    if ((sz) (rg_end - rg_at) < rg_csize(c)) {
      void * m = rg_dead || rg_nseg == RG_NSEG ? MAP_FAILED
               : mmap(NULL, RG_SEG, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
      if (m == MAP_FAILED) { rg_dead = 1;  return NULL; }
#ifdef RG_ASAN
      if (__lsan_register_root_region) __lsan_register_root_region(m, RG_SEG);
#endif
      if (rg_nseg) rg_used[rg_nseg - 1] = rg_at;
      rg_at = m;  rg_end = rg_at + RG_SEG;
#ifdef HAVE_SYSCONF
      if (!rg_nseg && sysconf(_SC_PAGESIZE) > 0)
        rg_page = (sz) sysconf(_SC_PAGESIZE);
#endif
      if (!rg_lo || rg_at < rg_lo)
        __atomic_store_n(&rg_lo, rg_at, __ATOMIC_RELAXED);
      if (rg_end > rg_hi) __atomic_store_n(&rg_hi, rg_end, __ATOMIC_RELAXED);
      rg_base[rg_nseg] = rg_at;
      __atomic_store_n(&rg_nseg, rg_nseg + 1, __ATOMIC_RELEASE);
    }
    b = rg_at;  rg_at += rg_csize(c);  h = (rg_head *) b;
  }
  size = rg_csize(c);  p = b + RG_PRE;
  h->c = (u16) c;  h->fl = foot ? RG_FOOTED : 0;  h->u.n = n;
  rg_set(h, RG_TAKEN);
  if (foot) rg_put_foot(b, n);
  live = rg_live + size;  __atomic_store_n(&rg_live, live, __ATOMIC_RELAXED);
  rg_tick();
  RG_POISON(b, size);  RG_UNPOISON(p, n);
  if (zero && reused && !(fl & RG_CLEAN)) memset(p, 0, n);
  else if (zero && reused) {
    u8 * lo, * hi, * e = p + n;
    rg_pages(b, c, &lo, &hi);
    memset(p, 0, (sz) (MIN(e, lo) - p));
    if (e > hi) memset(hi, 0, (sz) (e - hi));
  }
  if (rg_watch) rg_watch(live);
  return p;
}

static RG_RAW void rg_mine(const u8 * p, const char * what) {
  const rg_head * h = (const rg_head *) (p - RG_PRE);
  if (((uintptr_t) p & 15) || h->c >= RG_NCLS)
    rg_fail("a pointer the region did not give", p);
  if (h->st == RG_TAKEN && h->tag == rg_mark(h)) return;
  if (h->tag == rg_mark(h) && (h->st == RG_FREE || h->st == RG_HELD))
    rg_fail(what, p);
  rg_fail("a pointer the region did not give", p);
}

RG_RAW int rg_grow(void * p, sz n) {
  u8 * b = (u8 *) p - RG_PRE;
  rg_head * h = (rg_head *) b;
  sz need, size;
  unsigned c;
  const int foot = RG_CHK && !RG_ASANV;
  int ok = 0;
  rg_mine(p, "a freed block grows");
  need = RG_PRE + n + RG_RZ + (foot ? RG_FOOT : 0);
  if (need < n || need > RG_MAX) return 0;
  if (!RG_CHK && need <= rg_csize(h->c)) {
    h->u.n = n;
    RG_POISON(b, rg_csize(h->c));  RG_UNPOISON(p, n);
    return 1;
  }
  if (foot && !rg_foot_ok(b, h->u.n)) rg_fail("a write past a block's end", p);
  c = h->c;  size = rg_csize(c);
  if (need <= size) ok = 1;
  else {
    while (rg_csize(c) < need) c++;
    if (b + size == rg_at && (sz) (rg_end - b) >= rg_csize(c)) {
      __atomic_store_n(&rg_live, rg_live + rg_csize(c) - size,
                       __ATOMIC_RELAXED);
      rg_at = b + rg_csize(c);  h->c = (u16) c;  rg_set(h, RG_TAKEN);  ok = 1;
    }
  }
  if (ok) { h->u.n = n;  if (foot) rg_put_foot(b, n); }
  if (ok) { RG_POISON(b, rg_csize(h->c));  RG_UNPOISON(p, n); }
  return ok;
}

int rg_owns(const void * p) {
  const u8 * q = p;
  int i, n = __atomic_load_n(&rg_nseg, __ATOMIC_ACQUIRE);
  if (!n || q < __atomic_load_n(&rg_lo, __ATOMIC_RELAXED) ||
      q >= __atomic_load_n(&rg_hi, __ATOMIC_RELAXED)) return 0;
  Fi(n, if (q >= rg_base[i] + RG_PRE && q < rg_base[i] + RG_SEG) return 1);
  return 0;
}

RG_RAW sz rg_size(const void * p) {
  rg_mine(p, "a freed block is resized");
  return ((const rg_head *) ((const u8 *) p - RG_PRE))->u.n;
}

RG_RAW void rg_give(void * p) {
  u8 * b = (u8 *) p - RG_PRE;
  rg_head * h = (rg_head *) b;
  unsigned c, fl = 0;
  const int hold = RG_ASANV || RG_CHK;
  sz size;
  if (RG_CHK) {
    rg_mine(p, "a block freed twice");
    if ((h->fl & RG_FOOTED) && !rg_foot_ok(b, h->u.n))
      rg_fail("a write past a block's end", p);
    rg_set(h, RG_HELD);
  } else rg_mine(p, "a block freed twice");
  c = h->c;  size = rg_csize(c);
#if defined(HAVE_MADVISE) && defined(MADV_DONTNEED)
  if (size >= RG_TRIM) {
    u8 * lo, * hi;
    rg_pages(b, c, &lo, &hi);
    if (hi > lo && !madvise(lo, (sz) (hi - lo), MADV_DONTNEED)) fl = RG_CLEAN;
  }
#endif
  if (RG_CHK && !RG_ASANV && !fl) {
    memset(p, RG_JUNK, size - RG_PRE);  fl = RG_JUNKED;
  }
  RG_POISON(b, size);
  h->fl = (u8) fl;
  __atomic_store_n(&rg_live, rg_live - size, __ATOMIC_RELAXED);
  if (hold) {
    rg_set(h, RG_HELD);
    rg_q[(rg_qh + rg_qn) % RG_QN] = h;  rg_qn++;  rg_qb += size;
    while (rg_qn == RG_QN || rg_qb > RG_QB) rg_evict();
  } else {
    while (rg_qn) rg_evict();
    rg_set(h, RG_FREE);
    h->u.next = rg_list[c];  rg_list[c] = h;
  }
  rg_tick();
}

#else

void (*rg_watch)(sz live);
void * rg_take(sz n, int zero) { (void) n;  (void) zero;  return NULL; }
int rg_owns(const void * p) { (void) p;  return 0; }
sz rg_size(const void * p) { (void) p;  return 0; }
int rg_grow(void * p, sz n) { (void) p;  (void) n;  return 0; }
void rg_give(void * p) { (void) p; }
const char * rg_walk(void) { return NULL; }
sz rg_inuse(void) { return 0; }

#endif
