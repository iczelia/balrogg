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

#include "t_harness.h"
#include "rc.h"
#include <stdarg.h>

#ifdef HAVE_DIRENT_H
#include <dirent.h>
#include <sys/stat.h>
#endif
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#if defined(BLR_DOS)
#include <fcntl.h>
#include <process.h>
#include <unistd.h>
#endif

int xt_level = 1;
int xt_tracing;
unsigned long xt_checks, xt_failures;
const char * xt_section = "";
static FILE * report;
const char * xt_data;
const char * xt_regress;
const char * xt_corpus;
const char * xt_binary;

static const char * env(const char * name) {
  const char * v = getenv(name);
  return v && *v ? v : NULL;
}

void xt_init(void) {
  const char * v = env("BLR_TEST_LEVEL");
  if (v) {
    if (!strcmp(v, "quick")) xt_level = 1;
    else if (!strcmp(v, "full")) xt_level = 4;
    else if (!strcmp(v, "torture")) xt_level = 16;
  }
  v = env("BLR_TEST_TRACE");
  xt_tracing = v && strcmp(v, "0");
  xt_data = env("BLR_TEST_DATA");
  xt_regress = env("BLR_TEST_REGRESS");
  xt_corpus = env("BLR_TEST_CORPUS");
  xt_binary = env("BLR");
  /*  All layers share the adaptation table.  */
  rc_adapt_init();
}

int xt_open_report(const char * path) {
#ifdef BLR_DOS
  /* The guest has no visible console. Keep fatal library diagnostics too. */
  report = freopen(path, "w", stderr);
#else
  report = fopen(path, "w");
#endif
  return report != NULL;
}

void xt_close_report(void) {
  if (report) fclose(report);
  report = NULL;
}

void xt_section_begin(const char * name) {
  xt_section = name;
  if (xt_tracing) fprintf(stderr, "  == %s\n", name);
  if (report) { fprintf(report, "== %s\n", name);  fflush(report); }
}

void xt_trace(const char * fmt, ...) {
  va_list ap;
  if (!xt_tracing) return;
  va_start(ap, fmt);
  fputs("     ", stderr);  vfprintf(stderr, fmt, ap);  fputc('\n', stderr);
  va_end(ap);
}

void xt_report(int ok, const char * fmt, ...) {
  va_list ap;
  xt_checks++;
  if (ok) return;
  xt_failures++;
  fprintf(stderr, "FAIL [%s] ", xt_section);
  va_start(ap, fmt);  vfprintf(stderr, fmt, ap);  va_end(ap);
  fputc('\n', stderr);
  if (report && report != stderr) {
    fprintf(report, "FAIL [%s] ", xt_section);
    va_start(ap, fmt);  vfprintf(report, fmt, ap);  va_end(ap);
    fputc('\n', report);  fflush(report);
  }
}

int xt_finish(const char * program) {
  fprintf(stderr, "%s: %lu checks, %lu failed\n", program, xt_checks,
          xt_failures);
  if (report && report != stderr)
    fprintf(report, "%s: %lu checks, %lu failed\n", program, xt_checks,
            xt_failures);
  xt_close_report();
  return xt_failures ? 1 : 0;
}


/*  Tracked fixtures let hosts without dirent.h run the quick suite.  */
static const char * const VORBIS[] = {
  "tiny.ogg", "short.ogg", "silence.ogg", "tone48k.ogg", "tone_stereo.ogg",
  "noise_pink.ogg", "noise_white.ogg", "sweep.ogg", "vibrato.ogg",
  "mix_lowq.ogg", "lowfreq22k.ogg", "smallchain.ogg", "chain3.ogg",
  "eq_mono8k.ogg", "eq_st11k.ogg", "lowbr.ogg", NULL
};

static const char * const OPUS[] = {
  "silence_st_64k.opus", "silk_speech_12k.opus", "silk_mono_16k.opus",
  "hyb_mono_40k.opus", "celt_st_96k_60ms.opus", "celt_st_128k.opus", NULL
};

typedef struct { char ** v;  int n, cap; } strlist;

static void push(strlist * l, const char * s) {
  if (l->n + 1 >= l->cap) {
    l->cap = l->cap ? 2 * l->cap : 32;
    l->v = xrealloc(l->v, (sz) l->cap * sizeof *l->v);
  }
  l->v[l->n] = xmalloc(strlen(s) + 1);
  strcpy(l->v[l->n++], s);
  l->v[l->n] = NULL;
}

static int ends_with(const char * s, const char * ext) {
  sz n = strlen(s), m = strlen(ext);
  return n >= m && !strcmp(s + n - m, ext);
}

#ifdef HAVE_DIRENT_H
static int by_name(const void * a, const void * b) {
  return strcmp(*(char * const *) a, *(char * const *) b);
}

/*  Find matching files up to two levels deep and sort them.  */
static void scan(strlist * l, const char * dir, const char * ext, int depth) {
  DIR * d = opendir(dir);
  struct dirent * e;
  char path[4096];
  int first = l->n;
  if (!d) return;
  while ((e = readdir(d)) != NULL) {
    struct stat st;
    if (e->d_name[0] == '.') continue;
    if (snprintf(path, sizeof path, "%s/%s", dir, e->d_name) >= (int) sizeof path)
      continue;
    if (stat(path, &st)) continue;
    if (S_ISDIR(st.st_mode)) { if (depth > 0) scan(l, path, ext, depth - 1); }
    else if (S_ISREG(st.st_mode) && ends_with(e->d_name, ext)) push(l, path);
  }
  closedir(d);
  if (l->n > first) qsort(l->v + first, (sz) (l->n - first), sizeof *l->v, by_name);
}
#endif

void xt_dos_name(const char * name, char * out, sz cap) {
  char stem[256];
  const char * dot = strrchr(name, '.'), * p;
  sz n = 0, m;
  for (p = name; *p && p != dot && n < sizeof stem - 1; p++)
    if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9'))
      stem[n++] = *p;
  stem[n] = 0;
  if (n > 8) { memmove(stem + 5, stem + n - 3, 3);  stem[8] = 0; }
  m = dot ? strlen(dot + 1) : 0;
  if (m > 3) m = 3;
  FATAL_UNLESS(strlen(stem) + m + 2 <= cap, "t_suite: fixture name overflow");
  strcpy(out, stem);
  if (dot) { strcat(out, ".");  strncat(out, dot + 1, m); }
}

const char * xt_fixture(const char * dir, const char * name) {
  static char buf[4][4096];
  static int next;
  char * s = buf[next++ & 3];
#if defined(BLR_DOS)
  char dos[32];
  xt_dos_name(name, dos, sizeof dos);
  name = dos;
#endif
  if (snprintf(s, sizeof buf[0], "%s/%s", dir, name) >= (int) sizeof buf[0])
    FATAL_CODE(BLR_EXIT_INTERNAL, "t_suite: fixture path is too long");
  return s;
}

char ** xt_files(const char * ext) {
  strlist l;
  const char * const * fx = !strcmp(ext, ".ogg") ? VORBIS : OPUS;
  l.v = NULL;  l.n = l.cap = 0;
  if (xt_data)
    for (; *fx; fx++) push(&l, xt_fixture(xt_data, *fx));
#ifdef HAVE_DIRENT_H
  if (xt_level > 1 && xt_corpus) scan(&l, xt_corpus, ext, 2);
#endif
  if (!l.v) { l.v = xmalloc(sizeof *l.v);  l.v[0] = NULL; }
  return l.v;
}

void xt_files_free(char ** list) {
  char ** p;
  for (p = list; *p; p++) free(*p);
  free(list);
}

int xt_files_count(char ** list) {
  int n = 0;
  while (list[n]) n++;
  return n;
}

const char * xt_basename(const char * path) {
  const char * s = strrchr(path, '/');
#ifdef BLR_WIN32
  const char * b = strrchr(path, '\\');
  if (b && (!s || b > s)) s = b;
#endif
  return s ? s + 1 : path;
}

const char * xt_tmp(const char * tag) {
  static char buf[48][128], tags[48][32];
  static int used;
  int i;
  Fi(used, if (!strcmp(tags[i], tag)) return buf[i]);
  if (used == 48) FATAL_CODE(BLR_EXIT_INTERNAL, "t_suite: too many scratch names");
  FATAL_UNLESS(strlen(tag) < sizeof tags[0], "t_suite: scratch tag is too long");
  strcpy(tags[used], tag);
#if defined(BLR_DOS)
  { char dos[32];
    const char * ext;
    xt_dos_name(tag, dos, sizeof dos);
    ext = strrchr(dos, '.');
    /* Reserve the prefix inside the eight-character stem. The slot keeps
       different tags distinct even when their shortened 8.3 names collide. */
    snprintf(buf[used], sizeof buf[used], "ts%02d%s", used, ext ? ext : ""); }
#else
  snprintf(buf[used], sizeof buf[used], "t_suite-%s.tmp", tag);
#endif
  return buf[used++];
}

const char * xt_batch_name(int enc, const char * in) {
  static char buf[2][4096];
  static int next;
  char * s = buf[next++ & 1];
  sz n = strlen(in);
#if defined(BLR_DOS)
  const char * dot = strrchr(in, '.');
  sz stem = dot ? (sz) (dot - in) : n;
  memcpy(s, in, stem);
  strcpy(s + stem, enc ? ".blr" : ".ogg");
#else
  if (enc) sprintf(s, "%s.blr", in);
  else {
    strcpy(s, in);
    if (n > 4 && !strcmp(s + n - 4, ".blr")) s[n - 4] = 0;
    else strcat(s, ".out");
  }
#endif
  return s;
}

void xt_unlink(const char * path) { remove(path); }

int xt_same_file(const char * a, const char * b) {
  sz an, bn;
  u8 * x, * y;
  int eq;
  FILE * f = fopen(a, "rb");
  if (!f) return 0;
  fclose(f);
  f = fopen(b, "rb");
  if (!f) return 0;
  fclose(f);
  x = slurp(a, &an);  y = slurp(b, &bn);
  eq = an == bn && !memcmp(x, y, an);
  free(x);  free(y);
  return eq;
}

long xt_file_size(const char * path) {
  long n;
  FILE * f = fopen(path, "rb");
  if (!f) return -1;
  fseek(f, 0, SEEK_END);  n = ftell(f);  fclose(f);
  return n;
}

#if defined(BLR_DOS)
static int split_args(char * s, char ** argv, int cap) {
  int n = 0;
  while (*s) {
    char * w;
    while (*s == ' ') s++;
    if (!*s) break;
    if (n + 1 >= cap) return -1;
    argv[n++] = w = s;
    while (*s && *s != ' ') {
      if (*s == '"') { s++;  while (*s && *s != '"') *w++ = *s++;  if (*s) s++; }
      else *w++ = *s++;
    }
    if (*s) s++;
    *w = 0;
  }
  argv[n] = NULL;
  return n;
}

int xt_run(const char * args, const char * out) {
  char buf[16384], * argv[256];
  int fd, save_out, save_err, rc = -1;
  if (!xt_binary) return -1;
  if (strlen(args) >= sizeof buf) return -1;
  strcpy(buf, args);
  argv[0] = (char *) xt_binary;
  if (split_args(buf, argv + 1, 255) < 0) return -1;
  xt_trace("run: %s %s", xt_binary, args);
  fflush(stdout);  fflush(stderr);
  fd = open(out ? out : "NUL", O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0600);
  if (fd < 0) return -1;
  save_out = dup(1);  save_err = dup(2);
  if (save_out >= 0 && save_err >= 0 && dup2(fd, 1) >= 0 && dup2(fd, 2) >= 0)
    rc = spawnv(P_WAIT, xt_binary, argv);
  if (save_out >= 0) { dup2(save_out, 1);  close(save_out); }
  if (save_err >= 0) { dup2(save_err, 2);  close(save_err); }
  close(fd);
  return rc;
}
#else

/*  Quote for sh, with an outer pair required by cmd.exe on Windows.  */
int xt_run(const char * args, const char * out) {
  char cmd[16384];
  int st, n;
  if (!xt_binary) return -1;
#ifdef BLR_WIN32
  if (out) n = snprintf(cmd, sizeof cmd, "\"\"%s\" %s > \"%s\" 2>&1\"", xt_binary, args, out);
  else     n = snprintf(cmd, sizeof cmd, "\"\"%s\" %s\"", xt_binary, args);
#else
  if (out) n = snprintf(cmd, sizeof cmd, "\"%s\" %s > \"%s\" 2>&1", xt_binary, args, out);
  else     n = snprintf(cmd, sizeof cmd, "\"%s\" %s", xt_binary, args);
#endif
  if (n < 0 || n >= (int) sizeof cmd) return -1;
  xt_trace("run: %s", cmd);
  fflush(stdout);  fflush(stderr);
  st = system(cmd);
  if (st == -1) return -1;
#if defined(WIFEXITED) && defined(WEXITSTATUS)
  if (WIFEXITED(st)) return WEXITSTATUS(st);
  return 128;                           /*  a signal, as the shell reports it  */
#else
  return st;
#endif
}

#endif

int xt_file_contains(const char * path, const char * needle) {
  sz n, m = strlen(needle), i;
  u8 * b;
  FILE * f = fopen(path, "rb");
  if (!f) return 0;
  fclose(f);
  b = slurp(path, &n);
  for (i = 0; i + m <= n; i++)
    if (!memcmp(b + i, needle, m)) { free(b);  return 1; }
  free(b);
  return 0;
}

int xt_file_before(const char * path, const char * first, const char * second) {
  u8 * b;
  char * a, * z;
  sz n;
  int before;
  FILE * f = fopen(path, "rb");
  if (!f) return 0;
  fclose(f);
  b = slurp(path, &n);  b = xrealloc(b, n + 1);  b[n] = 0;
  a = strstr((char *) b, first);  z = strstr((char *) b, second);
  before = a && z && a < z;
  free(b);
  return before;
}

void xt_seed(xt_rng * r, u32 seed) { r->s = seed ? seed : 20260810UL; }

u32 xt_next(xt_rng * r, u32 n) {
  r->s = r->s * 1664525UL + 1013904223UL;
  return (r->s >> 8) % n;
}
