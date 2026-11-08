#define FZ_TAG "cli"
#include "harness.h"

int blr_cli_main(int argc, char ** argv);

static char lev[8], jobs[8];
static char FZ_OUT3[1200], FZ_INB[1200], FZ_ORIG[1200];
static const char * args[4];

static int cli(void) {
  char * v[8];
  int n = 0, i;
  v[n++] = (char *) "balrogg";  v[n++] = lev;  v[n++] = jobs;
  for (i = 0; i < 4 && args[i]; i++) v[n++] = (char *) args[i];
  v[n] = NULL;
  return blr_cli_main(n, v);
}

static int run(const char * what, int must, const char * out, const char * a,
               const char * b, const char * c) {
  args[0] = a;  args[1] = b;  args[2] = c;  args[3] = NULL;
  return fz_step(what, must, cli, out);
}

int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  if (size < 3 || size > (1u << 20)) return 0;
  fz_paths();
  snprintf(FZ_OUT3, sizeof FZ_OUT3, "%s/out3", FZ_DIR);
  snprintf(FZ_INB, sizeof FZ_INB, "%s.blr", FZ_IN);
  snprintf(FZ_ORIG, sizeof FZ_ORIG, "%s/orig", FZ_DIR);
  snprintf(lev, sizeof lev, "-%d",
           data[0] < 224 ? data[0] % 16 + 1 : data[0] % 4 + 17);
  snprintf(jobs, sizeof jobs, "-j%d", data[1] & 4 ? 1 : 2);
  fz_put_ogg(data + 2, size - 2, data[1] & 2);
  if (!run("encode", 0, FZ_OUT, "e", FZ_IN, FZ_OUT) &&
      !run("decode", 1, FZ_OUT2, "d", FZ_OUT, FZ_OUT2))
    fz_same(FZ_IN, FZ_OUT2);
  remove(FZ_OUT);  remove(FZ_OUT2);
  if (data[1] & 1) {
    if (!run("decode the input", 0, FZ_OUT, "d", FZ_IN, FZ_OUT)
        && !run("encode the decoded file", 0, FZ_ARC, "e", FZ_OUT, FZ_ARC)
        && !run("decode its archive", 1, FZ_OUT3, "d", FZ_ARC, FZ_OUT3))
      fz_same(FZ_OUT, FZ_OUT3);
    remove(FZ_OUT);  remove(FZ_ARC);  remove(FZ_OUT3);
  }
  if (data[1] & 8) {
    fz_put_ogg(data + 2, size - 2, data[1] & 2);
    rename(FZ_IN, FZ_ORIG);
    fz_put_ogg(data + 2, size - 2, data[1] & 2);
    if (!run("batch encode", 0, FZ_INB, "-b", "e", FZ_IN) &&
        !run("batch decode", 1, FZ_IN, "-b", "d", FZ_INB))
      fz_same(FZ_ORIG, FZ_IN);
    remove(FZ_INB);  remove(FZ_ORIG);
  }
  return 0;
}
