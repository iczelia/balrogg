/*  Build valid Ogg framing around arbitrary packet lists. Packets 0 through 2
    are identification, comment, and setup headers. Later packets are audio.  */

#define FZ_TAG "ogg"
#include "harness.h"
#include "codec.h"

int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  fz_pkt pk[FZ_MAXPKT];
  fz_buf ob;
  vb_opt o;
  sz n, i;
  if (size < 16 || size > (1u << 20)) return 0;
  n = fz_split(data, size, pk, FZ_MAXPKT);
  if (n < 3) return 0;
  ob.b = NULL;  ob.n = ob.cap = 0;
  for (i = 0; i < n; i++)
    fz_page(&ob, pk[i].p, pk[i].n,
            i == 0 ? 2 : i == n - 1 ? 4 : 0, 0xB1A0C0DEu, (u32) i,
            (u32) (i * 1024));
  if (!ob.n) { free(ob.b);  return 0; }
  fz_fixcrc(ob.b, ob.n);
  fz_paths();
  vb_opt_default(&o);
  o.flags = (u8) ((o.flags & 0x1F) | (3 << 5));
  fz_put(FZ_IN, ob.b, ob.n);
  if (FZ_TRY(vb_pack(FZ_IN, FZ_OUT, &o))) {
    if (!FZ_TRY(vb_unpack(FZ_OUT, FZ_OUT2)))
      { fprintf(stderr, "encode produced an archive we reject\n");  abort(); }
    fz_same(FZ_IN, FZ_OUT2);
  }
  free(ob.b);
  return 0;
}
