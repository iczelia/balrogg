#ifndef BLR_FZCFG_H
#define BLR_FZCFG_H

#include "vorbis/vorbis.h"
#include "opus/opus.h"
#include "mp3/mp3.h"

static int fz_vb_acm(int i) { return vb_config_acm(vb_config(i)); }

static int fz_pick(u8 sel, int cfg0, int n, int (*acm)(int)) {
  int cheap = 0, i, k;
  Fi(n, cheap += !acm(cfg0 + i));
  if (!(sel & 3) || !cheap) return cfg0 + (sel >> 2) % n;
  k = (sel >> 2) % cheap;
  Fi(n, if (!acm(cfg0 + i) && !k--) return cfg0 + i);
  return cfg0;
}

static int fz_vb_cfg(u8 sel) {
  return vb_config(fz_pick(sel, 0, vb_nconfig(), fz_vb_acm));
}
static int fz_opus_acm(int i) { return opus_config_acm(opus_config_id(i)); }
static int fz_opus_cfg(u8 sel) {
  return opus_config_id(fz_pick(sel, 0, opus_nconfig(), fz_opus_acm));
}
static int fz_mp3_cfg(u8 sel) {
  int n = 0, i, k;
  Fi(100, n += mp3_config(MP_CFG0 + i));
  k = sel % n;
  Fi(100, if (mp3_config(MP_CFG0 + i) && !k--) return MP_CFG0 + i);
  return MP_CFG0;
}

#endif
