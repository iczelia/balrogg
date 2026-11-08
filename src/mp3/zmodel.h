#ifndef MP3_ZMODEL_H
#define MP3_ZMODEL_H

#include "mp3/zframe.h"
#include "mp3/zrc.h"
#include "mp3/zmframe.h"
#include "mp3/zcoef.h"
#include "mp3/zc.h"

#define BITBUF_SIZE  0x8000
#define BITBUF_GUARD 8

typedef struct {
  u8 mem[BITBUF_GUARD + BITBUF_SIZE + BITBUF_GUARD];
  u32 top;
  i32 bitpos;
  i32 frame_bits;
} bitreader;

static INLINE u8 * br_buf(bitreader * in) { return in->mem + BITBUF_GUARD; }

typedef struct {
  u8 buf[BITBUF_SIZE + BITBUF_GUARD];
  i32 bitpos;
  i32 readpos;
} bitwriter;

#define SLOT_COUNT 512

typedef struct {
  u8 bytes[0x28];
  i32 len;
  i32 main_len;
} frame_slot;

typedef struct {
  mp3_frame frame;
  rcoder rc;

  FILE * mp3_in, * mp3_out;
  i32 reading;
  i32 file_size;
  i32 in_pos;
  i32 out_remaining;
  i32 in_resync;
  i32 raw_bits;
  i32 free_bytes;
  bitreader in;
  bitwriter out;
  frame_slot slots[SLOT_COUNT];
  u32 slot_head;
  u32 slot_tail;

  u8 anc[0x1000];
  i32 anc_bits;
  i32 main_end;
  i32 anc_bits_expected;
  i32 prev_main_bits;
  i32 anc_parity;
  ctr anc_bit[128][8][2][128];
  ctr anc_len[0x800];
  ctr anc_len_same;

  side_model side;
  scf_model scf;
  lsf_model lsf;
  p23_model part23;
  coef_model coef;
  mdb_model mdb;
  zc_model zc;
  i32 bad;
  u32 rc_limit;
  bstream mp3_bs, rc_bs;
} model;

static INLINE i32 model_frame_bytes(const model * m, u32 h) {
  return hdr_bitrate(h) ? mp3_frame_bytes(h)
                        : m->free_bytes + (i32) hdr_padding(h);
}

void mp3_reader_init(model * m, FILE * in);
void mp3_writer_init(model * m, FILE * out);
i32 mp3_read_frame(model * m);
void mp3_write_frame(model * m);
void mp3_flush_all(model * m);
int mp3_check_header(const model * m);
int mp3_check_spectrum(const model * m);
int mp3_check_layout(const model * m);
i32 huff_bits(const i16 * coef, i32 lo, i32 hi, int t);

void model_code_frame(model * m);
void side_model_init(model * m);
void side_model_code(model * m);
void scf_model_init(model * m);
void scf_model_code(model * m);
void lsf_model_init(model * m);
void lsf_scf_model_code(model * m);
void part23_model_init(model * m);
void part23_model_code(model * m);
void anc_model_init(model * m);
void anc_model_code_trailing(model * m);

int coef_model_init(model * m);
void coef_model_free(model * m);
void coef_model_code(model * m);

#endif
