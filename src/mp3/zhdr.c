#include "mp3/zhdr.h"

static int model_create(zctx * z) {
  model * m = z->model = xcalloc(1, sizeof *m);
  side_model_init(m);  scf_model_init(m);  lsf_model_init(m);
  part23_model_init(m);  anc_model_init(m);

  counters_init(&z->frame_flag, 1);
  counters_init(z->literal, ARRAY_LEN(z->literal));
  return coef_model_init(m);
}

void z_free(zctx * z) {
  if (!z->model) return;
  coef_model_free(z->model);
  xfree(z->model);  z->model = NULL;
}

int z_encoder_init(zctx * z, FILE * mp3, FILE * out) {
  z->in = mp3;  z->out = out;
  z->nframes = z->stored_nframes = 0;
  z->base = ftell(out);
  if (z->base < 0 || model_create(z)) return 1;
  mp3_reader_init(z->model, mp3);
  if (!write_le32(out, Z_MAGIC) || !write_le32(out, (u32) z->model->file_size)
      || !write_le32(out, 0) || !write_le32(out, 0))
    return 1;
  bs_open(&z->model->rc_bs, out, 1);
  rc_init_encoder(&z->model->rc, &z->model->rc_bs);
  return 0;
}

int z_decoder_init(zctx * z, FILE * in, FILE * mp3) {
  u32 magic, size, nframes, flags;
  long at = ftell(in), end = -1;
  z->in = in;  z->out = mp3;  z->nframes = 0;
  if (at >= 0 && !fseek(in, 0, SEEK_END)) end = ftell(in);
  if (at < 0 || fseek(in, at, SEEK_SET)) return 1;
  if (!read_le32(in, &magic) || magic != Z_MAGIC || !read_le32(in, &size) ||
      !read_le32(in, &nframes) || !read_le32(in, &flags) || flags ||
      (i32) size <= 0)
    return 1;
  z->stored_nframes = (i32) nframes;
  if (model_create(z)) return 1;
  z->model->rc_limit = end < at || end - at > 0x7fffffffL
                       ? 0xffffffffu : (u32) (end - at) + 64;
  z->model->file_size = (i32) size;
  mp3_writer_init(z->model, mp3);
  bs_open(&z->model->rc_bs, in, 0);
  rc_init_decoder(&z->model->rc, &z->model->rc_bs);
  return 0;
}

int z_encoder_finish(zctx * z) {
  model * m = z->model;
  int c;
  rc_flush(&m->rc);
  while ((c = bs_getc(&m->mp3_bs)) != EOF) bs_putc(&m->rc_bs, c);
  if (bs_flush(&m->rc_bs) || fseek(z->out, z->base + 8, SEEK_SET) ||
      !write_le32(z->out, (u32) z->nframes))
    return 1;
  z_free(z);
  return 0;
}

int z_decoder_finish(zctx * z) {
  model * m = z->model;
  int c;
  while ((c = bs_getc(&m->rc_bs)) != EOF) bs_putc(&m->mp3_bs, c);
  if (bs_flush(&m->mp3_bs)) return 1;
  z_free(z);
  return 0;
}
