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

#ifndef BLR_OGG_H
#define BLR_OGG_H

#include "coder/model.h"

#define OGG_HDRMIN  27
#define OGG_MAXSEG  255
#define OGG_NFIELD  7

typedef struct {
  u8 type;
  u32 glo, ghi;
  u32 serial, seq;
  int nseg;  u8 lace[OGG_MAXSEG];
  int np;  u32 plen[OGG_MAXSEG];
  int tail;
  sz blen;
} ogg_page;

u32 ogg_crc(const u8 * d, sz n);
sz ogg_fold_clmul(u32 c, const u8 * d, sz n, u8 r[16]);
u32 ogg_crc_page(const u8 * p, sz n);
int ogg_crc_ok(const u8 * p, sz n);
void ogg_crc_set(u8 * p, sz n);

sz ogg_parse(ogg_page * p, const u8 * b, sz n);
sz ogg_read(blr_file * file, sz off, ogg_page * p, u8 * image);
u8 * ogg_wrap(const ogg_page * p, u8 * body, sz * n);

void ogg_unpack(ogg_page * p);
void ogg_pack(ogg_page * p);

typedef struct { sz at, len, src; } ogg_gap;
typedef struct { ogg_gap * g;  sz n, cap; } ogg_gaps;

void ogg_gaps_find(ogg_gaps * gs, blr_file * input, int eos);
void ogg_gaps_free(ogg_gaps * gs);
sz ogg_gaps_len(const ogg_gaps * gs);
blr_file * ogg_gaps_view(const ogg_gaps * gs, blr_file * input);
void ogg_gaps_enc(const ogg_gaps * gs, blr_file * input, blr_file * stream);
void ogg_gaps_dec(blr_file * out, blr_file * f, sz off, sz len);

typedef struct {
  model f[OGG_NFIELD];
  model ct;
  u16 tl;
  u16 dp[2];
  u8 dh;
  u32 cum;
  blr_arena tbl;
} ogg_hdr;

void ogg_cont_enc(ogg_hdr * h, rc_enc * e, u32 spill);
u32 ogg_cont_dec(ogg_hdr * h, rc_dec * d);

void ogg_hdr_init(ogg_hdr * h);
void ogg_hdr_free(ogg_hdr * h);
void ogg_hdr_reset(ogg_hdr * h);

void ogg_hdr_enc(ogg_hdr * h, rc_enc * e, const ogg_page * p, int first,
                 const u8 * df);
int ogg_hdr_dec(ogg_hdr * h, rc_dec * d, ogg_page * p, int first, u8 * df);
int ogg_whole(const ogg_page * p, int j);
void ogg_hdr_step(ogg_hdr * h, u32 samples);

#endif
