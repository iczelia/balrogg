/*  Copyright (C) 2026 Kamila Szewczyk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, version 3.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program. If not, see <http://www.gnu.org/licenses/>.  */

#include "ogg.h"

/*  Ogg CRC-32, unreflected with zero seed and no final inversion.  */
static u32 CRC[256];

static void crcinit(void) {
  u32 i, j, c;
  if (CRC[1]) return;
  Fi(256,
    c = i << 24;
    Fj(8, c = c & 0x80000000UL ? c << 1 ^ 0x04C11DB7UL : c << 1);
    CRC[i] = c);
}

static u32 crcrun(u32 c, const u8 * d, sz n) {
  sz i;
  Fi(n, c = c << 8 ^ CRC[(c >> 24 ^ d[i]) & 0xFF]);
  return c;
}

static void wr32(u8 * b, u32 v) {
  b[0] = (u8) v;  b[1] = (u8) (v >> 8);  b[2] = (u8) (v >> 16);  b[3] = (u8) (v >> 24);
}

u32 ogg_crc(const u8 * d, sz n) {
  crcinit();
  return crcrun(0, d, n);
}

/*  Compute a page CRC while treating its stored CRC as zero.  */
u32 ogg_crc_page(const u8 * p, sz n) {
  static const u8 z4[4] = { 0, 0, 0, 0 };
  u32 c;
  if (n < 26) return 0;
  crcinit();
  c = crcrun(0, p, 22);
  c = crcrun(c, z4, 4);
  return crcrun(c, p + 26, n - 26);
}

void ogg_crc_set(u8 * p, sz n) {
  FATAL_UNLESS(n >= OGG_HDRMIN, "cannot checksum a short Ogg page");
  wr32(p + 22, ogg_crc_page(p, n));
}

static u32 rd32(const u8 * b) {
  return (u32) b[0] | (u32) b[1] << 8 | (u32) b[2] << 16 | (u32) b[3] << 24;
}

int ogg_crc_ok(const u8 * p, sz n) {
  return n >= OGG_HDRMIN && rd32(p + 22) == ogg_crc_page(p, n);
}

sz ogg_parse(ogg_page * p, const u8 * b, sz n) {
  sz i, hl;
  if (n < OGG_HDRMIN || memcmp(b, "OggS", 4) || b[4]) return 0;
  p->nseg = b[26];  hl = OGG_HDRMIN + (sz) p->nseg;
  if (n < hl) return 0;
  p->type = b[5];  p->glo = rd32(b + 6);  p->ghi = rd32(b + 10);
  p->serial = rd32(b + 14);  p->seq = rd32(b + 18);
  p->blen = 0;
  Fi((sz) p->nseg, p->lace[i] = b[OGG_HDRMIN + i];  p->blen += p->lace[i]);
  if (n < hl + p->blen) return 0;
  ogg_unpack(p);
  return hl + p->blen;
}

sz ogg_emit(const ogg_page * p, u8 * out, const u8 * body) {
  sz i, hl = OGG_HDRMIN + (sz) p->nseg;
  memcpy(out, "OggS", 4);  out[4] = 0;  out[5] = p->type;
  wr32(out + 6, p->glo);  wr32(out + 10, p->ghi);
  wr32(out + 14, p->serial);  wr32(out + 18, p->seq);
  out[26] = (u8) p->nseg;
  Fi((sz) p->nseg, out[OGG_HDRMIN + i] = p->lace[i]);
  memcpy(out + hl, body, p->blen);
  ogg_crc_set(out, hl + p->blen);
  return hl + p->blen;
}

void ogg_unpack(ogg_page * p) {
  sz i;
  u32 run = 0;
  p->np = 0;  p->tail = 0;
  Fi((sz) p->nseg,
    run += p->lace[i];
    if (p->lace[i] != OGG_MAXSEG) { p->plen[p->np++] = run;  run = 0; });
  /*  A final 255 continues the packet on the next page.  */
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
      FATAL_UNLESS(n < OGG_MAXSEG, "page needs more than %d segments", OGG_MAXSEG);
      p->lace[n++] = OGG_MAXSEG;  v -= OGG_MAXSEG;
    }
    if (!(p->tail && i == p->np - 1)) {
      FATAL_UNLESS(n < OGG_MAXSEG, "page needs more than %d segments", OGG_MAXSEG);
      p->lace[n++] = (u8) v;
    } else FATAL_UNLESS(!v, "continued packet has a partial segment"));
  p->nseg = (int) n;
}

/*  Page-header model; one configuration per field, in FCFG.  */
static const mdl_cfg FCFG[OGG_NFIELD] = {
  /*  depth  signed  shist  freeze  bank  order  */
  {   3,     0,      0,     2,      0,    0 },   /*  header type  */
  {   5,     1,      2,     8,      0,    1 },   /*  granule, low  */
  {   5,     1,      1,     1,      0,    1 },   /*  granule, high  */
  {   5,     1,      2,     8,      0,    1 },   /*  serial  */
  {   5,     1,      2,     8,      0,    2 },   /*  page sequence  */
  {   3,     0,      0,     8,      1,    0 },   /*  packet count  */
  {   4,     0,      0,     32,     1,    0 }    /*  packet lengths  */
};

/*  Continued-byte counts use a plain unsigned model.  */
static const mdl_cfg CTCFG = { 5, 0, 0, 32, 1, 0 };

void ogg_hdr_init(ogg_hdr * h) {
  sz i;
  Fi(OGG_NFIELD, mdl_init(h->f + i, FCFG + i));
  mdl_init(&h->ct, &CTCFG);
  rc_probs_init(&h->tl, 1);
  h->cum = 0;
}

void ogg_hdr_free(ogg_hdr * h) {
  sz i;
  Fi(OGG_NFIELD, mdl_free(h->f + i));
  mdl_free(&h->ct);
}

void ogg_hdr_reset(ogg_hdr * h) {
  sz i;
  Fi(OGG_NFIELD, mdl_reset(h->f + i));
  mdl_reset(&h->ct);
  h->cum = 0;
}

void ogg_cont_enc(ogg_hdr * h, rc_enc * e, u32 spill) { mdl_enc(&h->ct, e, spill); }
u32 ogg_cont_dec(ogg_hdr * h, rc_dec * d) { return mdl_dec(&h->ct, d); }

void ogg_hdr_step(ogg_hdr * h, u32 samples) { h->cum += samples; }

/*  Predict granules from prior packet sample counts. A first-order model then
    gives steady full pages a zero residual.  */
void ogg_hdr_enc(ogg_hdr * h, rc_enc * e, const ogg_page * p, int first) {
  int i;
  mdl_enc(h->f + 0, e, (u32) (first ? p->type & ~2 : p->type));
  mdl_enc(h->f + 1, e, p->glo - h->cum);
  mdl_enc(h->f + 2, e, p->ghi);
  mdl_enc(h->f + 3, e, p->serial);
  mdl_enc(h->f + 4, e, p->seq);
  mdl_enc(h->f + 5, e, (u32) p->np);
  Fi(p->np, mdl_enc(h->f + 6, e, p->plen[i]));
  /*  Distinguish a closing zero from packet continuation.  */
  if (p->nseg && !p->lace[p->nseg - 1]) rc_enc_bit(e, &h->tl, 1);
  else if (p->nseg && p->lace[p->nseg - 1] == OGG_MAXSEG) rc_enc_bit(e, &h->tl, 0);
}

void ogg_hdr_dec(ogg_hdr * h, rc_dec * d, ogg_page * p, int first) {
  int i;
  p->type = (u8) mdl_dec(h->f + 0, d);
  if (first) p->type |= 2;
  p->glo = mdl_dec(h->f + 1, d) + h->cum;
  p->ghi = mdl_dec(h->f + 2, d);
  p->serial = mdl_dec(h->f + 3, d);
  p->seq = mdl_dec(h->f + 4, d);
  p->np = (int) mdl_dec(h->f + 5, d);
  FATAL_UNLESS(p->np >= 0 && p->np <= OGG_MAXSEG, "page claims %d packets", p->np);
  Fi(p->np, p->plen[i] = mdl_dec(h->f + 6, d));
  p->tail = 0;
  if (p->np && !(p->plen[p->np - 1] % OGG_MAXSEG))
    p->tail = !rc_dec_bit(d, &h->tl);
  ogg_pack(p);
}

/* The lacing table gives the exact page size before reading the body. */
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
