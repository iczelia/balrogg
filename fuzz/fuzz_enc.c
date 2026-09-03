/*  Encode arbitrary Ogg bytes, then decode and compare accepted input.  */

#define FZ_TAG "enc"
#include "harness.h"
#include "codec.h"

int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  vb_opt o;
  if (size < 32 || size > (1u << 20)) return 0;
  fz_paths();
  vb_opt_default(&o);
  o.flags = (u8) ((o.flags & 0x1F) | (3 << 5));
  fz_put_ogg(data, size);
  if (!FZ_TRY(vb_pack(FZ_IN, FZ_OUT, &o))) return 0;
  if (!FZ_TRY(vb_unpack(FZ_OUT, FZ_OUT2)))
    { fprintf(stderr, "encode produced an archive we reject\n");  abort(); }
  fz_same(FZ_IN, FZ_OUT2);
  return 0;
}
