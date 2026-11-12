#include "mp3/zlayer.h"

#define MAX_LAG    0x2000
#define OUT_START  0x7c00
#define CLEAR_SPAN 0x800

#define RING_MASK (SLOT_COUNT - 1u)

static void emit_bytes(model * m, const u8 * p, i32 n, int down) {
  bstream * s = &m->mp3_bs;
  if (m->out_remaining >= 0) n = MIN(n, m->out_remaining);
  m->out_remaining -= n;
  while (n > 0) {
    if (s->p == s->e) bs_flush(s);
    i32 k = MIN(n, (i32) (s->e - s->p));
    if (down) for (i32 i = 0; i < k; i++) s->p[i] = p[-i];
    else memcpy(s->p, p, (size_t) k);
    s->p += k;  p += down ? -k : k;  n -= k;
  }
}

static void emit_frame(model * m) {
  frame_slot * slot = &m->slots[m->slot_tail];
  emit_bytes(m, slot->bytes, slot->len, 0);
  emit_bytes(m, m->out.buf + m->out.readpos, slot->main_len, 1);
  m->out.readpos -= slot->main_len;
  m->slot_tail = (m->slot_tail + 1) & RING_MASK;
}

static void put_ancillary(model * m, i32 end) {
  bitwriter * out = &m->out;
  if (end + MAX(m->anc_bits, 0) > (out->readpos + 1) * 8) {
    m->bad = 1;  return;
  }
  out->bitpos = end + MAX(m->anc_bits, 0);
  for (i32 i = 0; out->bitpos > end; i++) {
    int n = (int) MIN(out->bitpos - end, 8);
    bw_put(out, n, (u32) m->anc[i] >> (8 - n));
  }
}

void mp3_flush_all(model * m) {
  if (m->slot_head != m->slot_tail) put_ancillary(m, m->out.bitpos);
  while (m->slot_head != m->slot_tail && !m->bad) emit_frame(m);
}

static void clear_ahead(bitwriter * out, i32 frame_start) {
  memset(out->buf + frame_start / 8 - 1 - CLEAR_SPAN, 0, CLEAR_SPAN + 1);
}

static int put_header_side_info(model * m, i32 frame_start) {
  bitwriter * out = &m->out;
  u32 h = m->frame.header;

  out->bitpos = frame_start - MP3_HEADER_BYTES * 8;
  wr32le(out->buf + out->bitpos / 8, h);
  int hdr_len = MP3_HEADER_BYTES;
  if (!hdr_no_crc(h)) {
    out->bitpos -= MP3_CRC_BYTES * 8;
    hdr_len += MP3_CRC_BYTES;
  }
  mp3_code_side_info(m);
  int side_len = mp3_side_info_bytes(h);
  if (!hdr_no_crc(h)) {
    u8 * p = out->buf + out->bitpos / 8;
    u32 crc = mp3_crc(h, p, side_len);
    wr16le(p + side_len, crc);
  }
  return hdr_len + side_len;
}

static frame_slot * queue_header(model * m, int hdr_len) {
  frame_slot * slot = &m->slots[m->slot_head];
  u8 * p = m->out.buf + m->out.bitpos / 8;
  int i;
  Fi(hdr_len,
    slot->bytes[hdr_len - 1 - i] = p[i];
    p[i] = 0);
  slot->len = hdr_len;
  slot->main_len = model_frame_bytes(m, m->frame.header) - hdr_len;
  m->slot_head = (m->slot_head + 1) & RING_MASK;
  return slot;
}

static void put_main_data(model * m, i32 frame_start) {
  mp3_frame * f = &m->frame;
  put_ancillary(m, frame_start + (f->raw ? 0 : f->main_data_begin_used * 8));
  if (!f->raw && !m->bad) mp3_code_main_data(m);
}

static void emit_complete_frames(model * m) {
  bitwriter * out = &m->out;
  for (;;) {
    u32 head = m->slot_head, tail = m->slot_tail;
    int full = ((head + 1) & RING_MASK) == tail;
    int lagging = tail != head && out->readpos + 1 > out->bitpos / 8 + MAX_LAG;
    if (!full && !lagging) break;
    emit_frame(m);
  }
}

static void recentre(bitwriter * out) {
  if (out->readpos < (i32) BITBUF_HALF) {
    memcpy(out->buf + BITBUF_HALF, out->buf, BITBUF_HALF);
    out->bitpos += (i32) BITBUF_HALF * 8;
    out->readpos += (i32) BITBUF_HALF;
  }
}

void mp3_write_frame(model * m) {
  bitwriter * out = &m->out;
  i32 frame_start = out->bitpos;

  clear_ahead(out, frame_start);
  frame_slot * slot = queue_header(m, put_header_side_info(m, frame_start));
  put_main_data(m, frame_start);
  out->bitpos = frame_start - slot->main_len * 8;
  emit_complete_frames(m);
  recentre(out);
}

int mp3_check_header(const model * m) {
  u32 h = m->frame.header;
  if (!mp3_header_valid(h) ||
      (!hdr_bitrate(h) && m->free_bytes > FREE_MAX_BYTES))
    return 0;
  return model_frame_bytes(m, h) >= header_bytes(h);
}

static i32 table_max(int t) {
  if (t == 0) return 0;
  return mp3_huff_dims[t][2] ? 15 + (1 << mp3_huff_dims[t][2]) - 1
                         : mp3_huff_dims[t][0] - 1;
}

int mp3_check_spectrum(const model * m) {
  const mp3_frame * f = &m->frame;
  int j, k;
  for (int gr = 0; gr < hdr_granules(f->header); gr++)
    Fj(hdr_channels(f->header),
      const granule * g = &f->gr[gr][j];
      const i16 * co = f->coef[gr][j];
      regions r = granule_regions(f->header, g);
      const i32 bound[4] = {0, r.end0, r.end1, r.big_end};
      if (!g->window_switching &&
          g->region0_count + 2 + g->region1_count > MP3_LONG_BANDS)
        return 0;
      Fk(3,
        int t = g->table_select[k];
        i32 mx = table_max(t);
        if (t == 4 || t == 14) return 0;
        for (i32 i = bound[k]; i < bound[k + 1]; i++)
          if (co[i] > mx || co[i] < -mx) return 0);
      for (i32 i = r.big_end; i < MP3_LINES; i++)
        if (co[i] > 1 || co[i] < -1) return 0);
  return 1;
}

int mp3_check_layout(const model * m) {
  const mp3_frame * f = &m->frame;
  u32 h = f->header;
  i32 total = 0;
  int i, j;
  if (!hdr_mpeg1(h) && f->main_data_begin > 0xff) return 0;
  if (f->raw) return 1;
  Fi(hdr_granules(h),
    Fj(hdr_channels(h),
      i32 n = f->gr[i][j].part2_3_length;
      if (n < 0 || n > 0xfff) return 0;
      total += n));
  return total <= (model_frame_bytes(m, h) - header_bytes(h) +
                   f->main_data_begin_used) * 8;
}

void mp3_writer_init(model * m, FILE * out) {
  m->mp3_out = out;
  bs_open(&m->mp3_bs, out, 1);
  m->out_remaining = m->file_size;
  memset(m->out.buf, 0, sizeof m->out.buf);
  m->out.bitpos = OUT_START * 8;
  m->out.readpos = OUT_START - 1;
  m->anc_bits_expected = -1;
  m->slot_head = m->slot_tail = 0;
  m->reading = 0;
}
