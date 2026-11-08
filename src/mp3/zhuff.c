#include "mp3/zlayer.h"

#define OVERRUN_BITS  24
#define OVERRUN_MASK  ((1u << OVERRUN_BITS) - 1)
#define HUFF_ESCAPE   15

typedef struct { u8 * buf;  i32 pos;  int n;  u64 acc; } bacc;

static INLINE void ba_put(bacc * w, int n, u32 v) {
  w->acc = w->acc << n | v;  w->n += n;
  if (w->n >= 32) {
    int k = 24 + ((w->pos & 7) ? (w->pos & 7) : 8);
    u8 * p;
    w->n -= k;  w->pos -= k;
    p = w->buf + (w->pos >> 3);
    wr32le(p, rd32le(p) |
              ((u32) (w->acc >> w->n) & (u32) (((u64) 1 << k) - 1)));
  }
}

static INLINE void ba_end(bacc * w, bitwriter * out) {
  out->bitpos = w->pos;
  if (w->n > 16) {
    bw_put(out, w->n - 16, (u32) (w->acc >> 16) & ((1u << (w->n - 16)) - 1));
    w->n = 16;
  }
  if (w->n) bw_put(out, w->n, (u32) w->acc & ((1u << w->n) - 1));
}

static INLINE void ba_pair(bacc * w, int t, const i16 * xy) {
  const u8 * dims = mp3_huff_dims[t];
  i32 x = xy[0], y = xy[1];
  u32 sx = x < 0, sy = y < 0;
  if (sx) x = -x;
  if (sy) y = -y;
  i32 idx = dims[1] * MIN(x, HUFF_ESCAPE) + MIN(y, HUFF_ESCAPE);
  if (x < HUFF_ESCAPE && y < HUFF_ESCAPE) {
    u32 v = HUFF_CODE[t][idx];
    int n = HUFF_LEN[t][idx];
    if (x != 0) { v = v << 1 | sx;  n++; }
    if (y != 0) { v = v << 1 | sy;  n++; }
    ba_put(w, n, v);
    return;
  }
  ba_put(w, HUFF_LEN[t][idx], HUFF_CODE[t][idx]);
  if (x >= HUFF_ESCAPE) ba_put(w, dims[2], (u32) (x - HUFF_ESCAPE));
  if (x != 0) ba_put(w, 1, sx);
  if (y >= HUFF_ESCAPE) ba_put(w, dims[2], (u32) (y - HUFF_ESCAPE));
  if (y != 0) ba_put(w, 1, sy);
}

static void put_spectrum(model * m, const granule * g, const i16 * co,
                         i32 end) {
  bitwriter * out = &m->out;
  regions r = granule_regions(m->frame.header, g);
  const i32 bound[4] = {0, r.end0, r.end1, r.big_end};
  bacc w = {out->buf, out->bitpos, 0, 0};
  int k;

  Fk(3,
    int t = g->table_select[k];
    if (t != 0)
      for (i32 i = bound[k]; end < w.pos - w.n && i < bound[k + 1]; i += 2)
        ba_pair(&w, t, co + i));
  const u16 * quad = count1_codes[g->count1table_select];
  for (i32 i = r.big_end; end < w.pos - w.n && i + 4 <= MP3_LINES; i += 4) {
    u32 e = quad[(co[i] & 3) << 6 | (co[i + 1] & 3) << 4 |
                 (co[i + 2] & 3) << 2 | (co[i + 3] & 3)];
    ba_put(&w, (int) (e >> 10), e & 0x3ff);
  }
  while (end < w.pos - w.n) {
    int n = (int) MIN(w.pos - w.n - end, 24);
    ba_put(&w, n, (1u << n) - 1);
  }
  ba_end(&w, out);

  out->bitpos = end;
  u8 * p = out->buf + ((end - OVERRUN_BITS) >> 3);
  wr32le(p, rd32le(p) & ~(OVERRUN_MASK << (end & 7)));
}

#define HUFF_PEEK 10
static u16 huff_fast[34][1 << HUFF_PEEK];
static u16 huff_long[34];
static u8 huff_pair_bits[34][256];

static void quad_fast_init(void);

static void huff_fast_init(void) {
  int i;
  Fi(34,
    const u32 * list = HUFF_LIST[i];
    const u8 * dims = mp3_huff_dims[i];
    huff_long[i] = 0;
    if (!list) continue;
    if (i < 32)
      for (int x = 0; x < dims[0]; x++)
        for (int y = 0; y < dims[1]; y++)
          huff_pair_bits[i][x * 16 + y] =
            (u8) (HUFF_LEN[i][x * dims[1] + y] + (x > 0) + (y > 0) +
                  ((x == HUFF_ESCAPE) + (y == HUFF_ESCAPE)) * dims[2]);
    for (int k = 0; list[k] >> 8; k++) {
      u32 code = list[k] >> 8, len = 0, j;
      while (code >> (len + 1)) len++;
      if (len > HUFF_PEEK) {
        if (!huff_long[i]) huff_long[i] = (u16) k;
        continue;
      }
      u32 lo = (code - (1u << len)) << (HUFF_PEEK - len);
      Fj(1u << (HUFF_PEEK - len),
        huff_fast[i][lo + j] = (u16) (len << 8 | (list[k] & 0xff)));
    });
  quad_fast_init();
}

static INLINE u32 huff_pair_index(const i16 * xy) {
  i32 x = ABS(xy[0]), y = ABS(xy[1]);
  return (u32) MIN(x, HUFF_ESCAPE) * 16 + (u32) MIN(y, HUFF_ESCAPE);
}

static u16 quad_fast[2][1 << 10];

static void quad_fast_init(void) {
  for (int tb = 0; tb < 2; tb++)
    for (u32 w = 0; w < 1024; w++) {
      int p = 10, j;
      u32 v, q = 0;
#define RD(n) (p -= (n), (w >> p) & ((1u << (n)) - 1))
      if (tb) v = 15 - RD(4);
      else if (RD(1)) v = 0;
      else {
        u32 u = RD(3);
        if (u >= 4) v = count1a_tree0[u];
        else {
          u32 x = u * 2 + RD(1);
          v = x >= 3 ? count1a_tree1[x] : count1a_tree2[x * 2 + RD(1)];
        }
      }
      Fj(4, if (v & (8 >> j)) q |= (RD(1) ? 3u : 1u) << (6 - 2 * j));
#undef RD
      quad_fast[tb][w] = (u16) ((u32) (10 - p) << 8 | q);
    }
}

static INLINE void get_pair_lin(bitreader * in, const u8 * dims, i32 x, i32 y,
                                i16 * xy) {
  u32 lin = dims[2];
  if (lin != 0 && x + 1 == dims[0]) x += (i32) br_read(in, (int) lin);
  if (x != 0 && br_read(in, 1) == 1) x = -x;
  if (lin != 0 && y + 1 == dims[1]) y += (i32) br_read(in, (int) lin);
  if (y != 0 && br_read(in, 1) == 1) y = -y;
  xy[0] = (i16) x;  xy[1] = (i16) y;
}

static INLINE u32 get_pair_rest(bitreader * in, const u8 * dims, i32 x, i32 y,
                                i16 * xy) {
  if (dims[2] == 0 || (x + 1 != dims[0] && y + 1 != dims[1])) {
    int ns = (x != 0) + (y != 0);
    if (ns) {
      u32 sg = br_read(in, ns);
      if (y != 0) { if (sg & 1) y = -y;  sg >>= 1; }
      if (sg & 1) x = -x;
    }
    xy[0] = (i16) x;  xy[1] = (i16) y;
    return 0;
  }
  get_pair_lin(in, dims, x, y, xy);
  return 0;
}

static u32 get_pair(bitreader * in, int t, i16 * xy) {
  const u8 * dims = mp3_huff_dims[t];
  const u32 * list = HUFF_LIST[t];
  u32 v = 1;
  int k = 0;
  u32 e = huff_fast[t][br_peek(in, in->bitpos - HUFF_PEEK) &
                       ((1u << HUFF_PEEK) - 1)];
  if (e) {
    in->bitpos -= (i32) (e >> 8);
    return get_pair_rest(in, dims, (i32) (e >> 4 & 0xf), (i32) (e & 0xf), xy);
  }
  k = huff_long[t];
  for (;; k++) {
    u32 code = list[k] >> 8;
    if (code == 0) return 1;
    while ((i32) v < (i32) code) v = v * 2 + br_read(in, 1);
    if (v == code) {
      get_pair_lin(in, dims, (i32) (list[k] >> 4 & 0xf), (i32) (list[k] & 0xf),
                   xy);
      return 0;
    }
  }
}

static void get_quad(bitreader * in, int table_b, i16 * co) {
  u32 e = quad_fast[table_b][br_peek(in, in->bitpos - 10) & 1023];
  int j;
  in->bitpos -= (i32) (e >> 8);
  Fj(4, co[j] = (i16) ((i32) ((e >> (6 - 2 * j)) << 30) >> 30));
}

static u32 get_spectrum(model * m, const granule * g, i16 * co, i32 end) {
  bitreader * in = &m->in;
  regions r = granule_regions(m->frame.header, g);
  const i32 bound[4] = {0, r.end0, r.end1, r.big_end};
  u32 err = 0;
  int k;

  u8 * p = br_base(in) + ((end - OVERRUN_BITS) >> 3);
  u32 saved = rd32le(p);
  wr32le(p, saved | OVERRUN_MASK << (end & 7));
  memset(co, 0, MP3_LINES * sizeof *co);

  Fk(3,
    int t = g->table_select[k];
    if (t != 0)
      for (i32 i = bound[k]; end < in->bitpos && i < bound[k + 1]; i += 2)
        err |= get_pair(in, t, co + i));
  for (i32 i = r.big_end; end < in->bitpos && i + 4 <= MP3_LINES; i += 4)
    get_quad(in, g->count1table_select, co + i);
  while (end < in->bitpos) {
    int n = (int) MIN(in->bitpos - end, 24);
    if (br_read(in, n) != (1u << n) - 1) err = 1;
  }
  in->bitpos = end;
  wr32le(p, saved);
  return err;
}

static u32 code_spectrum(model * m, const granule * g, i16 * co, i32 end) {
  if (m->reading) return get_spectrum(m, g, co, end);
  put_spectrum(m, g, co, end);
  return 0;
}

static void code_scf_lsf(model * m, granule * g, u16 * sf, int ch) {
  u32 h = m->frame.header, slen;
  if ((hdr_mode_ext(h) & HDR_MODE_EXT_IS) && ch == 1)
    slen = lsf_slen_is_tab[g->scalefac_compress >> 1];
  else
    slen = lsf_slen_tab[g->scalefac_compress];
  g->preflag = (slen >> 15) & 1;
  int blocks = g->block_type == BLOCK_SHORT ? (g->mixed_block == 0 ? 1 : 2) : 0;
  const u8 * nr_sfb = mp3_lsf_nr + blocks * 24 + ((slen >> 12) & 7) * 4;
  int k;

  for (int part = 0; part < 4; part++, slen >>= 3) {
    int nbits = slen & 7;
    for (k = 0; k < nr_sfb[part]; k++, sf++) {
      if (nbits == 0) *sf = 0;
      else code_scalefac(m, nbits, sf);
    }
  }
  Fk(blocks * 2 + 1, *sf++ = 0);
}

static void code_scf_short(model * m, const granule * g, u16 * sf) {
  int slen1 = mp3_slen1[g->scalefac_compress];
  int slen2 = mp3_slen2[g->scalefac_compress];
  int sfb = 0, j, k;
  if (g->mixed_block) {
    Fk(8, code_scalefac(m, slen1, &sf[k]));
    sfb = 3;
  }
  for (; sfb < 12; sfb++)
    Fj(3, code_scalefac(m, sfb < 6 ? slen1 : slen2,
                        &sf[SCF_SHORT + sfb * 3 + j]));
}

static void code_scf_long(model * m, int gr, int ch) {
  mp3_frame * f = &m->frame;
  u16 * sf = f->scalefac[gr][ch];
  int slen1 = mp3_slen1[f->gr[gr][ch].scalefac_compress];
  int slen2 = mp3_slen2[f->gr[gr][ch].scalefac_compress];
  int j;
  Fj(4,
    for (int i = mp3_scfsi_band[j]; i < mp3_scfsi_band[j + 1]; i++) {
      if (gr == 0 || f->scfsi[ch][j] == 0)
        code_scalefac(m, i < 11 ? slen1 : slen2, &sf[i]);
      else
        sf[i] = f->scalefac[0][ch][i];
    });
}

u32 mp3_code_main_data(model * m) {
  mp3_frame * f = &m->frame;
  int mpeg1 = hdr_mpeg1(f->header);
  u32 err = 0;
  int i, j;

  Fi(hdr_granules(f->header),
    Fj(hdr_channels(f->header),
      granule * g = &f->gr[i][j];
      u16 * sf = f->scalefac[i][j];

      if (mpeg1 && g->part2_3_length == 0) continue;
      i32 end = mp3_bitpos(m) - g->part2_3_length;
      if (m->reading) memset(sf, 0, sizeof f->scalefac[i][j]);
      if (!mpeg1) code_scf_lsf(m, g, sf, j);
      else if (g->block_type == BLOCK_SHORT) code_scf_short(m, g, sf);
      else code_scf_long(m, i, j);
      err |= code_spectrum(m, g, f->coef[i][j], end)));
  return err;
}

i32 huff_bits(const i16 * coef, i32 lo, i32 hi, int t) {
  i32 sum = 0;
  if (t == 0 || t == 4 || t == 14) return 0;
  for (i32 k = lo; k < hi; k += 2)
    sum += huff_pair_bits[t][huff_pair_index(coef + k)];
  return sum;
}
