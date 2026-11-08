#define FZ_TAG "dmg"
#include "harness.h"

int blr_cli_main(int argc, char ** argv);

static char lev[8];
static char FZ_BAD[1200];
static const char * args[3];

static int cli(void) {
  char * v[8];
  int n = 0, i;
  v[n++] = (char *) "balrogg";  v[n++] = lev;  v[n++] = (char *) "-j1";
  Fi(3, v[n++] = (char *) args[i]);
  v[n] = NULL;
  return blr_cli_main(n, v);
}

static int run(const char * what, int must, const char * verb, const char * x,
               const char * y) {
  args[0] = verb;  args[1] = x;  args[2] = y;
  return fz_step(what, must, cli, y);
}

static void damage(u8 * b, sz * n, sz cap, const u8 * d) {
  sz at = *n ? ((sz) d[1] << 8 | d[2]) * *n >> 16 : 0;
  sz k = (sz) d[3] % 64 + 1, i;
  if (!*n) return;
  switch (d[0] % 8) {
  case 0: b[at] ^= (u8) (1 << (d[3] & 7));  break;
  case 1: b[at] = d[3];  break;
  case 2: *n = at;  break;
  case 3:
    k = MIN(k, *n - at);  memmove(b + at, b + at + k, *n - at - k);  *n -= k;
    break;
  case 4:
    if (*n < cap) {
      memmove(b + at + 1, b + at, *n - at);  b[at] = d[3];  (*n)++;
    }
    break;
  case 5: for (i = 0; i < k && at + i < *n; i++) b[at + i] = 0;  break;
  case 6:
    k = MIN(k, MIN(*n - at, cap - *n));
    memmove(b + at + k, b + at, *n - at);  *n += k;
    break;
  default:
    for (i = 0; i < k && at + i < *n; i++) b[at + i] ^= (u8) (d[3] * (i + 1));
    break;
  }
}

int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  sz n, m, cap;
  u8 * a, * b;
  if (size < 11 || size > (1u << 20)) return 0;
  fz_paths();
  snprintf(FZ_BAD, sizeof FZ_BAD, "%s/bad", FZ_DIR);
  snprintf(lev, sizeof lev, "-%d",
           data[0] < 240 ? data[0] % 16 + 1 : data[0] % 4 + 17);
  fz_put(FZ_IN, data + 9, size - 9);
  if (run("encode", 0, "e", FZ_IN, FZ_ARC)) return 0;
  a = fz_get(FZ_ARC, &n);
  cap = n + 128;
  b = xmalloc(cap);
  memcpy(b, a, n);  m = n;
  damage(b, &m, cap, data + 1);
  damage(b, &m, cap, data + 5);
  if (m != n || memcmp(a, b, n)) {
    fz_put(FZ_BAD, b, m);
    if (!run("decode the damaged archive", 0, "d", FZ_BAD, FZ_OUT))
      fz_same(FZ_IN, FZ_OUT);
  }
  xfree(a);  xfree(b);
  remove(FZ_ARC);  remove(FZ_BAD);  remove(FZ_OUT);
  return 0;
}
