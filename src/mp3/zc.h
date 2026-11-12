#ifndef MP3_ZC_H
#define MP3_ZC_H

#include "mp3/zrc.h"
#include "mp3/z.h"
#include "coder/acm.h"

#define ZC_GG_MID 8
#define ZC_GG_ESC 17

typedef struct {
  i32 w[3];
} mix2;

#define MIX_LR   6
#define ZC_BIAS2 16

static INLINE void mix_init(mix2 * x, sz count) {
  sz i;
  Fi(count, x[i].w[0] = 65536;  x[i].w[1] = 0;  x[i].w[2] = 0);
}

#define MIX_INIT(arr) mix_init((mix2 *) (arr), sizeof(arr) / sizeof(mix2))

static INLINE int mix_code(rcoder * rc, u32 p0, const ctr * c2, mix2 * x,
                           int bit) {
  i32 x0 = acm_str[(0x8000 - p0) >> 3], x1 = acm_str[(0x8000 - c2->p) >> 3];
  i32 dot = (i32) (((i64) x->w[0] * x0 + (i64) x->w[1] * x1 +
                    (i64) x->w[2] * 256) >> 16);
  i32 pm = acm_squash(MIN(dot, 2047)), pc0 = 0x8000 - pm * 8;
  bit = rc_bit(rc, (u32) CLAMP(pc0, 1, 0x7fff), bit);
  i32 err = ((bit ? 4095 : 0) - pm) * MIX_LR;
  x->w[0] += (x0 * err) >> 14;
  x->w[1] += (x1 * err) >> 14;
  x->w[2] += (256 * err) >> 14;
  return bit;
}

#define ZC_MIXN 4

typedef struct {
  i32 w[ZC_MIXN + 2];
} mixn;

static INLINE void mixn_init(mixn * x, sz count) {
  sz i;
  Fi(count,
    x[i].w[0] = 65536;
    for (int j = 1; j < ZC_MIXN + 2; j++) x[i].w[j] = 0);
}

#define MIXN_INIT(arr) mixn_init((mixn *) (arr), sizeof(arr) / sizeof(mixn))

static INLINE int mixn_code(rcoder * rc, u32 p0, ctr * const * c, int nc,
                            mixn * x, int bit) {
  i32 in[ZC_MIXN + 2];
  i64 dot = 0;
  int i;
  in[0] = acm_str[(0x8000 - p0) >> 3];
  Fi(nc, in[i + 1] = acm_str[(0x8000 - c[i]->p) >> 3]);
  in[nc + 1] = 256;
  Fi(nc + 2, dot += (i64) x->w[i] * in[i]);
  i32 pm = acm_squash(MIN((i32) (dot >> 16), 2047)), pc0 = 0x8000 - pm * 8;
  bit = rc_bit(rc, (u32) CLAMP(pc0, 1, 0x7fff), bit);
  i32 err = ((bit ? 4095 : 0) - pm) * MIX_LR;
  Fi(nc + 2, x->w[i] += (in[i] * err) >> 14);
  return bit;
}

static INLINE u32 tree_mix(rcoder * rc, ctr * nodes, ctr * c2, mix2 * x,
                           int nbits, u32 val, i32 hi, rate_tab rates,
                           u32 bias, rate_tab rates2) {
  u32 v = 0;
  for (int i = nbits - 1; i >= 0; i--) {
    int bit = tree_forced_bit(v, i, -1, hi);
    if (bit < 0) {
      u32 node = (v + (1u << nbits)) >> (i + 1);
      bit = mix_code(rc, nodes[node].p, &c2[node], x, ((i32) val >> i) & 1);
      counter_update(&nodes[node], bit, rates, bias);
      counter_update(&c2[node], bit, rates2, ZC_BIAS2);
    }
    v |= (u32) bit << i;
  }
  return v;
}


typedef struct {
  u32 opt;
  u8 sub_ready[COEF_CLASSES];
  u8 sse_ready[COEF_CLASSES];
  u8 nz_ready[COEF_CLASSES];
  u8 anc_ready[128];
  u64 ext_ready[2][2];
  u64 lsf_ready;
  u32 scf_ready[2];
  int kshift;
  int nzshift;

  ctr gg_tree[2][5][2][32];
  i32 gg_prev[2];
  i32 gg_diff[2];
  i32 ws_prev[2];

  ctr bitrate[16][16];
  ctr mode_ext[4][4];
  ctr sfc[2][16][16];
  u32 hdr_prev;
  i32 sfc_prev[2];

  ctr free_same;
  i32 free_last;

  ctr scf2[2][21][16][16];
  mix2 scfm[2][21];
  ctr lsf2[40][16][128];
  mix2 lsfm[40];
  u8 scf_prev[2][40];

  ctr dup_flag[2];
  int dup[2];
  int dup_last;
  i32 gg_old0;

  ctr nz1_2[2][37][1024];
  ctr nz_2[2][37][1024];
  mix2 nzm[2][10];
  i32 nz1_prev[2], nz_prev[2];
  ctr r0_2[23][16];
  ctr r1_2[23][16][8];
  mix2 rm[2];
} zc_model;

#endif
