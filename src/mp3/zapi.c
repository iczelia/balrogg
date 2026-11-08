#include "mp3/zhdr.h"
#include "mp3/z.h"

static const u8 ZC_KSHIFT[4][3] = {
  { 5, 4, 3 }, { 5, 4, 0 }, { 5, 5, 3 }, { 5, 5, 4 }
};

static void zc_setup(zctx * z, unsigned opt) {
  zc_model * c = &z->model->zc;
  i32 n = z->model->file_size;
  int tier = n < 16384 ? 0 : n < 65536 ? 1 : n < 262144 ? 2 : 3;
  int kind = opt & MP3Z_SLIM ? 0 : opt & MP3Z_MIX4 ? 1 : opt & MP3Z_MIX ? 2 : 3;
  c->opt = opt;
  c->kshift = tier < 3 ? ZC_KSHIFT[kind][tier] : 0;
  c->nzshift = tier < 3 ? 2 : 0;
  if (opt & MP3Z_EXT) {
    COUNTERS_INIT(c->r0_2);  COUNTERS_INIT(c->r1_2);
  }
}

int mp3z_pack_stream(FILE * in, FILE * out, unsigned opt, long * junk) {
  zctx z;
  memset(&z, 0, sizeof z);
  tables_init();
  if (z_encoder_init(&z, in, out) || z.model->file_size == 0) {
    z_free(&z);  return 1;
  }
  zc_setup(&z, opt);
  while (z_compress_frame(&z)) {}
  *junk = z.nframes ? z.lit_mid + z.model->raw_bits / 8 : z.model->file_size;
  return z_encoder_finish(&z) ? 2 : 0;
}

int mp3z_unpack_stream(FILE * in, FILE * out, unsigned opt) {
  zctx z;
  memset(&z, 0, sizeof z);
  tables_init();
  if (z_decoder_init(&z, in, out)) { z_free(&z);  return 1; }
  zc_setup(&z, opt);
  while (z_decompress_frame(&z)) {}
  if (z.model->bad) { z_free(&z);  return 1; }
  return z_decoder_finish(&z) ? 2 : 0;
}
