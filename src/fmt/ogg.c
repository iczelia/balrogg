/*  Copyright (C) 2026 Kamila Szewczyk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, version 3.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include "fmt/ogg.h"
#include "sys/cpu.h"

static u32 CRC[8][256];
static int crc_ready;

static void crcinit(void) {
  u32 i, j, c;
  if (crc_ready) return;
  Fi(256,
    c = i << 24;
    Fj(8, c = c & 0x80000000UL ? c << 1 ^ 0x04C11DB7UL : c << 1);
    CRC[0][i] = c);
  Fi(256, Fj(7, CRC[j + 1][i] = CRC[j][i] << 8 ^ CRC[0][CRC[j][i] >> 24]));
  crc_ready = 1;
}

static u32 crcrun(u32 c, const u8 * d, sz n) {
  for (; n >= 8; n -= 8, d += 8) {
    c ^= rd32be(d);
    c = CRC[7][c >> 24] ^ CRC[6][c >> 16 & 0xFF] ^ CRC[5][c >> 8 & 0xFF]
      ^ CRC[4][c & 0xFF] ^ CRC[3][d[4]] ^ CRC[2][d[5]] ^ CRC[1][d[6]]
      ^ CRC[0][d[7]];
  }
  for (; n; n--) c = c << 8 ^ CRC[0][(c >> 24 ^ *d++) & 0xFF];
  return c;
}

static u32 crcbig(u32 c, const u8 * d, sz n) {
#ifdef HAVE_CLMUL
  if (n >= 256 && blr_cpu_clmul()) {
    u8 r[16];
    sz k = ogg_fold_clmul(c, d, n, r);
    c = crcrun(0, r, 16);  d += k;  n -= k;
  }
#endif
  return crcrun(c, d, n);
}

u32 ogg_crc(const u8 * d, sz n) {
  crcinit();
  return crcbig(0, d, n);
}

u32 ogg_crc_page(const u8 * p, sz n) {
  static const u8 z4[4] = { 0, 0, 0, 0 };
  u32 c;
  if (n < 26) return 0;
  crcinit();
  c = crcrun(0, p, 22);
  c = crcrun(c, z4, 4);
  return crcbig(c, p + 26, n - 26);
}

void ogg_crc_set(u8 * p, sz n) {
  FATAL_UNLESS(n >= OGG_HDRMIN, "cannot checksum a short Ogg page");
  wr32le(p + 22, ogg_crc_page(p, n));
}

int ogg_crc_ok(const u8 * p, sz n) {
  return n >= OGG_HDRMIN && rd32le(p + 22) == ogg_crc_page(p, n);
}

sz ogg_parse(ogg_page * p, const u8 * b, sz n) {
  sz i, hl;
  if (n < OGG_HDRMIN || memcmp(b, "OggS", 4) || b[4]) return 0;
  p->nseg = b[26];  hl = OGG_HDRMIN + (sz) p->nseg;
  if (n < hl) return 0;
  p->type = b[5];  p->glo = rd32le(b + 6);  p->ghi = rd32le(b + 10);
  p->serial = rd32le(b + 14);  p->seq = rd32le(b + 18);
  p->blen = 0;
  Fi((sz) p->nseg, p->lace[i] = b[OGG_HDRMIN + i];  p->blen += p->lace[i]);
  if (n < hl + p->blen) return 0;
  ogg_unpack(p);
  return hl + p->blen;
}

static void put_header(const ogg_page * p, u8 * out) {
  sz i;
  memcpy(out, "OggS", 4);  out[4] = 0;  out[5] = p->type;
  wr32le(out + 6, p->glo);  wr32le(out + 10, p->ghi);
  wr32le(out + 14, p->serial);  wr32le(out + 18, p->seq);
  out[26] = (u8) p->nseg;
  Fi((sz) p->nseg, out[OGG_HDRMIN + i] = p->lace[i]);
}

u8 * ogg_wrap(const ogg_page * p, u8 * body, sz * n) {
  u8 * out = body - OGG_HDRMIN - p->nseg;
  put_header(p, out);
  *n = OGG_HDRMIN + (sz) p->nseg + p->blen;
  ogg_crc_set(out, *n);
  return out;
}

void ogg_unpack(ogg_page * p) {
  sz i;
  u32 run = 0;
  p->np = 0;  p->tail = 0;
  Fi((sz) p->nseg,
    run += p->lace[i];
    if (p->lace[i] != OGG_MAXSEG) { p->plen[p->np++] = run;  run = 0; });
  if (p->nseg && p->lace[p->nseg - 1] == OGG_MAXSEG) {
    p->plen[p->np++] = run;  p->tail = 1;
  }
}

void ogg_pack(ogg_page * p) {
  int i;
  sz n = 0;
  p->blen = 0;
  Fi(p->np,
    u32 v = p->plen[i];
    p->blen += v;
    while (v >= OGG_MAXSEG) {
      FATAL_UNLESS(n < OGG_MAXSEG, "page needs more than %d segments",
                   OGG_MAXSEG);
      p->lace[n++] = OGG_MAXSEG;  v -= OGG_MAXSEG;
    }
    if (!(p->tail && i == p->np - 1)) {
      FATAL_UNLESS(n < OGG_MAXSEG, "page needs more than %d segments",
                   OGG_MAXSEG);
      p->lace[n++] = (u8) v;
    } else FATAL_UNLESS(!v, "continued packet has a partial segment"));
  p->nseg = (int) n;
}

static sz valid_at(blr_file * f, sz at, ogg_page * p, u8 * image) {
  sz n = ogg_read(f, at, p, image);
  return n && ogg_crc_ok(image, n) ? n : 0;
}

#define GAP_BLK 65536
static sz resync(blr_file * f, sz at, ogg_page * p, u8 * image, u8 * blk) {
  sz x = at + 1;
  while (x + OGG_HDRMIN <= f->len) {
    sz n = MIN((sz) GAP_BLK + 3, f->len - x), i;
    bf_read(f, x, blk, n);
    Fi(n - 3,
      if (blk[i] == 'O' && !memcmp(blk + i, "OggS", 4) &&
          valid_at(f, x + i, p, image)) return x + i);
    x += n - 3;
  }
  return f->len;
}

static void gap_add(ogg_gaps * gs, sz at, sz src, sz len) {
  ogg_gap * l = gs->n ? gs->g + gs->n - 1 : NULL;
  if (l && l->src + l->len == src) { l->len += len;  return; }
  gs->g = xgrow(gs->g, &gs->cap, gs->n + 1, sizeof *gs->g, 4);
  gs->g[gs->n].at = at;  gs->g[gs->n].src = src;  gs->g[gs->n++].len = len;
}

void ogg_gaps_find(ogg_gaps * gs, blr_file * input, int eos) {
  u8 * image = xmalloc(OGG_HDRMIN + OGG_MAXSEG + OGG_MAXSEG * OGG_MAXSEG),
     * blk = NULL;
  sz at = 0, clean = 0;
  int ended = 0;
  gs->g = NULL;  gs->n = gs->cap = 0;
  while (at < input->len) {
    ogg_page p;
    sz n = valid_at(input, at, &p, image);
    if (!n) {
      sz next;
      if (!blk) blk = xmalloc(GAP_BLK + 3);
      next = resync(input, at, &p, image, blk);
      gap_add(gs, clean, at, next - at);  at = next;  continue;
    }
    if (eos && ended && !(p.type & 2)) {
      gap_add(gs, clean, at, n);  at += n;  continue;
    }
    ended = p.type & 2 ? 0 : ended;
    if (p.type & 4) ended = 1;
    clean += n;  at += n;
  }
  xfree(image);  xfree(blk);
}

void ogg_gaps_free(ogg_gaps * gs) {
  xfree(gs->g);  gs->g = NULL;  gs->n = gs->cap = 0;
}

sz ogg_gaps_len(const ogg_gaps * gs) {
  sz i, n = 0;
  Fi(gs->n, n += gs->g[i].len);
  return n;
}

blr_file * ogg_gaps_view(const ogg_gaps * gs, blr_file * input) {
  blr_file * v;
  sz i, from = 0;
  if (!gs->n) return input;
  v = bf_splice(input);
  Fi(gs->n,
    bf_splice_add(v, from, gs->g[i].src - from);
    from = gs->g[i].src + gs->g[i].len);
  bf_splice_add(v, from, input->len - from);
  return v;
}

static void gap_num_enc(rc_enc * e, sz v) {
  int nb = (int) blr_ilog64(v);
  rc_enc_bits(e, (uint64_t) nb, 7);  rc_enc_bits(e, v, nb - 1);
}

static sz gap_num_dec(rc_dec * d) {
  int nb = (int) rc_dec_bits(d, 7);
  FATAL_UNLESS(nb <= (int) (8 * sizeof(sz)), "invalid Ogg gap table");
  return nb ? (sz) 1 << (nb - 1) | (sz) rc_dec_bits(d, nb - 1) : 0;
}

#define GAP_RATIO 512
typedef struct { u16 p[3][256];  u8 c[3][256];  int prev; } gap_model;

static void gm_init(gap_model * m) {
  rc_probs_init(m->p[0], 3 * 256);  memset(m->c, 0, sizeof m->c);  m->prev = 0;
}
static int gm_cls(int b) { return !b ? 0 : b == 255 ? 1 : 2; }

static void gm_enc(gap_model * m, rc_enc * e, int b) {
  int k, node = 1, x = gm_cls(m->prev);
  Fk(8,
    int bit = b >> (7 - k) & 1;
    rc_enc_bit_ad(e, m->p[x] + node, m->c[x] + node, RC_ALIM, bit);
    node = node << 1 | bit);
  m->prev = b;
}

static int gm_dec(gap_model * m, rc_dec * d) {
  int k, node = 1, x = gm_cls(m->prev);
  Fk(8,
    node = node << 1 | rc_dec_bit_ad(d, m->p[x] + node, m->c[x] + node,
                                     RC_ALIM));
  return m->prev = node & 0xFF;
}

void ogg_gaps_enc(const ogg_gaps * gs, blr_file * input, blr_file * stream) {
  rc_enc e;
  gap_model m;
  sz i, j, prev = 0;
  u8 * b = xmalloc(GAP_BLK);
  rc_enc_file(&e, stream);  gm_init(&m);
  gap_num_enc(&e, gs->n);
  Fi(gs->n,
    gap_num_enc(&e, gs->g[i].at - prev);  gap_num_enc(&e, gs->g[i].len - 1);
    prev = gs->g[i].at);
  for (i = gs->n; i-- > 0;)
    for (j = 0; j < gs->g[i].len; j += GAP_BLK) {
      sz n = MIN((sz) GAP_BLK, gs->g[i].len - j), k;
      bf_read(input, gs->g[i].src + j, b, n);
      Fk(n, gm_enc(&m, &e, b[k]));
    }
  rc_enc_finish(&e);  rc_enc_free(&e);  xfree(b);
}

void ogg_gaps_dec(blr_file * out, blr_file * f, sz off, sz len) {
  rc_dec d;
  gap_model m;
  ogg_gaps gs;
  sz i, j, n, at = 0, total = 0, clean = out->len, shift;
  u8 * b = xmalloc(GAP_BLK);
  rc_dec_file(&d, f, off, len);  gm_init(&m);
  gs.g = NULL;  gs.n = gs.cap = 0;
  n = gap_num_dec(&d);
  Fi(n,
    sz da = gap_num_dec(&d), gl = gap_num_dec(&d);
    FATAL_UNLESS(!rc_dec_spent(&d), "the Ogg gap table ran out");
    FATAL_UNLESS(da <= clean - at && gl < SIZE_MAX - clean - total,
                 "invalid Ogg gap");
    at += da;  total += gl + 1;
    gs.g = xgrow(gs.g, &gs.cap, gs.n + 1, sizeof *gs.g, 4);
    gs.g[gs.n].at = at;  gs.g[gs.n++].len = gl + 1);
  FATAL_UNLESS(total / GAP_RATIO <= len + RC_SPENT_SLACK,
               "the Ogg gaps claim more bytes than their stream codes");
  bf_resize(out, clean + total);
  shift = total;
  for (i = gs.n; i-- > 0;) {
    sz end = i + 1 < gs.n ? gs.g[i + 1].at : clean;
    bf_move(out, gs.g[i].at + shift, gs.g[i].at, end - gs.g[i].at);
    shift -= gs.g[i].len;
    for (j = 0; j < gs.g[i].len; j += GAP_BLK) {
      sz k, c = MIN((sz) GAP_BLK, gs.g[i].len - j);
      Fk(c, b[k] = (u8) gm_dec(&m, &d));
      FATAL_UNLESS(!rc_dec_spent(&d), "the Ogg gap bytes ran out");
      bf_write(out, gs.g[i].at + shift + j, b, c);
    }
  }
  xfree(b);  xfree(gs.g);  rc_dec_free(&d);
}

static const mdl_cfg FCFG[OGG_NFIELD] = {
  {   3,     0,      0,     2,      0,    0 },
  {   5,     1,      2,     8,      0,    1 },
  {   5,     1,      1,     1,      0,    1 },
  {   5,     1,      2,     8,      0,    1 },
  {   5,     1,      2,     8,      0,    2 },
  {   3,     0,      0,     8,      1,    0 },
  {   4,     0,      0,     32,     1,    0 }
};

static const mdl_cfg CTCFG = { 5, 0, 0, 32, 1, 0 };

void ogg_hdr_init(ogg_hdr * h) {
  sz i;
  memset(&h->tbl, 0, sizeof h->tbl);
  Fi(OGG_NFIELD, mdl_init_in(h->f + i, FCFG + i, &h->tbl));
  mdl_init_in(&h->ct, &CTCFG, &h->tbl);
  rc_probs_init(&h->tl, 1);
  rc_probs_init(h->dp, 2);
  h->dh = 0;  h->cum = 0;
}

void ogg_hdr_free(ogg_hdr * h) { ar_free(&h->tbl); }

void ogg_hdr_reset(ogg_hdr * h) {
  sz i;
  Fi(OGG_NFIELD, mdl_reset(h->f + i));
  mdl_reset(&h->ct);
  h->dh = 0;  h->cum = 0;
}

void ogg_cont_enc(ogg_hdr * h, rc_enc * e, u32 spill) {
  mdl_enc(&h->ct, e, spill);
}
u32 ogg_cont_dec(ogg_hdr * h, rc_dec * d) { return mdl_dec(&h->ct, d); }

void ogg_hdr_step(ogg_hdr * h, u32 samples) { h->cum += samples; }

int ogg_whole(const ogg_page * p, int j) {
  return !(j == 0 && p->type & 1) && !(j == p->np - 1 && p->tail);
}

void ogg_hdr_enc(ogg_hdr * h, rc_enc * e, const ogg_page * p, int first,
                 const u8 * df) {
  int i;
  mdl_enc(h->f + 0, e, (u32) (first ? p->type & ~2 : p->type));
  mdl_enc(h->f + 1, e, p->glo - h->cum);
  mdl_enc(h->f + 2, e, p->ghi);
  mdl_enc(h->f + 3, e, p->serial);
  mdl_enc(h->f + 4, e, p->seq);
  mdl_enc(h->f + 5, e, (u32) p->np);
  if (p->np) rc_enc_bit(e, &h->tl, p->tail);
  Fi(p->np,
    if (ogg_whole(p, (int) i)) {
      rc_enc_bit(e, h->dp + h->dh, df[i]);  h->dh = df[i];
      if (df[i]) continue;
    }
    mdl_enc(h->f + 6, e, p->plen[i]));
}

int ogg_hdr_dec(ogg_hdr * h, rc_dec * d, ogg_page * p, int first, u8 * df) {
  int i, n = 0;
  p->type = (u8) mdl_dec(h->f + 0, d);
  if (first) p->type |= 2;
  p->glo = mdl_dec(h->f + 1, d) + h->cum;
  p->ghi = mdl_dec(h->f + 2, d);
  p->serial = mdl_dec(h->f + 3, d);
  p->seq = mdl_dec(h->f + 4, d);
  p->np = (int) mdl_dec(h->f + 5, d);
  FATAL_UNLESS(p->np >= 0 && p->np <= OGG_MAXSEG,
               "page claims %d packets", p->np);
  p->tail = p->np ? rc_dec_bit(d, &h->tl) : 0;
  Fi(p->np,
    df[i] = 0;  p->plen[i] = 0;
    if (ogg_whole(p, (int) i)) {
      df[i] = (u8) rc_dec_bit(d, h->dp + h->dh);  h->dh = df[i];
      if (df[i]) { n++;  continue; }
    }
    p->plen[i] = mdl_dec(h->f + 6, d));
  if (!n) ogg_pack(p);
  return n;
}

sz ogg_read(blr_file * file, sz off, ogg_page * p, u8 * image) {
  sz n = OGG_HDRMIN, body = 0, i, seg;
  if (off > file->len || file->len - off < OGG_HDRMIN) return 0;
  bf_read(file, off, image, n);
  if (memcmp(image, "OggS", 4)) return 0;
  seg = image[26];
  if (seg > file->len - off - n) return 0;
  bf_read(file, off + n, image + n, seg);
  Fi(seg, body += image[n + i]);
  n += seg;
  if (body > file->len - off - n) return 0;
  bf_read(file, off + n, image + n, body);
  return ogg_parse(p, image, n + body);
}
