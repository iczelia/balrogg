#include "mp3/zlayer.h"

static int fetch_byte(model * m, int zero_at_eof) {
  bitreader * in = &m->in;
  int c = -1;
  if (m->in_pos < m->file_size) {
    m->in_pos++;
    c = bs_getc(&m->mp3_bs);
  }
  if (c == -1) {
    if (!zero_at_eof) return 1;
    c = 0;
  }
  if (in->top == 0) {
    memcpy(br_buf(in) + BITBUF_HALF, br_buf(in), BITBUF_HALF);
    in->top = BITBUF_HALF;
  }
  br_buf(in)[--in->top] = (u8) c;
  in->frame_bits += 8;  in->bitpos += 8;
  return 0;
}

static void fetch_bytes(model * m, i32 n) {
  bitreader * in = &m->in;
  bstream * s = &m->mp3_bs;
  i32 i;
  while (n > 0) {
    i32 k = MIN(n, MIN((i32) (s->e - s->p), m->file_size - m->in_pos));
    if (k <= 0 || in->top == 0) { fetch_byte(m, 1);  n--;  continue; }
    k = MIN(k, (i32) in->top);
    u8 * d = br_buf(in) + in->top;
    Fi(k, d[-1 - i] = s->p[i]);
    s->p += k;  m->in_pos += k;  in->top -= (u32) k;
    in->frame_bits += 8 * k;  in->bitpos += 8 * k;  n -= k;
  }
}

static void unfetch(model * m, i32 k) {
  bitreader * in = &m->in;
  bstream * s = &m->mp3_bs;
  if (k <= 0) return;
  in->top += (u32) k;  in->bitpos -= k * 8;  in->frame_bits -= k * 8;
  if (m->main_end >= 0) m->main_end -= k * 8;
  m->in_pos -= k;
  if (s->p - s->b >= k) s->p -= k;
  else {
    long at = bs_tell(s) - k;
    fseek(s->f, at, SEEK_SET);
    s->base = at;  s->p = s->e = s->b;
  }
}

static int need_bits(model * m, i32 nbits) {
  while (m->in.bitpos < nbits)
    if (fetch_byte(m, 0)) return 1;
  return 0;
}

static void save_ancillary(model * m, const u8 * base, i32 to) {
  i32 from = m->main_end;
  if (from < 0) return;
  m->anc_bits = from - to;
  for (i32 i = 0; i * 8 <= from - to; i++) {
    i32 pos = from - 8 - i * 8;
    m->anc[i] = (u8) (rd32le(base + (pos >> 3)) >> (pos & 7));
  }
}

#define FREE_SAME_MASK 0xfffefc00u

static i32 free_frame_bytes(model * m, u32 h, long at, i32 min_bytes) {
  u8 b[FREE_MAX_BYTES + 8];
  i32 pad = (i32) hdr_padding(h), last = m->free_bytes, n, d;
  long here = m->mp3_bs.base + (long) (m->mp3_bs.e - m->mp3_bs.b);
  if (last < min_bytes) last = 0;
  if (fseek(m->mp3_in, at, SEEK_SET)) return 0;
  n = (i32) fread(b, 1, sizeof b, m->mp3_in);
  fseek(m->mp3_in, here, SEEK_SET);
  if (last && last + pad + 4 <= n &&
      (rd32be(b + last + pad) & FREE_SAME_MASK) == (h & FREE_SAME_MASK))
    return last;
  for (d = min_bytes; d + 4 <= n && d <= FREE_MAX_BYTES + pad; d++)
    if ((rd32be(b + d) & FREE_SAME_MASK) == (h & FREE_SAME_MASK) &&
        d - pad >= min_bytes)
      return d - pad;
  return last;
}

static i32 skip_byte(model * m, i32 start_bits) {
  bitreader * in = &m->in;
  save_ancillary(m, br_base(in) + in->frame_bits / 8, start_bits);
  m->main_end = -1;
  in->frame_bits -= 8;
  in->bitpos = in->frame_bits;
  if (in->frame_bits < 0) return 1;
  m->in_resync = 1;
  return (i32) br_base(in)[in->frame_bits / 8] - 0x100;
}

static int read_header_side_info(model * m) {
  bitreader * in = &m->in;
  mp3_frame * f = &m->frame;

  if (need_bits(m, MP3_HEADER_BYTES * 8)) return 0;
  long at = (long) m->in_pos - in->bitpos / 8;
  in->bitpos -= MP3_HEADER_BYTES * 8;
  u32 h = f->header = rd32le(br_base(in) + in->bitpos / 8);
  if (!mp3_header_valid(h)) return 0;

  int crc_len = hdr_no_crc(h) ? 0 : MP3_CRC_BYTES;
  int side_len = mp3_side_info_bytes(h);
  if (hdr_bitrate(h) == 0) {
    i32 n = free_frame_bytes(m, h, at, MP3_HEADER_BYTES + crc_len + side_len);
    if (n == 0) return 0;
    m->free_bytes = n;
  }
  if (need_bits(m, (crc_len + side_len) * 8)) return 0;
  if (crc_len != 0) {
    u32 crc = br_read(in, crc_len * 8);
    f->crc_computed =
        mp3_crc(h, br_base(in) + in->bitpos / 8 - side_len, side_len);
    if (crc != f->crc_computed) return 0;
  }
  f->raw = 0;
  if (mp3_code_side_info(m)) return 0;
  return MP3_HEADER_BYTES + crc_len + side_len;
}

static void drop_header(bitreader * in, int hdr_len) {
  memmove(br_base(in) + hdr_len, br_base(in), (size_t) (in->bitpos / 8));
  in->top += hdr_len;
}

static void make_raw(mp3_frame * f) {
  int i, j;
  f->raw = 1;
  f->main_data_begin_used = 0;
  Fi(2,
    Fj(2,
      f->gr[i][j].raw_part2_3_length = f->gr[i][j].part2_3_length;
      f->gr[i][j].part2_3_length = 0));
}

static i32 read_main_data(model * m, i32 main_bytes) {
  bitreader * in = &m->in;
  mp3_frame * f = &m->frame;

  f->main_data_begin_used = m->in_resync ? 0 : f->main_data_begin;
  i32 slot_start = in->bitpos;
  i32 main_start = in->bitpos += f->main_data_begin_used * 8;
  memset(f->coef, 0, sizeof f->coef);

  int missing = f->main_data_begin >= 1 && m->in_resync;
  int too_long = (main_bytes + f->main_data_begin_used) * 8 < part2_3_total(f);
  int overlaps = m->main_end >= 0 && main_start > m->main_end;
  if (!missing && !too_long && !overlaps && mp3_code_main_data(m) == 0)
    return main_start;
  m->raw_bits += part2_3_total(f);
  make_raw(f);
  in->bitpos = slot_start;
  return slot_start;
}

i32 mp3_read_frame(model * m) {
  bitreader * in = &m->in;
  i32 start_bits = in->bitpos;

  int hdr_len = read_header_side_info(m);
  if (hdr_len == 0) return skip_byte(m, start_bits);

  i32 main_bytes = model_frame_bytes(m, m->frame.header) - hdr_len;
  drop_header(in, hdr_len);
  unfetch(m, in->bitpos / 8 - main_bytes);
  if (in->bitpos < main_bytes * 8)
    fetch_bytes(m, (main_bytes * 8 - in->bitpos + 7) / 8);
  if (m->main_end >= 0) m->main_end += main_bytes * 8;

  i32 main_start = read_main_data(m, main_bytes);
  save_ancillary(m, br_base(in), main_start);
  m->main_end = in->bitpos;
  in->bitpos = 0;  in->frame_bits = 0;
  m->in_resync = 0;
  return 0;
}

void mp3_reader_init(model * m, FILE * in) {
  m->mp3_in = in;
  long pos = ftell(in);
  fseek(in, 0, SEEK_END);
  m->file_size = (i32) ftell(in);
  m->in_pos = (i32) pos;
  fseek(in, pos, SEEK_SET);
  bs_open(&m->mp3_bs, in, 0);

  memset(m->in.mem, 0, sizeof m->in.mem);
  m->in.top = BITBUF_SIZE;
  m->in.bitpos = 0;  m->in.frame_bits = 0;
  m->anc_bits_expected = -1;  m->anc_bits = -1;  m->main_end = -1;
  m->in_resync = 1;
  m->reading = 1;
}
