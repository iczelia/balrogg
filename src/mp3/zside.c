#include "mp3/zhdr.h"

#define RATES rates_w28
#define BIAS  16

static int bit(rcoder * rc, ctr * c, int b) {
  return code_bit(rc, c, b, RATES, BIAS);
}

static u32 tree(rcoder * rc, ctr * nodes, int nbits, u32 v) {
  return code_tree_lt(rc, nodes, nbits, v, 1 << nbits, RATES, BIAS);
}

static void hdr_put(u32 * h, int shift, int nbits, u32 v) {
  u32 mask = ((1u << nbits) - 1) << shift;
  *h = (*h & ~mask) | (v << shift & mask);
}

static void code_field(model * m, ctr * nodes, int shift, int nbits) {
  u32 * h = &m->frame.header;
  hdr_put(h, shift, nbits,
          tree(&m->rc, nodes, nbits, *h >> shift & ((1u << nbits) - 1)));
}

static void code_header(model * m) {
  side_model * s = &m->side;
  u32 * h = &m->frame.header;
  int padding;

  code_field(m, s->emphasis, 0, 2);
  code_field(m, m->zc.mode_ext[hdr_mode_ext(m->zc.hdr_prev)], 4, 2);
  code_field(m, s->mode, 6, 2);
  padding = code_runbit(&m->rc, &s->padding, (int) hdr_padding(*h), RATES,
                        BIAS);
  hdr_put(h, 9, 1, (u32) padding);
  code_field(m, s->samplerate, 10, 2);
  code_field(m, s->protection, 16, 1);
  *h |= HDR_SYNC_MASK | HDR_LAYER_III << 17;
  code_field(m, s->mpeg1, 19, 1);
  if (hdr_mpeg1(*h)) hdr_put(h, 20, 1, 1);
  else code_field(m, s->not_mpeg25, 20, 1);
}

static void code_header_extra(model * m) {
  side_model * s = &m->side;
  mp3_frame * f = &m->frame;

  code_field(m, m->zc.bitrate[hdr_bitrate(m->zc.hdr_prev)], 12, 4);
  if (hdr_bitrate(f->header) == 0) {
    zc_model * z = &m->zc;
    if (!code_bit(&m->rc, &z->free_same, m->free_bytes == z->free_last,
                  RATES, BIAS))
      m->free_bytes = (i32) rc_raw_bits(&m->rc, 11, (u32) m->free_bytes);
    else m->free_bytes = z->free_last;
    z->free_last = m->free_bytes;
  }
  code_field(m, s->original, 2, 1);
  code_field(m, s->copyright, 3, 1);
  code_field(m, s->private_bit, 8, 1);
  f->private_bits = (i32) tree(&m->rc, s->private_bits, 5,
                               (u32) f->private_bits);
}

static void code_scfsi(model * m, int nch) {
  side_model * s = &m->side;
  mp3_frame * f = &m->frame;
  int i, j;

  Fi(nch,
    int prev = 0;
    Fj(4,
      u8 * last = &s->scfsi_prev[i][j];
      prev = bit(&m->rc, &s->scfsi[prev][*last][i][j], f->scfsi[i][j]);
      f->scfsi[i][j] = prev;
      *last = (u8) prev));
}

static void code_block_type(model * m, granule * g, int ch) {
  side_model * s = &m->side;
  rcoder * rc = &m->rc;
  int miss;

  if (ch == 0 && s->block_switch < 2) s->block_switch++;
  miss = code_runbit(rc, &s->block_type_miss,
                     (u32) s->block_switch != (u32) g->block_type, RATES, BIAS);
  if (!miss) {
    g->block_type = s->block_switch;
  } else {
    u32 t = tree(rc, s->block_type, 2, (u32) g->block_type);
    g->block_type = (i32) t;
    if (t == 0) s->block_switch++;
    else s->block_switch = 0;
  }
  if (ch == 1 && s->block_switch == 3) s->block_switch = 0;
}

static void code_gain(model * m, int gr, int ch) {
  zc_model * z = &m->zc;
  granule * g = &m->frame.gr[gr][ch];
  i32 base = z->gg_prev[ch];
  if (ch == 1 && !(hdr_mode(m->frame.header) == 1 &&
                   (hdr_mode_ext(m->frame.header) & 2)))
    base = m->frame.gr[gr][0].global_gain + z->gg_prev[1] - z->gg_old0;
  i32 d = g->global_gain - base, sym = d + ZC_GG_MID, r;
  i32 ref = ch == 1 ? m->frame.gr[gr][0].global_gain - z->gg_prev[0]
                    : z->gg_diff[0];
  int dq = ref == 0 ? 0 : ref > 0 ? (ref > 2 ? 2 : 1) : (ref < -2 ? 4 : 3);
  ctr * t = z->gg_tree[ch][dq][z->ws_prev[ch]];

  if (sym < 0 || sym >= ZC_GG_ESC) sym = ZC_GG_ESC;
  sym = (i32) code_tree_lt(&m->rc, t, 5, (u32) sym, ZC_GG_ESC + 1, rates_w40, 16);
  if (sym == ZC_GG_ESC)
    g->global_gain = (i32) tree(&m->rc, m->side.global_gain, 8,
                                (u32) g->global_gain);
  else
    g->global_gain = base + sym - ZC_GG_MID;
  r = g->global_gain - z->gg_prev[ch];
  if (ch == 0 || hdr_channels(m->frame.header) == 1) z->gg_diff[0] = r;
  if (ch == 0) z->gg_old0 = z->gg_prev[0];
  z->gg_prev[ch] = g->global_gain;
}

static void code_granule(model * m, int mpeg1, int gr, int ch) {
  side_model * s = &m->side;
  rcoder * rc = &m->rc;
  granule * g = &m->frame.gr[gr][ch];
  u8 * scale_prev = &s->scalefac_scale_prev[gr][ch];
  u32 sfc;
  int i;

  code_gain(m, gr, ch);
  if (mpeg1)
    g->scalefac_compress = m->zc.sfc_prev[ch] =
        (i32) tree(rc, m->zc.sfc[ch][m->zc.sfc_prev[ch] & 15], 4,
                   (u32) g->scalefac_compress);
  else
    g->scalefac_compress =
        (i32) tree(rc, s->sfc_lsf[ch], 9, (u32) g->scalefac_compress);

  g->window_switching = bit(rc, &s->window_switching[s->block_switch != 0][ch],
                            g->window_switching);

  sfc = (u32) g->scalefac_compress & 15;
  if (mpeg1) g->preflag = bit(rc, &s->preflag[sfc][ch], g->preflag);
  g->scalefac_scale =
      bit(rc, &s->scalefac_scale[sfc][*scale_prev][ch], g->scalefac_scale);
  *scale_prev = (u8) g->scalefac_scale;
  g->count1table_select =
      bit(rc, &s->count1table[g->scalefac_scale][ch], g->count1table_select);

  m->zc.ws_prev[ch] = g->window_switching;
  if (g->window_switching) {
    code_block_type(m, g, ch);
    g->mixed_block = bit(rc, &s->mixed_block, g->mixed_block);
    Fi(3,
      g->subblock_gain[i] =
          (i32) tree(rc, s->subblock_gain[i], 3, (u32) g->subblock_gain[i]));
  }
}

void side_model_code(model * m) {
  int mpeg1, nch, i;

  code_header(m);
  code_header_extra(m);
  mpeg1 = (int) hdr_mpeg1(m->frame.header);
  nch = hdr_channels(m->frame.header);
  if (mpeg1) code_scfsi(m, nch);
  for (int gr = 0; gr <= mpeg1; gr++)
    Fi(nch, code_granule(m, mpeg1, gr, i));
  m->zc.hdr_prev = m->frame.header;
}

void side_model_init(model * m) {
  side_model * s = &m->side;
  memset(s, 0, sizeof *s);
  counters_init((ctr *) s, offsetof(side_model, padding) / sizeof(ctr));
  runbit_init(&s->padding);
  runbit_init(&s->block_type_miss);
  COUNTERS_INIT(m->zc.gg_tree);
  counters_init(m->zc.dup_flag, 2);
  counters_init(&m->zc.free_same, 1);
  counters_init(&m->zc.bitrate[0][0],
                (offsetof(zc_model, sfc) + sizeof m->zc.sfc -
                 offsetof(zc_model, bitrate)) / sizeof (ctr));
}
