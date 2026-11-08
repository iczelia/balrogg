#define FZ_TAG "mp3"
#include "harness.h"
#include "fzcfg.h"

static int cfg;

static int enc(void) { FZ_JOB(FZ_OUT, mp3_pack(FZ_IN, FZ_OUT, cfg)); }
static int dec(void) { FZ_JOB(FZ_OUT2, mp3_unpack(FZ_OUT, FZ_OUT2)); }

int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  if (size < 2 || size > (1u << 20)) return 0;
  fz_paths();
  cfg = fz_mp3_cfg(data[0]);
  fz_put(FZ_IN, data + 1, size - 1);
  fz_pack_trip(enc, dec);
  return 0;
}
