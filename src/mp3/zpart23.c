#include "mp3/zhdr.h"

#define RATES rates_w64
#define BIAS  16

#define P23_BITS   12

static i32 line_at(const model * m, int gr, int ch, i32 k) {
  const mp3_frame * f = &m->frame;
  i32 flat = (gr * 2 + ch) * MP3_LINES + k;
  if (flat < 4 * MP3_LINES)
    return f->coef[flat / (2 * MP3_LINES)][flat / MP3_LINES % 2]
                  [flat % MP3_LINES];
  return flat == 4 * MP3_LINES ? (i16) f->crc_computed : 1;
}

static i32 count1_bits(const model * m, int gr, int ch, i32 from, i32 nz) {
  const granule * g = &m->frame.gr[gr][ch];
  i32 est = 0;
  for (i32 i = from; i < nz; i += 4) {
    u32 q = (u32) (line_at(m, gr, ch, i) & 3) << 6 |
            (u32) (line_at(m, gr, ch, i + 1) & 3) << 4 |
            (u32) (line_at(m, gr, ch, i + 2) & 3) << 2 |
            (u32) (line_at(m, gr, ch, i + 3) & 3);
    est += count1_codes[g->count1table_select][q] >> 10;
  }
  return est;
}

static i32 estimate_bits(const model * m, int gr, int ch) {
  const granule * g = &m->frame.gr[gr][ch];
  const i16 * coef = m->frame.coef[gr][ch];
  regions r = granule_regions(m->frame.header, g);
  i32 nz = MP3_LINES;

  while (nz > 0 && coef[nz - 1] == 0) nz--;
  return huff_bits(coef, 0, r.end0, g->table_select[0]) +
         huff_bits(coef, r.end0, r.end1, g->table_select[1]) +
         huff_bits(coef, r.end1, r.big_end, g->table_select[2]) +
         count1_bits(m, gr, ch, r.big_end, nz);
}

static i32 code_error(model * m, int gr, int ch, i32 err) {
  p23_model * p = &m->part23;
  rcoder * rc = &m->rc;
  int neg;
  u32 a;

  if (!code_bit(rc, &p->nonzero[gr][ch], err != 0, RATES, BIAS)) return 0;
  if (err > 0) err--;
  neg = err < 0;
  if (neg) err = -err;
  neg = code_bit(rc, &p->negative[gr][ch], neg, RATES, BIAS);
  a = code_tree_lt(rc, p->magnitude[gr][ch][neg], P23_BITS, (u32) err,
                   1 << P23_BITS, RATES, BIAS);
  if (neg) a = -a;
  if ((i32) a >= 0) a++;
  return (i32) a;
}

static void code_raw_lengths(model * m, int ngr, int nch) {
  mp3_frame * f = &m->frame;
  int i, j;
  Fi(ngr,
    Fj(nch,
      granule * g = &f->gr[i][j];
      g->raw_part2_3_length =
          (i32) rc_raw_bits(&m->rc, P23_BITS, (u32) g->raw_part2_3_length);
      g->part2_3_length = 0));
}

void part23_model_code(model * m) {
  mp3_frame * f = &m->frame;
  int ngr = hdr_granules(f->header), nch = hdr_channels(f->header), i, j;

  f->raw = code_bit(&m->rc, &m->part23.raw, f->raw != 0, RATES, BIAS);
  if (f->raw) {
    code_raw_lengths(m, ngr, nch);
    return;
  }

  Fi(ngr,
    Fj(nch,
      granule * g = &f->gr[i][j];
      i32 est = estimate_bits(m, i, j) + g->part2_length;
      g->part2_3_length = code_error(m, i, j, g->part2_3_length - est) + est));
}

void part23_model_init(model * m) {
  COUNTERS_INIT(m->part23);
}
