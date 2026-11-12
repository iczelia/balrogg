#include "mp3/zhdr.h"

#define RATES rates_w28
#define BIAS  16

#define ANC_LEN_BITS  11
#define ANC_MAX_BYTES 0x7a2
#define ANC_LAST_POS  127
#define ANC_HIST_MASK 0x7f

#define MDB_BITS 9
#define MDB_MAX  0x1ff

static void anc_code_length(model * m) {
  rcoder * rc = &m->rc;
  int whole_gap = m->anc_bits_expected == m->anc_bits && !m->in_resync;

  if (code_bit(rc, &m->anc_len_same, whole_gap, RATES, BIAS)) {
    m->anc_bits = m->anc_bits_expected;
  } else {
    u32 bytes = code_tree_lt(rc, m->anc_len, ANC_LEN_BITS,
                             (u32) (m->anc_bits / 8), ANC_MAX_BYTES, RATES,
                             BIAS);
    m->anc_bits = (i32) (bytes * 8 + ((u32) -m->prev_main_bits & 7));
  }
}

static void anc_code_bits(model * m) {
  int parity = m->anc_parity;
  u32 hist = 0;
  i32 i;

  Fi(m->anc_bits,
    int pos = MIN(i / 8, ANC_LAST_POS), b = i % 8, bit;
    u8 * byte = &m->anc[i / 8];
    ctr * c;
    if (!m->zc.anc_ready[pos]) {
      COUNTERS_INIT(m->anc_bit[pos]);
      m->zc.anc_ready[pos] = 1;
    }
    c = &m->anc_bit[pos][b][parity][hist];
    if (c->n == 1 && i >= 8) *c = m->anc_bit[pos - 1][b][parity][hist];
    bit = code_bit(&m->rc, c, *byte >> (7 - b) & 1, rates_w20, BIAS);
    *byte = (u8) ((*byte & ~(0x80u >> b)) | (u32) bit << (7 - b));
    hist = (hist * 2 + (u32) bit) & ANC_HIST_MASK);
}

static void anc_code(model * m) {
  if (m->prev_main_bits < 0) { m->anc_bits = -1;  return; }
  anc_code_length(m);
  if (m->anc_bits > 0) {
    anc_code_bits(m);
    m->anc_parity ^= m->anc_bits & 1;
  }
}

void anc_model_code_trailing(model * m) {
  anc_code(m);
  m->prev_main_bits = -1;  m->anc_parity = 0;
}

static i32 mdb_code(model * m) {
  rcoder * rc = &m->rc;
  mp3_frame * f = &m->frame;
  i32 gap = m->anc_bits_expected, mdb;

  if (gap == -1) mdb = 0;
  else mdb = (i32) ((u32) gap - (u32) m->anc_bits) / 8;
  if (code_bit(rc, &m->mdb.mispredicted, mdb != f->main_data_begin, RATES,
               BIAS))
    mdb = (i32) code_tree_lt(rc, m->mdb.value, MDB_BITS,
                             (u32) f->main_data_begin, 1 << MDB_BITS, RATES,
                             BIAS);
  f->main_data_begin = mdb;  f->main_data_begin_used = mdb;
  return mdb;
}

static int dup_allowed(const mp3_frame * f, int gr) {
  u32 h = f->header;
  const granule * a = &f->gr[gr][0], * b = &f->gr[gr][1];
  int i;
  if (hdr_channels(h) != 2 || (hdr_mode(h) == 1 && hdr_mode_ext(h) != 0))
    return 0;
  if (a->scalefac_compress != b->scalefac_compress ||
      a->window_switching != b->window_switching ||
      a->block_type != b->block_type || a->mixed_block != b->mixed_block ||
      (hdr_mpeg1(h) && a->preflag != b->preflag) ||
      a->scalefac_scale != b->scalefac_scale)
    return 0;
  if (gr == 1 && hdr_mpeg1(h))
    Fi(4, if (f->scfsi[0][i] != f->scfsi[1][i]) return 0);
  return 1;
}

static int dup_same(const mp3_frame * f, int gr) {
  const granule * a = &f->gr[gr][0], * b = &f->gr[gr][1];
  return !memcmp(f->scalefac[gr][0], f->scalefac[gr][1],
                 sizeof f->scalefac[gr][0]) &&
         !memcmp(f->coef[gr][0], f->coef[gr][1], sizeof f->coef[gr][0]) &&
         a->big_values == b->big_values &&
         a->region0_count == b->region0_count &&
         a->region1_count == b->region1_count &&
         !memcmp(a->table_select, b->table_select, sizeof a->table_select);
}

static void dup_code(model * m) {
  zc_model * z = &m->zc;
  int i;
  Fi(hdr_granules(m->frame.header),
    z->dup[i] = 0;
    if (!dup_allowed(&m->frame, i)) continue;
    z->dup[i] = code_bit(&m->rc, &z->dup_flag[z->dup_last],
                         !m->rc.decoding && dup_same(&m->frame, i),
                         rates_w40, 16);
    z->dup_last = z->dup[i]);
}

static i32 frame_gap(const model * m, i32 mdb) {
  u32 h = m->frame.header;
  i32 bytes = model_frame_bytes(m, h) - header_bytes(h) + mdb;
  return (i32) ((u32) bytes * 8 - (u32) part2_3_total(&m->frame));
}

static void clear_frame(mp3_frame * f) {
  memset(f, 0, offsetof(mp3_frame, crc_computed));
}

void model_code_frame(model * m) {
  mp3_frame * f = &m->frame;
  i32 mdb;

  if (m->rc.decoding) clear_frame(f);
  side_model_code(m);
  if (m->rc.decoding && !mp3_check_header(m)) { m->bad = 1;  return; }
  dup_code(m);
  if (hdr_mpeg1(f->header)) scf_model_code(m);
  else lsf_scf_model_code(m);

  coef_model_code(m);
  if (m->rc.decoding && !mp3_check_spectrum(m)) { m->bad = 1;  return; }
  part23_model_code(m);

  anc_code(m);
  m->prev_main_bits = part2_3_total(&m->frame);
  mdb = mdb_code(m);
  if (m->rc.decoding && !mp3_check_layout(m)) { m->bad = 1;  return; }
  m->anc_bits_expected = frame_gap(m, mdb);
}

void anc_model_init(model * m) {
  counters_init(m->anc_len, ARRAY_LEN(m->anc_len));
  counters_init(&m->anc_len_same, 1);
  COUNTERS_INIT(m->mdb);
  m->prev_main_bits = -1;
}
