#define FZ_TAG "enc"
#include "harness.h"
#include "fzcfg.h"

static int cfg;

static int enc(void) {
  FZ_JOB(FZ_OUT, (vb_pack(FZ_IN, FZ_OUT, cfg), 0));
}

static int dec(void) { FZ_JOB(FZ_OUT2, (vb_unpack(FZ_OUT, FZ_OUT2), 0)); }

int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  if (size < 34 || size > (1u << 20)) return 0;
  fz_paths();
  cfg = fz_vb_cfg(data[0]);
  fz_put_ogg(data + 2, size - 2, data[1] & 1);
  fz_pack_trip(enc, dec);
  return 0;
}
