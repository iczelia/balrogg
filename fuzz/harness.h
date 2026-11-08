#ifndef BLR_HARNESS_H
#define BLR_HARNESS_H

#include "sys/common.h"
#include "sys/file.h"
#include "sys/map.h"
#include "fmt/ogg.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__SANITIZE_ADDRESS__)
#define FZ_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define FZ_ASAN 1
#endif
#endif
#ifdef FZ_ASAN
#include <sanitizer/allocator_interface.h>
#include <sanitizer/common_interface_defs.h>
#endif

#ifndef FZ_TAG
#define FZ_TAG "x"
#endif

int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size);

static unsigned fz_secs = 20;
static long long fz_heap = 768LL << 20, fz_fsize = 256LL << 20;

static u8 * fz_cov;
static sz fz_ncov;
#ifndef FZ_STANDALONE
void __real___sanitizer_cov_8bit_counters_init(u8 * a, u8 * b);
void __wrap___sanitizer_cov_8bit_counters_init(u8 * a, u8 * b);
void __wrap___sanitizer_cov_8bit_counters_init(u8 * a, u8 * b) {
  if (b > a && (!fz_cov || a < fz_cov || b > fz_cov + fz_ncov)) {
    u8 * e = fz_cov && fz_cov + fz_ncov > b ? fz_cov + fz_ncov : b;
    if (!fz_cov || a < fz_cov) fz_cov = a;
    fz_ncov = (sz) (e - fz_cov);
  }
  __real___sanitizer_cov_8bit_counters_init(a, b);
}
#endif

typedef struct { long long peak;  u8 cov[]; } fz_shared;

static fz_shared * fz_sh;
static volatile long long fz_cur, fz_peak;
static volatile int fz_inchild, fz_dying;
static char FZ_DIR[1024], FZ_IN[1100], FZ_OUT[1100], FZ_OUT2[1100],
            FZ_LOG[1100], FZ_ARC[1100];

static void fz_say(const char * s) { if (write(2, s, strlen(s)) < 0) return; }

static void fz_die(const char * why) {
  fz_dying = 1;
  fz_say("==fz== ");  fz_say(why);  fz_say("\n");
#ifdef FZ_ASAN
  __sanitizer_print_stack_trace();
#endif
  abort();
}

static void fz_count(long long c) {
  if (c > fz_peak) fz_peak = c;
  if (c > fz_heap) fz_die("heap over the limit");
}

#ifdef FZ_ASAN
static void fz_malloc_hook(const volatile void * p, size_t n) {
  (void) n;
  if (!fz_inchild || fz_dying) return;
  fz_count(__sync_add_and_fetch(&fz_cur,
               (long long) __sanitizer_get_allocated_size((const void *) p)) +
           (long long) rg_inuse());
}

static void fz_free_hook(const volatile void * p) {
  if (!fz_inchild || !p || !__sanitizer_get_ownership((const void *) p)) return;
  __sync_sub_and_fetch(&fz_cur,
      (long long) __sanitizer_get_allocated_size((const void *) p));
}
#endif

static void fz_rg_watch(sz live) {
  if (fz_inchild && !fz_dying) fz_count(fz_cur + (long long) live);
}

static long long fz_statm(int rss) {
  long a = 0, b = 0;
  FILE * f = fopen("/proc/self/statm", "r");
  if (!f) return 0;
  if (fscanf(f, "%ld %ld", &a, &b) != 2) a = b = 0;
  fclose(f);
  return (long long) (rss ? b : a) * sysconf(_SC_PAGESIZE);
}

static void fz_alarm(int s) { (void) s;  fz_die("timeout"); }
static void fz_xfsz(int s) {
  (void) s;  fz_die("a file grew over the size limit");
}

static void fz_child_done(void) {
  const char * w = rg_walk();
  if (w) { fz_say("==fz== region: ");  fz_die(w); }
  if (fz_ncov) memcpy(fz_sh->cov, fz_cov, fz_ncov);
  fz_sh->peak = fz_peak;
  fflush(NULL);
}

#ifdef __GLIBC__
static void fz_child_exit(int status, void * arg) {
  (void) arg;
  fz_child_done();
  _exit(status);
}
#endif

static pid_t fz_parent;

static void fz_rm_dir(void) {
  DIR * d;
  struct dirent * e;
  char p[1400];
  if (getpid() != fz_parent || !(d = opendir(FZ_DIR))) return;
  while ((e = readdir(d)))
    if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) {
      snprintf(p, sizeof p, "%s/%s", FZ_DIR, e->d_name);  remove(p);
    }
  closedir(d);  rmdir(FZ_DIR);
}

static long fz_env(const char * name, long def) {
  const char * e = getenv(name);
  return e && *e ? atol(e) : def;
}

static void fz_paths(void) {
  const char * dir;
  if (FZ_DIR[0]) return;
  dir = getenv("TMPDIR");
  if (!dir || !*dir) dir = "/tmp";
  snprintf(FZ_DIR, sizeof FZ_DIR, "%s/blrfz-%s-%ld", dir, FZ_TAG,
           (long) getpid());
  if (mkdir(FZ_DIR, 0700) && errno != EEXIST) abort();
  snprintf(FZ_IN, sizeof FZ_IN, "%s/in", FZ_DIR);
  snprintf(FZ_OUT, sizeof FZ_OUT, "%s/out", FZ_DIR);
  snprintf(FZ_OUT2, sizeof FZ_OUT2, "%s/out2", FZ_DIR);
  snprintf(FZ_ARC, sizeof FZ_ARC, "%s/arc", FZ_DIR);
  snprintf(FZ_LOG, sizeof FZ_LOG, "%s/log", FZ_DIR);
  fz_secs = (unsigned) fz_env("FZ_SECS", (long) fz_secs);
  fz_heap = (long long) fz_env("FZ_HEAP_MB", (long) (fz_heap >> 20)) << 20;
  fz_fsize = (long long) fz_env("FZ_FSIZE_MB", (long) (fz_fsize >> 20)) << 20;
  fz_sh = mmap(NULL, sizeof *fz_sh + fz_ncov + 1, PROT_READ | PROT_WRITE,
               MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (fz_sh == MAP_FAILED) abort();
#ifdef FZ_ASAN
  if (__sanitizer_install_malloc_and_free_hooks(fz_malloc_hook,
                                                fz_free_hook) == 0)
    abort();
#else
  if (setenv("BLR_MEMCAP", "0", 1)) abort();
#endif
  rg_watch = fz_rg_watch;
  fz_parent = getpid();
  if (atexit(fz_rm_dir)) abort();
}

static void fz_put(const char * path, const u8 * d, sz n) {
  FILE * f = fopen(path, "wb");
  if (!f) abort();
  if ((n && fwrite(d, 1, n, f) != n) || fclose(f)) abort();
}

static u8 * fz_get(const char * path, sz * n) {
  FILE * f = fopen(path, "rb");
  sz cap = 1 << 16, k;
  u8 * b;
  *n = 0;
  if (!f) return NULL;
  b = xmalloc(cap);
  while ((k = fread(b + *n, 1, cap - *n, f)) > 0)
    if ((*n += k) == cap) b = xrealloc(b, cap *= 2);
  fclose(f);
  return b;
}

static void fz_ls(char * out, sz cap) {
  char names[64][64];
  int n = 0, i, j;
  struct dirent * e;
  DIR * d = opendir(FZ_DIR);
  sz at = 0;
  if (!d) abort();
  while ((e = readdir(d)))
    if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..") && n < 64)
      snprintf(names[n++], sizeof names[0], "%s", e->d_name);
  closedir(d);
  for (i = 1; i < n; i++)
    for (j = i; j > 0 && strcmp(names[j - 1], names[j]) > 0; j--) {
      char t[64];
      memcpy(t, names[j], 64);  memcpy(names[j], names[j - 1], 64);
      memcpy(names[j - 1], t, 64);
    }
  out[0] = 0;
  Fi(n,
    at += (sz) snprintf(out + at, at < cap ? cap - at : 0, "%s ", names[i]));
}

static void fz_show_log(void) {
  sz n;
  u8 * b = fz_get(FZ_LOG, &n);
  if (b) { if (n && write(2, b, n) < 0) n = 0;  xfree(b); }
}

static int fz_log_has(const char * s) {
  sz n, i, k = strlen(s);
  u8 * b = fz_get(FZ_LOG, &n);
  int has = 0;
  for (i = 0; b && i + k <= n && !has; i++) has = !memcmp(b + i, s, k);
  xfree(b);
  return has;
}

static NORETURN void fz_fail(const char * what, const char * why) {
  fz_show_log();
  fprintf(stderr, "==fz== %s: %s\n", what, why);
  abort();
}

static void fz_child(void) {
  struct rlimit rl;
  int fd = open(FZ_LOG, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0 || dup2(fd, 2) < 0) _exit(99);
  close(fd);
  fd = open("/dev/null", O_RDWR);
  if (fd < 0 || dup2(fd, 1) < 0 || dup2(fd, 0) < 0) _exit(99);
  close(fd);
  prctl(PR_SET_PDEATHSIG, SIGKILL);
  signal(SIGALRM, fz_alarm);  signal(SIGXFSZ, fz_xfsz);
  alarm(fz_secs);
  rl.rlim_cur = rl.rlim_max = (rlim_t) fz_fsize;
  setrlimit(RLIMIT_FSIZE, &rl);
#ifndef FZ_ASAN
  rl.rlim_cur = rl.rlim_max = (rlim_t) fz_statm(0) + (rlim_t) fz_heap * 2 +
                              ((rlim_t) 3 << 30);
  setrlimit(RLIMIT_AS, &rl);
#endif
  fz_cur = fz_peak = 0;  fz_inchild = 1;
#ifdef __GLIBC__
  if (on_exit(fz_child_exit, NULL)) _exit(99);
#else
  if (atexit(fz_child_done)) _exit(99);
#endif
}

static long long fz_last_peak;

static int fz_step(const char * what, int must, int (*body)(void),
                   const char * out) {
  char before[4096], after[4096], want[4096];
  int st, code;
  pid_t c;
  sz i;
  struct rusage ru;
  long long base = fz_statm(1);
  fz_put(FZ_LOG, NULL, 0);
  fz_ls(before, sizeof before);
  memset(fz_sh, 0, sizeof *fz_sh + fz_ncov);
  fflush(stdout);  fflush(stderr);
  c = fork();
  if (c < 0) abort();
  if (!c) { fz_child();  exit(body()); }
  while (wait4(c, &st, 0, &ru) < 0) if (errno != EINTR) abort();
  Fi(fz_ncov, if (fz_sh->cov[i]) fz_cov[i] = fz_sh->cov[i]);
  fz_last_peak = fz_sh->peak;
#ifndef FZ_ASAN
  if ((long long) ru.ru_maxrss * 1024 - base > fz_last_peak)
    fz_last_peak = (long long) ru.ru_maxrss * 1024 - base;
#else
  (void) ru;  (void) base;
#endif
  if (WIFSIGNALED(st)) {
    char m[64];
    snprintf(m, sizeof m, "killed by signal %d", WTERMSIG(st));
    fz_fail(what, m);
  }
  code = WEXITSTATUS(st);
  if (fz_log_has("runtime error:") || fz_log_has("Sanitizer"))
    fz_fail(what, "sanitizer report");
  if (fz_last_peak > fz_heap) fz_fail(what, "heap over the limit");
  if (fz_log_has("balrogg: out of memory") || fz_log_has("MiB cap"))
    fz_fail(what, "out of memory");
  if (code > BLR_EXIT_REFUSED) {
    char m[64];
    snprintf(m, sizeof m, "exit status %d", code);
    fz_fail(what, m);
  }
  fz_ls(after, sizeof after);
  if (code == BLR_EXIT_REFUSED) {
    if (must) fz_fail(what, "refused");
    if (!fz_log_has("balrogg: ")) fz_fail(what, "refused without a message");
    if (strcmp(before, after)) fz_fail(what, "a refusal left files behind");
    return 1;
  }
  if (strcmp(before, after) && out) {
    char tok[256];
    char * w;
    snprintf(tok, sizeof tok, " %s ", strrchr(out, '/') + 1);
    snprintf(want, sizeof want, " %s", after);
    if ((w = strstr(want, tok)))
      memmove(w, w + strlen(tok) - 1, strlen(w + strlen(tok) - 1) + 1);
    if (strcmp(want + 1, before))
      fz_fail(what, "left files other than its output");
  } else if (strcmp(before, after)) fz_fail(what, "left files behind");
  return 0;
}

static void fz_heap_like(long long enc, long long dec) {
  char m[96];
  if (dec <= enc + enc / 2 + (16LL << 20)) return;
  snprintf(m, sizeof m, "decoding took %lld KiB of heap, encoding %lld KiB",
           dec >> 10, enc >> 10);
  fz_fail("decode", m);
}

static void fz_same(const char * a, const char * b) {
  sz an, bn;
  u8 * x = fz_get(a, &an), * y = fz_get(b, &bn);
  int same = x && y && an == bn && !memcmp(x, y, an);
  xfree(x);  xfree(y);
  if (!same) { fprintf(stderr, "==fz== round trip differs\n");  abort(); }
}

static void fz_pack_trip(int (*enc)(void), int (*dec)(void)) {
  if (!fz_step("encode", 0, enc, FZ_OUT)) {
    long long ep = fz_last_peak;
    fz_step("decode", 1, dec, FZ_OUT2);
    fz_heap_like(ep, fz_last_peak);
    fz_same(FZ_IN, FZ_OUT2);
  }
  remove(FZ_OUT);  remove(FZ_OUT2);
}

static void fz_unpack_trip(int (*dec)(void), int (*enc)(void), int (*dec2)(void)) {
  if (!fz_step("decode", 0, dec, FZ_OUT) &&
      !fz_step("encode the decoded file", 0, enc, FZ_ARC)) {
    long long ep = fz_last_peak;
    fz_step("decode its archive", 1, dec2, FZ_OUT2);
    fz_heap_like(ep, fz_last_peak);
    fz_same(FZ_OUT, FZ_OUT2);
  }
  remove(FZ_OUT);  remove(FZ_ARC);  remove(FZ_OUT2);
}

#define FZ_JOB(out, call)                                                     \
  do { int rc_;  bf_doom(out);  rc_ = (call);                                 \
       if (rc_) bf_doomed_remove();  bf_doom(NULL);  return rc_; } while (0)

typedef struct { const u8 * p;  sz n; } fz_pkt;

#define FZ_MAXPKT 4096

static sz fz_split(const u8 * d, sz n, fz_pkt * out, sz max) {
  sz at = 0, k = 0;
  while (at + 2 <= n && k < max) {
    sz l = rd16le(d + at);
    at += 2;
    if (!l || at + l > n) break;
    out[k].p = d + at;  out[k].n = l;  k++;  at += l;
  }
  return k;
}

static void fz_fixcrc(u8 * b, sz n) {
  ogg_page p;
  sz at = 0, tot;
  while ((tot = ogg_parse(&p, b + at, n - at))) {
    ogg_crc_set(b + at, tot);  at += tot;
  }
}

static void fz_put_ogg(const u8 * d, sz n, int raw) {
  u8 * b = xmalloc(n ? n : 1);
  memcpy(b, d, n);
  if (!raw) fz_fixcrc(b, n);
  fz_put(FZ_IN, b, n);
  xfree(b);
}

typedef struct { u8 * b;  sz n, cap; } fz_buf;

static void fz_grow(fz_buf * o, sz n) {
  if (n > SIZE_MAX - o->n) abort();
  o->b = xgrow(o->b, &o->cap, o->n + n, 1, 65536);
}

static void fz_page(fz_buf * o, const u8 * body, sz len, int type, u32 serial,
                    u32 seq, u32 granule) {
  sz nseg = len / 255 + 1;
  u8 * h;
  if (nseg > 255) return;
  fz_grow(o, 27 + nseg + len);
  h = o->b + o->n;
  memset(h, 0, 27);  memcpy(h, "OggS", 4);  h[5] = (u8) type;
  wr16le(h + 6, granule);  wr32le(h + 14, serial);  wr16le(h + 18, seq);
  h[26] = (u8) nseg;  memset(h + 27, 255, nseg - 1);
  h[27 + nseg - 1] = (u8) (len % 255);
  memcpy(h + 27 + nseg, body, len);
  o->n += 27 + nseg + len;
}

#endif
