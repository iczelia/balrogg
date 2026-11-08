#include "mp3/zlayer.h"

#define MAX_BIG_VALUES  (MP3_LINES / 2)

u32 mp3_crc(u32 h, const u8 * side, int side_len) {
  u32 c = mp3_crc8(mp3_crc8(0xffff, h >> 8 & 0xff), h & 0xff);
  while (side_len-- > 0) c = mp3_crc8(c, side[side_len]);
  return c;
}

static void code_frame_fields(model * m, int mpeg1, int nch) {
  mp3_frame * f = &m->frame;
  int i, j;
  code_field(m, mpeg1 ? 9 : 8, &f->main_data_begin);
  code_field(m, mpeg1 ? (nch == 2 ? 3 : 5) : (nch == 2 ? 2 : 1),
             &f->private_bits);
  if (mpeg1)
    Fi(nch, Fj(4, code_field(m, 1, &f->scfsi[i][j])));
}

static int code_regions(model * m, granule * g) {
  int i;
  g->block_type = BLOCK_LONG;  g->mixed_block = 0;
  Fi(3, code_field(m, 5, &g->table_select[i]));
  code_field(m, 4, &g->region0_count);
  code_field(m, 3, &g->region1_count);
  return g->region0_count + 2 + g->region1_count > MP3_LONG_BANDS;
}

static void code_window_switching(model * m, granule * g, int mpeg1) {
  int i;
  code_field(m, 2, &g->block_type);
  code_field(m, 1, &g->mixed_block);
  Fi(2, code_field(m, 5, &g->table_select[i]));
  g->table_select[2] = 0;
  Fi(3, code_field(m, 3, &g->subblock_gain[i]));
  g->region0_count = !mpeg1 && g->block_type == BLOCK_SHORT ? 5 : 7;
  g->region1_count = 22;
}

static int code_granule(model * m, granule * g, int mpeg1) {
  int bad = 0, i;
  code_field(m, 12, m->frame.raw ? &g->raw_part2_3_length : &g->part2_3_length);

  if (code_field(m, 9, &g->big_values) > MAX_BIG_VALUES) bad = 1;
  code_field(m, 8, &g->global_gain);
  code_field(m, mpeg1 ? 4 : 9, &g->scalefac_compress);
  if (code_field(m, 1, &g->window_switching) == 0) {
    if (code_regions(m, g)) bad = 1;
  } else {
    code_window_switching(m, g, mpeg1);
  }
  Fi(3, if (g->table_select[i] == 4 || g->table_select[i] == 14) bad = 1);
  if (mpeg1) code_field(m, 1, &g->preflag);
  code_field(m, 1, &g->scalefac_scale);
  code_field(m, 1, &g->count1table_select);
  return bad;
}

int mp3_code_side_info(model * m) {
  mp3_frame * f = &m->frame;
  u32 h = f->header;
  int mpeg1 = hdr_mpeg1(h), nch = hdr_channels(h);
  int bad = 0, i, j;

  code_frame_fields(m, mpeg1, nch);
  Fi(hdr_granules(h),
    Fj(nch, if (code_granule(m, &f->gr[i][j], mpeg1)) bad = 1));
  return bad;
}
