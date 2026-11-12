#include "mp3/zhdr.h"

#define LSF_TREE_BITS 7
#define PARTITIONS    4

static u32 lsf_slen(const model * m, int ch, i32 sfc) {
  if ((hdr_mode_ext(m->frame.header) & HDR_MODE_EXT_IS) && ch == 1)
    return lsf_slen_is_tab[sfc >> 1];
  return lsf_slen_tab[sfc];
}

static const u8 * lsf_partitions(const granule * g, u32 slen) {
  int kind = g->block_type == BLOCK_SHORT ? (g->mixed_block == 0 ? 1 : 2) : 0;
  return &mp3_lsf_nr[((slen >> 12) & 7) * PARTITIONS + kind * 24];
}

void lsf_scf_model_code(model * m) {
  mp3_frame * f = &m->frame;
  int nch = hdr_channels(f->header), i, j;

  for (int ch = 0; ch < nch; ch++) {
    granule * g = &f->gr[0][ch];
    u16 * sf = f->scalefac[0][ch];
    u32 slen = lsf_slen(m, ch, g->scalefac_compress);
    const u8 * nr_sfb = lsf_partitions(g, slen);
    int k = 0;

    g->part2_length = 0;
    if (ch == 1 && m->zc.dup[0]) {
      memcpy(f->scalefac[0][1], f->scalefac[0][0], sizeof f->scalefac[0][0]);
      g->part2_length = f->gr[0][0].part2_length;
      memcpy(m->zc.scf_prev[1], m->zc.scf_prev[0], sizeof m->zc.scf_prev[0]);
      continue;
    }
    for (int part = 0; part < PARTITIONS; part++, slen >>= 3) {
      u32 sl = slen & 7;
      if (sl == 0) { Fi(nr_sfb[part], sf[k++] = 0);  continue; }
      for (i = 0; i < nr_sfb[part]; i++, k++) {
        if (m->zc.opt & MP3Z_SCF && !(m->zc.lsf_ready >> k & 1)) {
          COUNTERS_INIT(m->zc.lsf2[k]);
          m->zc.lsf_ready |= (u64) 1 << k;
        }
        if (m->zc.opt & MP3Z_SCF)
          sf[k] = (u16) tree_mix(&m->rc, m->lsf.tree[k + 1],
                                 m->zc.lsf2[k][m->zc.scf_prev[ch][k] & 15],
                                 &m->zc.lsfm[k], LSF_TREE_BITS, sf[k], 1 << sl,
                                 rates_w40, 64, rates_w40);
        else
          sf[k] = (u16) code_tree_lt(&m->rc, m->lsf.tree[k + 1], LSF_TREE_BITS,
                                     sf[k], 1 << sl, rates_w40, 64);
        g->part2_length += (i32) sl;
      }
    }
    Fj(40, m->zc.scf_prev[ch][j] = j < k ? (u8) sf[j] : 0);
  }
}

void lsf_model_init(model * m) {
  COUNTERS_INIT(m->lsf);
}
