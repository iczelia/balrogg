/*  Fuzz both Ogg Opus directions, selected by the first input byte.  */

#define FZ_TAG "opus"
#include "harness.h"
#include "opusmode.h"

int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  volatile int rc = -1;                 /*  survives the longjmp  */
  int enc;
  if (size < 33 || size > (1u << 19)) return 0;
  enc = data[0] & 1;
  data++;  size--;
  fz_paths();
  if (!enc) {
    fz_put(FZ_IN, data, size);
    FZ_TRY(opus_unpack(FZ_IN, FZ_OUT));
    return 0;
  }
  fz_put_ogg(data, size);
  if (!FZ_TRY(rc = opus_pack(FZ_IN, FZ_OUT, 6)) || rc) return 0;
  if (!FZ_TRY(rc = opus_unpack(FZ_OUT, FZ_OUT2)) || rc)
    { fprintf(stderr, "opus encode produced an archive we reject\n");  abort(); }
  fz_same(FZ_IN, FZ_OUT2);
  return 0;
}
