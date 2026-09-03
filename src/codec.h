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

#ifndef BLR_CODEC_H
#define BLR_CODEC_H

#include "common.h"

/*  Whole-file Vorbis codec with link and header deduplication.  */

typedef struct {
  u8 flags;                   /*  the archive flags byte, ARC_* in archive.h  */
  int dd;                     /*  no whole-link (Ogg) deduplication  */
  int df;                     /*  no header-page deduplication  */
  int search;                 /*  how many tunes the encoder may try, 0 = none  */
} vb_opt;

void vb_opt_default(vb_opt * o);

void vb_pack(const char * in, const char * out, const vb_opt * o);
void vb_unpack(const char * in, const char * out);

#endif
