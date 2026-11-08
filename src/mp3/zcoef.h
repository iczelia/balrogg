#ifndef MP3_ZCOEF_H
#define MP3_ZCOEF_H

#include "mp3/zrc.h"

typedef struct {
  u16 count;
  u16 prob;
} sse_node;

#define COEF_CLASSES 8
#define NZ_CTX       13
#define NZ_NODES     800

typedef struct coef_sub coef_sub;

typedef struct {
  ctr region0[2][16];
  ctr region1[16][2][8];
  ctr table_select[32][3][32];

  ctr nz1_tree[COEF_CLASSES][NZ_CTX][NZ_NODES];
  sse_node nz1_sse[COEF_CLASSES][NZ_NODES][4][6];
  u8 nz1_hist[COEF_CLASSES][NZ_NODES];
  ctr nz_tree[COEF_CLASSES][NZ_CTX][NZ_NODES];
  sse_node nz_sse[COEF_CLASSES][NZ_NODES][4][6];
  u8 nz_hist[COEF_CLASSES][NZ_NODES];

  ctr bv_escape;
  ctr bv_tree[512];
  ctr bv_is_est[2];

  i32 prev_nz[COEF_CLASSES];
  i32 prev_nz1[COEF_CLASSES];

  coef_sub * sub[COEF_CLASSES];
  u8 sfb_of_line[576];
} coef_model;

#endif
