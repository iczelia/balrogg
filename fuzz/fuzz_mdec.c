#define FZ_TAG "mdec"
#include "harness.h"
#include "fzcfg.h"

static int cfg;

static int dec(void) { FZ_JOB(FZ_OUT, mp3_unpack(FZ_IN, FZ_OUT)); }
static int enc(void) { FZ_JOB(FZ_ARC, mp3_pack(FZ_OUT, FZ_ARC, cfg)); }
static int dec2(void) { FZ_JOB(FZ_OUT2, mp3_unpack(FZ_ARC, FZ_OUT2)); }

int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  if (size < 10 || size > (1u << 20)) return 0;
  fz_paths();
  cfg = fz_mp3_cfg(data[0]);
  fz_put(FZ_IN, data + 1, size - 1);
  fz_unpack_trip(dec, enc, dec2);
  return 0;
}
