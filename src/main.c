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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "sys/yarg.h"
#include "vorbis/vorbis.h"
#include "fmt/arc.h"
#include "opus/opus.h"
#include "mp3/mp3.h"
#include "mod/mod.h"
#include "sys/cpu.h"
#include "sys/crc.h"
#include "sys/prof.h"

#include <errno.h>
#include <limits.h>
#ifdef BLR_WIN32
#include "sys/win32.h"
#else
#include <sys/stat.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#ifdef HAVE_SCHED_GETAFFINITY
#include <sched.h>
#endif

#if defined(BLR_WIN_CRT)
int blr_main(int argc, char ** argv);
#define main blr_main
#endif

#define BLR_DEFLEV 8

static void banner(FILE * to) {
  fprintf(to,
    "balrogg " PACKAGE_VERSION " -- demonically compacting audio recompressor\n"
    "Written by Kamila Szewczyk <k@iczelia.net>.\n"
    "License GNU GPL version 3.\n");
}

static void usage(FILE * to) {
  banner(to);
  fprintf(to,
    "\n"
    "usage: balrogg [-1..-%d] e IN OUT      compress Ogg, MP3, MOD to OUT\n"
    "       balrogg d IN OUT                expand archive to OUT\n"
    "       balrogg [-1..-%d] -b e|d FILE... process files in parallel\n"
    "       balrogg -h | -v\n"
    "\n"
    "  -1 .. -%-6d effort: each level is slower and smaller than the one\n"
    "                below it; -%d is the default; decoding needs no level\n"
    "  -b, --batch   process every remaining path\n"
    "  -j, --jobs=N  process N files at once\n"
    "  -p, --progress show tuning and codec progress on stderr\n"
    "  --progress-lines write progress as separate log lines\n"
    "  --no-mmap     use heap arenas and buffered file I/O\n"
    "  -h, --help    show help        -v, --version   show version\n"
    "\n"
    "Exit codes  0 success, 1 refused input, 2 usage, 3 I/O error,\n"
    "4 internal error.\n", BLR_NLEV, BLR_NLEV, BLR_NLEV, BLR_DEFLEV);
}

static void version(void) {
  banner(stdout);
  printf("Build host %s.\n", BLR_HOST_TRIPLE);
  printf("Copyright (C) 2022-2026 Kamila Szewczyk.\n"
         "This is free software: you are free to change and redistribute it.\n"
         "There is NO WARRANTY, to the extent permitted by law.\n");
  printf("Ogg Opus mode: %s\n", opus_mode_version());
  printf("SIMD: %s built, %s dispatched\n", blr_simd_built(),
         cm_dispatched());
  printf("CRC-32C: %s\n", crc32c_dispatched());
}

static int peek(const char * path, u8 * b, int max) {
  sz n;
  int bad;
  FILE * f = fopen(path, "rb");
  if (!f) return -1;
  n = fread(b, 1, (sz) max, f);
  bad = ferror(f);
  if (fclose(f)) bad = 1;
  if (bad) return -2;
  return (int) n;
}

enum {
  F_READ = -2, F_BAD = -1, F_UNKNOWN = 0, F_VORBIS = 1, F_OPUS = 2, F_MP3 = 3,
  F_MOD = 4
};

#define BLR_MP3_PEEK (1 << 20)

static int sniff_mp3(const char * path) {
  u8 * b = xmalloc(BLR_MP3_PEEK);
  int n = peek(path, b, BLR_MP3_PEEK),
      r = n > 0 && mp3_sniff(b, (sz) n, n < BLR_MP3_PEEK);
  xfree(b);
  return r ? F_MP3 : F_UNKNOWN;
}

static int sniff(const char * path) {
  u8 b[512];
  int n = peek(path, b, (int) sizeof b), at;
  if (n < 0) return n;
  if (n < 28 || memcmp(b, "OggS", 4))
    return mod_sniff(path) ? F_MOD : sniff_mp3(path);
  at = 27 + b[26];
  if (at + 8 > n) return F_UNKNOWN;
  if (!memcmp(b + at, "\001vorbis", 7)) return F_VORBIS;
  if (!memcmp(b + at, "OpusHead", 8)) return F_OPUS;
  return F_UNKNOWN;
}

static int arc_is_opus(const char * path) {
  u8 b[ARC_HDRLEN];
  int n = peek(path, b, (int) sizeof b);
  return n >= (int) ARC_HDRLEN && arc_is(b, (sz) n) &&
         b[ARC_MAGLEN + 1] & ARC_KOPUS;
}

typedef unsigned long long bytes_t;

#if defined(BLR_WIN32)

static int same_file(const char * a, const char * b) {
  BY_HANDLE_FILE_INFORMATION x, y;
  HANDLE ha, hb;
  int same = 0;
  if (!strcmp(a, b)) return 1;
  ha = blr_win_open(a, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    OPEN_EXISTING, 0);
  if (ha == INVALID_HANDLE_VALUE) return 0;
  hb = blr_win_open(b, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    OPEN_EXISTING, 0);
  if (hb != INVALID_HANDLE_VALUE) {
    if (GetFileInformationByHandle(ha, &x) &&
        GetFileInformationByHandle(hb, &y))
      same = x.dwVolumeSerialNumber == y.dwVolumeSerialNumber
             && x.nFileIndexHigh == y.nFileIndexHigh
             && x.nFileIndexLow == y.nFileIndexLow;
    CloseHandle(hb);
  }
  CloseHandle(ha);
  return same;
}

static bytes_t file_size(const char * path) {
  DWORD lo, hi = 0, error, attr = blr_win_attrs(path);
  HANDLE h;
  if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY))
    return 0;
  h = blr_win_open(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                   OPEN_EXISTING, 0);
  if (h == INVALID_HANDLE_VALUE) return 0;
  SetLastError(NO_ERROR);
  lo = GetFileSize(h, &hi);
  error = GetLastError();
  CloseHandle(h);
  if (lo == INVALID_FILE_SIZE && error != NO_ERROR) return 0;
  return ((bytes_t) hi << 32) | lo;
}

#else

static int same_file(const char * a, const char * b) {
  struct stat si, so;
  if (!strcmp(a, b)) return 1;
  return !stat(a, &si) && !stat(b, &so) &&
         si.st_dev == so.st_dev && si.st_ino == so.st_ino;
}

static bytes_t file_size(const char * path) {
  struct stat st;
  if (stat(path, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0) return 0;
  return (bytes_t) st.st_size;
}

#endif

static int level_acm(int level) {
  int i, acm = 0;
  Fi(VC_N, acm |= vb_config_acm(vb_level_cfg(level, i)));
  Fi(OC_N, acm |= opus_config_acm(opus_level_cfg(level, i)));
  return acm;
}

static int code_one(int enc, const char * in, const char * out,
                    int level);

static int check_sum(const char * in, const char * out) {
  u8 h[MAX(ARC_OGGHDR, ARC_MP3HDR + ARC_SUMLEN)];
  u32 want, got;
  int n = peek(in, h, (int) sizeof h), streamed = 0;
  if (n < 0 || !arc_sum_peek(h, (sz) n, &want)) {
    fprintf(stderr, "balrogg: %s: the archive header is truncated\n", in);
    return BLR_EXIT_REFUSED;
  }
  if (!bf_crc_path(out, &got)) {
    if (!bf_crc_output(&got)) {
      fprintf(stderr,
              "balrogg: %s: the output cannot be read back to check it\n", out);
      return BLR_EXIT_IO;
    }
    streamed = 1;
  }
  if (got == want) return BLR_EXIT_OK;
  if (streamed)
    fprintf(stderr, "balrogg: %s: checksum mismatch: "
                    "the archive is damaged and %s is wrong\n", in, out);
  else
    fprintf(stderr, "balrogg: %s: checksum mismatch: the archive is damaged\n", in);
  return BLR_EXIT_REFUSED;
}

static int do_one(int enc, const char * in, const char * out,
                  int level) {
  if (same_file(in, out)) {
    fprintf(stderr, "balrogg: %s is both input and output\n", in);
    return BLR_EXIT_IO;
  }
  { int rc;
    bf_doom(out);
    arc_sum_set(0);
    rc = code_one(enc, in, out, level);
    if (!rc && !enc) rc = check_sum(in, out);
    if (rc) bf_doomed_remove();
    bf_doom(NULL);
    return rc; }
}

static int code_one(int enc, const char * in, const char * out,
                    int level) {
  int kind;
  u32 crc;
  if (!enc) {
    if (arc_is_opus(in)) return opus_unpack(in, out);
    if (mp3_is_archive(in)) return mp3_unpack(in, out);
    if (mod_is_archive(in)) return mod_unpack(in, out);
    vb_unpack(in, out);  return BLR_EXIT_OK;
  }
  kind = sniff(in);
  if (kind > F_UNKNOWN) {
    if (!bf_crc_path(in, &crc)) {
      fprintf(stderr, "balrogg: %s is not a regular file\n", in);
      return BLR_EXIT_IO;
    }
    arc_sum_set(crc);
  }
  switch (kind) {
  case F_OPUS:
    { int cfg[OC_N], i;
      Fi(OC_N, cfg[i] = opus_level_cfg(level, i));
      return opus_pack_classed(in, out, cfg); }
  case F_MP3:
    return mp3_pack(in, out, mp3_level_cfg(level));
  case F_MOD:
    return mod_pack(in, out, mod_level_cfg(level), mod_level_scfg(level));
  case F_UNKNOWN:
    fprintf(stderr, "balrogg: %s is not Ogg Vorbis, Ogg Opus, MP3 or MOD\n", in);
    return BLR_EXIT_REFUSED;
  case F_BAD:
    fprintf(stderr, "balrogg: cannot open %s\n", in);
    return BLR_EXIT_IO;
  case F_READ:
    fprintf(stderr, "balrogg: cannot read %s\n", in);
    return BLR_EXIT_IO;
  case F_VORBIS:
    vb_pack(in, out, vb_level_cfg(level, vb_classify(in)));
    return BLR_EXIT_OK;
  }
  return BLR_EXIT_INTERNAL;
}

#define BLR_JOB_FIXED (72UL << 20)
#define BLR_JOB_ACM   (512UL << 20)
static int blr_acm;

static long blr_cores(void) {
#if defined(BLR_WIN32)
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  return si.dwNumberOfProcessors > 0 ? (long) si.dwNumberOfProcessors : 1;
#elif defined(BLR_DOS)
  return 1;
#elif defined(HAVE_SYSCONF) && defined(_SC_NPROCESSORS_ONLN)
  long n;
#if defined(HAVE_SCHED_GETAFFINITY) && defined(CPU_COUNT)
  cpu_set_t cs;
  if (!sched_getaffinity(0, sizeof cs, &cs) && CPU_COUNT(&cs) > 0)
    return (long) CPU_COUNT(&cs);
#endif
  n = sysconf(_SC_NPROCESSORS_ONLN);
  return n > 0 ? n : 1;
#else
  return 1;
#endif
}

static bytes_t blr_avail(void) {
#if defined(BLR_WIN_LEGACY)
  MEMORYSTATUS ms;
  ms.dwLength = sizeof ms;
  GlobalMemoryStatus(&ms);
  return (bytes_t) ms.dwAvailPhys;
#elif defined(BLR_WIN32)
  MEMORYSTATUSEX ms;
  ms.dwLength = sizeof ms;
  if (GlobalMemoryStatusEx(&ms)) return (bytes_t) ms.ullAvailPhys;
  return 0;
#elif defined(BLR_DOS)
  return 0;
#else
  FILE * f = fopen("/proc/meminfo", "r");
  char line[256];
  unsigned long kb = 0;
  if (f) {
    while (fgets(line, (int) sizeof line, f))
      if (!strncmp(line, "MemAvailable:", 13)) {
        kb = strtoul(line + 13, NULL, 10);  break;
      }
    fclose(f);
  }
  if (kb) return (bytes_t) kb * 1024;
#if defined(HAVE_SYSCONF) && defined(_SC_AVPHYS_PAGES) && defined(_SC_PAGESIZE)
  { long p = sysconf(_SC_AVPHYS_PAGES), z = sysconf(_SC_PAGESIZE);
    if (p > 0 && z > 0) return (bytes_t) p * (bytes_t) z; }
#endif
  return 0;
#endif
}

#if defined(BLR_DOS)
static char * out_name(int enc, const char * in) {
  sz n = strlen(in), stem;
  const char * dot = strrchr(in, '.'), * sep = strrchr(in, '/');
  const char * bsep = strrchr(in, '\\');
  char * s;
  FATAL_UNLESS(n <= SIZE_MAX - 5, "path is too long");
  if (bsep && (!sep || bsep > sep)) sep = bsep;
  stem = dot && (!sep || dot > sep) ? (sz) (dot - in) : n;
  s = xmalloc(stem + 5);
  memcpy(s, in, stem);
  strcpy(s + stem, enc ? ".blr" : arc_is_opus(in) ? ".opu"
                  : mp3_is_archive(in) ? ".mp3"
                  : mod_is_archive(in) ? ".mod" : ".ogg");
  return s;
}
#else
static char * out_name(int enc, const char * in) {
  sz n = strlen(in);
  FATAL_UNLESS(n <= SIZE_MAX - 5, "path is too long");
  char * s = xmalloc(n + 5);
  if (enc) { sprintf(s, "%s.blr", in);  return s; }
  strcpy(s, in);
  if (n > 4 && !strcmp(s + n - 4, ".blr")) s[n - 4] = 0;
  else strcat(s, ".out");
  return s;
}
#endif

typedef struct { const char * name;  bytes_t size;  int order; } job;

static int by_size_desc(const void * a, const void * b) {
  const job * x = a, * y = b;
  if (x->size != y->size) return x->size < y->size ? 1 : -1;
  return x->order < y->order ? -1 : x->order > y->order;
}

typedef struct {
  const char * self;
  const char * lev;
  int enc, jobs, live, bad, status;
#if defined(BLR_WIN32)
  HANDLE h[MAXIMUM_WAIT_OBJECTS];
#endif
} pool;

static void pool_failed(pool * p, int status) {
  if (!status) return;
  p->bad++;
  if (status < BLR_EXIT_REFUSED || status > BLR_EXIT_INTERNAL)
    status = BLR_EXIT_INTERNAL;
  if (status > p->status) p->status = status;
}

#if defined(BLR_WIN32)

static void pool_reap(pool * p) {
  DWORD i, code;
  i = WaitForMultipleObjects((DWORD) p->live, p->h, FALSE, INFINITE);
  if (i >= (DWORD) p->live) FATAL_CODE(BLR_EXIT_IO, "batch: wait failed");
  if (!GetExitCodeProcess(p->h[i], &code)) pool_failed(p, BLR_EXIT_IO);
  else pool_failed(p, (int) code);
  CloseHandle(p->h[i]);
  p->h[i] = p->h[--p->live];
}

static char * winquote(const char * s) {
  sz n = strlen(s), i, k = 0, bs;
  FATAL_UNLESS(n <= (SIZE_MAX - 3) / 2, "path is too long");
  char * q = xmalloc(2 * n + 3);
  q[k++] = '"';
  Fi(n,
    for (bs = 0; i < n && s[i] == '\\'; i++) bs++;
    if (i == n) { while (bs--) { q[k++] = '\\';  q[k++] = '\\'; }  break; }
    if (s[i] == '"') {
      while (bs--) { q[k++] = '\\';  q[k++] = '\\'; }
      q[k++] = '\\';
    } else while (bs--) q[k++] = '\\';
    q[k++] = s[i]);
  q[k++] = '"';  q[k] = 0;
  return q;
}

static void pool_spawn(pool * p, const char * in, const char * out,
                       int level) {
  char * qin = winquote(in), * qout = winquote(out),
       * qself = winquote(p->self);
  char * cmd = xmalloc(strlen(qself) + strlen(qin) + strlen(qout) +
                       strlen(p->lev) + 80);
  PROCESS_INFORMATION pi;
  BOOL ok;
  if (p->jobs > MAXIMUM_WAIT_OBJECTS) p->jobs = MAXIMUM_WAIT_OBJECTS;
  while (p->live >= p->jobs) pool_reap(p);
  sprintf(cmd, "%s %s %s %s -- %s %s %s", qself, p->lev,
          blr_progress_enabled ? "--progress-lines" : "",
          blr_no_mmap ? "--no-mmap" : "", p->enc ? "e" : "d", qin, qout);
  ok = blr_win_spawn(p->self, cmd, &pi);
  xfree(qin);  xfree(qout);  xfree(qself);  xfree(cmd);
  if (!ok) {
    pool_failed(p, do_one(p->enc, in, out, level));
    return;
  }
  CloseHandle(pi.hThread);
  p->h[p->live++] = pi.hProcess;
}

#elif defined(HAVE_FORK) && defined(HAVE_WAITPID) && !defined(BLR_DOS)

static void pool_reap(pool * p) {
  int st;
  pid_t child;
  do child = waitpid(-1, &st, 0);  while (child < 0 && errno == EINTR);
  if (child < 0) FATAL_CODE(BLR_EXIT_IO, "batch: wait failed");
  p->live--;
  pool_failed(p, WIFEXITED(st) ? WEXITSTATUS(st) : BLR_EXIT_INTERNAL);
}

static void pool_spawn(pool * p, const char * in, const char * out,
                       int level) {
  pid_t c;
  while (p->live >= p->jobs) pool_reap(p);
  c = fork();
  if (c < 0) {
    pool_failed(p, do_one(p->enc, in, out, level));
  } else if (!c) {
    int rc = do_one(p->enc, in, out, level);
    _exit(rc);
  } else p->live++;
}

#else

static void pool_reap(pool * p) { p->live = 0; }

static void pool_spawn(pool * p, const char * in, const char * out,
                       int level) {
  pool_failed(p, do_one(p->enc, in, out, level));
}

#endif

static long jobs_for(bytes_t big) {
  bytes_t fixed = BLR_JOB_FIXED + (blr_acm ? BLR_JOB_ACM : 0);
  bytes_t per = big > (ULLONG_MAX - fixed) / 3
                  ? ULLONG_MAX : fixed + 3 * big;
  long n = blr_cores();
  bytes_t avail = blr_avail();
  if (avail && avail / per < (bytes_t) n) n = (long) (avail / per);
  return n < 1 ? 1 : n;
}

static int do_batch(int enc, const char * self, const char * lev,
                    yarg_result * r, int level,
                    int jobs) {
  int n = r->pos_argc - 1, i;
  bytes_t big = 0;
  job * q = xmalloc((sz) (n > 0 ? n : 1) * sizeof *q);
  pool p;
  Fi(n,
    q[i].name = r->pos_args[i + 1];
    q[i].order = i;  q[i].size = file_size(q[i].name);
    if (q[i].size > big) big = q[i].size);
  qsort(q, (sz) n, sizeof *q, by_size_desc);
  if (!jobs) jobs = (int) jobs_for(big);
  if (jobs < 1) jobs = 1;
  if (jobs > n) jobs = n;
  fprintf(stderr, "balrogg: %d file%s, %d job%s\n",
          n, n == 1 ? "" : "s", jobs, jobs == 1 ? "" : "s");
  p.self = self;  p.lev = lev;  p.enc = enc;  p.jobs = jobs;
  p.live = 0;  p.bad = 0;  p.status = BLR_EXIT_OK;
  Fi(n,
    char * out = out_name(enc, q[i].name);
    pool_spawn(&p, q[i].name, out, level);
    xfree(out));
  while (p.live > 0) pool_reap(&p);
  xfree(q);
  if (p.bad) fprintf(stderr, "balrogg: %d of %d failed\n", p.bad, n);
  return p.status;
}

static int parse_jobs(const char * s, int * jobs) {
  char * end;
  unsigned long n;
  if (!s || *s < '0' || *s > '9') return 0;
  errno = 0;
  n = strtoul(s, &end, 10);
  if (errno || *end || !n || n > INT_MAX) return 0;
  *jobs = (int) n;
  return 1;
}

int main(int argc, char ** argv) {
  const char * verb, * in = NULL, * out = NULL;
  yarg_options opt[] = {
    { 'h', no_argument, "help" },
    { 'v', no_argument, "version" },
    { 'b', no_argument, "batch" },
    { 'j', required_argument, "jobs" },
    { 'p', no_argument, "progress" },
    { 256, no_argument, "progress-lines" },
    { 257, no_argument, "no-mmap" },
    { '1', no_argument, NULL }, { '2', no_argument, NULL },
    { '3', no_argument, NULL }, { '4', no_argument, NULL },
    { '5', no_argument, NULL }, { '6', no_argument, NULL },
    { '7', no_argument, NULL }, { '8', no_argument, NULL },
    { '9', no_argument, NULL },
    { 0, no_argument, NULL }
  };
  yarg_settings set;
  yarg_result * r;
  char lev[8];
  int i, k, level = BLR_DEFLEV, np, batch = 0, jobs = 0, rc = 0;

  set.dash_dash = true;
  set.style = YARG_STYLE_UNIX;

  blr_memcap();

#ifdef BLR_PROFILE
  { const char * pd = getenv("BLR_PROF_DIR");
    if (pd) { prof_open(pd);  rc_hook_set(prof_hook, NULL);
              atexit(prof_close); } }
#endif

  for (i = k = 1; i < argc; i++) {
    const char * a = argv[i];
    if (!strcmp(a, "--")) { while (i < argc) argv[k++] = argv[i++];  break; }
    if (a[0] == '-' && a[1] >= '0' && a[1] <= '9' &&
        strspn(a + 1, "0123456789") == strlen(a + 1)) {
      level = strlen(a + 1) <= 2 ? atoi(a + 1) : 0;
      if (level < 1 || level > BLR_NLEV) {
        fprintf(stderr, "balrogg: no level %s; levels are -1 to -%d\n", a,
                BLR_NLEV);
        return BLR_EXIT_USAGE;
      }
    } else argv[k++] = argv[i];
  }
  argc = k;

  r = yarg_parse(argc, argv, opt, set);
  if (!r || r->error) {
    if (r) { fprintf(stderr, "balrogg: %s\n", r->error);  yarg_destroy(r); }
    usage(stderr);
    return BLR_EXIT_USAGE;
  }
  Fi(r->argc,
    int c = r->args[i].opt;
    if (c == 'h') { yarg_destroy(r);  usage(stdout);  return BLR_EXIT_OK; }
    if (c == 'v') { yarg_destroy(r);  version();  return BLR_EXIT_OK; }
    if (c == 'b') batch = 1;
    if (c == 'p') blr_progress_enabled = 1;
    if (c == 256) blr_progress_enabled = 2;
    if (c == 257) blr_no_mmap = 1;
    if (c == 'j') {
      if (!parse_jobs(r->args[i].arg, &jobs)) {
        fprintf(stderr, "balrogg: --jobs requires a positive count\n");
        yarg_destroy(r);  return BLR_EXIT_USAGE;
      }
    }
    if (c >= '1' && c <= '9') level = c - '0');
  np = r->pos_argc;
  if (np < 1) { yarg_destroy(r);  usage(stderr);  return BLR_EXIT_USAGE; }
  verb = r->pos_args[0];
  if (np > 1) in = r->pos_args[1];
  if (np > 2) out = r->pos_args[2];
  np--;

  sprintf(lev, "-%d", level);
  blr_acm = level_acm(level);

  if (batch) {
    if (blr_progress_enabled) blr_progress_enabled = 2;
    if (np < 1 || (strcmp(verb, "e") && strcmp(verb, "d")))
      { usage(stderr);  rc = BLR_EXIT_USAGE; }
    else {
      const char * self = argv[0];
#if defined(BLR_WIN32)
      char * image = blr_win_image();
      if (image) self = image;
#endif
      rc = do_batch(!strcmp(verb, "e"), self, lev, r, level, jobs);
#if defined(BLR_WIN32)
      xfree(image);
#endif
    }
  }
  else if (strcmp(verb, "e") && strcmp(verb, "d")) {
    fprintf(stderr, "balrogg: unknown command '%s'\n", verb);
    usage(stderr);  rc = BLR_EXIT_USAGE;
  } else if (np == 2) rc = do_one(!strcmp(verb, "e"), in, out, level);
  else {
    fprintf(stderr, "balrogg: '%s' takes an input and an output\n", verb);
    usage(stderr);  rc = BLR_EXIT_USAGE;
  }
  yarg_destroy(r);
  return rc;
}
