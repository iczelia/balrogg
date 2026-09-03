/*  Decode arbitrary bytes as an archive, exercising allocation and index
    bounds produced by modeled values.  */

#define FZ_TAG "dec"
#include "harness.h"
#include "codec.h"

int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  if (size < 9 || size > (1u << 20)) return 0;
  fz_paths();
  fz_put(FZ_IN, data, size);
  FZ_TRY(vb_unpack(FZ_IN, FZ_OUT));
  return 0;
}
