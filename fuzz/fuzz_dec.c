#define FZ_TAG "dec"
#include "harness.h"
#include "fzcfg.h"

static int cfg;

static int dec(void) { FZ_JOB(FZ_OUT, (vb_unpack(FZ_IN, FZ_OUT), 0)); }

static int enc(void) {
  FZ_JOB(FZ_ARC, (vb_pack(FZ_OUT, FZ_ARC, cfg), 0));
}

static int dec2(void) { FZ_JOB(FZ_OUT2, (vb_unpack(FZ_ARC, FZ_OUT2), 0)); }

int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  if (size < 10 || size > (1u << 20)) return 0;
  fz_paths();
  cfg = fz_vb_cfg(data[0]);
  fz_put(FZ_IN, data + 1, size - 1);
  fz_unpack_trip(dec, enc, dec2);
  return 0;
}
