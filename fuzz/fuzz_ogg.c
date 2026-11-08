#define FZ_TAG "ogg"
#include "harness.h"
#include "fzcfg.h"

static int cfg;

static int enc(void) {
  FZ_JOB(FZ_OUT, (vb_pack(FZ_IN, FZ_OUT, cfg), 0));
}

static int dec(void) { FZ_JOB(FZ_OUT2, (vb_unpack(FZ_OUT, FZ_OUT2), 0)); }

int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  static fz_pkt pk[FZ_MAXPKT];
  fz_buf ob;
  sz n, i;
  if (size < 17 || size > (1u << 20)) return 0;
  n = fz_split(data + 1, size - 1, pk, FZ_MAXPKT);
  if (n < 3) return 0;
  ob.b = NULL;  ob.n = ob.cap = 0;
  Fi(n,
    fz_page(&ob, pk[i].p, pk[i].n,
            i == 0 ? 2 : i == n - 1 ? 4 : 0, 0xB1A0C0DEu, (u32) i,
            (u32) (i * 1024)));
  if (!ob.n) { xfree(ob.b);  return 0; }
  fz_fixcrc(ob.b, ob.n);
  fz_paths();
  cfg = fz_vb_cfg(data[0]);
  fz_put(FZ_IN, ob.b, ob.n);
  xfree(ob.b);
  fz_pack_trip(enc, dec);
  return 0;
}
