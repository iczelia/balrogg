#include "mp3/zhdr.h"

#define SCF_BITS 4

#define LONG_BANDS        21
#define SHORT_BANDS       12
#define MIXED_LONG_BANDS  8
#define MIXED_FIRST_SHORT 3
#define SLEN1_LONG        11
#define SLEN1_SHORT       6

static int long_coded(const granule * g, int sfb) {
  return g->block_type != BLOCK_SHORT ||
         (g->mixed_block && sfb < MIXED_LONG_BANDS);
}

static u16 code_scf(model * m, ctr * tree, u32 slen, u16 v) {
  return (u16) code_tree_lt(&m->rc, tree, SCF_BITS, v,
                            (i32) (1u << slen), rates_w40, 0x80);
}

static void code_short(model * m, granule * g, u16 * sf, int ch) {
  scf_model * s = &m->scf;
  i32 sfc = g->scalefac_compress;
  u32 slen1 = (u32) mp3_slen1[sfc], slen2 = (u32) mp3_slen2[sfc];
  int first_short = 0, i, j;

  if (g->mixed_block) {
    Fi(MIXED_LONG_BANDS,
      sf[i] = code_scf(m, s->mixed_long[ch][i], slen1, sf[i]));
    g->part2_length += (i32) (slen1 * MIXED_LONG_BANDS);
    first_short = MIXED_FIRST_SHORT;
  }
  for (int sfb = first_short; sfb < SHORT_BANDS; sfb++) {
    u32 slen = sfb < SLEN1_SHORT ? slen1 : slen2;
    Fj(3,
      u16 * v = &sf[SCF_SHORT + sfb * 3 + j];
      *v = code_scf(m, s->short_[sfc][ch][sfb], slen, *v);
      g->part2_length += (i32) slen);
  }
}

static void code_long(model * m, int gr, int ch) {
  scf_model * s = &m->scf;
  mp3_frame * f = &m->frame;
  granule * g = &f->gr[gr][ch];
  u16 * sf = f->scalefac[gr][ch];
  i32 sfc = g->scalefac_compress;
  u32 slen1 = (u32) mp3_slen1[sfc], slen2 = (u32) mp3_slen2[sfc];
  u32 prev = 0;
  int i;

  Fi(4,
    int lo = mp3_scfsi_band[i], hi = mp3_scfsi_band[i + 1];
    if (gr == 1 && f->scfsi[ch][i]) {
      for (int sfb = lo; sfb < hi; sfb++)
        f->scalefac[1][ch][sfb] = long_coded(&f->gr[0][ch], sfb)
                                  ? f->scalefac[0][ch][sfb] : 0;
      continue;
    }
    for (int sfb = lo; sfb < hi; sfb++) {
      u32 slen = sfb < SLEN1_LONG ? slen1 : slen2;
      if (m->zc.opt & MP3Z_SCF && !(m->zc.scf_ready[ch] >> sfb & 1)) {
        COUNTERS_INIT(m->zc.scf2[ch][sfb]);
        m->zc.scf_ready[ch] |= 1u << sfb;
      }
      if (m->zc.opt & MP3Z_SCF)
        sf[sfb] = (u16) tree_mix(&m->rc, s->long_[slen & 3][prev][ch][sfb],
                                 m->zc.scf2[ch][sfb]
                                           [m->zc.scf_prev[ch][sfb] & 15],
                                 &m->zc.scfm[ch][sfb], SCF_BITS, sf[sfb],
                                 (i32) (1u << slen), rates_w40, 0x80,
                                 rates_w40);
      else
        sf[sfb] = code_scf(m, s->long_[slen & 3][prev][ch][sfb], slen,
                           sf[sfb]);
      prev = sf[sfb];
      g->part2_length += (i32) slen;
    });
}

void scf_model_code(model * m) {
  mp3_frame * f = &m->frame;
  int nch = hdr_channels(f->header), i, j, k;

  Fi(2,
    Fj(nch,
      granule * g = &f->gr[i][j];
      g->part2_length = 0;
      if (j == 1 && m->zc.dup[i]) {
        memcpy(f->scalefac[i][1], f->scalefac[i][0], sizeof f->scalefac[i][0]);
        g->part2_length = f->gr[i][0].part2_length;
        memcpy(m->zc.scf_prev[1], m->zc.scf_prev[0], sizeof m->zc.scf_prev[0]);
      } else if (g->block_type == BLOCK_SHORT)
        code_short(m, g, f->scalefac[i][j], j);
      else {
        code_long(m, i, j);
        Fk(LONG_BANDS, m->zc.scf_prev[j][k] = (u8) f->scalefac[i][j][k]);
      }));
}

void scf_model_init(model * m) {
  COUNTERS_INIT(m->scf);
  MIX_INIT(m->zc.scfm);  MIX_INIT(m->zc.lsfm);
}
