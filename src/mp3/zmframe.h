#ifndef MP3_ZMFRAME_H
#define MP3_ZMFRAME_H

#include "mp3/zrc.h"

typedef struct {
  ctr emphasis[4];
  ctr original[2];
  ctr copyright[2];
  ctr mode[4];
  ctr private_bit[2];
  ctr samplerate[4];
  ctr protection[2];
  ctr mpeg1[2];
  ctr not_mpeg25[2];
  ctr private_bits[32];

  ctr scfsi[2][2][2][4];
  ctr global_gain[256];
  ctr sfc_lsf[2][512];
  ctr window_switching[2][2];
  ctr preflag[16][2];
  ctr scalefac_scale[16][2][2];
  ctr count1table[2][2];
  ctr block_type[4];
  ctr mixed_block;
  ctr subblock_gain[3][8];

  runbit padding;
  runbit block_type_miss;
  u8 scfsi_prev[2][4];
  u8 scalefac_scale_prev[2][2];
  u8 block_switch;
} side_model;

typedef ctr scf_tree[16];

typedef struct {
  scf_tree short_[16][2][12];
  scf_tree mixed_long[2][8];
  scf_tree long_[4][16][2][21];
} scf_model;

typedef struct {
  ctr tree[39][128];
} lsf_model;

typedef struct {
  ctr raw;
  ctr nonzero[2][2];
  ctr negative[2][2];
  ctr magnitude[2][2][2][4096];
} p23_model;

typedef struct {
  ctr mispredicted;
  ctr value[512];
} mdb_model;

#endif
