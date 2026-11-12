#ifndef MP3_ZHDR_H
#define MP3_ZHDR_H

#include "mp3/zbase.h"
#include "mp3/zmodel.h"
#include "mp3/ztab.h"

#define Z_MAGIC 0x01050405u

typedef struct {
  model * model;
  ctr frame_flag;
  ctr literal[512];
  FILE * in, * out;
  i32 nframes;
  i32 lit_run;
  long lit_mid;
  i32 stored_nframes;
  long base;
} zctx;

int z_encoder_init(zctx * z, FILE * mp3, FILE * out);
int z_decoder_init(zctx * z, FILE * in, FILE * mp3);
int z_encoder_finish(zctx * z);
int z_decoder_finish(zctx * z);
void z_free(zctx * z);

i32 z_compress_frame(zctx * z);
i32 z_decompress_frame(zctx * z);

#endif
