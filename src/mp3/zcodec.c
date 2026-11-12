#include "mp3/zhdr.h"

#define LITERAL_BITS 9
#define LITERAL_END  0x100

static int code_frame_flag(zctx * z, int not_frame) {
  return code_bit(&z->model->rc, &z->frame_flag, not_frame, rates_w28, 16);
}

static u32 code_literal(zctx * z, u32 sym) {
  return code_tree_lt(&z->model->rc, z->literal, LITERAL_BITS, sym,
                      LITERAL_END + 1, rates_w250, 0xa3);
}

i32 z_compress_frame(zctx * z) {
  model * m = z->model;
  i32 r = mp3_read_frame(m);

  for (;;) {
    if (!code_frame_flag(z, r != 0)) {
      model_code_frame(m);
      if (z->nframes++) z->lit_mid += z->lit_run;
      z->lit_run = 0;
      return 1;
    }

    anc_model_code_trailing(m);
    if (r == 1) return 0;

    while (r < 0) {
      code_literal(z, (u32) (r + 0x100));
      z->lit_run++;
      r = mp3_read_frame(m);
    }
    code_literal(z, LITERAL_END);
  }
}

i32 z_decompress_frame(zctx * z) {
  model * m = z->model;

  for (;;) {
    if (m->bad || m->rc.nbytes > m->rc_limit) { m->bad = 1;  return 0; }
    if (!code_frame_flag(z, 0)) {
      if (z->nframes >= z->stored_nframes) { m->bad = 1;  return 0; }
      model_code_frame(m);
      if (m->bad) return 0;
      mp3_write_frame(m);
      z->nframes++;
      return 1;
    }

    anc_model_code_trailing(m);
    mp3_flush_all(m);
    if (m->bad) return 0;
    if (m->out_remaining < 1) return 0;

    for (u32 v; (v = code_literal(z, 0)) != LITERAL_END; m->out_remaining--) {
      if (m->out_remaining < 1 || m->rc.nbytes > m->rc_limit) {
        m->bad = 1;  return 0;
      }
      bs_putc(&m->mp3_bs, (int) v);
    }
  }
}
