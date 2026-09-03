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

#ifndef BLR_OGG_H
#define BLR_OGG_H

#include "model.h"

/*  Ogg framing and the seven-field page-header model.  */

#define OGG_HDRMIN  27
#define OGG_MAXSEG  255
#define OGG_NFIELD  7

typedef struct {
  u8 type;
  u32 glo, ghi;               /*  granule position, low and high half  */
  u32 serial, seq;
  int nseg;  u8 lace[OGG_MAXSEG];
  int np;  u32 plen[OGG_MAXSEG];
  int tail;                   /*  the last packet runs onto the next page  */
  sz blen;                    /*  payload bytes = sum of the lacing values  */
} ogg_page;

u32 ogg_crc(const u8 * d, sz n);
/*  Return the CRC a complete page should contain.  */
u32 ogg_crc_page(const u8 * p, sz n);
int ogg_crc_ok(const u8 * p, sz n);
/*  Update a complete page's CRC field.  */
void ogg_crc_set(u8 * p, sz n);

/*  Parse the page starting at b; returns its total size, or 0 if b does not
    begin a complete, well-formed page.  Does not check the CRC.  */
sz ogg_parse(ogg_page * p, const u8 * b, sz n);
/*  Serialize a header and payload with a computed CRC.  */
sz ogg_emit(const ogg_page * p, u8 * out, const u8 * body);

/*  Convert between lacing values and packets.  */
void ogg_unpack(ogg_page * p);
void ogg_pack(ogg_page * p);


typedef struct {
  model f[OGG_NFIELD];
  model ct;                   /*  a continued packet's spill (see below)  */
  u16 tl;                     /*  the lacing-table disambiguator  */
  u32 cum;                    /*  samples predicted so far in this link  */
} ogg_hdr;

/*  Code a continued packet's bytes on later pages.  */
void ogg_cont_enc(ogg_hdr * h, rc_enc * e, u32 spill);
u32 ogg_cont_dec(ogg_hdr * h, rc_dec * d);

void ogg_hdr_init(ogg_hdr * h);
void ogg_hdr_free(ogg_hdr * h);
/*  Reset link-local predictors and histories, retaining probabilities.  */
void ogg_hdr_reset(ogg_hdr * h);

/*  `first` marks a link's first page, whose BOS bit is implied.  */
void ogg_hdr_enc(ogg_hdr * h, rc_enc * e, const ogg_page * p, int first);
void ogg_hdr_dec(ogg_hdr * h, rc_dec * d, ogg_page * p, int first);
/*  Add a page's samples to the granule prediction.  */
void ogg_hdr_step(ogg_hdr * h, u32 samples);

#endif
