#include "sys/common.h"
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size);

typedef struct { char * path;  sz n; } fm_in;

static fm_in * ins;
static sz nin, capin, maxlen = 16384;
static uint64_t rs = 0x9e3779b97f4a7c15ULL;
static const char * cur;

static u32 rnd(u32 n) {
  rs ^= rs << 13;  rs ^= rs >> 7;  rs ^= rs << 17;
  return n ? (u32) ((rs >> 11) % n) : 0;
}

static void add(const char * path) {
  struct stat st;
  if (stat(path, &st)) return;
  if (S_ISDIR(st.st_mode)) {
    DIR * d = opendir(path);
    struct dirent * e;
    char p[4096];
    if (!d) return;
    while ((e = readdir(d)))
      if (e->d_name[0] != '.') {
        snprintf(p, sizeof p, "%s/%s", path, e->d_name);  add(p);
      }
    closedir(d);
    return;
  }
  if (!S_ISREG(st.st_mode) || !st.st_size ||
      (uintmax_t) st.st_size > (1u << 20))
    return;
  if (nin == capin)
    ins = xrealloc(ins, (capin = capin ? capin * 2 : 256) * sizeof *ins);
  ins[nin].path = xmalloc(strlen(path) + 1);  strcpy(ins[nin].path, path);
  ins[nin++].n = (sz) st.st_size;
}

static void run(const u8 * d, sz n) {
  if (cur) spew(cur, d, n);
  LLVMFuzzerTestOneInput(d, n);
}

static void mutate(u8 * b, sz * n, sz cap) {
  static const u8 magic[] = { 0, 1, 0x7f, 0x80, 0xff, 0xfe, 0x10, 0x40 };
  sz at = *n ? rnd((u32) *n) : 0, len, j;
  switch (rnd(9)) {
  case 0: if (*n) b[at] ^= (u8) (1u << rnd(8));  break;
  case 1: if (*n) b[at] = (u8) rnd(256);  break;
  case 2: if (*n) b[at] = magic[rnd(sizeof magic)];  break;
  case 3: if (*n) b[at] += (u8) (rnd(33) - 16);  break;
  case 4:
    len = 1 + rnd(1 + (u32) MIN(*n - at, (sz) 256));
    if (at + len <= *n) {
      memmove(b + at, b + at + len, *n - at - len);  *n -= len;
    }
    break;
  case 5:
    len = 1 + rnd(64);
    if (*n + len <= cap) {
      memmove(b + at + len, b + at, *n - at);
      Fj(len, b[at + j] = (u8) rnd(256));
      *n += len;
    }
    break;
  case 6: {
    sz from = *n ? rnd((u32) *n) : 0;
    len = 1 + rnd(1 + (u32) MIN(*n - from, (sz) 512));
    if (from + len <= *n && *n + len <= cap) {
      u8 t[512];
      memcpy(t, b + from, len);  memmove(b + at + len, b + at, *n - at);
      memcpy(b + at, t, len);  *n += len;
    }
    break; }
  case 7:
    *n = at;
    break;
  default: {
    sz k, m;
    u8 * o = slurp(ins[rnd((u32) nin)].path, &m);
    k = m ? rnd((u32) m) : 0;
    len = MIN(m - k, cap - at);
    len = len ? 1 + rnd((u32) MIN(len, (sz) 4096)) : 0;
    if (len && at + len <= cap) {
      memcpy(b + at, o + k, len);
      if (at + len > *n) *n = at + len;
    }
    xfree(o); }
  }
}

int main(int argc, char ** argv) {
  long mutants = -1, secs = 0, k;
  time_t t0 = time(NULL);
  sz i;
  int a, replay = 1;
  for (a = 1; a < argc; a++) {
    if (!strcmp(argv[a], "-n") && a + 1 < argc) mutants = atol(argv[++a]);
    else if (!strcmp(argv[a], "-t") && a + 1 < argc) secs = atol(argv[++a]);
    else if (!strcmp(argv[a], "-s") && a + 1 < argc)
      rs += (uint64_t) strtoull(argv[++a], NULL, 10) << 17;
    else if (!strcmp(argv[a], "-m") && a + 1 < argc)
      maxlen = (sz) atol(argv[++a]);
    else if (!strcmp(argv[a], "-c") && a + 1 < argc) cur = argv[++a];
    else if (!strcmp(argv[a], "-R")) replay = 0;
    else add(argv[a]);
  }
  if (!nin) { fprintf(stderr, "fzmain: no inputs\n");  return 2; }
  for (i = 0; replay && i < nin && (!secs || time(NULL) - t0 < secs); i++) {
    sz n;
    u8 * b = slurp(ins[i].path, &n);
    run(b, n);  xfree(b);
  }
  fprintf(stderr, "fzmain: replayed %lu inputs in %ld s\n", (unsigned long) i,
          (long) (time(NULL) - t0));
  if (!mutants) return 0;
  for (k = 0; mutants < 0 || k < mutants; k++) {
    sz n, m, cap = maxlen;
    u8 * o, * b;
    int r = 1 + (int) rnd(8);
    if (secs && time(NULL) - t0 >= secs) break;
    o = slurp(ins[rnd((u32) nin)].path, &m);  b = xmalloc(cap);
    n = MIN(m, cap);  memcpy(b, o, n);  xfree(o);
    while (r--) mutate(b, &n, cap);
    run(b, n);  xfree(b);
  }
  fprintf(stderr, "fzmain: %ld mutants in %ld s\n", k,
          (long) (time(NULL) - t0));
  return 0;
}
