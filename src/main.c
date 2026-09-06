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

/*  Command line and batch execution.  */

#include "codec.h"
#include "archive.h"
#include "ogg.h"
#include "vorbis.h"
#include "yarg.h"
#include "opusmode.h"
#include "cpu.h"
#include "win32.h"

#include <errno.h>
#include <limits.h>
#ifdef BLR_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/stat.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

/*  The native Windows runtime calls blr_main from its own entry point.  */
#if defined(BLR_WIN_CRT)
int blr_main(int argc, char ** argv);
#define main blr_main
#endif

static void banner(FILE * to) {
  fprintf(to,
    "balrogg " PACKAGE_VERSION " -- demonically compacting OGG recompressor\n"
    "Written by Kamila Szewczyk <k@iczelia.net>.\n"
    "License GNU GPL version 3.\n");
}

static void usage(FILE * to) {
  banner(to);
  fprintf(to,
    "\n"
    "usage: balrogg [-1..-9] e IN OUT       compress Ogg to OUT\n"
    "       balrogg d IN OUT                expand archive to OUT\n"
    "       balrogg [-1..-9] -b e|d FILE... process files in parallel\n"
    "       balrogg dump IN                 print an archive's layout\n"
    "       balrogg pages IN                print an Ogg file's page framing\n"
    "       balrogg -h | -v\n"
    "\n"
    "  -1 .. -9      effort; -9 is the default\n"
    "  -b, --batch   process every remaining path\n"
    "  -j, --jobs=N  process N files at once\n"
    "  -p, --progress show tuning and codec progress on stderr\n"
    "  --progress-lines write progress as separate log lines\n"
    "  -h, --help    show help        -v, --version   show version\n"
    "\n"
    "Exit codes  0 success, 1 refused input, 2 usage, 3 I/O error,\n"
    "4 internal error.\n");
}

static void version(void) {
  banner(stdout);
  printf("Build host %s.\n", BLR_HOST_TRIPLE);
  printf("Copyright (C) 2022-2026 Kamila Szewczyk.\n"
         "This is free software: you are free to change and redistribute it.\n"
         "There is NO WARRANTY, to the extent permitted by law.\n");
  printf("Ogg Opus mode: %s\n", opus_mode_version());
  printf("SIMD: %s built, %s dispatched\n", blr_simd_built(),
         blr_simd_dispatched());
}

static void dump(const char * path) {
  archive a;
  sz i, len;
  blr_file * b = bf_open(path, 0);
  len = b->len;
  arc_read(&a, b);
  printf("%s: %lu bytes, flags 0x%02x (%lu slots, %s, level %d), %lu streams\n",
         path, (unsigned long) len, a.flags,
         (unsigned long) (ARC_BLOCK(a.flags) >> 10),
         ARC_SOLID(a.flags) ? "solid" : "non-solid", (int) ARC_LEVEL(a.flags),
         (unsigned long) a.n);
  if (a.flags & ARC_OPUS) printf("  Ogg Opus mode\n");
  if (a.ntune) {
    vb_tune t;
    vb_tune_get(&t, a.tune, a.ntune);
    printf("  tune: adapt cap %u, CM rate %u, flags 0x%02x%s%s\n",
           t.alim, t.lr, t.flags,
           (t.flags & VB_TF_CLS) ? " (class from the previous packet)" : "",
           (t.flags & VB_TF_MATCH) ? " (match model)" : "");
  }
  Fi(a.n, printf("  stream %lu: %lu bytes\n",
                 (unsigned long) i, (unsigned long) a.s[i].len));
  arc_free(&a);  bf_close(b);
}

/*  Print framing rebuilt from the parsed page.  */
static void pages(const char * path) {
  ogg_page p;
  sz len, at = 0, n = 0, got;
  blr_file * input = bf_open(path, 0);
  u8 * b = xmalloc(OGG_HDRMIN + OGG_MAXSEG + OGG_MAXSEG * OGG_MAXSEG);
  u8 * img = xmalloc(OGG_HDRMIN + OGG_MAXSEG + OGG_MAXSEG * OGG_MAXSEG);
  len = input->len;
  printf("%s: %lu bytes\n", path, (unsigned long) len);
  while (at < len) {
    const u8 * body;
    got = ogg_read(input, at, &p, b);
    FATAL_UNLESS(got != 0, "%s: no Ogg page at %lu", path, (unsigned long) at);
    body = b + OGG_HDRMIN + p.nseg;
    printf("  page %-5lu type %02x  granule %08lx%08lx  serial %08lx  seq %-6lu"
           "  %3d seg  %3d pkt  %6lu B  %s\n", (unsigned long) n, p.type,
           (unsigned long) p.ghi, (unsigned long) p.glo, (unsigned long) p.serial,
           (unsigned long) p.seq, p.nseg, p.np, (unsigned long) got,
           ogg_emit(&p, img, body) == got && !memcmp(img, b, got)
             ? "reframes" : "REFRAME MISMATCH");
    at += got;  n++;
  }
  free(img);  free(b);  bf_close(input);
}

/*  Read up to `max` bytes; return -1 if open fails.  */
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

enum { F_READ = -2, F_BAD = -1, F_UNKNOWN = 0, F_VORBIS = 1, F_OPUS = 2 };

/*  Detect a codec from the first page's identification header.  */
static int sniff(const char * path) {
  u8 b[512];
  int n = peek(path, b, (int) sizeof b), at;
  if (n < 0) return n;
  if (n < 28 || memcmp(b, "OggS", 4)) return F_UNKNOWN;
  at = 27 + b[26];
  if (at + 8 > n) return F_UNKNOWN;
  if (!memcmp(b + at, "\001vorbis", 7)) return F_VORBIS;
  if (!memcmp(b + at, "OpusHead", 8)) return F_OPUS;
  return F_UNKNOWN;
}

/*  Detect an Opus archive.  */
static int arc_is_opus(const char * path) {
  u8 b[ARC_HDRLEN];
  int n = peek(path, b, (int) sizeof b);
  return n >= (int) ARC_HDRLEN && !memcmp(b, ARC_MAGIC, ARC_MAGLEN)
         && b[ARC_MAGLEN] <= ARC_VER && (b[ARC_MAGLEN + 1] & ARC_OPUS);
}

/*  File identity and size without the C runtime's stat(), which the
    Windows 95 build lacks and which reports no inode on Windows anyway.  */
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
    if (GetFileInformationByHandle(ha, &x) && GetFileInformationByHandle(hb, &y))
      same = x.dwVolumeSerialNumber == y.dwVolumeSerialNumber
             && x.nFileIndexHigh == y.nFileIndexHigh
             && x.nFileIndexLow == y.nFileIndexLow;
    CloseHandle(hb);
  }
  CloseHandle(ha);
  return same;
}

/*  The size of a regular file, or zero.  */
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

/*  Levels 1-4 add models; higher levels expand the encoder search.  */

typedef struct { int vidx, odepth, search; } effort;

static const effort EFFORT[9] = {
  { 0, 0, 0 },   /*  -1  */
  { 1, 1, 0 },   /*  -2  */
  { 2, 2, 0 },   /*  -3  */
  { 3, 3, 0 },   /*  -4  */
  { 3, 4, 2 },   /*  -5  */
  { 3, 5, 4 },   /*  -6  */
  { 3, 6, 6 },   /*  -7  */
  { 3, 6, 8 },   /*  -8  */
  { 3, 6, 12 }   /*  -9  */
};


static int do_one(int enc, const char * in, const char * out,
                  const vb_opt * o, const effort * e) {
  /*  Refuse identical paths and hard links before opening the output.  */
  if (same_file(in, out)) {
    fprintf(stderr, "balrogg: %s is both input and output\n", in);
    return BLR_EXIT_IO;
  }
  if (!enc) {
    if (arc_is_opus(in)) return opus_unpack(in, out);
    vb_unpack(in, out);  return BLR_EXIT_OK;
  }
  switch (sniff(in)) {
  case F_OPUS:
    return opus_pack(in, out, e->odepth);
  case F_UNKNOWN:
    fprintf(stderr, "balrogg: %s is not Ogg Vorbis or Ogg Opus\n", in);
    return BLR_EXIT_REFUSED;
  case F_BAD:
    fprintf(stderr, "balrogg: cannot open %s\n", in);
    return BLR_EXIT_IO;
  case F_READ:
    fprintf(stderr, "balrogg: cannot read %s\n", in);
    return BLR_EXIT_IO;
  case F_VORBIS:
    vb_pack(in, out, o);  return BLR_EXIT_OK;
  }
  return BLR_EXIT_INTERNAL;
}

/*  Processes isolate mutable model state.  */

#define BLR_JOB_FIXED (72UL << 20)

static long blr_cores(void) {
#if defined(BLR_WIN32)
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  return si.dwNumberOfProcessors > 0 ? (long) si.dwNumberOfProcessors : 1;
#elif defined(BLR_DOS)
  return 1;
#elif defined(HAVE_SYSCONF) && defined(_SC_NPROCESSORS_ONLN)
  long n = sysconf(_SC_NPROCESSORS_ONLN);
  return n > 0 ? n : 1;
#else
  return 1;
#endif
}

/*  Available memory, including reclaimable cache. Zero if unknown.  */

static bytes_t blr_avail(void) {
#if defined(BLR_WIN_LEGACY)
  /*  GlobalMemoryStatusEx is NT-only; the 32-bit fields suffice here.  */
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
      if (!strncmp(line, "MemAvailable:", 13)) { kb = strtoul(line + 13, NULL, 10);  break; }
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
/*  DOS replaces the extension and uses .opu for decoded Opus.  */
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
  strcpy(s + stem, enc ? ".blr" : arc_is_opus(in) ? ".opu" : ".ogg");
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

/*  POSIX forks, Windows re-executes, and other hosts run serially.  */

typedef struct {
  const char * self;          /*  argv[0], for re-execution  */
  const char * lev;           /*  "-N"  */
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

/*  Quote an argument for the Windows C runtime.  */
static char * winquote(const char * s) {
  sz n = strlen(s), i, k = 0, bs;
  FATAL_UNLESS(n <= (SIZE_MAX - 3) / 2, "path is too long");
  char * q = xmalloc(2 * n + 3);
  q[k++] = '"';
  Fi(n,
    for (bs = 0; i < n && s[i] == '\\'; i++) bs++;
    if (i == n) { while (bs--) { q[k++] = '\\';  q[k++] = '\\'; }  break; }
    if (s[i] == '"') { while (bs--) { q[k++] = '\\';  q[k++] = '\\'; }  q[k++] = '\\'; }
    else while (bs--) q[k++] = '\\';
    q[k++] = s[i]);
  q[k++] = '"';  q[k] = 0;
  return q;
}

/*  Re-execute this image for one file.  */
static void pool_spawn(pool * p, const char * in, const char * out,
                       const vb_opt * o, const effort * e) {
  char * qin = winquote(in), * qout = winquote(out), * qself = winquote(p->self);
  char * cmd = xmalloc(strlen(qself) + strlen(qin) + strlen(qout) + 40);
  PROCESS_INFORMATION pi;
  BOOL ok;
  if (p->jobs > MAXIMUM_WAIT_OBJECTS) p->jobs = MAXIMUM_WAIT_OBJECTS;
  while (p->live >= p->jobs) pool_reap(p);
  sprintf(cmd, "%s %s %s -- %s %s %s", qself, p->lev,
          blr_progress_enabled ? "--progress-lines" : "",
          p->enc ? "e" : "d", qin, qout);
  ok = blr_win_spawn(p->self, cmd, &pi);
  free(qin);  free(qout);  free(qself);  free(cmd);
  if (!ok) {                    /*  cannot spawn: run it here instead  */
    pool_failed(p, do_one(p->enc, in, out, o, e));
    return;
  }
  CloseHandle(pi.hThread);
  p->h[p->live++] = pi.hProcess;
}

#elif defined(HAVE_FORK) && defined(HAVE_WAITPID) && !defined(BLR_DOS)

static void pool_reap(pool * p) {
  int st;
  pid_t child;
  do child = waitpid(-1, &st, 0); while (child < 0 && errno == EINTR);
  if (child < 0) FATAL_CODE(BLR_EXIT_IO, "batch: wait failed");
  p->live--;
  pool_failed(p, WIFEXITED(st) ? WEXITSTATUS(st) : BLR_EXIT_INTERNAL);
}

static void pool_spawn(pool * p, const char * in, const char * out,
                       const vb_opt * o, const effort * e) {
  pid_t c;
  while (p->live >= p->jobs) pool_reap(p);
  c = fork();
  if (c < 0) {                  /*  out of processes: run it here instead  */
    pool_failed(p, do_one(p->enc, in, out, o, e));
  } else if (!c) {
    int rc = do_one(p->enc, in, out, o, e);
    _exit(rc);
  } else p->live++;
}

#else

static void pool_reap(pool * p) { p->live = 0; }

static void pool_spawn(pool * p, const char * in, const char * out,
                       const vb_opt * o, const effort * e) {
  pool_failed(p, do_one(p->enc, in, out, o, e));
}

#endif

static int do_batch(int enc, const char * self, const char * lev,
                    yarg_result * r, const vb_opt * o, const effort * e,
                    int jobs) {
  int n = r->pos_argc - 1, i;
  bytes_t big = 0;
  bytes_t avail;
  job * q = xmalloc((sz) (n > 0 ? n : 1) * sizeof *q);
  pool p;
  Fi(n,
    q[i].name = r->pos_args[i + 1];
    q[i].order = i;  q[i].size = file_size(q[i].name);
    if (q[i].size > big) big = q[i].size);
  qsort(q, (sz) n, sizeof *q, by_size_desc);
  if (!jobs) {
    bytes_t per = big > (ULLONG_MAX - BLR_JOB_FIXED) / 3
                    ? ULLONG_MAX : BLR_JOB_FIXED + 3 * big;
    long by_mem = blr_cores();
    avail = blr_avail();
    if (avail && avail / per < (bytes_t) by_mem) by_mem = (long) (avail / per);
    if (by_mem < 1) by_mem = 1;
    jobs = (int) by_mem;
  }
  if (jobs < 1) jobs = 1;
  if (jobs > n) jobs = n;
  fprintf(stderr, "balrogg: %d file%s, %d job%s\n",
          n, n == 1 ? "" : "s", jobs, jobs == 1 ? "" : "s");
  p.self = self;  p.lev = lev;  p.enc = enc;  p.jobs = jobs;
  p.live = 0;  p.bad = 0;  p.status = BLR_EXIT_OK;
  Fi(n,
    char * out = out_name(enc, q[i].name);
    pool_spawn(&p, q[i].name, out, o, e);
    free(out));
  while (p.live > 0) pool_reap(&p);
  free(q);
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
  vb_opt o;
  yarg_options opt[] = {
    { 'h', no_argument, "help" },
    { 'v', no_argument, "version" },
    { 'b', no_argument, "batch" },
    { 'j', required_argument, "jobs" },
    { 'p', no_argument, "progress" },
    { 256, no_argument, "progress-lines" },
    { '1', no_argument, NULL }, { '2', no_argument, NULL },
    { '3', no_argument, NULL }, { '4', no_argument, NULL },
    { '5', no_argument, NULL }, { '6', no_argument, NULL },
    { '7', no_argument, NULL }, { '8', no_argument, NULL },
    { '9', no_argument, NULL },
    { 0, no_argument, NULL }
  };
  yarg_settings set;
  yarg_result * r;
  const effort * e;
  char lev[3];
  int i, level = 9, np, batch = 0, jobs = 0, rc = 0;

  set.dash_dash = true;
  set.style = YARG_STYLE_UNIX;

  /*  Batch children inherit this per-file cap.  */
  blr_memcap();

#ifdef BLR_PROFILE
  { const char * pd = getenv("BLR_PROF_DIR");
    if (pd) { prof_open(pd);  rc_hook_set(prof_hook, NULL);
              atexit(prof_close); } }
#endif

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
  np--;                                 /*  the verb is not a file  */

  e = EFFORT + (level - 1);
  lev[0] = '-';  lev[1] = (char) ('0' + level);  lev[2] = 0;
  vb_opt_default(&o);
  o.flags = (u8) ((o.flags & 0x1F) | (e->vidx << 5));
  o.search = e->search;

  /*  Free parsed arguments through one exit path.  */
  if (batch) {
    if (blr_progress_enabled) blr_progress_enabled = 2;
    if (np < 1 || (strcmp(verb, "e") && strcmp(verb, "d")))
      { usage(stderr);  rc = BLR_EXIT_USAGE; }
    else {
      const char * self = argv[0];
#if defined(BLR_WIN32)
      /*  argv[0] is whatever the shell typed; children need the image.  */
      char * image = blr_win_image();
      if (image) self = image;
#endif
      rc = do_batch(!strcmp(verb, "e"), self, lev, r, &o, e, jobs);
#if defined(BLR_WIN32)
      free(image);
#endif
    }
  }
  else if (!strcmp(verb, "dump") && np == 1) dump(in);
  else if (!strcmp(verb, "pages") && np == 1) pages(in);
  else if (!strcmp(verb, "e") && np == 2) rc = do_one(1, in, out, &o, e);
  else if (!strcmp(verb, "d") && np == 2) rc = do_one(0, in, out, &o, e);
  else if (strcmp(verb, "e") && strcmp(verb, "d") && strcmp(verb, "dump")
           && strcmp(verb, "pages")) {
    fprintf(stderr, "balrogg: unknown command '%s'\n", verb);
    usage(stderr);  rc = BLR_EXIT_USAGE;
  } else {
    fprintf(stderr, "balrogg: '%s' takes %s\n", verb,
            strcmp(verb, "e") && strcmp(verb, "d") ? "one file"
                                                   : "an input and an output");
    usage(stderr);  rc = BLR_EXIT_USAGE;
  }
  yarg_destroy(r);
  return rc;
}
